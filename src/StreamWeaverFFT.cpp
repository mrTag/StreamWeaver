#include "StreamWeaverFFT.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream_playback.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

#include <kiss_fftr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace godot;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInf = std::numeric_limits<float>::infinity();

struct FftContext {
    int fft_size;
    int hop;
    std::vector<float> hann;
    kiss_fftr_cfg fwd = nullptr;
    kiss_fftr_cfg inv = nullptr;

    FftContext(int p_fft, int p_hop) : fft_size(p_fft), hop(p_hop), hann(p_fft) {
        for (int i = 0; i < fft_size; ++i) {
            hann[i] = 0.5f * (1.f - std::cos(2.f * kPi * i / (fft_size - 1)));
        }
        fwd = kiss_fftr_alloc(fft_size, 0, nullptr, nullptr);
        inv = kiss_fftr_alloc(fft_size, 1, nullptr, nullptr);
    }
    ~FftContext() {
        if (fwd) kiss_fftr_free(fwd);
        if (inv) kiss_fftr_free(inv);
    }
    int num_bins() const { return fft_size / 2 + 1; }
};

std::vector<float> decode_mono(Ref<AudioStream> stream, float& out_sample_rate) {
    std::vector<float> out;
    if (stream.is_null()) return out;
    out_sample_rate = AudioServer::get_singleton()->get_mix_rate();
    double length = stream->get_length();
    if (length <= 0) return out;
    int num_samples = static_cast<int>(std::ceil(length * out_sample_rate));
    if (num_samples <= 0) return out;

    Ref<AudioStreamPlayback> pb = stream->instantiate_playback();
    if (pb.is_null()) return out;
    pb->start(0.0);
    PackedVector2Array stereo = pb->mix_audio(1.0f, num_samples);
    pb->stop();

    out.resize(stereo.size());
    const Vector2* r = stereo.ptr();
    for (int i = 0; i < stereo.size(); ++i) {
        out[i] = 0.5f * (r[i].x + r[i].y);
    }
    return out;
}

float parabolic_peak_offset(float ym1, float y0, float yp1) {
    float denom = (ym1 - 2.f * y0 + yp1);
    if (std::abs(denom) < 1e-12f) return 0.f;
    return 0.5f * (ym1 - yp1) / denom;
}

// Hamming-windowed sinc lowpass FIR. cutoff_normalized is in (0, 0.5), as a
// fraction of the sample rate.
std::vector<float> design_lowpass_fir(int taps, float cutoff_normalized) {
    if (taps < 3) taps = 3;
    if ((taps & 1) == 0) ++taps; // force odd length
    std::vector<float> h(taps);
    const float fc = cutoff_normalized;
    const int M = taps - 1;
    for (int n = 0; n < taps; ++n) {
        float k = static_cast<float>(n) - 0.5f * M;
        float sinc;
        if (std::abs(k) < 1e-9f) {
            sinc = 2.f * fc;
        } else {
            sinc = std::sin(2.f * kPi * fc * k) / (kPi * k);
        }
        float w = 0.54f - 0.46f * std::cos(2.f * kPi * n / M);
        h[n] = sinc * w;
    }
    float sum = 0.f;
    for (float v : h) sum += v;
    if (sum > 1e-12f) {
        for (float& v : h) v /= sum;
    }
    return h;
}

// Convolve `in` with FIR filter `h`, taking every D-th output sample.
std::vector<float> filter_and_decimate(const std::vector<float>& in, const std::vector<float>& h, int D) {
    int N = static_cast<int>(in.size());
    int taps = static_cast<int>(h.size());
    int half = taps / 2;
    std::vector<float> out;
    out.reserve(N / D + 1);
    for (int i = 0; i < N; i += D) {
        float acc = 0.f;
        int k_start = std::max(0, half - i);
        int k_end = std::min(taps, N + half - i);
        for (int k = k_start; k < k_end; ++k) {
            acc += in[i + k - half] * h[k];
        }
        out.push_back(acc);
    }
    return out;
}

// Highpass FIR via spectral inversion of a lowpass at the same cutoff.
std::vector<float> design_highpass_fir(int taps, float cutoff_normalized) {
    auto lp = design_lowpass_fir(taps, cutoff_normalized);
    int n = static_cast<int>(lp.size());
    int center = n / 2;
    std::vector<float> hp(n);
    for (int i = 0; i < n; ++i) hp[i] = -lp[i];
    hp[center] += 1.0f;
    return hp;
}

// Full (non-decimating) FIR convolution.
std::vector<float> filter_fir(const std::vector<float>& in, const std::vector<float>& h) {
    int N = static_cast<int>(in.size());
    int taps = static_cast<int>(h.size());
    int half = taps / 2;
    std::vector<float> out(N, 0.f);
    for (int i = 0; i < N; ++i) {
        float acc = 0.f;
        int k_start = std::max(0, half - i);
        int k_end = std::min(taps, N + half - i);
        for (int k = k_start; k < k_end; ++k) {
            acc += in[i + k - half] * h[k];
        }
        out[i] = acc;
    }
    return out;
}

// Matplotlib "inferno" colormap sampled at 9 evenly-spaced points. Linear
// interpolation between samples gives a smooth perceptual gradient running
// from near-black through dark purple and magenta into orange and pale yellow.
constexpr int kInfernoSize = 9;
constexpr float kInferno[kInfernoSize][3] = {
    {0.001462f, 0.000466f, 0.013866f},
    {0.078815f, 0.054184f, 0.211667f},
    {0.217949f, 0.036615f, 0.383522f},
    {0.361254f, 0.063460f, 0.429841f},
    {0.498563f, 0.116256f, 0.422144f},
    {0.649661f, 0.189300f, 0.364307f},
    {0.795780f, 0.280197f, 0.262984f},
    {0.920128f, 0.474625f, 0.110404f},
    {0.987053f, 0.991438f, 0.749504f},
};

void inferno_color(float t, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
    if (t < 0.f) t = 0.f;
    else if (t > 1.f) t = 1.f;
    float idx_f = t * (kInfernoSize - 1);
    int i0 = static_cast<int>(idx_f);
    if (i0 >= kInfernoSize - 1) {
        out_r = static_cast<uint8_t>(std::round(kInferno[kInfernoSize - 1][0] * 255.f));
        out_g = static_cast<uint8_t>(std::round(kInferno[kInfernoSize - 1][1] * 255.f));
        out_b = static_cast<uint8_t>(std::round(kInferno[kInfernoSize - 1][2] * 255.f));
        return;
    }
    float u = idx_f - i0;
    float fr = kInferno[i0][0] + u * (kInferno[i0 + 1][0] - kInferno[i0][0]);
    float fg = kInferno[i0][1] + u * (kInferno[i0 + 1][1] - kInferno[i0][1]);
    float fb = kInferno[i0][2] + u * (kInferno[i0 + 1][2] - kInferno[i0][2]);
    out_r = static_cast<uint8_t>(std::round(fr * 255.f));
    out_g = static_cast<uint8_t>(std::round(fg * 255.f));
    out_b = static_cast<uint8_t>(std::round(fb * 255.f));
}

} // namespace

// -------------------- StreamWeaverFFT --------------------

void StreamWeaverFFT::_bind_methods() {
    ClassDB::bind_static_method("StreamWeaverFFT", D_METHOD("create_from_audio_stream",
        "stream", "fft_size", "db_floor", "y_resolution", "x_resolution_per_second",
        "min_hz", "max_hz"),
        &StreamWeaverFFT::create_from_audio_stream);

    ClassDB::bind_method(D_METHOD("set_display_settings",
        "display_db_min", "display_db_max", "gamma", "per_frame_normalize", "spectral_whiten", "log_hz"),
        &StreamWeaverFFT::set_display_settings);
    ClassDB::bind_method(D_METHOD("compute_auto_display_range", "per_frame_normalize", "spectral_whiten"),
        &StreamWeaverFFT::compute_auto_display_range);

    ClassDB::bind_method(D_METHOD("get_image"), &StreamWeaverFFT::get_image);
    ClassDB::bind_method(D_METHOD("get_x_resolution"), &StreamWeaverFFT::get_x_resolution);
    ClassDB::bind_method(D_METHOD("get_y_resolution"), &StreamWeaverFFT::get_y_resolution);
    ClassDB::bind_method(D_METHOD("get_seconds_per_frame"), &StreamWeaverFFT::get_seconds_per_frame);
    ClassDB::bind_method(D_METHOD("get_sample_rate"), &StreamWeaverFFT::get_sample_rate);
    ClassDB::bind_method(D_METHOD("get_decimated_sample_rate"), &StreamWeaverFFT::get_decimated_sample_rate);
    ClassDB::bind_method(D_METHOD("get_decimation_factor"), &StreamWeaverFFT::get_decimation_factor);
    ClassDB::bind_method(D_METHOD("get_analysis_db_floor"), &StreamWeaverFFT::get_analysis_db_floor);
    ClassDB::bind_method(D_METHOD("get_display_min_hz"), &StreamWeaverFFT::get_display_min_hz);
    ClassDB::bind_method(D_METHOD("get_display_max_hz"), &StreamWeaverFFT::get_display_max_hz);
    ClassDB::bind_method(D_METHOD("get_bin_hz"), &StreamWeaverFFT::get_bin_hz);

    ClassDB::bind_method(D_METHOD("pixel_x_to_time", "px"), &StreamWeaverFFT::pixel_x_to_time);
    ClassDB::bind_method(D_METHOD("pixel_y_to_hz", "py"), &StreamWeaverFFT::pixel_y_to_hz);
    ClassDB::bind_method(D_METHOD("hz_to_pixel_y", "hz"), &StreamWeaverFFT::hz_to_pixel_y);
    ClassDB::bind_method(D_METHOD("time_to_pixel_x", "time_s"), &StreamWeaverFFT::time_to_pixel_x);

    ClassDB::bind_method(D_METHOD("track_fundamental",
        "start_time_s", "harmonic_picks", "bandwidth_hz", "max_slope_hz_per_sec",
        "transition_penalty", "sketch_hint", "sketch_weight", "stop_drop_db"),
        &StreamWeaverFFT::track_fundamental);
    ClassDB::bind_method(D_METHOD("reconstruct_isolated", "tracked_curve", "bandwidth_hz"),
        &StreamWeaverFFT::reconstruct_isolated);
    ClassDB::bind_method(D_METHOD("detect_low_freq_cycles",
        "start_time_s", "end_time_s", "hz_min", "hz_max", "sensitivity", "sketch_hint"),
        &StreamWeaverFFT::detect_low_freq_cycles);
    ClassDB::bind_method(D_METHOD("get_last_onset_debug"),
        &StreamWeaverFFT::get_last_onset_debug);
    ClassDB::bind_method(D_METHOD("get_last_track_debug"),
        &StreamWeaverFFT::get_last_track_debug);
    ClassDB::bind_method(D_METHOD("get_mono_pcm_packed"), &StreamWeaverFFT::get_mono_pcm_packed);
    ClassDB::bind_method(D_METHOD("get_original_mono_pcm_packed"), &StreamWeaverFFT::get_original_mono_pcm_packed);
    ClassDB::bind_method(D_METHOD("get_total_duration"), &StreamWeaverFFT::get_total_duration);
}

Ref<StreamWeaverFFT> StreamWeaverFFT::create_from_audio_stream(
    Ref<AudioStream> stream,
    int fft_size,
    float db_floor,
    int y_resolution,
    float x_resolution_per_second,
    float min_hz,
    float max_hz)
{
    Ref<StreamWeaverFFT> result;
    result.instantiate();

    if (stream.is_null() || fft_size < 16 || y_resolution < 2 || x_resolution_per_second <= 0) {
        UtilityFunctions::push_warning("StreamWeaverFFT: invalid parameters");
        return result;
    }
    if (fft_size & 1) ++fft_size;

    float source_rate = 0;
    std::vector<float> pcm = decode_mono(stream, source_rate);
    if (pcm.empty() || source_rate <= 0) {
        UtilityFunctions::push_warning("StreamWeaverFFT: could not decode audio stream");
        return result;
    }

    float nyquist_source = source_rate * 0.5f;
    if (max_hz <= 0 || max_hz > nyquist_source) max_hz = nyquist_source;
    if (min_hz < 0) min_hz = 0;
    if (min_hz >= max_hz) min_hz = 0;

    // Zoom FFT: lowpass and decimate to a new rate of ~2.2 * max_hz. This
    // concentrates all FFT bins onto [0, new_nyquist], improving frequency
    // resolution by the decimation factor D for the same fft_size.
    int D = 1;
    std::vector<float> decimated = pcm;
    float decimated_rate = source_rate;
    if (max_hz < nyquist_source * 0.95f) {
        float target_rate = 2.2f * max_hz;
        D = std::max(1, static_cast<int>(std::floor(source_rate / target_rate)));
        if (D > 1) {
            // Normalized cutoff slightly below new-nyquist to leave a transition band.
            float new_rate = source_rate / static_cast<float>(D);
            float cutoff = 0.90f * (new_rate * 0.5f) / source_rate;
            auto fir = design_lowpass_fir(65, cutoff);
            decimated = filter_and_decimate(pcm, fir, D);
            decimated_rate = new_rate;
        }
    }

    // Highpass at min_hz to kill DC offset and subsonic rumble. Without this,
    // the tracker's Viterbi happily rides the (often very loud) bins near bin 0.
    if (min_hz > 0 && min_hz < decimated_rate * 0.45f) {
        float hp_cutoff = min_hz / decimated_rate;
        auto hp = design_highpass_fir(129, hp_cutoff);
        decimated = filter_fir(decimated, hp);
    }

    int hop = std::max(1, static_cast<int>(std::round(decimated_rate / x_resolution_per_second)));
    int num_bins = fft_size / 2 + 1;
    int num_frames = 0;
    if (static_cast<int>(decimated.size()) >= fft_size) {
        num_frames = (static_cast<int>(decimated.size()) - fft_size) / hop + 1;
    }
    if (num_frames < 1) {
        UtilityFunctions::push_warning("StreamWeaverFFT: audio shorter than fft_size after decimation");
        return result;
    }

    FftContext ctx(fft_size, hop);
    std::vector<float> windowed(fft_size);
    std::vector<kiss_fft_cpx> freq(num_bins);

    std::vector<float> mag_db(static_cast<size_t>(num_frames) * num_bins, db_floor);
    for (int f = 0; f < num_frames; ++f) {
        const float* base = decimated.data() + f * hop;
        for (int i = 0; i < fft_size; ++i) {
            windowed[i] = base[i] * ctx.hann[i];
        }
        kiss_fftr(ctx.fwd, windowed.data(), freq.data());
        for (int b = 0; b < num_bins; ++b) {
            float mag = std::sqrt(freq[b].r * freq[b].r + freq[b].i * freq[b].i);
            float db = 20.f * std::log10(std::max(mag, 1e-10f));
            if (db < db_floor) db = db_floor;
            mag_db[static_cast<size_t>(f) * num_bins + b] = db;
        }
    }

    result->full_mag_db = std::move(mag_db);
    result->num_frames = num_frames;
    result->num_bins = num_bins;
    result->y_resolution = y_resolution;
    result->fft_size_used = fft_size;
    result->hop_used = hop;
    result->seconds_per_frame = static_cast<float>(hop) / decimated_rate;
    result->original_sample_rate = source_rate;
    result->decimated_sample_rate = decimated_rate;
    result->decimation_factor = D;
    result->analysis_db_floor = db_floor;
    result->bin_hz = decimated_rate / static_cast<float>(fft_size);
    result->mono_pcm = std::move(decimated);
    result->original_mono_pcm = std::move(pcm);
    result->display_min_hz = min_hz;
    result->display_max_hz = std::min(max_hz, decimated_rate * 0.5f);

    result->display_db_min = db_floor;
    result->display_db_max = 0.f;
    result->gamma_value = 1.0f;
    result->per_frame_normalize = false;
    result->spectral_whiten = false;
    result->log_hz = true;
    result->rebuild_image_internal();

    return result;
}

void StreamWeaverFFT::apply_transforms(std::vector<float>& work, bool whiten, bool per_frame) const {
    // Spectral whitening: subtract per-bin 75th percentile (in dB) — less aggressive
    // than the true median, so it doesn't blow up quiet-most-of-the-time bins.
    // The shift is clamped so a bin cannot be lifted by more than 30 dB, keeping
    // the transformed range well-behaved.
    if (whiten && num_frames > 0) {
        std::vector<float> column(num_frames);
        const float max_shift = 30.f;
        for (int b = 0; b < num_bins; ++b) {
            for (int f = 0; f < num_frames; ++f) {
                column[f] = full_mag_db[static_cast<size_t>(f) * num_bins + b];
            }
            int idx = std::min(num_frames - 1, static_cast<int>(num_frames * 0.75f));
            std::nth_element(column.begin(), column.begin() + idx, column.end());
            float p75 = column[idx];
            for (int f = 0; f < num_frames; ++f) {
                float v = full_mag_db[static_cast<size_t>(f) * num_bins + b] - p75;
                if (v > max_shift) v = max_shift;
                if (v < analysis_db_floor) v = analysis_db_floor;
                work[static_cast<size_t>(f) * num_bins + b] = v;
            }
        }
    }

    if (per_frame) {
        for (int f = 0; f < num_frames; ++f) {
            float* row = work.data() + static_cast<size_t>(f) * num_bins;
            float peak = -1e30f;
            for (int b = 0; b < num_bins; ++b) {
                if (row[b] > peak) peak = row[b];
            }
            if (peak > -1e29f) {
                for (int b = 0; b < num_bins; ++b) row[b] -= peak;
            }
        }
    }
}

Vector2 StreamWeaverFFT::compute_auto_display_range(bool p_per_frame_normalize, bool p_spectral_whiten) const {
    if (num_frames <= 0 || num_bins <= 0) return Vector2(analysis_db_floor, 0);
    std::vector<float> work = full_mag_db;
    apply_transforms(work, p_spectral_whiten, p_per_frame_normalize);

    // Sample every few values and pick p5 / p99.5 via nth_element on a copy.
    size_t total = work.size();
    size_t stride = std::max<size_t>(1, total / 200000);
    std::vector<float> sample;
    sample.reserve(total / stride + 1);
    for (size_t i = 0; i < total; i += stride) sample.push_back(work[i]);
    if (sample.empty()) return Vector2(analysis_db_floor, 0);

    int lo_idx = static_cast<int>(sample.size() * 0.05f);
    int hi_idx = static_cast<int>(sample.size() * 0.995f);
    if (hi_idx >= static_cast<int>(sample.size())) hi_idx = static_cast<int>(sample.size()) - 1;
    if (lo_idx < 0) lo_idx = 0;
    std::vector<float> copy = sample;
    std::nth_element(copy.begin(), copy.begin() + lo_idx, copy.end());
    float lo = copy[lo_idx];
    copy = sample;
    std::nth_element(copy.begin(), copy.begin() + hi_idx, copy.end());
    float hi = copy[hi_idx];
    if (hi - lo < 6.f) hi = lo + 6.f;
    return Vector2(lo, hi);
}

void StreamWeaverFFT::set_display_settings(
    float p_display_db_min,
    float p_display_db_max,
    float p_gamma,
    bool p_per_frame_normalize,
    bool p_spectral_whiten,
    bool p_log_hz)
{
    display_db_min = p_display_db_min;
    display_db_max = p_display_db_max;
    gamma_value = std::max(0.01f, p_gamma);
    per_frame_normalize = p_per_frame_normalize;
    spectral_whiten = p_spectral_whiten;
    log_hz = p_log_hz;
    rebuild_image_internal();
}

void StreamWeaverFFT::rebuild_image_internal() {
    if (num_frames <= 0 || num_bins <= 0 || y_resolution <= 0) return;

    std::vector<float> work = full_mag_db;
    apply_transforms(work, spectral_whiten, per_frame_normalize);

    int64_t pixel_count = static_cast<int64_t>(num_frames) * y_resolution * 3;
    PackedByteArray pixels;
    pixels.resize(pixel_count);
    uint8_t* w = pixels.ptrw();

    float range = display_db_max - display_db_min;
    if (range < 1e-6f) range = 1e-6f;
    float inv_range = 1.f / range;
    float inv_gamma = 1.f / gamma_value;

    // log(0) is undefined, so clamp the bottom of a log-Hz window to 1 Hz.
    // The linear path uses display_min_hz/display_max_hz as-is.
    float min_hz_eff = log_hz ? std::max(display_min_hz, 1.0f) : display_min_hz;
    float max_hz_eff = log_hz ? std::max(display_max_hz, min_hz_eff * 1.01f)
                              : std::max(display_max_hz, min_hz_eff + bin_hz);
    float log_min = log_hz ? std::log(min_hz_eff) : 0.f;
    float log_span = log_hz ? (std::log(max_hz_eff) - log_min) : 0.f;
    float lin_span = max_hz_eff - min_hz_eff;

    for (int y = 0; y < y_resolution; ++y) {
        // Image y=0 = top = display_max_hz.
        float t_top = 1.f - static_cast<float>(y) / y_resolution;
        float t_bot = 1.f - static_cast<float>(y + 1) / y_resolution;
        float t_center = 1.f - (static_cast<float>(y) + 0.5f) / y_resolution;
        float bin_top_f, bin_bot_f, bin_center_f;
        if (log_hz) {
            bin_top_f = std::exp(log_min + t_top * log_span) / bin_hz;
            bin_bot_f = std::exp(log_min + t_bot * log_span) / bin_hz;
            bin_center_f = std::exp(log_min + t_center * log_span) / bin_hz;
        } else {
            bin_top_f = (min_hz_eff + t_top * lin_span) / bin_hz;
            bin_bot_f = (min_hz_eff + t_bot * lin_span) / bin_hz;
            bin_center_f = (min_hz_eff + t_center * lin_span) / bin_hz;
        }

        // When a pixel row spans more than one FFT bin (high frequencies on a
        // log axis), take the peak of the covered bins. When it spans less
        // than a bin (low frequencies, where bin_hz >> hz-per-pixel), sample
        // with linear interpolation between the two nearest bins so
        // neighbouring pixel rows don't collapse onto the same bin.
        bool interpolate = (bin_top_f - bin_bot_f) < 1.0f;
        int b_lo = 0, b_hi = 0, b0 = 0, b1 = 0;
        float u = 0.f;
        if (interpolate) {
            b0 = static_cast<int>(std::floor(bin_center_f));
            if (b0 < 0) b0 = 0;
            if (b0 > num_bins - 1) b0 = num_bins - 1;
            b1 = std::min(num_bins - 1, b0 + 1);
            u = bin_center_f - static_cast<float>(b0);
            if (u < 0.f) u = 0.f;
            else if (u > 1.f) u = 1.f;
        } else {
            b_lo = std::max(0, static_cast<int>(std::floor(bin_bot_f)));
            b_hi = std::min(num_bins - 1, static_cast<int>(std::ceil(bin_top_f)));
            if (b_hi < b_lo) b_hi = b_lo;
        }

        for (int f = 0; f < num_frames; ++f) {
            const float* row = work.data() + static_cast<size_t>(f) * num_bins;
            float value;
            if (interpolate) {
                value = row[b0] * (1.f - u) + row[b1] * u;
            } else {
                value = -1e30f;
                for (int b = b_lo; b <= b_hi; ++b) {
                    if (row[b] > value) value = row[b];
                }
            }
            float t = (value - display_db_min) * inv_range;
            if (t < 0) t = 0;
            else if (t > 1) t = 1;
            t = std::pow(t, inv_gamma);
            uint8_t r_out, g_out, b_out;
            inferno_color(t, r_out, g_out, b_out);
            int64_t base = (static_cast<int64_t>(y) * num_frames + f) * 3;
            w[base + 0] = r_out;
            w[base + 1] = g_out;
            w[base + 2] = b_out;
        }
    }

    image = Image::create_from_data(num_frames, y_resolution, false, Image::FORMAT_RGB8, pixels);
}

float StreamWeaverFFT::pixel_x_to_time(int px) const {
    if (num_frames <= 0) return 0;
    return px * seconds_per_frame;
}

float StreamWeaverFFT::pixel_y_to_hz(int py) const {
    if (y_resolution <= 0) return 0;
    float t = 1.f - (static_cast<float>(py) + 0.5f) / y_resolution;
    if (log_hz) {
        float min_hz_eff = std::max(display_min_hz, 1.0f);
        float max_hz_eff = std::max(display_max_hz, min_hz_eff * 1.01f);
        float log_min = std::log(min_hz_eff);
        float log_max = std::log(max_hz_eff);
        return std::exp(log_min + t * (log_max - log_min));
    }
    return display_min_hz + t * (display_max_hz - display_min_hz);
}

int StreamWeaverFFT::hz_to_pixel_y(float hz) const {
    if (y_resolution <= 0) return 0;
    float t;
    if (log_hz) {
        float min_hz_eff = std::max(display_min_hz, 1.0f);
        float max_hz_eff = std::max(display_max_hz, min_hz_eff * 1.01f);
        if (max_hz_eff <= min_hz_eff) return 0;
        float hz_eff = std::max(hz, min_hz_eff);
        float log_min = std::log(min_hz_eff);
        float log_max = std::log(max_hz_eff);
        t = (std::log(hz_eff) - log_min) / (log_max - log_min);
    } else {
        if (display_max_hz <= display_min_hz) return 0;
        t = (hz - display_min_hz) / (display_max_hz - display_min_hz);
    }
    int py = static_cast<int>(std::round((1.f - t) * y_resolution - 0.5f));
    if (py < 0) py = 0;
    if (py >= y_resolution) py = y_resolution - 1;
    return py;
}

int StreamWeaverFFT::time_to_pixel_x(float time_s) const {
    if (seconds_per_frame <= 0) return 0;
    int px = static_cast<int>(std::round(time_s / seconds_per_frame));
    if (px < 0) px = 0;
    if (px >= num_frames) px = num_frames - 1;
    return px;
}

// -------------------- Viterbi tracker --------------------

namespace {

struct ViterbiStep {
    int frame;
    float bin_f;
};

std::vector<ViterbiStep> viterbi_walk(
    const std::vector<float>& obs,
    int num_frames,
    int num_bins,
    int search_bin_lo,
    int search_bin_hi,
    int start_frame,
    int start_bin,
    int half_band_bins,
    int max_delta_bins,
    float max_delta_bins_f,
    float transition_penalty,
    float stop_thresh_db,
    int direction)
{
    std::vector<ViterbiStep> result;
    if (num_frames <= 0 || num_bins <= 0) return result;

    auto obs_at = [&](int f, int b) {
        return obs[static_cast<size_t>(f) * num_bins + b];
    };
    auto parabolic_bin = [&](int f, int b) {
        float frac = 0.f;
        if (b > search_bin_lo && b < search_bin_hi) {
            frac = parabolic_peak_offset(obs_at(f, b - 1), obs_at(f, b), obs_at(f, b + 1));
            if (frac < -1.f) frac = -1.f;
            if (frac > 1.f) frac = 1.f;
        }
        return static_cast<float>(b) + frac;
    };

    std::vector<float> dp_prev(num_bins, kInf);
    std::vector<float> dp_curr(num_bins, kInf);

    int lo_init = std::max(search_bin_lo, start_bin - half_band_bins);
    int hi_init = std::min(search_bin_hi, start_bin + half_band_bins);
    for (int b = lo_init; b <= hi_init; ++b) {
        dp_prev[b] = -obs_at(start_frame, b);
    }
    int seed_best_bin = lo_init;
    float seed_best_obs = -1e30f;
    for (int b = lo_init; b <= hi_init; ++b) {
        float m = obs_at(start_frame, b);
        if (m > seed_best_obs) { seed_best_obs = m; seed_best_bin = b; }
    }
    result.push_back({start_frame, parabolic_bin(start_frame, seed_best_bin)});

    std::vector<std::vector<int>> backptrs;

    int f = start_frame + direction;
    while (f >= 0 && f < num_frames) {
        std::fill(dp_curr.begin(), dp_curr.end(), kInf);
        std::vector<int> bp(num_bins, -1);
        for (int b = search_bin_lo; b <= search_bin_hi; ++b) {
            int lo = std::max(search_bin_lo, b - max_delta_bins);
            int hi = std::min(search_bin_hi, b + max_delta_bins);
            float best = kInf;
            int best_prev = -1;
            for (int pb = lo; pb <= hi; ++pb) {
                float prev = dp_prev[pb];
                if (prev == kInf) continue;
                float dbn = static_cast<float>(pb - b) / max_delta_bins_f;
                float tc = transition_penalty * dbn * dbn;
                float total = prev + tc;
                if (total < best) { best = total; best_prev = pb; }
            }
            if (best < kInf) {
                dp_curr[b] = best + (-obs_at(f, b));
                bp[b] = best_prev;
            }
        }

        int best_cur = -1;
        float best_dp = kInf;
        for (int b = search_bin_lo; b <= search_bin_hi; ++b) {
            if (dp_curr[b] < best_dp) { best_dp = dp_curr[b]; best_cur = b; }
        }
        // Only break on degenerate Viterbi state (no valid backptr from the
        // previous frame); we no longer terminate on low obs. Gap tagging is
        // done by the caller after backtracing the full walk.
        if (best_cur < 0) break;
        (void)stop_thresh_db;

        backptrs.push_back(std::move(bp));
        std::swap(dp_prev, dp_curr);
        f += direction;
    }

    if (backptrs.empty()) return result;

    int cur_bin = -1;
    float best_final = kInf;
    for (int b = search_bin_lo; b <= search_bin_hi; ++b) {
        if (dp_prev[b] < best_final) { best_final = dp_prev[b]; cur_bin = b; }
    }
    if (cur_bin < 0) return result;

    std::vector<int> bins_path;
    bins_path.push_back(cur_bin);
    for (int i = static_cast<int>(backptrs.size()) - 1; i >= 1; --i) {
        cur_bin = backptrs[i][cur_bin];
        if (cur_bin < 0) { bins_path.clear(); break; }
        bins_path.push_back(cur_bin);
    }
    std::reverse(bins_path.begin(), bins_path.end());

    for (int i = 0; i < static_cast<int>(bins_path.size()); ++i) {
        int step_frame = start_frame + (i + 1) * direction;
        int b = bins_path[i];
        result.push_back({step_frame, parabolic_bin(step_frame, b)});
    }
    return result;
}

void median_filter_curve(std::vector<Vector2>& curve, int window) {
    if (window < 3 || static_cast<int>(curve.size()) < window) return;
    int half = window / 2;
    std::vector<Vector2> out = curve;
    std::vector<float> buf(window);
    for (int i = half; i < static_cast<int>(curve.size()) - half; ++i) {
        for (int k = 0; k < window; ++k) buf[k] = curve[i - half + k].y;
        std::nth_element(buf.begin(), buf.begin() + half, buf.end());
        out[i].y = buf[half];
    }
    curve = std::move(out);
}

} // namespace

PackedVector2Array StreamWeaverFFT::track_fundamental(
    float start_time_s,
    PackedVector2Array harmonic_picks,
    float bandwidth_hz,
    float max_slope_hz_per_sec,
    float transition_penalty,
    PackedVector2Array sketch_hint,
    float sketch_weight,
    float stop_drop_db) const
{
    PackedVector2Array out;
    if (num_frames <= 0 || num_bins <= 0 || bin_hz <= 0) return out;
    if (harmonic_picks.size() == 0) return out;

    // Collect unique orders and infer start fundamental from picks.
    std::vector<int> orders;
    std::vector<float> f0_candidates;
    orders.reserve(harmonic_picks.size());
    f0_candidates.reserve(harmonic_picks.size());
    int max_order = 1;
    for (int i = 0; i < harmonic_picks.size(); ++i) {
        Vector2 p = harmonic_picks[i];
        int k = std::max(1, static_cast<int>(std::round(p.y)));
        if (std::find(orders.begin(), orders.end(), k) == orders.end()) {
            orders.push_back(k);
        }
        if (k > max_order) max_order = k;
        if (p.x > 0) f0_candidates.push_back(p.x / static_cast<float>(k));
    }
    if (f0_candidates.empty()) return out;
    std::sort(f0_candidates.begin(), f0_candidates.end());
    float start_hz = f0_candidates[f0_candidates.size() / 2];

    int start_frame = static_cast<int>(std::round(start_time_s / seconds_per_frame));
    if (start_frame < 0) start_frame = 0;
    if (start_frame >= num_frames) start_frame = num_frames - 1;

    int start_bin = static_cast<int>(std::round(start_hz / bin_hz));
    if (start_bin < 1) start_bin = 1;
    if (start_bin >= num_bins) start_bin = num_bins - 1;

    int half_band_bins = std::max(1, static_cast<int>(std::ceil(bandwidth_hz * 0.5f / bin_hz)));

    float max_delta_bins_f = std::max(1.f, max_slope_hz_per_sec * seconds_per_frame / bin_hz);
    int max_delta_bins = std::max(1, static_cast<int>(std::ceil(max_delta_bins_f)));

    // Always exclude bin 0 (DC).
    int search_bin_lo = std::max(1, static_cast<int>(std::floor(display_min_hz / bin_hz)));
    int search_bin_hi = std::min(num_bins - 1, static_cast<int>(std::ceil(display_max_hz / bin_hz)));
    if (search_bin_hi <= search_bin_lo) {
        search_bin_lo = 1;
        search_bin_hi = num_bins - 1;
    }
    // Constrain so the highest harmonic still fits inside num_bins.
    int max_fund_bin = (num_bins - 1) / max_order;
    if (search_bin_hi > max_fund_bin) search_bin_hi = max_fund_bin;
    if (search_bin_hi <= search_bin_lo) return out;

    // Build observation buffer: mean dB over picked harmonic orders.
    std::vector<float> harm_db(full_mag_db.size());
    for (int f = 0; f < num_frames; ++f) {
        const float* row = full_mag_db.data() + static_cast<size_t>(f) * num_bins;
        float* out_row = harm_db.data() + static_cast<size_t>(f) * num_bins;
        for (int b = 0; b < num_bins; ++b) {
            float sum = 0.f;
            int count = 0;
            for (int k : orders) {
                int hb = b * k;
                if (hb >= num_bins) continue;
                sum += row[hb];
                ++count;
            }
            out_row[b] = (count > 0) ? (sum / static_cast<float>(count)) : analysis_db_floor;
        }
    }

    // Apply sketch hint as a per-frame linear pull toward the sketched bin.
    if (sketch_hint.size() >= 2 && sketch_weight > 0.f) {
        auto sketch_hz_at = [&](float t) -> float {
            int n = sketch_hint.size();
            if (t < sketch_hint[0].x || t > sketch_hint[n - 1].x) return -1.f;
            int lo = 0, hi = n - 1;
            while (hi - lo > 1) {
                int m = (lo + hi) / 2;
                if (sketch_hint[m].x <= t) lo = m; else hi = m;
            }
            // Gap sentinels (y <= 0) mark boundaries between separate sketch strokes.
            if (sketch_hint[lo].y <= 0.f || sketch_hint[hi].y <= 0.f) return -1.f;
            float dx = sketch_hint[hi].x - sketch_hint[lo].x;
            float u = (dx > 1e-9f) ? (t - sketch_hint[lo].x) / dx : 0.f;
            return sketch_hint[lo].y + u * (sketch_hint[hi].y - sketch_hint[lo].y);
        };
        for (int f = 0; f < num_frames; ++f) {
            float t = f * seconds_per_frame;
            float hz_target = sketch_hz_at(t);
            if (hz_target <= 0) continue;
            float bin_target = hz_target / bin_hz;
            float* out_row = harm_db.data() + static_cast<size_t>(f) * num_bins;
            for (int b = search_bin_lo; b <= search_bin_hi; ++b) {
                out_row[b] -= sketch_weight * std::abs(static_cast<float>(b) - bin_target);
            }
        }
    }

    const std::vector<float>& obs = harm_db;

    int lo_init = std::max(search_bin_lo, start_bin - half_band_bins);
    int hi_init = std::min(search_bin_hi, start_bin + half_band_bins);
    float seed_peak = -1e30f;
    for (int b = lo_init; b <= hi_init; ++b) {
        float m = obs[static_cast<size_t>(start_frame) * num_bins + b];
        if (m > seed_peak) seed_peak = m;
    }
    if (seed_peak < analysis_db_floor + 6.f) return out;
    float stop_thresh_db = seed_peak - std::max(0.f, stop_drop_db);

    auto forward = viterbi_walk(obs, num_frames, num_bins, search_bin_lo, search_bin_hi,
        start_frame, start_bin, half_band_bins, max_delta_bins, max_delta_bins_f,
        transition_penalty, stop_thresh_db, +1);
    auto backward = viterbi_walk(obs, num_frames, num_bins, search_bin_lo, search_bin_hi,
        start_frame, start_bin, half_band_bins, max_delta_bins, max_delta_bins_f,
        transition_penalty, stop_thresh_db, -1);

    std::vector<Vector2> merged;
    merged.reserve(forward.size() + backward.size());
    for (auto it = backward.rbegin(); it != backward.rend(); ++it) {
        if (it->frame == forward[0].frame) continue;
        merged.push_back(Vector2(it->frame * seconds_per_frame, it->bin_f * bin_hz));
    }
    for (const auto& s : forward) {
        merged.push_back(Vector2(s.frame * seconds_per_frame, s.bin_f * bin_hz));
    }

    median_filter_curve(merged, 5);

    // Populate debug snapshot for visualisation: capture the harmonic-mean
    // observation (dB) at each frame of the tracked path so the editor can
    // show why tracking terminates (cyan curve dipping below the red
    // stop_thresh line is the smoking gun). The obs in this buffer comes
    // from the harmonic-mean observation row WITHOUT the per-frame
    // sketch_weight bias applied — we want the user to see the raw signal
    // strength relative to the gap threshold, not the sketch-pulled cost.
    last_track_seed_peak = seed_peak;
    last_track_stop_thresh = stop_thresh_db;
    last_track_start_frame = start_frame;

    // Per-step obs along the path (used both for debug viz and gap tagging).
    // Computed against `harm_db`, NOT `obs`, so the sketch-weight bias from
    // earlier doesn't muddy the threshold comparison.
    std::vector<float> path_obs(merged.size(), analysis_db_floor);
    for (size_t i = 0; i < merged.size(); ++i) {
        int f = static_cast<int>(std::round(merged[i].x / seconds_per_frame));
        if (f < 0 || f >= num_frames) continue;
        int b = static_cast<int>(std::round(merged[i].y / bin_hz));
        if (b < 0) b = 0;
        if (b >= num_bins) b = num_bins - 1;
        path_obs[i] = harm_db[static_cast<size_t>(f) * num_bins + b];
    }

    if (!merged.empty()) {
        int frame_lo = static_cast<int>(std::round(merged.front().x / seconds_per_frame));
        int frame_hi = static_cast<int>(std::round(merged.back().x / seconds_per_frame));
        if (frame_lo > frame_hi) std::swap(frame_lo, frame_hi);
        if (frame_lo < 0) frame_lo = 0;
        if (frame_hi >= num_frames) frame_hi = num_frames - 1;
        last_track_frame_lo = frame_lo;
        last_track_frame_hi = frame_hi;
        last_track_obs.assign(static_cast<size_t>(frame_hi - frame_lo + 1), analysis_db_floor);
        for (size_t i = 0; i < merged.size(); ++i) {
            int f = static_cast<int>(std::round(merged[i].x / seconds_per_frame));
            if (f < frame_lo || f > frame_hi) continue;
            last_track_obs[static_cast<size_t>(f - frame_lo)] = path_obs[i];
        }
    } else {
        last_track_obs.clear();
        last_track_frame_lo = 0;
        last_track_frame_hi = -1;
    }

    // Tag low-obs frames as gaps with a Vector2(time, -1) sentinel so the
    // grain extractor and curve drawing can split tracking into segments
    // (matching the gap convention used by sketch_hint).
    out.resize(static_cast<int>(merged.size()));
    for (int i = 0; i < static_cast<int>(merged.size()); ++i) {
        if (path_obs[i] < stop_thresh_db) {
            out[i] = Vector2(merged[i].x, -1.f);
        } else {
            out[i] = merged[i];
        }
    }
    return out;
}

Ref<AudioStreamWAV> StreamWeaverFFT::reconstruct_isolated(
    PackedVector2Array tracked_curve, float bandwidth_hz) const
{
    Ref<AudioStreamWAV> wav;
    wav.instantiate();
    if (mono_pcm.empty() || tracked_curve.size() < 1) return wav;

    // Use the exact FFT size and hop from analysis to ensure bin alignment.
    int fft_size = fft_size_used > 0 ? fft_size_used : 2048;
    int hop = hop_used > 0 ? hop_used : std::max(1, fft_size / 4);
    if (fft_size & 1) ++fft_size;
    int num_bins_r = fft_size / 2 + 1;
    float bin_hz_r = decimated_sample_rate / static_cast<float>(fft_size);

    int num_samples = static_cast<int>(mono_pcm.size());
    int num_frames_r = 0;
    if (num_samples >= fft_size) num_frames_r = (num_samples - fft_size) / hop + 1;
    if (num_frames_r < 1) return wav;

    FftContext ctx(fft_size, hop);
    std::vector<float> windowed(fft_size);
    std::vector<kiss_fft_cpx> freq(num_bins_r);
    std::vector<float> time_out(fft_size);

    std::vector<float> out(num_samples, 0.f);
    std::vector<float> wsum(num_samples, 0.f);

    auto lookup_hz = [&](float time_s) -> float {
        int n = tracked_curve.size();
        if (n == 0) return -1.f;
        if (time_s <= tracked_curve[0].x) return tracked_curve[0].y;
        if (time_s >= tracked_curve[n - 1].x) return tracked_curve[n - 1].y;
        int lo = 0, hi = n - 1;
        while (hi - lo > 1) {
            int m = (lo + hi) / 2;
            if (tracked_curve[m].x <= time_s) lo = m; else hi = m;
        }
        // Either bracket point being a gap sentinel (y <= 0) means this
        // sample is inside or adjacent to a tracking gap — return -1 so
        // the caller skips the frame.
        if (tracked_curve[lo].y <= 0.f || tracked_curve[hi].y <= 0.f) return -1.f;
        float t = (time_s - tracked_curve[lo].x) / (tracked_curve[hi].x - tracked_curve[lo].x);
        return tracked_curve[lo].y + t * (tracked_curve[hi].y - tracked_curve[lo].y);
    };

    float curve_start = tracked_curve[0].x;
    float curve_end = tracked_curve[tracked_curve.size() - 1].x;

    for (int f = 0; f < num_frames_r; ++f) {
        float frame_center_t = (f * hop + fft_size * 0.5f) / decimated_sample_rate;
        if (frame_center_t < curve_start || frame_center_t > curve_end) continue;
        float track_hz = lookup_hz(frame_center_t);
        if (track_hz <= 0) continue;

        const float* base = mono_pcm.data() + f * hop;
        for (int i = 0; i < fft_size; ++i) windowed[i] = base[i] * ctx.hann[i];
        kiss_fftr(ctx.fwd, windowed.data(), freq.data());

        int lo_bin = std::max(0, static_cast<int>(std::floor((track_hz - bandwidth_hz * 0.5f) / bin_hz_r)));
        int hi_bin = std::min(num_bins_r - 1, static_cast<int>(std::ceil((track_hz + bandwidth_hz * 0.5f) / bin_hz_r)));
        for (int b = 0; b < lo_bin; ++b) { freq[b].r = 0; freq[b].i = 0; }
        for (int b = hi_bin + 1; b < num_bins_r; ++b) { freq[b].r = 0; freq[b].i = 0; }

        kiss_fftri(ctx.inv, freq.data(), time_out.data());
        float inv_scale = 1.f / static_cast<float>(fft_size);
        int off = f * hop;
        for (int i = 0; i < fft_size; ++i) {
            out[off + i] += time_out[i] * ctx.hann[i] * inv_scale;
            wsum[off + i] += ctx.hann[i] * ctx.hann[i];
        }
    }

    float peak = 0.f;
    for (int i = 0; i < num_samples; ++i) {
        if (wsum[i] > 1e-6f) out[i] /= wsum[i];
        float a = std::abs(out[i]);
        if (a > peak) peak = a;
    }
    float norm = (peak > 1e-6f && peak > 1.f) ? (0.99f / peak) : 1.f;

    PackedByteArray bytes;
    bytes.resize(num_samples * 2);
    uint8_t* w = bytes.ptrw();
    for (int i = 0; i < num_samples; ++i) {
        int32_t s = static_cast<int32_t>(std::round(out[i] * norm * 32767.f));
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        w[i * 2 + 0] = static_cast<uint8_t>(s & 0xff);
        w[i * 2 + 1] = static_cast<uint8_t>((s >> 8) & 0xff);
    }
    wav->set_format(AudioStreamWAV::FORMAT_16_BITS);
    wav->set_mix_rate(static_cast<int>(decimated_sample_rate));
    wav->set_stereo(false);
    wav->set_data(bytes);
    return wav;
}

// -------------------- Low-frequency cycle onset detection --------------------

PackedVector2Array StreamWeaverFFT::detect_low_freq_cycles(
    float start_time_s,
    float end_time_s,
    float hz_min,
    float hz_max,
    float sensitivity,
    PackedVector2Array sketch_hint) const
{
    PackedVector2Array out;
    if (num_frames < 4 || full_mag_db.empty() || seconds_per_frame <= 0.f) return out;
    if (hz_min <= 0.f) hz_min = 0.5f;
    if (hz_max <= hz_min) return out;
    if (sensitivity < 0.f) sensitivity = 0.f;
    if (sensitivity > 1.f) sensitivity = 1.f;

    // Frame range gated by user-selected time window.
    int frame_lo = std::max(0, static_cast<int>(std::floor(start_time_s / seconds_per_frame)));
    int frame_hi = std::min(num_frames - 1,
        static_cast<int>(std::ceil(end_time_s / seconds_per_frame)));
    if (frame_hi <= frame_lo + 2) frame_hi = std::min(num_frames - 1, frame_lo + 2);

    // Mid-band energy bin range — broadband transient energy lives well above
    // hz_max, so use a generous band starting around 50 Hz up to ~80% Nyquist.
    int bin_lo = std::max(1, static_cast<int>(std::ceil(50.f / std::max(bin_hz, 1e-6f))));
    int bin_hi = std::min(num_bins - 1, static_cast<int>(std::floor(0.4f * decimated_sample_rate / std::max(bin_hz, 1e-6f))));
    if (bin_hi <= bin_lo) {
        bin_lo = std::max(1, num_bins / 8);
        bin_hi = std::max(bin_lo + 1, num_bins / 2);
    }

    // 1a) Onset function: spectral flux over the mid-band (positive-only
    //     per-bin difference). Resists the cancellation problem that plagues
    //     summed-energy difference during pitch ramps — when a harmonic moves
    //     bins, only the "energy gained" side contributes. Used for DETECTION.
    std::vector<float> onset_fn(num_frames, 0.f);
    for (int f = 1; f < num_frames; ++f) {
        const float* row  = full_mag_db.data() + static_cast<size_t>(f)     * num_bins;
        const float* prev = full_mag_db.data() + static_cast<size_t>(f - 1) * num_bins;
        float flux = 0.f;
        for (int b = bin_lo; b < bin_hi; ++b) {
            float d = row[b] - prev[b];
            if (d > 0.f) flux += d;
        }
        onset_fn[f] = flux;
    }

    // 1b) Energy envelope (summed log-power over the same mid-band). Used for
    //     POSITIONING only: the flux peak sits on the leading edge of an
    //     attack, but the grain's Hann window should center on the loudest
    //     moment — otherwise the explosion lands off-center and the grain
    //     edges cut through neighboring cycles non-zero, producing the
    //     "starts/ends mid-explosion" artifact.
    std::vector<float> env(num_frames, 0.f);
    for (int f = 0; f < num_frames; ++f) {
        const float* row = full_mag_db.data() + static_cast<size_t>(f) * num_bins;
        float sum_pow = 0.f;
        for (int b = bin_lo; b < bin_hi; ++b) {
            float mag = std::pow(10.f, row[b] * 0.05f);
            sum_pow += mag * mag;
        }
        env[f] = 10.f * std::log10(std::max(sum_pow, 1e-12f));
    }

    // Refine a flux-detected frame to the nearby summed-energy maximum,
    // returning (frame_for_centering, sub-frame_offset_via_parabolic_on_env).
    // Search radius is small (±2 frames ≈ ±10 ms at 200 fps) so we don't
    // wander to a different cycle's peak.
    auto refine_to_env_peak = [&](int f) -> std::pair<int, float> {
        int radius = 2;
        int lo = std::max(1, f - radius);
        int hi = std::min(num_frames - 2, f + radius);
        int best_f = f;
        float best_v = (f >= 0 && f < num_frames) ? env[f] : -1e9f;
        for (int g = lo; g <= hi; ++g) {
            if (env[g] > best_v) { best_v = env[g]; best_f = g; }
        }
        float frac = parabolic_peak_offset(env[best_f - 1], env[best_f], env[best_f + 1]);
        return {best_f, frac};
    };

    // 2) Adaptive threshold. Two terms, take the max:
    //      (a) local-median + k*MAD, where sensitivity 0..1 maps k 4..0.
    //          At sensitivity=1 this collapses to median, letting weaker
    //          onsets through; at sensitivity=0 it's strict.
    //      (b) floor_frac * local p95, so high sensitivity still excludes
    //          regions whose typical peaks are tiny (silence/noise floor).
    int med_radius = std::max(2, static_cast<int>(std::round(0.5f / std::max(hz_min * seconds_per_frame, 1e-6f))));
    med_radius = std::min(med_radius, num_frames / 2);
    float k_thresh   = 4.f * (1.f - sensitivity);              // 4 .. 0
    // floor_frac was 0.10..0.30; raised to 0.20..0.45 because the lower
    // floor admitted sub-explosion ripples whose centers produced grains
    // with the actual explosion misaligned to the Hann window.
    float floor_frac = 0.20f + 0.25f * (1.f - sensitivity);    // 0.45 .. 0.20

    std::vector<float> threshold(num_frames, 0.f);
    std::vector<float> window;
    window.reserve(static_cast<size_t>(2 * med_radius + 2));
    for (int f = 0; f < num_frames; ++f) {
        int wlo = std::max(0, f - med_radius);
        int whi = std::min(num_frames - 1, f + med_radius);
        window.assign(onset_fn.begin() + wlo, onset_fn.begin() + whi + 1);
        int nw = static_cast<int>(window.size());
        int mid = nw / 2;
        int p95_idx = std::min(nw - 1, static_cast<int>(std::round(0.95f * (nw - 1))));

        std::nth_element(window.begin(), window.begin() + mid, window.end());
        float med = window[mid];

        // p95 lives in the upper partition created by the median nth_element.
        float p95_local;
        if (p95_idx > mid) {
            std::nth_element(window.begin() + mid + 1, window.begin() + p95_idx, window.end());
            p95_local = window[p95_idx];
        } else {
            p95_local = med;
        }

        for (auto& v : window) v = std::abs(v - med);
        std::nth_element(window.begin(), window.begin() + mid, window.end());
        float mad = window[mid];

        float mad_thresh   = med + k_thresh * std::max(mad, 1e-6f);
        float floor_thresh = floor_frac * p95_local;
        threshold[f] = std::max(mad_thresh, floor_thresh);
    }

    // 3) Peak picking with a refractory period of 1 / hz_max seconds, plus
    //    sub-frame parabolic interpolation around each peak.
    int refractory_frames = std::max(1,
        static_cast<int>(std::floor(1.f / std::max(hz_max * seconds_per_frame, 1e-6f))));

    std::vector<float> onset_times;
    std::vector<float> onset_strength;
    int last_peak_frame = -refractory_frames - 1;
    int picking_lo = std::max(1, frame_lo);
    int picking_hi = std::min(num_frames - 2, frame_hi);
    for (int f = picking_lo; f <= picking_hi; ++f) {
        if (onset_fn[f] < threshold[f]) continue;
        if (onset_fn[f] <= onset_fn[f - 1]) continue;
        if (onset_fn[f] < onset_fn[f + 1]) continue;

        // Detection frame is `f` (flux peak); position grain center on the
        // nearby energy peak so the loudest moment lands at the Hann center.
        auto [center_f, center_frac] = refine_to_env_peak(f);
        float t = (static_cast<float>(center_f) + center_frac) * seconds_per_frame;

        if (center_f - last_peak_frame < refractory_frames) {
            // Replace previous if the new peak is stronger; otherwise drop.
            if (!onset_strength.empty() && onset_fn[f] > onset_strength.back()) {
                onset_times.back() = t;
                onset_strength.back() = onset_fn[f];
                last_peak_frame = center_f;
            }
            continue;
        }

        onset_times.push_back(t);
        onset_strength.push_back(onset_fn[f]);
        last_peak_frame = center_f;
    }

    // 4) Gap-fill pass. For each inter-onset gap that's significantly larger
    //    than the local median IOI, search the gap for missed onsets at a
    //    relaxed threshold. Local (sliding ±5) median lets the expectation
    //    track frequency ramps. Single pass, no recursion — genuine silences
    //    must remain gaps so the resulting hz reflects the silence.
    {
        int n_initial = static_cast<int>(onset_times.size());
        if (n_initial >= 3) {
            auto local_median_ioi = [&](int center_idx) -> float {
                int lo = std::max(0, center_idx - 5);
                int hi = std::min(n_initial - 1, center_idx + 5);
                std::vector<float> iois;
                iois.reserve(static_cast<size_t>(hi - lo));
                for (int j = lo; j < hi; ++j)
                    iois.push_back(onset_times[j + 1] - onset_times[j]);
                if (iois.empty()) return -1.f;
                int m = static_cast<int>(iois.size()) / 2;
                std::nth_element(iois.begin(), iois.begin() + m, iois.end());
                return iois[m];
            };

            std::vector<float> filled_times;
            std::vector<float> filled_strengths;
            filled_times.reserve(onset_times.size() * 2);
            filled_strengths.reserve(onset_times.size() * 2);

            for (int i = 0; i < n_initial; ++i) {
                filled_times.push_back(onset_times[i]);
                filled_strengths.push_back(onset_strength[i]);

                if (i + 1 >= n_initial) continue;
                float gap = onset_times[i + 1] - onset_times[i];
                float expected = local_median_ioi(i);
                if (expected <= 0.f) continue;
                if (gap < 1.7f * expected) continue;

                int n_missing = std::max(0,
                    static_cast<int>(std::round(gap / expected)) - 1);
                if (n_missing < 1) continue;

                float search_lo = onset_times[i]     + 0.6f * expected;
                float search_hi = onset_times[i + 1] - 0.6f * expected;
                if (search_hi <= search_lo) continue;

                int f_lo = std::max(picking_lo,
                    static_cast<int>(std::ceil(search_lo / seconds_per_frame)));
                int f_hi = std::min(picking_hi,
                    static_cast<int>(std::floor(search_hi / seconds_per_frame)));
                if (f_hi <= f_lo) continue;

                // Gap-fill threshold is stricter than the relaxed 0.3× we
                // tried first: at 0.3× we accepted noise wiggles that
                // produced bad-quality grains. 0.6× requires the candidate
                // to be at least ~60% of the local rejection bar.
                struct Cand { int frame; float val; };
                std::vector<Cand> cands;
                for (int f = f_lo; f <= f_hi; ++f) {
                    if (onset_fn[f] < 0.6f * threshold[f]) continue;
                    if (onset_fn[f] <= onset_fn[f - 1]) continue;
                    if (onset_fn[f] < onset_fn[f + 1]) continue;
                    cands.push_back({f, onset_fn[f]});
                }
                if (cands.empty()) continue;

                std::sort(cands.begin(), cands.end(),
                    [](const Cand& a, const Cand& b) { return a.val > b.val; });

                int min_sep_frames = std::max(1,
                    static_cast<int>(std::round(0.6f * expected / seconds_per_frame)));
                std::vector<Cand> picked;
                for (const auto& c : cands) {
                    bool ok = true;
                    for (const auto& p : picked) {
                        if (std::abs(c.frame - p.frame) < min_sep_frames) { ok = false; break; }
                    }
                    if (ok) picked.push_back(c);
                    if (static_cast<int>(picked.size()) >= n_missing) break;
                }
                std::sort(picked.begin(), picked.end(),
                    [](const Cand& a, const Cand& b) { return a.frame < b.frame; });

                for (const auto& p : picked) {
                    auto [center_f, center_frac] = refine_to_env_peak(p.frame);
                    float t = (static_cast<float>(center_f) + center_frac) * seconds_per_frame;
                    filled_times.push_back(t);
                    filled_strengths.push_back(p.val);
                }
            }

            onset_times = std::move(filled_times);
            onset_strength = std::move(filled_strengths);
        }
    }

    // Cache onset_fn / threshold for the debug overlay.
    last_onset_frame_lo = picking_lo;
    last_onset_frame_hi = picking_hi;
    last_onset_fn.assign(onset_fn.begin() + picking_lo, onset_fn.begin() + picking_hi + 1);
    last_threshold.assign(threshold.begin() + picking_lo, threshold.begin() + picking_hi + 1);

    int n = static_cast<int>(onset_times.size());
    if (n < 2) return out;

    // Sketch-hint sampling — same gap-sentinel convention as track_fundamental.
    auto sketch_hz_at = [&](float t) -> float {
        int sn = sketch_hint.size();
        if (sn < 2) return -1.f;
        if (t < sketch_hint[0].x || t > sketch_hint[sn - 1].x) return -1.f;
        int lo = 0, hi = sn - 1;
        while (hi - lo > 1) {
            int m = (lo + hi) / 2;
            if (sketch_hint[m].x <= t) lo = m; else hi = m;
        }
        if (sketch_hint[lo].y <= 0.f || sketch_hint[hi].y <= 0.f) return -1.f;
        float dx = sketch_hint[hi].x - sketch_hint[lo].x;
        float u = (dx > 1e-9f) ? (t - sketch_hint[lo].x) / dx : 0.f;
        return sketch_hint[lo].y + u * (sketch_hint[hi].y - sketch_hint[lo].y);
    };

    // 5) Build per-onset hz from inter-onset interval, with optional sketch
    //    constraint, then 3-tap median on hz to suppress IOI jitter.
    std::vector<float> hz_raw(n);
    for (int i = 0; i < n; ++i) {
        float ioi;
        if (i + 1 < n) ioi = onset_times[i + 1] - onset_times[i];
        else           ioi = onset_times[i] - onset_times[i - 1];
        if (ioi > 1e-6f) hz_raw[i] = 1.f / ioi;
        else             hz_raw[i] = hz_min;
        if (hz_raw[i] < hz_min) hz_raw[i] = hz_min;
        if (hz_raw[i] > hz_max) hz_raw[i] = hz_max;
    }

    std::vector<int> keep;
    keep.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (sketch_hint.size() >= 2) {
            float hint = sketch_hz_at(onset_times[i]);
            if (hint > 0.f) {
                if (hz_raw[i] < hint * 0.5f || hz_raw[i] > hint * 2.0f) continue;
            }
        }
        keep.push_back(i);
    }
    int kn = static_cast<int>(keep.size());
    if (kn < 2) return out;

    out.resize(kn);
    Vector2* w = out.ptrw();
    for (int i = 0; i < kn; ++i) {
        int im = (i == 0) ? keep[0] : keep[i - 1];
        int ic = keep[i];
        int ip = (i + 1 < kn) ? keep[i + 1] : keep[i];
        float a = hz_raw[im], b = hz_raw[ic], c = hz_raw[ip];
        float med3 = std::max(std::min(a, b), std::min(std::max(a, b), c));
        w[i] = Vector2(onset_times[ic], med3);
    }
    return out;
}

Dictionary StreamWeaverFFT::get_last_onset_debug() const {
    Dictionary d;
    if (last_onset_fn.empty()) return d;

    PackedFloat32Array onset_arr;
    onset_arr.resize(static_cast<int>(last_onset_fn.size()));
    std::memcpy(onset_arr.ptrw(), last_onset_fn.data(),
        last_onset_fn.size() * sizeof(float));

    PackedFloat32Array thresh_arr;
    thresh_arr.resize(static_cast<int>(last_threshold.size()));
    std::memcpy(thresh_arr.ptrw(), last_threshold.data(),
        last_threshold.size() * sizeof(float));

    d["onset_fn"] = onset_arr;
    d["threshold"] = thresh_arr;
    d["frame_lo"] = last_onset_frame_lo;
    d["frame_hi"] = last_onset_frame_hi;
    d["seconds_per_frame"] = seconds_per_frame;
    return d;
}

Dictionary StreamWeaverFFT::get_last_track_debug() const {
    Dictionary d;
    if (last_track_obs.empty()) return d;

    PackedFloat32Array obs_arr;
    obs_arr.resize(static_cast<int>(last_track_obs.size()));
    std::memcpy(obs_arr.ptrw(), last_track_obs.data(),
        last_track_obs.size() * sizeof(float));

    d["obs"] = obs_arr;
    d["seed_peak"] = last_track_seed_peak;
    d["stop_thresh"] = last_track_stop_thresh;
    d["frame_lo"] = last_track_frame_lo;
    d["frame_hi"] = last_track_frame_hi;
    d["start_frame"] = last_track_start_frame;
    d["seconds_per_frame"] = seconds_per_frame;
    d["analysis_db_floor"] = analysis_db_floor;
    return d;
}

PackedFloat32Array StreamWeaverFFT::get_mono_pcm_packed() const {
    PackedFloat32Array out;
    int n = static_cast<int>(mono_pcm.size());
    out.resize(n);
    float* ptr = out.ptrw();
    for (int i = 0; i < n; ++i) ptr[i] = mono_pcm[i];
    return out;
}

PackedFloat32Array StreamWeaverFFT::get_original_mono_pcm_packed() const {
    PackedFloat32Array out;
    int n = static_cast<int>(original_mono_pcm.size());
    out.resize(n);
    float* ptr = out.ptrw();
    for (int i = 0; i < n; ++i) ptr[i] = original_mono_pcm[i];
    return out;
}
