#ifndef STREAMWEAVER_FFT_H
#define STREAMWEAVER_FFT_H

#include "godot_cpp/classes/audio_stream.hpp"
#include "godot_cpp/classes/audio_stream_wav.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"

#include <vector>

class StreamWeaverFFT : public godot::RefCounted {
    GDCLASS(StreamWeaverFFT, RefCounted)

    static void _bind_methods();

    // Full-resolution FFT magnitudes in dB (after decimation if zoom FFT is used).
    // Row-major: full_mag_db[frame * num_bins + bin]. num_bins == fft_size/2 + 1.
    std::vector<float> full_mag_db;
    int num_frames = 0;
    int num_bins = 0;
    int y_resolution = 0;
    int fft_size_used = 0;
    int hop_used = 0;
    float seconds_per_frame = 0;
    float decimated_sample_rate = 0;
    float original_sample_rate = 0;
    float analysis_db_floor = -80;
    float bin_hz = 0;
    int decimation_factor = 1;

    // Visible frequency window. The image shows only [display_min_hz, display_max_hz].
    float display_min_hz = 0;
    float display_max_hz = 0;

    // Decimated mono PCM, used for reconstruction.
    std::vector<float> mono_pcm;

    // Current display transform settings (applied to produce `image`).
    float display_db_min = -80;
    float display_db_max = 0;
    float gamma_value = 1.0f;
    bool per_frame_normalize = false;
    bool spectral_whiten = false;

    godot::Ref<godot::Image> image;

    void apply_transforms(std::vector<float>& work, bool whiten, bool per_frame) const;
    void rebuild_image_internal();

public:
    static godot::Ref<StreamWeaverFFT> create_from_audio_stream(
        godot::Ref<godot::AudioStream> stream,
        int fft_size,
        float db_floor,
        int y_resolution,
        float x_resolution_per_second,
        float min_hz,
        float max_hz);

    void set_display_settings(
        float p_display_db_min,
        float p_display_db_max,
        float p_gamma,
        bool p_per_frame_normalize,
        bool p_spectral_whiten);

    // Returns (p5, p99.5) of the transformed spectrogram, suitable as sensible
    // default values for display_db_min and display_db_max.
    godot::Vector2 compute_auto_display_range(bool p_per_frame_normalize, bool p_spectral_whiten) const;

    godot::Ref<godot::Image> get_image() const { return image; }
    int get_x_resolution() const { return num_frames; }
    int get_y_resolution() const { return y_resolution; }
    float get_seconds_per_frame() const { return seconds_per_frame; }
    float get_sample_rate() const { return original_sample_rate; }
    float get_decimated_sample_rate() const { return decimated_sample_rate; }
    int get_decimation_factor() const { return decimation_factor; }
    float get_analysis_db_floor() const { return analysis_db_floor; }
    float get_display_min_hz() const { return display_min_hz; }
    float get_display_max_hz() const { return display_max_hz; }
    float get_bin_hz() const { return bin_hz; }

    float pixel_x_to_time(int px) const;
    float pixel_y_to_hz(int py) const;
    int hz_to_pixel_y(float hz) const;
    int time_to_pixel_x(float time_s) const;

    // Viterbi-based fundamental tracker. harmonic_picks is a list of
    // (hz, order) pairs: each entry is a user click at a known harmonic with
    // its order relative to the fundamental. The start fundamental is inferred
    // from the picks (median of hz/order); the observation cost at candidate
    // fundamental bin b is the mean dB over the picked orders evaluated at
    // round(k*b). sketch_hint (optional) is a (time, hz) polyline the user
    // drew over the spectrogram; frames inside its time range get an extra
    // linear cost pulling the path toward the sketched fundamental.
    godot::PackedVector2Array track_fundamental(
        float start_time_s,
        godot::PackedVector2Array harmonic_picks,
        float bandwidth_hz,
        float max_slope_hz_per_sec,
        float transition_penalty,
        godot::PackedVector2Array sketch_hint,
        float sketch_weight) const;

    // STFT bin-masking reconstruction.
    godot::Ref<godot::AudioStreamWAV> reconstruct_isolated(
        godot::PackedVector2Array tracked_curve,
        float bandwidth_hz) const;

    // Raw PCM access for grain extraction.
    const std::vector<float>& get_mono_pcm() const { return mono_pcm; }
    int get_num_frames() const { return num_frames; }
    int get_num_bins() const { return num_bins; }
    const std::vector<float>& get_full_mag_db() const { return full_mag_db; }
};

#endif
