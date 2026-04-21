#include "GrainsDatabaseResource.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream.hpp"
#include "godot_cpp/classes/audio_stream_playback.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

#include "kiss_fft.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

using namespace godot;

// -------------------- Helpers --------------------

float StreamWeaverGrainsDatabase::lookup_curve(const PackedVector2Array& curve, float time_s) {
    int n = curve.size();
    if (n == 0) return 0.f;
    if (time_s <= curve[0].x) return curve[0].y;
    if (time_s >= curve[n - 1].x) return curve[n - 1].y;
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if (curve[m].x <= time_s) lo = m; else hi = m;
    }
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

void StreamWeaverGrainsDatabase::build_from_fft(
    Ref<StreamWeaverFFT> fft,
    Ref<AudioStream> source_stream,
    PackedVector2Array tracked_curve,
    TypedArray<Dictionary> axis_configs,
    int cycles_per_grain,
    float crossfade_cycles,
    float energy_threshold_db,
    PackedVector2Array sketch_hint) {

    if (fft.is_null() || tracked_curve.size() < 2) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: invalid FFT or tracked curve");
        return;
    }
    if (source_stream.is_null()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: source stream is null");
        return;
    }

    // Clamp parameters
    cycles_per_grain = CLAMP(cycles_per_grain, 2, 6);
    crossfade_cycles = CLAMP(crossfade_cycles, 0.25f, 1.0f);

    // Get target sample rate (system mix rate)
    float mix_rate = 48000.f;
    if (AudioServer::get_singleton()) {
        mix_rate = AudioServer::get_singleton()->get_mix_rate();
    }
    stored_sample_rate = mix_rate;

    // Decode the original AudioStream directly at mix rate. Grain PCM MUST
    // come from the source audio, not the FFT's decimated/filtered mono_pcm,
    // to preserve fidelity. The FFT is used only for timing info.
    double stream_length = source_stream->get_length();
    if (stream_length <= 0) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: source stream has zero length");
        return;
    }
    int num_samples = static_cast<int>(std::ceil(stream_length * mix_rate));
    Ref<AudioStreamPlayback> pb = source_stream->instantiate_playback();
    if (pb.is_null()) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: failed to instantiate playback");
        return;
    }
    pb->start(0.0);
    PackedVector2Array stereo = pb->mix_audio(1.0f, num_samples);
    pb->stop();

    std::vector<float> work_pcm(stereo.size());
    const Vector2* sr = stereo.ptr();
    for (int i = 0; i < stereo.size(); ++i) {
        work_pcm[i] = 0.5f * (sr[i].x + sr[i].y);
    }
    float work_rate = mix_rate;
    int total_samples = static_cast<int>(work_pcm.size());
    if (total_samples == 0) {
        UtilityFunctions::push_warning("StreamWeaverGrainsDatabase: decoded stream is empty");
        return;
    }

    // Parse axis configs
    axes.clear();
    num_axes_stored = std::min(static_cast<int>(axis_configs.size()), MAX_AXES);
    axes.resize(num_axes_stored);

    struct AxisBuildInfo {
        bool derived_from_fundamental = false;
        Vector2 cal_a, cal_b; // (hz, param_value)
        PackedVector2Array keyframes;
    };
    std::vector<AxisBuildInfo> axis_build(num_axes_stored);

    for (int i = 0; i < num_axes_stored; ++i) {
        Dictionary cfg = axis_configs[i];
        axes[i].name = cfg.get("name", StringName("axis_" + String::num_int64(i)));
        axes[i].min_val = cfg.get("min_value", 0.0f);
        axes[i].max_val = cfg.get("max_value", 1.0f);
        axis_build[i].derived_from_fundamental = cfg.get("derived_from_fundamental", false);
        axis_build[i].cal_a = cfg.get("calibration_a", Vector2(0, 0));
        axis_build[i].cal_b = cfg.get("calibration_b", Vector2(1, 1));
        axis_build[i].keyframes = cfg.get("keyframes", PackedVector2Array());
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
        // Mean dB across first few harmonics at this time
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

    // Lambda to compute parameter value for a given axis at a given time
    auto compute_param = [&](int axis_idx, float time_s, float f0_hz) -> float {
        const auto& ab = axis_build[axis_idx];
        if (ab.derived_from_fundamental) {
            float hz_range = ab.cal_b.x - ab.cal_a.x;
            if (std::abs(hz_range) < 1e-6f) return ab.cal_a.y;
            float t = (f0_hz - ab.cal_a.x) / hz_range;
            return ab.cal_a.y + t * (ab.cal_b.y - ab.cal_a.y);
        } else {
            return lookup_curve(ab.keyframes, time_s);
        }
    };

    // Interpolate sketch hint at a given time; returns -1 if time is not covered
    // by any sketch stroke. Gap sentinels (y <= 0) mark boundaries between strokes.
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

    // Clear existing data
    grains.clear();
    pcm_pool = work_pcm;

    // Walk the audio and extract one pitch-synchronous unit per cycle.
    // Start at the tracked curve start time.
    int cursor = static_cast<int>(std::round(curve_start * work_rate));
    cursor = CLAMP(cursor, 0, total_samples - 1);

    // // precompute phase reference signal
    // std::vector<float> phase_pcm;
    // bandpass_block_kissfft(work_pcm, phase_pcm, work_rate, /*approx f0*/ 200.f, 0.3f);
    //
    // // Optional: high-pass to remove DC bias
    // for (int i = 1; i < phase_pcm.size(); ++i) {
    //     phase_pcm[i] = phase_pcm[i] - phase_pcm[i - 1];
    // }

    while (cursor < total_samples) {
        float time_s = static_cast<float>(cursor) / work_rate;

        // Stop if past tracked curve
        if (time_s > curve_end) break;

        // Skip if before tracked curve
        if (time_s < curve_start) {
            cursor += 64;
            continue;
        }

        // Skip times not covered by any sketch stroke
        if (sketch_hint.size() >= 2 && sketch_hz_at(time_s) <= 0.f) {
            cursor += 64;
            continue;
        }

        // Look up fundamental at this time
        float f0 = lookup_curve(tracked_curve, time_s);
        if (f0 <= 0) {
            cursor += 64;
            continue;
        }

        // Check energy threshold
        float energy = get_harmonic_energy(time_s, f0);
        bool low_energy = energy < energy_threshold_db;

        int period_samples = static_cast<int>(std::round(work_rate / f0));
        if (period_samples < 2) {
            cursor += 1;
            continue;
        }

        // Snap predicted pitch mark to nearest positive-going zero crossing.
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
        int center_sample = best_zc;
        float center_time = static_cast<float>(center_sample) / work_rate;
        float center_f0 = lookup_curve(tracked_curve, center_time);
        if (center_f0 <= 0.f) {
            cursor += period_samples;
            continue;
        }

        GrainEntry entry;
        entry.center_sample = center_sample;
        entry.fundamental_hz = center_f0;
        entry.original_time_s = center_time;
        entry.low_energy = low_energy;
        for (int a = 0; a < num_axes_stored; ++a) {
            entry.params[a] = compute_param(a, center_time, center_f0);
        }
        grains.push_back(entry);

        cursor = center_sample + std::max(1, period_samples);
    }

    // Build spatial index
    index_dirty = true;
    rebuild_index();

    UtilityFunctions::print("StreamWeaverGrainsDatabase: extracted ",
        static_cast<int>(grains.size()), " units, PCM pool: ",
        static_cast<int>(pcm_pool.size()), " samples");
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
    int start = std::max(0, g.center_sample - half_window);
    int end = std::min(static_cast<int>(pcm_pool.size()), g.center_sample + half_window);
    arr.resize(std::max(0, end - start));
    for (int i = 0; i < end - start; ++i) {
        arr[i] = pcm_pool[start + i];
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

// -------------------- Serialization --------------------

TypedArray<Dictionary> StreamWeaverGrainsDatabase::get_axes_data() const {
    TypedArray<Dictionary> arr;
    for (const auto& a : axes) {
        Dictionary d;
        d["name"] = a.name;
        d["min_val"] = a.min_val;
        d["max_val"] = a.max_val;
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
    }
    index_dirty = true;
}

PackedFloat32Array StreamWeaverGrainsDatabase::get_pcm_pool() const {
    PackedFloat32Array arr;
    arr.resize(static_cast<int>(pcm_pool.size()));
    for (int i = 0; i < static_cast<int>(pcm_pool.size()); ++i) {
        arr[i] = pcm_pool[i];
    }
    return arr;
}

void StreamWeaverGrainsDatabase::set_pcm_pool(PackedFloat32Array data) {
    pcm_pool.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        pcm_pool[i] = data[i];
    }
}

// -------------------- Diagnostics --------------------

void StreamWeaverGrainsDatabase::print_stats() {
    UtilityFunctions::print("StreamWeaverGrainsDatabase stats:");
    UtilityFunctions::print("  unit count:  ", static_cast<int>(grains.size()));
    UtilityFunctions::print("  num axes:    ", num_axes_stored);
    UtilityFunctions::print("  sample rate: ", stored_sample_rate);
    UtilityFunctions::print("  pcm pool:    ", static_cast<int>(pcm_pool.size()), " samples");

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
    ClassDB::bind_method(D_METHOD("build_from_fft", "fft", "source_stream", "tracked_curve", "axis_configs",
        "cycles_per_grain", "crossfade_cycles", "energy_threshold_db", "sketch_hint"),
        &StreamWeaverGrainsDatabase::build_from_fft, DEFVAL(PackedVector2Array()));

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

    // Serialization properties
    ClassDB::bind_method(D_METHOD("get_axes_data"), &StreamWeaverGrainsDatabase::get_axes_data);
    ClassDB::bind_method(D_METHOD("set_axes_data", "data"), &StreamWeaverGrainsDatabase::set_axes_data);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "axes_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_axes_data", "get_axes_data");

    ClassDB::bind_method(D_METHOD("get_grain_data"), &StreamWeaverGrainsDatabase::get_grain_data);
    ClassDB::bind_method(D_METHOD("set_grain_data", "data"), &StreamWeaverGrainsDatabase::set_grain_data);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "grain_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_grain_data", "get_grain_data");

    ClassDB::bind_method(D_METHOD("get_pcm_pool"), &StreamWeaverGrainsDatabase::get_pcm_pool);
    ClassDB::bind_method(D_METHOD("set_pcm_pool", "data"), &StreamWeaverGrainsDatabase::set_pcm_pool);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "pcm_pool", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_pcm_pool", "get_pcm_pool");

    ClassDB::bind_method(D_METHOD("get_stored_sample_rate"), &StreamWeaverGrainsDatabase::get_stored_sample_rate);
    ClassDB::bind_method(D_METHOD("set_stored_sample_rate", "rate"), &StreamWeaverGrainsDatabase::set_stored_sample_rate);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stored_sample_rate", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_stored_sample_rate", "get_stored_sample_rate");

    ClassDB::bind_method(D_METHOD("get_num_axes_stored"), &StreamWeaverGrainsDatabase::get_num_axes_stored);
    ClassDB::bind_method(D_METHOD("set_num_axes_stored", "n"), &StreamWeaverGrainsDatabase::set_num_axes_stored);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "num_axes_stored", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
        "set_num_axes_stored", "get_num_axes_stored");
}
