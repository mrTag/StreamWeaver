#include "GrainsDatabaseResource.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream.hpp"
#include "godot_cpp/classes/audio_stream_playback.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

#include "kiss_fft.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {
constexpr float kPi = 3.14159265358979323846f;

static uint8_t encode_ulaw(float sample) {
    constexpr float mu = 255.f;
    sample = std::clamp(sample, -1.0f, 1.0f);
    float sign = (sample < 0.f) ? -1.f : 1.f;
    float compressed = sign * std::log1p(mu * std::abs(sample)) / std::log1p(mu);
    // Map [-1, 1] → [0, 255]
    return static_cast<uint8_t>(std::clamp(static_cast<int>((compressed + 1.0f) * 127.5f), 0, 255));
}

static void init_ulaw_lut(float out[256]) {
    constexpr float mu = 255.f;
    const float log_mu1 = std::log(1.f + mu); // log(256)
    for (int u = 0; u < 256; ++u) {
        float y = static_cast<float>(u) / 127.5f - 1.0f; // [-1, 1]
        float sign = (y < 0.f) ? -1.f : 1.f;
        out[u] = sign * (std::exp(std::abs(y) * log_mu1) - 1.f) / mu;
    }
}
} // namespace

// Static member definitions
float StreamWeaverGrainsDatabase::ulaw_decode_lut[256];

using namespace godot;

// -------------------- Helpers --------------------

float StreamWeaverGrainsDatabase::lookup_curve(const PackedVector2Array& curve, float time_s,
                                               bool treat_zero_as_gap) {
    int n = curve.size();
    if (n == 0) return 0.f;
    if (time_s <= curve[0].x) return curve[0].y;
    if (time_s >= curve[n - 1].x) return curve[n - 1].y;
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if (curve[m].x <= time_s) lo = m; else hi = m;
    }
    // Gap sentinels (y <= 0) mark frames where tracking dropped below the
    // stop threshold; treat the surrounding interval as untracked so the
    // grain extractor (which checks `f0 <= 0`) will skip these times rather
    // than interpolate across the gap. This only applies to the pitch
    // tracking curve — parameter automation keyframes use 0 as a valid value.
    if (treat_zero_as_gap && (curve[lo].y <= 0.f || curve[hi].y <= 0.f)) return -1.f;
    float t = (time_s - curve[lo].x) / (curve[hi].x - curve[lo].x);
    return curve[lo].y + t * (curve[hi].y - curve[lo].y);
}

std::vector<float> StreamWeaverGrainsDatabase::resample_linear(
    const std::vector<float>& src, float src_rate, float dst_rate) {
    if (src.empty() || src_rate <= 0 || dst_rate <= 0) return {};
    double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    int dst_len = static_cast<int>(std::ceil(src.size() * ratio));
    std::vector<float> dst(dst_len);
    for (int i = 0; i < dst_len; ++i) {
        double src_pos = i / ratio;
        int idx = static_cast<int>(src_pos);
        float frac = static_cast<float>(src_pos - idx);
        if (idx + 1 < static_cast<int>(src.size())) {
            dst[i] = src[idx] * (1.f - frac) + src[idx + 1] * frac;
        } else if (idx < static_cast<int>(src.size())) {
            dst[i] = src[idx];
        }
    }
    return dst;
}

void bandpass_block_kissfft(
    const std::vector<float>& in,
    std::vector<float>& out,
    int sample_rate,
    float center_hz,
    float bandwidth_ratio // e.g. 0.2 = ±20%
) {
    int N = in.size();
    kiss_fft_cfg cfg_fwd = kiss_fft_alloc(N, 0, nullptr, nullptr);
    kiss_fft_cfg cfg_inv = kiss_fft_alloc(N, 1, nullptr, nullptr);

    std::vector<kiss_fft_cpx> freq(N);
    std::vector<kiss_fft_cpx> time(N);

    // Forward FFT
    for (int i = 0; i < N; ++i) {
        time[i].r = in[i];
        time[i].i = 0.f;
    }
    kiss_fft(cfg_fwd, time.data(), freq.data());

    float bin_hz = (float)sample_rate / (float)N;
    float low = center_hz * (1.f - bandwidth_ratio);
    float high = center_hz * (1.f + bandwidth_ratio);

    for (int k = 0; k < N; ++k) {
        float hz = k * bin_hz;

        bool keep = (hz >= low && hz <= high) ||
                    (hz >= sample_rate - high && hz <= sample_rate - low);

        if (!keep) {
            freq[k].r = 0.f;
            freq[k].i = 0.f;
        }
    }

    // Inverse FFT
    kiss_fft(cfg_inv, freq.data(), time.data());

    out.resize(N);
    for (int i = 0; i < N; ++i) {
        out[i] = time[i].r / (float)N;
    }

    free(cfg_fwd);
    free(cfg_inv);
}

// -------------------- Build --------------------

bool StreamWeaverGrainsDatabase::decode_source_pcm(
    Ref<AudioStream> source_stream,
    std::vector<float>& out_pcm,
    float& out_rate) {

    out_pcm.clear();
    out_rate = 0.f;

    if (source_stream.is_null()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: source stream is null");
        return false;
    }

    float mix_rate = 48000.f;
    if (AudioServer::get_singleton()) {
        mix_rate = AudioServer::get_singleton()->get_mix_rate();
    }

    double stream_length = source_stream->get_length();
    if (stream_length <= 0) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: source stream has zero length");
        return false;
    }
    int num_samples = static_cast<int>(std::ceil(stream_length * mix_rate));
    Ref<AudioStreamPlayback> pb = source_stream->instantiate_playback();
    if (pb.is_null()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: failed to instantiate playback");
        return false;
    }
    pb->start(0.0);
    PackedVector2Array stereo = pb->mix_audio(1.0f, num_samples);
    pb->stop();

    out_pcm.resize(stereo.size());
    const Vector2* sr = stereo.ptr();
    for (int i = 0; i < stereo.size(); ++i) {
        out_pcm[i] = 0.5f * (sr[i].x + sr[i].y);
    }
    if (out_pcm.empty()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: decoded stream is empty");
        return false;
    }
    out_rate = mix_rate;
    return true;
}

String StreamWeaverGrainsDatabase::parse_or_validate_axis_configs(
    TypedArray<Dictionary> axis_configs,
    std::vector<AxisBuildInfo>& out_build) {

    int incoming_n = std::min(static_cast<int>(axis_configs.size()), MAX_AXES);
    out_build.clear();
    out_build.resize(incoming_n);

    // Decode incoming configs into a local vector first so we can validate
    // before mutating this->axes.
    std::vector<GrainAxisDef> incoming_axes(incoming_n);
    for (int i = 0; i < incoming_n; ++i) {
        Dictionary cfg = axis_configs[i];
        incoming_axes[i].name = cfg.get("name", StringName("axis_" + String::num_int64(i)));
        incoming_axes[i].min_val = cfg.get("min_value", 0.0f);
        incoming_axes[i].max_val = cfg.get("max_value", 1.0f);
        incoming_axes[i].derived_from_fundamental = cfg.get("derived_from_fundamental", false);
        out_build[i].derived_from_fundamental = incoming_axes[i].derived_from_fundamental;
        out_build[i].min_val = incoming_axes[i].min_val;
        out_build[i].max_val = incoming_axes[i].max_val;
        out_build[i].cal_a = cfg.get("calibration_a", Vector2(0, 0));
        out_build[i].cal_b = cfg.get("calibration_b", Vector2(1, 1));
        out_build[i].keyframes = cfg.get("keyframes", PackedVector2Array());
    }

    if (axes.empty()) {
        // Adopt the incoming axes as the database's definition.
        axes = incoming_axes;
        num_axes_stored = incoming_n;
        return String();
    }

    // Validate against existing axes — name / range / derived flag must match.
    if (static_cast<int>(axes.size()) != incoming_n) {
        return String("axis count mismatch: database has ") + String::num_int64(static_cast<int>(axes.size())) +
               String(", import provides ") + String::num_int64(incoming_n);
    }
    for (int i = 0; i < incoming_n; ++i) {
        const auto& exist = axes[i];
        const auto& incoming = incoming_axes[i];
        if (exist.name != incoming.name) {
            return String("axis ") + String::num_int64(i) + String(" name mismatch: '") +
                   String(exist.name) + String("' vs '") + String(incoming.name) + String("'");
        }
        if (std::abs(exist.min_val - incoming.min_val) > 1e-6f ||
            std::abs(exist.max_val - incoming.max_val) > 1e-6f) {
            return String("axis ") + String::num_int64(i) + String(" range mismatch on '") +
                   String(exist.name) + String("'");
        }
        if (exist.derived_from_fundamental != incoming.derived_from_fundamental) {
            return String("axis ") + String::num_int64(i) + String(" derived flag mismatch on '") +
                   String(exist.name) + String("'");
        }
    }
    return String();
}

float StreamWeaverGrainsDatabase::compute_param_value(
    const AxisBuildInfo& ab, float time_s, float f0_hz) {

    float val;
    if (ab.derived_from_fundamental) {
        float hz_range = ab.cal_b.x - ab.cal_a.x;
        if (std::abs(hz_range) < 1e-6f)
            val = ab.cal_a.y;
        else {
            float t = (f0_hz - ab.cal_a.x) / hz_range;
            val = ab.cal_a.y + t * (ab.cal_b.y - ab.cal_a.y);
        }
    } else {
        val = lookup_curve(ab.keyframes, time_s);
    }
    return std::clamp(val, ab.min_val, ab.max_val);
}

void StreamWeaverGrainsDatabase::encode_sample(uint8_t* dst, float f) const {
    switch (compression_mode) {
        case FLOAT32:
            std::memcpy(dst, &f, 4);
            break;
        case INT16: {
            int16_t v = static_cast<int16_t>(std::clamp(f * 32768.f, -32768.f, 32767.f));
            std::memcpy(dst, &v, 2);
            break;
        }
        case ULAW8:
            *dst = encode_ulaw(f);
            break;
    }
}

int StreamWeaverGrainsDatabase::append_to_pcm_pool(const std::vector<float>& decoded) {
    int offset = pcm_sample_count();
    int bps = bytes_per_sample();
    size_t old_bytes = pcm_pool_bytes.size();
    pcm_pool_bytes.resize(old_bytes + decoded.size() * bps);
    uint8_t* dst = pcm_pool_bytes.data() + old_bytes;
    for (int i = 0; i < static_cast<int>(decoded.size()); ++i) {
        encode_sample(dst + i * bps, decoded[i]);
    }
    return offset;
}

int StreamWeaverGrainsDatabase::intern_source_path(const String& path) {
    if (path.is_empty()) return -1;
    for (int i = 0; i < source_paths.size(); ++i) {
        if (source_paths[i] == path) return i;
    }
    source_paths.push_back(path);
    return source_paths.size() - 1;
}

PackedInt32Array StreamWeaverGrainsDatabase::append_from_fft(
    Ref<StreamWeaverFFT> fft,
    Ref<AudioStream> source_stream,
    String source_path,
    PackedVector2Array tracked_curve,
    TypedArray<Dictionary> axis_configs,
    int cycles_per_grain,
    float crossfade_cycles,
    float energy_threshold_db,
    PackedVector2Array sketch_hint) {

    PackedInt32Array added;
    if (fft.is_null() || tracked_curve.size() < 2) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: invalid FFT or tracked curve");
        return added;
    }

    // Clamp parameters
    cycles_per_grain = CLAMP(cycles_per_grain, 2, 6);
    crossfade_cycles = CLAMP(crossfade_cycles, 0.25f, 1.0f);

    // Decode the original AudioStream directly at mix rate.
    std::vector<float> work_pcm;
    float work_rate = 0.f;
    if (!decode_source_pcm(source_stream, work_pcm, work_rate)) return added;
    int total_samples = static_cast<int>(work_pcm.size());

    // Validate or adopt sample rate. Mixing rates inside one DB would break
    // both runtime playback and grain audition.
    if (!grains.empty() && std::abs(stored_sample_rate - work_rate) > 0.5f) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: import sample rate ", work_rate,
            " differs from database rate ", stored_sample_rate, "; aborting append");
        return added;
    }
    stored_sample_rate = work_rate;

    std::vector<AxisBuildInfo> axis_build;
    String err = parse_or_validate_axis_configs(axis_configs, axis_build);
    if (!err.is_empty()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: axis validation failed: ", err);
        return added;
    }

    // Get tracked curve time range
    float curve_start = tracked_curve[0].x;
    float curve_end = tracked_curve[tracked_curve.size() - 1].x;

    // Prepare energy check using FFT mag data
    const std::vector<float>& mag_db = fft->get_full_mag_db();
    int fft_num_frames = fft->get_num_frames();
    int fft_num_bins = fft->get_num_bins();
    float fft_spf = fft->get_seconds_per_frame();
    float fft_bin_hz = fft->get_bin_hz();

    auto get_harmonic_energy = [&](float time_s, float f0_hz) -> float {
        if (fft_num_frames == 0 || fft_num_bins == 0 || fft_bin_hz <= 0) return 0.f;
        int frame = static_cast<int>(std::round(time_s / fft_spf));
        frame = CLAMP(frame, 0, fft_num_frames - 1);
        float sum_db = 0;
        int count = 0;
        for (int h = 1; h <= 4; ++h) {
            int bin = static_cast<int>(std::round(f0_hz * h / fft_bin_hz));
            if (bin >= 0 && bin < fft_num_bins) {
                sum_db += mag_db[frame * fft_num_bins + bin];
                ++count;
            }
        }
        return count > 0 ? sum_db / count : -100.f;
    };

    auto sketch_hz_at = [&](float t) -> float {
        int n = sketch_hint.size();
        if (n < 2) return -1.f;
        if (t < sketch_hint[0].x || t > sketch_hint[n - 1].x) return -1.f;
        int lo = 0, hi = n - 1;
        while (hi - lo > 1) {
            int m = (lo + hi) / 2;
            if (sketch_hint[m].x <= t) lo = m; else hi = m;
        }
        if (sketch_hint[lo].y <= 0.f || sketch_hint[hi].y <= 0.f) return -1.f;
        float dx = sketch_hint[hi].x - sketch_hint[lo].x;
        float u = (dx > 1e-9f) ? (t - sketch_hint[lo].x) / dx : 0.f;
        return sketch_hint[lo].y + u * (sketch_hint[hi].y - sketch_hint[lo].y);
    };

    // Append the source PCM to the shared pool. New grain center_sample
    // values are computed in the LOCAL `work_pcm` index space and then
    // shifted by `pool_offset` so they reference the right samples in pcm_pool.
    int pool_offset = append_to_pcm_pool(work_pcm);
    int source_id = intern_source_path(source_path);

    int cursor = static_cast<int>(std::round(curve_start * work_rate));
    cursor = CLAMP(cursor, 0, total_samples - 1);

    int starting_count = static_cast<int>(grains.size());

    while (cursor < total_samples) {
        float time_s = static_cast<float>(cursor) / work_rate;
        if (time_s > curve_end) break;
        if (time_s < curve_start) {
            cursor += 64;
            continue;
        }
        if (sketch_hint.size() >= 2 && sketch_hz_at(time_s) <= 0.f) {
            cursor += 64;
            continue;
        }

        float f0 = lookup_curve(tracked_curve, time_s, /*treat_zero_as_gap=*/true);
        if (f0 <= 0) {
            cursor += 64;
            continue;
        }

        float energy = get_harmonic_energy(time_s, f0);
        bool low_energy = energy < energy_threshold_db;

        int period_samples = static_cast<int>(std::round(work_rate / f0));
        if (period_samples < 2) {
            cursor += 1;
            continue;
        }

        int search_range = std::max(4, period_samples / 8);
        int best_zc = cursor;
        float best_dist = static_cast<float>(search_range + 1);
        for (int s = -search_range; s <= search_range; ++s) {
            int idx = cursor + s;
            if (idx < 0 || idx + 1 >= total_samples) continue;
            if (work_pcm[idx] <= 0.f && work_pcm[idx + 1] > 0.f) {
                float dist = std::abs(static_cast<float>(s));
                if (dist < best_dist) {
                    best_dist = dist;
                    best_zc = idx;
                }
            }
        }
        int local_center_sample = best_zc;
        float center_time = static_cast<float>(local_center_sample) / work_rate;
        float center_f0 = lookup_curve(tracked_curve, center_time, /*treat_zero_as_gap=*/true);
        if (center_f0 <= 0.f) {
            cursor += period_samples;
            continue;
        }

        GrainEntry entry;
        entry.center_sample = local_center_sample + pool_offset;
        entry.fundamental_hz = center_f0;
        entry.original_time_s = center_time;
        entry.low_energy = low_energy;
        entry.source_id = source_id;
        for (int a = 0; a < num_axes_stored; ++a) {
            entry.params[a] = compute_param_value(axis_build[a], center_time, center_f0);
        }
        grains.push_back(entry);
        added.push_back(static_cast<int>(grains.size()) - 1);

        cursor = local_center_sample + std::max(1, period_samples);
    }

    index_dirty = true;
    rebuild_index();

    UtilityFunctions::print("StreamWeaverGrainsDatabase: appended ",
        added.size(), " units (total ", static_cast<int>(grains.size()),
        "), PCM pool: ", pcm_sample_count(), " samples");
    (void)starting_count;
    return added;
}

// -------------------- Append (Low Hz mode, onset-driven) --------------------

PackedInt32Array StreamWeaverGrainsDatabase::append_from_onsets(
    Ref<StreamWeaverFFT> fft,
    Ref<AudioStream> source_stream,
    String source_path,
    PackedVector2Array onsets,
    TypedArray<Dictionary> axis_configs,
    float energy_threshold_db) {

    PackedInt32Array added;
    if (fft.is_null()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: FFT is null");
        return added;
    }
    if (onsets.size() < 1) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: onset list is empty");
        return added;
    }

    std::vector<float> work_pcm;
    float work_rate = 0.f;
    if (!decode_source_pcm(source_stream, work_pcm, work_rate)) return added;
    int total_samples = static_cast<int>(work_pcm.size());

    if (!grains.empty() && std::abs(stored_sample_rate - work_rate) > 0.5f) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: import sample rate ", work_rate,
            " differs from database rate ", stored_sample_rate, "; aborting append");
        return added;
    }
    stored_sample_rate = work_rate;

    std::vector<AxisBuildInfo> axis_build;
    String err = parse_or_validate_axis_configs(axis_configs, axis_build);
    if (!err.is_empty()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: axis validation failed: ", err);
        return added;
    }

    const std::vector<float>& mag_db = fft->get_full_mag_db();
    int fft_num_frames = fft->get_num_frames();
    int fft_num_bins = fft->get_num_bins();
    float fft_spf = fft->get_seconds_per_frame();
    float fft_bin_hz = fft->get_bin_hz();

    auto get_harmonic_energy = [&](float time_s, float f0_hz) -> float {
        if (fft_num_frames == 0 || fft_num_bins == 0 || fft_bin_hz <= 0) return 0.f;
        int frame = static_cast<int>(std::round(time_s / fft_spf));
        frame = CLAMP(frame, 0, fft_num_frames - 1);
        float sum_db = 0;
        int count = 0;
        for (int h = 1; h <= 4; ++h) {
            int bin = static_cast<int>(std::round(f0_hz * h / fft_bin_hz));
            if (bin >= 0 && bin < fft_num_bins) {
                sum_db += mag_db[frame * fft_num_bins + bin];
                ++count;
            }
        }
        return count > 0 ? sum_db / count : -100.f;
    };

    int pool_offset = append_to_pcm_pool(work_pcm);
    int source_id = intern_source_path(source_path);

    int n_onsets = onsets.size();
    const Vector2* op = onsets.ptr();
    for (int i = 0; i < n_onsets; ++i) {
        float t = op[i].x;
        float f0 = op[i].y;
        if (f0 <= 0.f) continue;

        int local_center_sample = static_cast<int>(std::round(t * work_rate));
        if (local_center_sample < 0 || local_center_sample >= total_samples) continue;

        float energy = get_harmonic_energy(t, f0);
        bool low_energy = energy < energy_threshold_db;

        GrainEntry entry;
        entry.center_sample = local_center_sample + pool_offset;
        entry.fundamental_hz = f0;
        entry.original_time_s = t;
        entry.low_energy = low_energy;
        entry.source_id = source_id;
        for (int a = 0; a < num_axes_stored; ++a) {
            entry.params[a] = compute_param_value(axis_build[a], t, f0);
        }
        grains.push_back(entry);
        added.push_back(static_cast<int>(grains.size()) - 1);
    }

    index_dirty = true;
    rebuild_index();

    UtilityFunctions::print("StreamWeaverGrainsDatabase: appended ",
        added.size(), " onset units (total ", static_cast<int>(grains.size()),
        "), PCM pool: ", pcm_sample_count(), " samples");
    return added;
}

// -------------------- Manual marker extraction --------------------

PackedInt32Array StreamWeaverGrainsDatabase::append_from_manual_markers(
    Ref<StreamWeaverFFT> fft,
    Ref<AudioStream> source_stream,
    String source_path,
    PackedFloat64Array sorted_marker_times_s,
    TypedArray<Dictionary> axis_configs,
    float energy_threshold_db,
    PackedVector2Array tracked_curve) {

    PackedInt32Array added;
    int n_markers = sorted_marker_times_s.size();
    if (n_markers < 2) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: need at least 2 markers to extract grains");
        return added;
    }

    std::vector<float> work_pcm;
    float work_rate = 0.f;
    if (!decode_source_pcm(source_stream, work_pcm, work_rate)) return added;
    int total_samples = static_cast<int>(work_pcm.size());

    if (!grains.empty() && std::abs(stored_sample_rate - work_rate) > 0.5f) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: import sample rate ", work_rate,
            " differs from database rate ", stored_sample_rate, "; aborting append");
        return added;
    }
    stored_sample_rate = work_rate;

    std::vector<AxisBuildInfo> axis_build;
    String err = parse_or_validate_axis_configs(axis_configs, axis_build);
    if (!err.is_empty()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: axis validation failed: ", err);
        return added;
    }

    // Optional energy check via FFT.
    const std::vector<float>* mag_db_ptr = nullptr;
    int fft_num_frames = 0, fft_num_bins = 0;
    float fft_spf = 0.f, fft_bin_hz = 0.f;
    if (fft.is_valid()) {
        mag_db_ptr = &fft->get_full_mag_db();
        fft_num_frames = fft->get_num_frames();
        fft_num_bins = fft->get_num_bins();
        fft_spf = fft->get_seconds_per_frame();
        fft_bin_hz = fft->get_bin_hz();
    }

    auto get_harmonic_energy = [&](float time_s, float f0_hz) -> float {
        if (!mag_db_ptr || fft_num_frames == 0 || fft_num_bins == 0 || fft_bin_hz <= 0) return 0.f;
        int frame = static_cast<int>(std::round(time_s / fft_spf));
        frame = CLAMP(frame, 0, fft_num_frames - 1);
        float sum_db = 0;
        int count = 0;
        for (int h = 1; h <= 4; ++h) {
            int bin = static_cast<int>(std::round(f0_hz * h / fft_bin_hz));
            if (bin >= 0 && bin < fft_num_bins) {
                sum_db += (*mag_db_ptr)[frame * fft_num_bins + bin];
                ++count;
            }
        }
        return count > 0 ? sum_db / count : -100.f;
    };

    int pool_offset = append_to_pcm_pool(work_pcm);
    int source_id = intern_source_path(source_path);

    // Re-snap every marker to the nearest upward zero-crossing in the
    // full-resolution PCM.  The editor snaps against a decimated display
    // waveform, so zero-crossings can be off by a few samples; doing a final
    // pass here using the actual audio data (with a narrow period/8 window)
    // gives each grain a consistent phase reference without risk of jumping
    // to the wrong cycle.
    const double* markers = sorted_marker_times_s.ptr();
    std::vector<int> snapped_samples(n_markers);
    for (int i = 0; i < n_markers; ++i) {
        int si = static_cast<int>(std::round(static_cast<float>(markers[i]) * work_rate));
        si = std::clamp(si, 0, total_samples - 1);
        float interval_s = 0.0f;
        if (i + 1 < n_markers) interval_s = static_cast<float>(markers[i + 1] - markers[i]);
        else if (i > 0)        interval_s = static_cast<float>(markers[i] - markers[i - 1]);
        int period_samp = (interval_s > 0.0f)
            ? static_cast<int>(std::round(interval_s * work_rate)) : 0;
        int search_range = (period_samp > 0)
            ? std::max(4, period_samp / 8)
            : std::max(4, static_cast<int>(work_rate * 0.01f));
        int best = si, best_d = search_range + 1;
        for (int s = -search_range; s <= search_range; ++s) {
            int idx = si + s;
            if (idx <= 0 || idx + 1 >= total_samples) continue;
            if (work_pcm[idx] <= 0.f && work_pcm[idx + 1] > 0.f) {
                int d = std::abs(s);
                if (d < best_d) { best_d = d; best = idx; }
            }
        }
        snapped_samples[i] = best;
    }

    for (int i = 0; i < n_markers - 1; ++i) {
        int s_start = snapped_samples[i];
        int s_end   = snapped_samples[i + 1];
        if (s_end <= s_start) continue;

        float t_start = static_cast<float>(s_start) / work_rate;
        float t_end   = static_cast<float>(s_end)   / work_rate;
        float center_t      = (t_start + t_end) * 0.5f;
        float structural_f0 = 1.0f / (t_end - t_start); // period for windowing / energy
        float tracked_f0    = lookup_curve(tracked_curve, center_t, /*treat_zero_as_gap=*/true); // pitch at this time
        int   local_center  = static_cast<int>(std::round(center_t * work_rate));
        if (local_center < 0 || local_center >= total_samples) continue;

        float energy   = mag_db_ptr ? get_harmonic_energy(center_t, structural_f0) : 0.f;
        bool low_energy = mag_db_ptr && (energy < energy_threshold_db);

        GrainEntry entry;
        entry.center_sample   = local_center + pool_offset;
        entry.fundamental_hz  = structural_f0;
        entry.original_time_s = center_t;
        entry.low_energy      = low_energy;
        entry.source_id       = source_id;
        for (int a = 0; a < num_axes_stored; ++a) {
            // "from fundamental" axes use the tracked pitch at center time,
            // matching the semantics of FFT extraction.
            float f0_for_axis = axis_build[a].derived_from_fundamental ? tracked_f0 : structural_f0;
            entry.params[a] = compute_param_value(axis_build[a], center_t, f0_for_axis);
        }
        grains.push_back(entry);
        added.push_back(static_cast<int>(grains.size()) - 1);
    }

    index_dirty = true;
    rebuild_index();

    UtilityFunctions::print("StreamWeaverGrainsDatabase: appended ",
        added.size(), " manual grains (total ", static_cast<int>(grains.size()),
        "), PCM pool: ", pcm_sample_count(), " samples");
    return added;
}

// -------------------- Editing / Compaction --------------------

bool StreamWeaverGrainsDatabase::delete_grain(int index) {
    if (index < 0 || index >= static_cast<int>(grains.size())) return false;
    grains.erase(grains.begin() + index);
    index_dirty = true;
    rebuild_index();
    emit_changed();
    return true;
}

int StreamWeaverGrainsDatabase::delete_grains(PackedInt32Array indices) {
    if (indices.is_empty() || grains.empty()) return 0;
    // Sort descending so erase doesn't shift earlier indices.
    std::vector<int> sorted_idx;
    sorted_idx.reserve(indices.size());
    for (int i = 0; i < indices.size(); ++i) sorted_idx.push_back(indices[i]);
    std::sort(sorted_idx.begin(), sorted_idx.end(), std::greater<int>());
    int prev = -1;
    int deleted = 0;
    for (int idx : sorted_idx) {
        if (idx == prev) continue; // dedup
        prev = idx;
        if (idx < 0 || idx >= static_cast<int>(grains.size())) continue;
        grains.erase(grains.begin() + idx);
        ++deleted;
    }
    if (deleted > 0) {
        index_dirty = true;
        rebuild_index();
        emit_changed();
    }
    return deleted;
}

int StreamWeaverGrainsDatabase::orphan_pcm_bytes() const {
    int pool_size = pcm_sample_count();
    if (pool_size == 0) return 0;
    if (grains.empty()) return pool_size * bytes_per_sample();

    // Build [start, end) intervals for each grain's window, clipped to pool.
    std::vector<std::pair<int, int>> intervals;
    intervals.reserve(grains.size());
    for (const auto& g : grains) {
        if (g.fundamental_hz <= 0.f) continue;
        int period_samples = std::max(1, static_cast<int>(std::round(stored_sample_rate / g.fundamental_hz)));
        int half_window = period_samples;
        int start = std::max(0, g.center_sample - half_window);
        int end   = std::min(pool_size, g.center_sample + half_window);
        if (end > start) intervals.emplace_back(start, end);
    }
    if (intervals.empty()) return pool_size * bytes_per_sample();

    std::sort(intervals.begin(), intervals.end());
    int referenced = 0;
    int cur_start = intervals[0].first;
    int cur_end   = intervals[0].second;
    for (size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i].first <= cur_end) {
            cur_end = std::max(cur_end, intervals[i].second);
        } else {
            referenced += cur_end - cur_start;
            cur_start = intervals[i].first;
            cur_end   = intervals[i].second;
        }
    }
    referenced += cur_end - cur_start;
    int orphan_samples = pool_size - referenced;
    if (orphan_samples < 0) orphan_samples = 0;
    return orphan_samples * bytes_per_sample();
}

bool StreamWeaverGrainsDatabase::is_compact() const {
    return orphan_pcm_bytes() == 0;
}

void StreamWeaverGrainsDatabase::compact_pcm_pool() {
    if (grains.empty()) {
        pcm_pool_bytes.clear();
        return;
    }

    // Group grains by source_id so different-source time origins don't mix.
    std::map<int, std::vector<int>> source_groups;
    for (int gi = 0; gi < static_cast<int>(grains.size()); ++gi)
        source_groups[grains[gi].source_id].push_back(gi);

    int bps = bytes_per_sample();
    int old_pool_size = pcm_sample_count();

    // Compute source-time-proportional layout for each source group.
    // Within a group, center_sample[i] = base_offset + max_hw + round((t[i] - t_min) * sr).
    // This preserves the ~1-period center spacing required for PSOLA phase coherence,
    // as opposed to sequential packing which produces ~2-period spacing and causes phasing.
    struct GroupInfo { int base_offset; float t_min; int max_hw; int region_size; };
    std::map<int, GroupInfo> ginfos;
    int total = 0;
    for (auto& [sid, ids] : source_groups) {
        float t_min = grains[ids[0]].original_time_s;
        float t_max = t_min;
        int max_hw = 0;
        for (int gi : ids) {
            const auto& g = grains[gi];
            if (g.original_time_s < t_min) t_min = g.original_time_s;
            if (g.original_time_s > t_max) t_max = g.original_time_s;
            int hw = (g.fundamental_hz > 0.f)
                ? std::max(1, static_cast<int>(std::round(stored_sample_rate / g.fundamental_hz))) : 1;
            if (hw > max_hw) max_hw = hw;
        }
        int rsz = max_hw
            + static_cast<int>(std::round((t_max - t_min) * stored_sample_rate))
            + max_hw;
        rsz = std::max(rsz, 2);
        ginfos[sid] = { total, t_min, max_hw, rsz };
        total += rsz;
    }

    std::vector<uint8_t> new_bytes(static_cast<size_t>(total) * bps, 0);
    for (auto& [sid, ids] : source_groups) {
        auto& inf = ginfos[sid];
        for (int idx : ids) {
            auto& g = grains[idx];
            int hw = (g.fundamental_hz > 0.f)
                ? std::max(1, static_cast<int>(std::round(stored_sample_rate / g.fundamental_hz))) : 1;
            int new_center = inf.base_offset + inf.max_hw
                + static_cast<int>(std::round((g.original_time_s - inf.t_min) * stored_sample_rate));
            int src_start = g.center_sample - hw;
            int dst_start = new_center - hw;
            for (int i = 0; i < hw * 2; ++i) {
                int dst = dst_start + i;
                if (dst < 0 || dst >= total) continue;
                int src = src_start + i;
                if (src >= 0 && src < old_pool_size)
                    encode_sample(new_bytes.data() + dst * bps, decode_sample(src));
            }
            g.center_sample = new_center;
        }
    }

    pcm_pool_bytes = std::move(new_bytes);
    emit_changed();
}

void StreamWeaverGrainsDatabase::recompress_pcm_pool(int new_mode) {
    CompressionMode target = static_cast<CompressionMode>(new_mode);
    if (target == compression_mode) return;
    int n = pcm_sample_count();
    // Decode all to float first.
    std::vector<float> tmp(n);
    for (int i = 0; i < n; ++i) tmp[i] = decode_sample(i);
    // Switch mode and re-encode.
    compression_mode = target;
    int new_bps = bytes_per_sample();
    pcm_pool_bytes.resize(static_cast<size_t>(n) * new_bps);
    for (int i = 0; i < n; ++i) {
        encode_sample(pcm_pool_bytes.data() + i * new_bps, tmp[i]);
    }
    emit_changed();
}

PackedFloat32Array StreamWeaverGrainsDatabase::get_grain_pcm_windowed(int index) const {
    PackedFloat32Array arr;
    if (index < 0 || index >= static_cast<int>(grains.size())) return arr;
    const auto& g = grains[index];
    int period_samples = (g.fundamental_hz > 0.f)
        ? std::max(1, static_cast<int>(std::round(stored_sample_rate / g.fundamental_hz)))
        : 1;
    int fade_samples = 4;
    int window_size = std::max(2, period_samples + fade_samples);
    int half_window = window_size / 2;
    int pool_size = pcm_sample_count();
    arr.resize(window_size);
    for (int i = 0; i < window_size; ++i) {
        int src_index = g.center_sample - half_window + i;
        float sample = (src_index >= 0 && src_index < pool_size) ? decode_sample(src_index) : 0.0f;
        float window = 1.0f;
        if (i < fade_samples) {
            float t = static_cast<float>(i) / static_cast<float>(fade_samples);
            window = 0.5f * (1.0f - std::cos(kPi * t));
        } else if (i >= window_size - fade_samples) {
            float t = static_cast<float>(window_size - 1 - i) / static_cast<float>(fade_samples);
            window = 0.5f * (1.0f - std::cos(kPi * t));
        }
        arr[i] = sample * window;
    }
    return arr;
}

PackedByteArray StreamWeaverGrainsDatabase::get_source_pcm_slice(
    Ref<AudioStream> source_stream, float start_s, float end_s) const {

    PackedByteArray result;
    std::vector<float> pcm;
    float rate = 0.f;
    if (!decode_source_pcm(source_stream, pcm, rate)) return result;
    if (rate <= 0.f || pcm.empty()) return result;

    int total = static_cast<int>(pcm.size());
    int i_start = std::max(0, static_cast<int>(std::round(start_s * rate)));
    int i_end   = std::min(total, static_cast<int>(std::round(end_s * rate)));
    if (i_end <= i_start) return result;

    int len = i_end - i_start;
    float* src = pcm.data() + i_start;

    // Minimal cosine fade (4 samples) to reveal clicks without heavy windowing.
    int fade = std::min(4, len / 2);
    for (int i = 0; i < fade; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(fade);
        float w = 0.5f * (1.f - std::cos(kPi * t));
        src[i]           *= w;
        src[len - 1 - i] *= w;
    }

    // Encode as int16 little-endian — ready for AudioStreamWAV.FORMAT_16_BITS.
    result.resize(len * 2);
    uint8_t* dst = result.ptrw();
    for (int i = 0; i < len; ++i) {
        int16_t s = static_cast<int16_t>(std::clamp(src[i] * 32768.f, -32768.f, 32767.f));
        std::memcpy(dst + i * 2, &s, 2);
    }
    return result;
}

// -------------------- Index --------------------

void StreamWeaverGrainsDatabase::rebuild_index() {
    if (!index_dirty) return;
    if (num_axes_stored >= 2) {
        rebuild_2d_index();
    }
    rebuild_1d_index();
    index_dirty = false;
}

void StreamWeaverGrainsDatabase::rebuild_1d_index() {
    int n = static_cast<int>(grains.size());
    sorted_indices.resize(n);
    for (int i = 0; i < n; ++i) sorted_indices[i] = i;
    if (num_axes_stored >= 1) {
        std::sort(sorted_indices.begin(), sorted_indices.end(),
            [this](int a, int b) { return grains[a].params[0] < grains[b].params[0]; });
    }
}

void StreamWeaverGrainsDatabase::rebuild_2d_index() {
    if (num_axes_stored < 2) return;
    int total_cells = GRID_RES * GRID_RES;
    grid_cells.clear();
    grid_cells.resize(total_cells);

    float range0 = axes[0].max_val - axes[0].min_val;
    float range1 = axes[1].max_val - axes[1].min_val;
    if (range0 <= 0) range0 = 1.f;
    if (range1 <= 0) range1 = 1.f;

    for (int i = 0; i < static_cast<int>(grains.size()); ++i) {
        float norm0 = (grains[i].params[0] - axes[0].min_val) / range0;
        float norm1 = (grains[i].params[1] - axes[1].min_val) / range1;
        int cx = CLAMP(static_cast<int>(norm0 * GRID_RES), 0, GRID_RES - 1);
        int cy = CLAMP(static_cast<int>(norm1 * GRID_RES), 0, GRID_RES - 1);
        // Insert into this cell and neighbors for overlap
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                int nx = cx + dx;
                int ny = cy + dy;
                if (nx >= 0 && nx < GRID_RES && ny >= 0 && ny < GRID_RES) {
                    grid_cells[ny * GRID_RES + nx].push_back(i);
                }
            }
        }
    }
}

// -------------------- Lookup --------------------

PackedInt32Array StreamWeaverGrainsDatabase::find_nearest_grains(PackedFloat32Array params, int k) {
    PackedInt32Array result;
    if (grains.empty() || k <= 0) return result;
    if (index_dirty) rebuild_index();

    k = std::min(k, static_cast<int>(grains.size()));

    if (num_axes_stored <= 1) {
        // 1D binary search
        float target = params.size() > 0 ? params[0] : 0.f;
        int n = static_cast<int>(sorted_indices.size());

        // Binary search for closest
        int lo = 0, hi = n - 1;
        while (lo < hi) {
            int m = (lo + hi) / 2;
            if (grains[sorted_indices[m]].params[0] < target) lo = m + 1; else hi = m;
        }

        // Gather k nearest around lo
        int left = lo - 1, right = lo;
        while (result.size() < k) {
            bool use_left = (left >= 0);
            bool use_right = (right < n);
            if (!use_left && !use_right) break;

            if (use_left && use_right) {
                float dl = std::abs(grains[sorted_indices[left]].params[0] - target);
                float dr = std::abs(grains[sorted_indices[right]].params[0] - target);
                if (dl <= dr) {
                    result.push_back(sorted_indices[left--]);
                } else {
                    result.push_back(sorted_indices[right++]);
                }
            } else if (use_left) {
                result.push_back(sorted_indices[left--]);
            } else {
                result.push_back(sorted_indices[right++]);
            }
        }
    } else {
        // 2D grid lookup
        float range0 = axes[0].max_val - axes[0].min_val;
        float range1 = axes[1].max_val - axes[1].min_val;
        if (range0 <= 0) range0 = 1.f;
        if (range1 <= 0) range1 = 1.f;

        float p0 = params.size() > 0 ? params[0] : 0.f;
        float p1 = params.size() > 1 ? params[1] : 0.f;
        float norm0 = (p0 - axes[0].min_val) / range0;
        float norm1 = (p1 - axes[1].min_val) / range1;
        int cx = CLAMP(static_cast<int>(norm0 * GRID_RES), 0, GRID_RES - 1);
        int cy = CLAMP(static_cast<int>(norm1 * GRID_RES), 0, GRID_RES - 1);

        // Collect candidates from cell
        const auto& cell = grid_cells[cy * GRID_RES + cx];

        // Score by normalized Euclidean distance
        struct Candidate { int idx; float dist; };
        std::vector<Candidate> candidates;
        candidates.reserve(cell.size());
        for (int gi : cell) {
            float d0 = (grains[gi].params[0] - p0) / range0;
            float d1 = (grains[gi].params[1] - p1) / range1;
            candidates.push_back({gi, d0 * d0 + d1 * d1});
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

        int count = std::min(k, static_cast<int>(candidates.size()));
        for (int i = 0; i < count; ++i) {
            result.push_back(candidates[i].idx);
        }

        // If not enough candidates from grid, fall back to 1D
        if (result.size() < k) {
            float target = p0;
            int n = static_cast<int>(sorted_indices.size());
            int lo2 = 0, hi2 = n - 1;
            while (lo2 < hi2) {
                int m = (lo2 + hi2) / 2;
                if (grains[sorted_indices[m]].params[0] < target) lo2 = m + 1; else hi2 = m;
            }
            int left = lo2 - 1, right = lo2;
            while (result.size() < k) {
                bool use_left = (left >= 0);
                bool use_right = (right < n);
                if (!use_left && !use_right) break;
                int pick = -1;
                if (use_left && use_right) {
                    float dl = std::abs(grains[sorted_indices[left]].params[0] - target);
                    float dr = std::abs(grains[sorted_indices[right]].params[0] - target);
                    pick = (dl <= dr) ? sorted_indices[left--] : sorted_indices[right++];
                } else if (use_left) {
                    pick = sorted_indices[left--];
                } else {
                    pick = sorted_indices[right++];
                }
                // Check not already in result
                bool dup = false;
                for (int j = 0; j < result.size(); ++j) {
                    if (result[j] == pick) { dup = true; break; }
                }
                if (!dup) result.push_back(pick);
            }
        }
    }

    return result;
}

// -------------------- GDScript Accessors --------------------

Dictionary StreamWeaverGrainsDatabase::get_grain_metadata(int index) const {
    Dictionary d;
    if (index < 0 || index >= static_cast<int>(grains.size())) return d;
    const auto& g = grains[index];
    int period_samples = (g.fundamental_hz > 0.f)
        ? std::max(1, static_cast<int>(std::round(stored_sample_rate / g.fundamental_hz)))
        : 1;
    int window_size = period_samples * 2;
    d["center_sample"] = g.center_sample;
    d["fundamental_hz"] = g.fundamental_hz;
    d["original_time_s"] = g.original_time_s;
    d["period_samples"] = period_samples;
    d["window_size"] = window_size;
    d["low_energy"] = g.low_energy;
    d["source_id"] = g.source_id;
    PackedFloat32Array pv;
    pv.resize(num_axes_stored);
    for (int i = 0; i < num_axes_stored; ++i) pv[i] = g.params[i];
    d["params"] = pv;
    return d;
}

PackedFloat32Array StreamWeaverGrainsDatabase::get_grain_pcm(int index) const {
    PackedFloat32Array arr;
    if (index < 0 || index >= static_cast<int>(grains.size())) return arr;
    const auto& g = grains[index];
    int period_samples = (g.fundamental_hz > 0.f)
        ? std::max(1, static_cast<int>(std::round(stored_sample_rate / g.fundamental_hz)))
        : 1;
    int window_size = period_samples * 2;
    int half_window = window_size / 2;
    int pool_size = pcm_sample_count();
    int start = std::max(0, g.center_sample - half_window);
    int end = std::min(pool_size, g.center_sample + half_window);
    arr.resize(std::max(0, end - start));
    for (int i = 0; i < end - start; ++i) {
        arr[i] = decode_sample(start + i);
    }
    return arr;
}

float StreamWeaverGrainsDatabase::estimate_f0_for_params(PackedFloat32Array params, int k) {
    if (grains.empty()) return 0.0f;

    PackedInt32Array candidates = find_nearest_grains(params, std::max(1, k));
    if (candidates.is_empty()) return 0.0f;

    double weighted_sum = 0.0;
    double total_weight = 0.0;
    for (int i = 0; i < candidates.size(); ++i) {
        int idx = candidates[i];
        if (idx < 0 || idx >= static_cast<int>(grains.size())) continue;
        const auto& g = grains[idx];
        if (g.fundamental_hz <= 0.0f) continue;

        double norm_dist_sq = 0.0;
        for (int axis = 0; axis < num_axes_stored && axis < params.size(); ++axis) {
            double range = std::max(1e-6, static_cast<double>(axes[axis].max_val - axes[axis].min_val));
            double delta = (static_cast<double>(g.params[axis]) - static_cast<double>(params[axis])) / range;
            norm_dist_sq += delta * delta;
        }

        double weight = 1.0 / (1.0 + norm_dist_sq);
        if (g.low_energy) {
            weight *= 0.25;
        }
        weighted_sum += weight * g.fundamental_hz;
        total_weight += weight;
    }

    if (total_weight > 0.0) {
        return static_cast<float>(weighted_sum / total_weight);
    }
    return grains[candidates[0]].fundamental_hz;
}

StringName StreamWeaverGrainsDatabase::get_axis_name(int axis) const {
    if (axis < 0 || axis >= static_cast<int>(axes.size())) return StringName();
    return axes[axis].name;
}

float StreamWeaverGrainsDatabase::get_axis_min(int axis) const {
    if (axis < 0 || axis >= static_cast<int>(axes.size())) return 0.f;
    return axes[axis].min_val;
}

float StreamWeaverGrainsDatabase::get_axis_max(int axis) const {
    if (axis < 0 || axis >= static_cast<int>(axes.size())) return 1.f;
    return axes[axis].max_val;
}

bool StreamWeaverGrainsDatabase::get_axis_derived(int axis) const {
    if (axis < 0 || axis >= static_cast<int>(axes.size())) return false;
    return axes[axis].derived_from_fundamental;
}

PackedInt32Array StreamWeaverGrainsDatabase::get_grain_source_ids() const {
    PackedInt32Array arr;
    arr.resize(static_cast<int>(grains.size()));
    for (int i = 0; i < static_cast<int>(grains.size()); ++i) {
        arr[i] = grains[i].source_id;
    }
    return arr;
}

void StreamWeaverGrainsDatabase::set_grain_source_ids(PackedInt32Array ids) {
    int n = static_cast<int>(grains.size());
    int incoming = ids.size();
    for (int i = 0; i < n; ++i) {
        grains[i].source_id = (i < incoming) ? ids[i] : -1;
    }
}

int StreamWeaverGrainsDatabase::get_grain_source_id(int index) const {
    if (index < 0 || index >= static_cast<int>(grains.size())) return -1;
    return grains[index].source_id;
}

// -------------------- Serialization --------------------

TypedArray<Dictionary> StreamWeaverGrainsDatabase::get_axes_data() const {
    TypedArray<Dictionary> arr;
    for (const auto& a : axes) {
        Dictionary d;
        d["name"] = a.name;
        d["min_val"] = a.min_val;
        d["max_val"] = a.max_val;
        d["derived_from_fundamental"] = a.derived_from_fundamental;
        arr.push_back(d);
    }
    return arr;
}

void StreamWeaverGrainsDatabase::set_axes_data(TypedArray<Dictionary> data) {
    axes.clear();
    axes.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        Dictionary d = data[i];
        axes[i].name = d.get("name", StringName());
        axes[i].min_val = d.get("min_val", 0.0f);
        axes[i].max_val = d.get("max_val", 1.0f);
        axes[i].derived_from_fundamental = d.get("derived_from_fundamental", false);
    }
    index_dirty = true;
}

PackedFloat32Array StreamWeaverGrainsDatabase::get_grain_data() const {
    PackedFloat32Array arr;
    int n = static_cast<int>(grains.size());
    arr.resize(n * FIELDS_PER_GRAIN);
    for (int i = 0; i < n; ++i) {
        int base = i * FIELDS_PER_GRAIN;
        const auto& g = grains[i];
        arr[base + 0] = static_cast<float>(g.center_sample);
        arr[base + 1] = g.fundamental_hz;
        arr[base + 2] = g.original_time_s;
        for (int a = 0; a < MAX_AXES; ++a) {
            arr[base + 3 + a] = g.params[a];
        }
    }
    return arr;
}

void StreamWeaverGrainsDatabase::set_grain_data(PackedFloat32Array data) {
    int n = data.size() / FIELDS_PER_GRAIN;
    grains.clear();
    grains.resize(n);
    for (int i = 0; i < n; ++i) {
        int base = i * FIELDS_PER_GRAIN;
        auto& g = grains[i];
        g.center_sample = static_cast<int>(data[base + 0]);
        g.fundamental_hz = data[base + 1];
        g.original_time_s = data[base + 2];
        for (int a = 0; a < MAX_AXES; ++a) {
            g.params[a] = data[base + 3 + a];
        }
        g.source_id = -1; // populated separately by set_grain_source_ids if present
    }
    index_dirty = true;
}

PackedByteArray StreamWeaverGrainsDatabase::get_pcm_data() const {
    PackedByteArray arr;
    arr.resize(static_cast<int>(pcm_pool_bytes.size()));
    std::memcpy(arr.ptrw(), pcm_pool_bytes.data(), pcm_pool_bytes.size());
    return arr;
}

void StreamWeaverGrainsDatabase::set_pcm_data(PackedByteArray data) {
    pcm_pool_bytes.resize(data.size());
    std::memcpy(pcm_pool_bytes.data(), data.ptr(), data.size());
}

void StreamWeaverGrainsDatabase::set_pcm_pool(PackedFloat32Array data) {
    // Legacy migration: old .tres files stored float32 samples directly.
    // Re-encode them using whatever compression_mode is currently set (which
    // defaults to FLOAT32, preserving exact bit patterns).
    int n = data.size();
    int bps = bytes_per_sample();
    pcm_pool_bytes.resize(static_cast<size_t>(n) * bps);
    for (int i = 0; i < n; ++i) {
        encode_sample(pcm_pool_bytes.data() + i * bps, data[i]);
    }
}

bool StreamWeaverGrainsDatabase::_set(const godot::StringName& p_name, const godot::Variant& p_value) {
    // Handle the old "pcm_pool" property from pre-compression .tres files.
    if (p_name == godot::StringName("pcm_pool")) {
        set_pcm_pool(godot::PackedFloat32Array(p_value));
        return true;
    }
    return false;
}

// -------------------- Diagnostics --------------------

void StreamWeaverGrainsDatabase::print_stats() {
    UtilityFunctions::print("StreamWeaverGrainsDatabase stats:");
    UtilityFunctions::print("  unit count:  ", static_cast<int>(grains.size()));
    UtilityFunctions::print("  num axes:    ", num_axes_stored);
    UtilityFunctions::print("  sample rate: ", stored_sample_rate);
    UtilityFunctions::print("  pcm pool:    ", pcm_sample_count(), " samples (",
        get_pcm_pool_byte_count(), " bytes, mode=", static_cast<int>(compression_mode), ")");
    UtilityFunctions::print("  sources:     ", source_paths.size());

    if (grains.empty()) return;

    float t_min = grains[0].original_time_s;
    float t_max = grains[0].original_time_s;
    float f0_min = grains[0].fundamental_hz;
    float f0_max = grains[0].fundamental_hz;
    for (const auto& g : grains) {
        t_min = std::min(t_min, g.original_time_s);
        t_max = std::max(t_max, g.original_time_s);
        f0_min = std::min(f0_min, g.fundamental_hz);
        f0_max = std::max(f0_max, g.fundamental_hz);
    }
    UtilityFunctions::print("  time range:  ", t_min, " .. ", t_max, " s");
    UtilityFunctions::print("  f0 range:    ", f0_min, " .. ", f0_max, " Hz");

    for (int a = 0; a < num_axes_stored && a < MAX_AXES; ++a) {
        float p_min = grains[0].params[a];
        float p_max = grains[0].params[a];
        double p_sum = 0;
        for (const auto& g : grains) {
            p_min = std::min(p_min, g.params[a]);
            p_max = std::max(p_max, g.params[a]);
            p_sum += g.params[a];
        }
        float p_mean = static_cast<float>(p_sum / grains.size());
        String name = a < static_cast<int>(axes.size()) ? String(axes[a].name) : String("?");
        float cfg_min = a < static_cast<int>(axes.size()) ? axes[a].min_val : 0;
        float cfg_max = a < static_cast<int>(axes.size()) ? axes[a].max_val : 0;
        UtilityFunctions::print("  axis ", a, " (", name, "): unit params actual ",
            p_min, " .. ", p_max, " (mean ", p_mean, "), configured range ",
            cfg_min, " .. ", cfg_max);
    }
}

// -------------------- Bind Methods --------------------

void StreamWeaverGrainsDatabase::_bind_methods() {
    // Initialize the μ-law decode lookup table once at class registration time.
    init_ulaw_lut(ulaw_decode_lut);

    ClassDB::bind_method(D_METHOD("append_from_fft", "fft", "source_stream", "source_path", "tracked_curve",
        "axis_configs", "cycles_per_grain", "crossfade_cycles", "energy_threshold_db", "sketch_hint"),
        &StreamWeaverGrainsDatabase::append_from_fft, DEFVAL(PackedVector2Array()));

    ClassDB::bind_method(D_METHOD("append_from_onsets", "fft", "source_stream", "source_path", "onsets",
        "axis_configs", "energy_threshold_db"),
        &StreamWeaverGrainsDatabase::append_from_onsets);

    ClassDB::bind_method(D_METHOD("append_from_manual_markers",
        "fft", "source_stream", "source_path", "sorted_marker_times_s",
        "axis_configs", "energy_threshold_db", "tracked_curve"),
        &StreamWeaverGrainsDatabase::append_from_manual_markers,
        DEFVAL(PackedVector2Array()));

    ClassDB::bind_method(D_METHOD("delete_grain", "index"),
        &StreamWeaverGrainsDatabase::delete_grain);
    ClassDB::bind_method(D_METHOD("delete_grains", "indices"),
        &StreamWeaverGrainsDatabase::delete_grains);

    ClassDB::bind_method(D_METHOD("orphan_pcm_bytes"),
        &StreamWeaverGrainsDatabase::orphan_pcm_bytes);
    ClassDB::bind_method(D_METHOD("is_compact"),
        &StreamWeaverGrainsDatabase::is_compact);
    ClassDB::bind_method(D_METHOD("compact_pcm_pool"),
        &StreamWeaverGrainsDatabase::compact_pcm_pool);
    ClassDB::bind_method(D_METHOD("recompress_pcm_pool", "new_mode"),
        &StreamWeaverGrainsDatabase::recompress_pcm_pool);

    ClassDB::bind_method(D_METHOD("get_grain_pcm_windowed", "index"),
        &StreamWeaverGrainsDatabase::get_grain_pcm_windowed);

    ClassDB::bind_method(D_METHOD("get_source_pcm_slice", "source_stream", "start_s", "end_s"),
        &StreamWeaverGrainsDatabase::get_source_pcm_slice);

    ClassDB::bind_method(D_METHOD("find_nearest_grains", "params", "k"),
        &StreamWeaverGrainsDatabase::find_nearest_grains);

    ClassDB::bind_method(D_METHOD("print_stats"),
        &StreamWeaverGrainsDatabase::print_stats);

    ClassDB::bind_method(D_METHOD("get_grain_count"),
        &StreamWeaverGrainsDatabase::get_grain_count);
    ClassDB::bind_method(D_METHOD("get_num_axes"),
        &StreamWeaverGrainsDatabase::get_num_axes);
    ClassDB::bind_method(D_METHOD("get_sample_rate"),
        &StreamWeaverGrainsDatabase::get_sample_rate);

    ClassDB::bind_method(D_METHOD("get_grain_metadata", "index"),
        &StreamWeaverGrainsDatabase::get_grain_metadata);
    ClassDB::bind_method(D_METHOD("get_grain_pcm", "index"),
        &StreamWeaverGrainsDatabase::get_grain_pcm);
    ClassDB::bind_method(D_METHOD("estimate_f0_for_params", "params", "k"),
        &StreamWeaverGrainsDatabase::estimate_f0_for_params, DEFVAL(8));
    ClassDB::bind_method(D_METHOD("get_axis_name", "axis"),
        &StreamWeaverGrainsDatabase::get_axis_name);
    ClassDB::bind_method(D_METHOD("get_axis_min", "axis"),
        &StreamWeaverGrainsDatabase::get_axis_min);
    ClassDB::bind_method(D_METHOD("get_axis_max", "axis"),
        &StreamWeaverGrainsDatabase::get_axis_max);
    ClassDB::bind_method(D_METHOD("get_axis_derived", "axis"),
        &StreamWeaverGrainsDatabase::get_axis_derived);

    ClassDB::bind_method(D_METHOD("get_grain_source_id", "index"),
        &StreamWeaverGrainsDatabase::get_grain_source_id);

    // Pool size / compression queries (for GDScript UI / diagnostics).
    ClassDB::bind_method(D_METHOD("get_pcm_pool_byte_count"),
        &StreamWeaverGrainsDatabase::get_pcm_pool_byte_count);
    ClassDB::bind_method(D_METHOD("get_pcm_sample_count"),
        &StreamWeaverGrainsDatabase::get_pcm_sample_count);
    ClassDB::bind_method(D_METHOD("get_compression_mode"),
        &StreamWeaverGrainsDatabase::get_compression_mode);
    ClassDB::bind_method(D_METHOD("set_compression_mode", "mode"),
        &StreamWeaverGrainsDatabase::set_compression_mode);

    // Serialization properties
    ClassDB::bind_method(D_METHOD("get_axes_data"), &StreamWeaverGrainsDatabase::get_axes_data);
    ClassDB::bind_method(D_METHOD("set_axes_data", "data"), &StreamWeaverGrainsDatabase::set_axes_data);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "axes_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_axes_data", "get_axes_data");

    ClassDB::bind_method(D_METHOD("get_grain_data"), &StreamWeaverGrainsDatabase::get_grain_data);
    ClassDB::bind_method(D_METHOD("set_grain_data", "data"), &StreamWeaverGrainsDatabase::set_grain_data);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "grain_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_grain_data", "get_grain_data");

    ClassDB::bind_method(D_METHOD("get_pcm_data"), &StreamWeaverGrainsDatabase::get_pcm_data);
    ClassDB::bind_method(D_METHOD("set_pcm_data", "data"), &StreamWeaverGrainsDatabase::set_pcm_data);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "pcm_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_pcm_data", "get_pcm_data");

    ClassDB::bind_method(D_METHOD("get_compression_mode_stored"), &StreamWeaverGrainsDatabase::get_compression_mode);
    ClassDB::bind_method(D_METHOD("set_compression_mode_stored", "mode"), &StreamWeaverGrainsDatabase::set_compression_mode);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "compression_mode_stored", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_compression_mode_stored", "get_compression_mode_stored");

    // Legacy set_pcm_pool bound so _set() can dispatch through it; also callable from GDScript for migration.
    ClassDB::bind_method(D_METHOD("set_pcm_pool", "data"), &StreamWeaverGrainsDatabase::set_pcm_pool);

    ClassDB::bind_method(D_METHOD("get_stored_sample_rate"), &StreamWeaverGrainsDatabase::get_stored_sample_rate);
    ClassDB::bind_method(D_METHOD("set_stored_sample_rate", "rate"), &StreamWeaverGrainsDatabase::set_stored_sample_rate);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stored_sample_rate", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_stored_sample_rate", "get_stored_sample_rate");

    ClassDB::bind_method(D_METHOD("get_num_axes_stored"), &StreamWeaverGrainsDatabase::get_num_axes_stored);
    ClassDB::bind_method(D_METHOD("set_num_axes_stored", "n"), &StreamWeaverGrainsDatabase::set_num_axes_stored);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "num_axes_stored", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_num_axes_stored", "get_num_axes_stored");

    ClassDB::bind_method(D_METHOD("get_source_paths"), &StreamWeaverGrainsDatabase::get_source_paths);
    ClassDB::bind_method(D_METHOD("set_source_paths", "paths"), &StreamWeaverGrainsDatabase::set_source_paths);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "source_paths", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_source_paths", "get_source_paths");

    ClassDB::bind_method(D_METHOD("get_grain_source_ids"), &StreamWeaverGrainsDatabase::get_grain_source_ids);
    ClassDB::bind_method(D_METHOD("set_grain_source_ids", "ids"), &StreamWeaverGrainsDatabase::set_grain_source_ids);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "grain_source_ids", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_grain_source_ids", "get_grain_source_ids");
}
