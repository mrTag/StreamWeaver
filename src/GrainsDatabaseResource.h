#ifndef STREAMWEAVER_GRAINSDATABASE_H
#define STREAMWEAVER_GRAINSDATABASE_H

#include "StreamWeaverFFT.h"

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/string_name.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/dictionary.hpp"

#include <vector>

class StreamWeaverGrainsDatabase : public godot::Resource {
    GDCLASS(StreamWeaverGrainsDatabase, Resource)
    static void _bind_methods();

public:
    static constexpr int MAX_AXES = 4;
    static constexpr int FIELDS_PER_GRAIN = 3 + MAX_AXES; // center_sample, f0, time, params[MAX_AXES]

    struct GrainAxisDef {
        godot::StringName name;
        float min_val = 0;
        float max_val = 1;
    };

    struct GrainEntry {
        int center_sample = 0;
        float fundamental_hz = 0;
        float original_time_s = 0;
        float params[MAX_AXES] = {};
        bool low_energy = false;
    };

private:
    // Axis metadata
    std::vector<GrainAxisDef> axes;

    // Grain storage
    std::vector<GrainEntry> grains;
    std::vector<float> pcm_pool;
    float stored_sample_rate = 48000.f;
    int num_axes_stored = 0;

    // 1D sorted index (for single-axis fast lookup)
    std::vector<int> sorted_indices; // grain indices sorted by params[0]

    // 2D grid index
    static constexpr int GRID_RES = 32;
    std::vector<std::vector<int>> grid_cells; // GRID_RES * GRID_RES cells
    bool index_dirty = true;

    void rebuild_index();
    void rebuild_1d_index();
    void rebuild_2d_index();

    // Interpolation helper for tracked curve
    static float lookup_curve(const godot::PackedVector2Array& curve, float time_s);

    // Resampling helper
    static std::vector<float> resample_linear(const std::vector<float>& src, float src_rate, float dst_rate);

public:
    // Build the grain database from FFT analysis results.
    // axis_configs: Array of Dictionaries, each with:
    //   "name": StringName
    //   "min_value": float, "max_value": float
    //   "derived_from_fundamental": bool
    //   "calibration_a": Vector2 (hz, param_value)
    //   "calibration_b": Vector2 (hz, param_value)
    //   "keyframes": PackedVector2Array (time_s, value) - for manual axes
    // source_stream is the ORIGINAL AudioStream. Grain PCM is cut from this
    // (at mix rate) to preserve audio fidelity; the FFT is used only for
    // timing info (tracked_curve interpolation, energy threshold).
    void build_from_fft(
        godot::Ref<StreamWeaverFFT> fft,
        godot::Ref<godot::AudioStream> source_stream,
        godot::PackedVector2Array tracked_curve,
        godot::TypedArray<godot::Dictionary> axis_configs,
        int cycles_per_grain,
        float crossfade_cycles,
        float energy_threshold_db,
        godot::PackedVector2Array sketch_hint = godot::PackedVector2Array());

    // Runtime lookup: find k nearest grains for a parameter constellation.
    // Returns PackedInt32Array of grain indices.
    godot::PackedInt32Array find_nearest_grains(godot::PackedFloat32Array params, int k);

    // Accessors for grain data (used by runtime output node)
    int get_grain_count() const { return static_cast<int>(grains.size()); }
    int get_num_axes() const { return num_axes_stored; }
    float get_sample_rate() const { return stored_sample_rate; }

    const GrainEntry& get_grain_entry(int index) const { return grains[index]; }
    const float* get_pcm_pool_ptr() const { return pcm_pool.data(); }
    int get_pcm_pool_size() const { return static_cast<int>(pcm_pool.size()); }

    // GDScript-friendly accessors
    godot::Dictionary get_grain_metadata(int index) const;
    godot::PackedFloat32Array get_grain_pcm(int index) const;
    float estimate_f0_for_params(godot::PackedFloat32Array params, int k = 8);

    // Axis info
    godot::StringName get_axis_name(int axis) const;
    float get_axis_min(int axis) const;
    float get_axis_max(int axis) const;

    // Serialization properties
    godot::TypedArray<godot::Dictionary> get_axes_data() const;
    void set_axes_data(godot::TypedArray<godot::Dictionary> data);

    godot::PackedFloat32Array get_grain_data() const;
    void set_grain_data(godot::PackedFloat32Array data);

    godot::PackedFloat32Array get_pcm_pool() const;
    void set_pcm_pool(godot::PackedFloat32Array data);

    float get_stored_sample_rate() const { return stored_sample_rate; }
    void set_stored_sample_rate(float rate) { stored_sample_rate = rate; }

    int get_num_axes_stored() const { return num_axes_stored; }
    void set_num_axes_stored(int n) { num_axes_stored = n; }

    // Diagnostic: prints grain count, axis ranges, and the actual min/max
    // of grain.params per axis so you can verify the data spread.
    void print_stats();
};

#endif
