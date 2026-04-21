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

} // namespace

// -------------------- StreamWeaverFFT --------------------

void StreamWeaverFFT::_bind_methods() {
    ClassDB::bind_static_method("StreamWeaverFFT", D_METHOD("create_from_audio_stream",
        "stream", "fft_size", "db_floor", "y_resolution", "x_resolution_per_second",
        "min_hz", "max_hz"),
        &StreamWeaverFFT::create_from_audio_stream);

    ClassDB::bind_method(D_METHOD("set_display_settings",
        "display_db_min", "display_db_max", "gamma", "per_frame_normalize", "spectral_whiten"),
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
        "transition_penalty", "sketch_hint", "sketch_weight"),
        &StreamWeaverFFT::track_fundamental);
    ClassDB::bind_method(D_METHOD("reconstruct_isolated", "tracked_curve", "bandwidth_hz"),
        &StreamWeaverFFT::reconstruct_isolated);
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
    result->display_min_hz = min_hz;
    result->display_max_hz = std::min(max_hz, decimated_rate * 0.5f);

    result->display_db_min = db_floor;
    result->display_db_max = 0.f;
    result->gamma_value = 1.0f;
    result->per_frame_normalize = false;
    result->spectral_whiten = false;
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
    bool p_spectral_whiten)
{
    display_db_min = p_display_db_min;
    display_db_max = p_display_db_max;
    gamma_value = std::max(0.01f, p_gamma);
    per_frame_normalize = p_per_frame_normalize;
    spectral_whiten = p_spectral_whiten;
    rebuild_image_internal();
}

void StreamWeaverFFT::rebuild_image_internal() {
    if (num_frames <= 0 || num_bins <= 0 || y_resolution <= 0) return;

    std::vector<float> work = full_mag_db;
    apply_transforms(work, spectral_whiten, per_frame_normalize);

    int64_t pixel_count = static_cast<int64_t>(num_frames) * y_resolution;
    PackedByteArray pixels;
    pixels.resize(pixel_count);
    uint8_t* w = pixels.ptrw();

    float range = display_db_max - display_db_min;
    if (range < 1e-6f) range = 1e-6f;
    float inv_range = 1.f / range;
    float inv_gamma = 1.f / gamma_value;

    float span = display_max_hz - display_min_hz;
    if (span <= 0) span = bin_hz;

    for (int y = 0; y < y_resolution; ++y) {
        // Image y=0 = top = display_max_hz.
        float t_top = 1.f - static_cast<float>(y) / y_resolution;
        float t_bot = 1.f - static_cast<float>(y + 1) / y_resolution;
        float hz_top = display_min_hz + t_top * span;
        float hz_bot = display_min_hz + t_bot * span;
        int b_lo = std::max(0, static_cast<int>(std::floor(hz_bot / bin_hz)));
        int b_hi = std::min(num_bins - 1, static_cast<int>(std::ceil(hz_top / bin_hz)));
        if (b_hi < b_lo) b_hi = b_lo;
        for (int f = 0; f < num_frames; ++f) {
            const float* row = work.data() + static_cast<size_t>(f) * num_bins;
            float peak = -1e30f;
            for (int b = b_lo; b <= b_hi; ++b) {
                if (row[b] > peak) peak = row[b];
            }
            float t = (peak - display_db_min) * inv_range;
            if (t < 0) t = 0;
            else if (t > 1) t = 1;
            t = std::pow(t, inv_gamma);
            w[static_cast<int64_t>(y) * num_frames + f] = static_cast<uint8_t>(t * 255.f);
        }
    }

    image = Image::create_from_data(num_frames, y_resolution, false, Image::FORMAT_L8, pixels);
}

float StreamWeaverFFT::pixel_x_to_time(int px) const {
    if (num_frames <= 0) return 0;
    return px * seconds_per_frame;
}

float StreamWeaverFFT::pixel_y_to_hz(int py) const {
    if (y_resolution <= 0) return 0;
    float t = 1.f - (static_cast<float>(py) + 0.5f) / y_resolution;
    return display_min_hz + t * (display_max_hz - display_min_hz);
}

int StreamWeaverFFT::hz_to_pixel_y(float hz) const {
    if (y_resolution <= 0 || display_max_hz <= display_min_hz) return 0;
    float t = (hz - display_min_hz) / (display_max_hz - display_min_hz);
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
        if (best_cur < 0) break;
        if (obs_at(f, best_cur) < stop_thresh_db) break;

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
    float sketch_weight) const
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
    float stop_thresh_db = seed_peak - 20.f;

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

    out.resize(static_cast<int>(merged.size()));
    for (int i = 0; i < static_cast<int>(merged.size()); ++i) out[i] = merged[i];
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
