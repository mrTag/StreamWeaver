#ifndef STREAMWEAVER_GRAINSDATABASE_H
#define STREAMWEAVER_GRAINSDATABASE_H

#include "StreamWeaverFFT.h"

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include "godot_cpp/variant/packed_string_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/string_name.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/dictionary.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

class StreamWeaverGrainsDatabase : public godot::Resource {
    GDCLASS(StreamWeaverGrainsDatabase, Resource)
    static void _bind_methods();

public:
    static constexpr int MAX_AXES = 4;
    static constexpr int FIELDS_PER_GRAIN = 3 + MAX_AXES; // center_sample, f0, time, params[MAX_AXES]

    enum CompressionMode : int { FLOAT32 = 0, INT16 = 1, ULAW8 = 2 };

    struct GrainAxisDef {
        godot::StringName name;
        float min_val = 0;
        float max_val = 1;
        bool derived_from_fundamental = false;
    };

    struct GrainEntry {
        int center_sample = 0;
        float fundamental_hz = 0;
        float original_time_s = 0;
        float params[MAX_AXES] = {};
        bool low_energy = false;
        int source_id = -1; // index into source_paths; -1 = unknown / pre-provenance data
    };

private:
    // Axis metadata
    std::vector<GrainAxisDef> axes;

    // Grain storage
    std::vector<GrainEntry> grains;
    std::vector<uint8_t> pcm_pool_bytes; // raw bytes; layout determined by compression_mode
    CompressionMode compression_mode = FLOAT32;
    float stored_sample_rate = 48000.f;
    int num_axes_stored = 0;

    // Source provenance: each grain references one entry in source_paths via
    // source_id. Stored separately so old .tres files (no source data) load
    // fine; missing entries default to -1 / "(unknown)".
    godot::PackedStringArray source_paths;

    // 1D sorted index (for single-axis fast lookup)
    std::vector<int> sorted_indices; // grain indices sorted by params[0]

    // 2D grid index
    static constexpr int GRID_RES = 32;
    std::vector<std::vector<int>> grid_cells; // GRID_RES * GRID_RES cells
    bool index_dirty = true;

    // Per-axis configuration parsed out of axis_configs (Dictionary array).
    struct AxisBuildInfo {
        bool derived_from_fundamental = false;
        float min_val = 0.f;
        float max_val = 1.f;
        godot::Vector2 cal_a;
        godot::Vector2 cal_b;
        godot::PackedVector2Array keyframes;
    };

    void rebuild_index();
    void rebuild_1d_index();
    void rebuild_2d_index();

    // Interpolation helper for tracked curve
    static float lookup_curve(const godot::PackedVector2Array& curve, float time_s,
                               bool treat_zero_as_gap = false);

    // Resampling helper
    static std::vector<float> resample_linear(const std::vector<float>& src, float src_rate, float dst_rate);

    // Decode the source AudioStream to mono at the system mix rate. Returns
    // false (and pushes a warning) on failure.
    static bool decode_source_pcm(godot::Ref<godot::AudioStream> source_stream,
                                  std::vector<float>& out_pcm,
                                  float& out_rate);

    // Parse axis_configs Dictionary array into a fresh AxisBuildInfo vector.
    // Behaviour:
    //   - If this->axes is empty, defines new axes from the configs (and
    //     populates this->axes / num_axes_stored).
    //   - If this->axes is non-empty, validates that incoming configs match
    //     existing axis name/range/derived flag. Calibration & keyframes from
    //     the configs are still accepted and used for the new grains.
    //   - Returns the empty string on success, an error message on mismatch.
    godot::String parse_or_validate_axis_configs(
        godot::TypedArray<godot::Dictionary> axis_configs,
        std::vector<AxisBuildInfo>& out_build);

    // Compute the parameter value for one axis at a given time / fundamental.
    static float compute_param_value(const AxisBuildInfo& ab, float time_s, float f0_hz);

    // Append `decoded` to the pool (encoding per compression_mode) and return
    // the sample offset of the new block.
    int append_to_pcm_pool(const std::vector<float>& decoded);

    // Encode one float sample to the raw byte destination.
    void encode_sample(uint8_t* dst, float f) const;

    // Resolve a source resource path to an index in source_paths, appending
    // a new entry if needed. Empty path → -1 (legacy / unknown).
    int intern_source_path(const godot::String& path);

    // μ-law decode lookup table (256 entries), initialized once in _bind_methods.
    static float ulaw_decode_lut[256];

    // Bytes consumed per stored sample.
    int bytes_per_sample() const {
        switch (compression_mode) {
            case FLOAT32: return 4;
            case INT16:   return 2;
            case ULAW8:   return 1;
        }
        return 4;
    }

public:
    // Number of samples currently in the pool.
    int pcm_sample_count() const {
        int bps = bytes_per_sample();
        return bps > 0 ? static_cast<int>(pcm_pool_bytes.size()) / bps : 0;
    }

    // Raw byte count — use for file-size / memory reporting.
    int get_pcm_pool_byte_count() const { return static_cast<int>(pcm_pool_bytes.size()); }

    // Decode one sample from the compressed pool. Used in the hot playback path.
    float decode_sample(int sample_index) const {
        int bps = bytes_per_sample();
        const uint8_t* p = pcm_pool_bytes.data() + sample_index * bps;
        switch (compression_mode) {
            case FLOAT32: {
                float v;
                std::memcpy(&v, p, 4);
                return v;
            }
            case INT16: {
                int16_t v;
                std::memcpy(&v, p, 2);
                return static_cast<float>(v) * (1.f / 32768.f);
            }
            case ULAW8:
                return ulaw_decode_lut[*p];
        }
        return 0.f;
    }

    // Append grains extracted from an FFT analysis to this database.
    // axis_configs: Array of Dictionaries, each with:
    //   "name": StringName
    //   "min_value": float, "max_value": float
    //   "derived_from_fundamental": bool
    //   "calibration_a": Vector2 (hz, param_value)
    //   "calibration_b": Vector2 (hz, param_value)
    //   "keyframes": PackedVector2Array (time_s, value) - for manual axes
    // source_path: identifies the source AudioStream this grain set was cut
    // from (used for the editor's per-source grouping). Pass empty for unknown.
    // source_stream is the ORIGINAL AudioStream. Grain PCM is cut from this
    // (at mix rate) to preserve audio fidelity; the FFT is used only for
    // timing info (tracked_curve interpolation, energy threshold).
    // Returns the indices of the newly appended grains, or an empty array
    // on error (with a warning pushed).
    godot::PackedInt32Array append_from_fft(
        godot::Ref<StreamWeaverFFT> fft,
        godot::Ref<godot::AudioStream> source_stream,
        godot::String source_path,
        godot::PackedVector2Array tracked_curve,
        godot::TypedArray<godot::Dictionary> axis_configs,
        int cycles_per_grain,
        float crossfade_cycles,
        float energy_threshold_db,
        godot::PackedVector2Array sketch_hint = godot::PackedVector2Array());

    // Manual marker-based extraction. `sorted_marker_times_s` is an ascending
    // list of boundary times; N markers → N-1 grains. Each grain spans
    // [markers[i], markers[i+1]], its center is the midpoint.
    // For energy checking, fft may be null (check skipped).
    // tracked_curve: (time_s, hz) fundamental-tracking curve. Used for
    // "derived_from_fundamental" axes — those axes sample the tracked pitch at
    // each grain's center time (same semantics as FFT extraction). Pass an
    // empty array if no tracking was done; those axes will receive 0 Hz.
    // The grain's fundamental_hz field is still set to 1/interval_duration
    // (structural period used for windowing).
    godot::PackedInt32Array append_from_manual_markers(
        godot::Ref<StreamWeaverFFT> fft,
        godot::Ref<godot::AudioStream> source_stream,
        godot::String source_path,
        godot::PackedFloat64Array sorted_marker_times_s,
        godot::TypedArray<godot::Dictionary> axis_configs,
        float energy_threshold_db,
        godot::PackedVector2Array tracked_curve = godot::PackedVector2Array());

    // Onset-driven extraction for Low Hz mode. `onsets` is the (time_s,
    // fundamental_hz) curve from StreamWeaverFFT::detect_low_freq_cycles —
    // one entry per detected cycle peak. Each entry produces exactly one
    // grain centered on time_s; no zero-crossing snap is performed.
    godot::PackedInt32Array append_from_onsets(
        godot::Ref<StreamWeaverFFT> fft,
        godot::Ref<godot::AudioStream> source_stream,
        godot::String source_path,
        godot::PackedVector2Array onsets,
        godot::TypedArray<godot::Dictionary> axis_configs,
        float energy_threshold_db);

    // Editing
    bool delete_grain(int index);
    int  delete_grains(godot::PackedInt32Array indices); // returns # deleted

    // Pool maintenance
    int  orphan_pcm_bytes() const;
    bool is_compact() const;
    void compact_pcm_pool();

    // Re-encode the entire pool from its current compression to new_mode.
    // This decodes all samples to float and re-encodes; call before saving to
    // change the on-disk format. Triggers emit_changed().
    void recompress_pcm_pool(int new_mode);

    // Grain audition (windowed Hann, ready to play through AudioStreamWAV).
    // Returns float32 PCM [-1, 1] — used for waveform display and int16 conversion in GDScript.
    godot::PackedFloat32Array get_grain_pcm_windowed(int index) const;

    // Preview: extract raw mono PCM for [start_s, end_s] from source_stream.
    // Applies a short cosine fade-in/out to avoid click artifacts.
    // Returns int16 little-endian bytes ready for AudioStreamWAV.FORMAT_16_BITS.
    // Returns an empty array on failure.
    godot::PackedByteArray get_source_pcm_slice(
        godot::Ref<godot::AudioStream> source_stream,
        float start_s, float end_s) const;

    // Runtime lookup: find k nearest grains for a parameter constellation.
    // Returns PackedInt32Array of grain indices.
    godot::PackedInt32Array find_nearest_grains(godot::PackedFloat32Array params, int k);

    // Accessors for grain data (used by runtime output node)
    int get_grain_count() const { return static_cast<int>(grains.size()); }
    int get_num_axes() const { return num_axes_stored; }
    float get_sample_rate() const { return stored_sample_rate; }

    const GrainEntry& get_grain_entry(int index) const { return grains[index]; }

    // GDScript-friendly accessors
    godot::Dictionary get_grain_metadata(int index) const;
    godot::PackedFloat32Array get_grain_pcm(int index) const;
    float estimate_f0_for_params(godot::PackedFloat32Array params, int k = 8);

    // Axis info
    godot::StringName get_axis_name(int axis) const;
    float get_axis_min(int axis) const;
    float get_axis_max(int axis) const;
    bool  get_axis_derived(int axis) const;

    // Source provenance
    godot::PackedStringArray get_source_paths() const { return source_paths; }
    void set_source_paths(godot::PackedStringArray p) { source_paths = p; }
    godot::PackedInt32Array get_grain_source_ids() const;
    void set_grain_source_ids(godot::PackedInt32Array ids);
    int get_grain_source_id(int index) const;

    // Serialization properties
    godot::TypedArray<godot::Dictionary> get_axes_data() const;
    void set_axes_data(godot::TypedArray<godot::Dictionary> data);

    godot::PackedFloat32Array get_grain_data() const;
    void set_grain_data(godot::PackedFloat32Array data);

    // Primary PCM storage — raw bytes in whichever compression_mode was active at save time.
    godot::PackedByteArray get_pcm_data() const;
    void set_pcm_data(godot::PackedByteArray data);

    // Legacy backward-compat setter: called when loading old .tres files that
    // stored pcm_pool as PackedFloat32Array. Migrates to FLOAT32 byte encoding.
    void set_pcm_pool(godot::PackedFloat32Array data);

    int  get_compression_mode() const { return static_cast<int>(compression_mode); }
    void set_compression_mode(int mode) { compression_mode = static_cast<CompressionMode>(mode); }

    // Expose sample count and byte count to GDScript for diagnostics / UI.
    int get_pcm_sample_count() const { return pcm_sample_count(); }

    float get_stored_sample_rate() const { return stored_sample_rate; }
    void set_stored_sample_rate(float rate) { stored_sample_rate = rate; }

    int get_num_axes_stored() const { return num_axes_stored; }
    void set_num_axes_stored(int n) { num_axes_stored = n; }

    // Override to migrate legacy "pcm_pool" (PackedFloat32Array) from old .tres files.
    bool _set(const godot::StringName& p_name, const godot::Variant& p_value);

    // Diagnostic: prints grain count, axis ranges, and the actual min/max
    // of grain.params per axis so you can verify the data spread.
    void print_stats();
};

#endif
