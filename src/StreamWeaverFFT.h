#ifndef STREAMWEAVER_FFT_H
#define STREAMWEAVER_FFT_H

#include "godot_cpp/classes/audio_stream.hpp"
#include "godot_cpp/classes/audio_stream_wav.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/dictionary.hpp"
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

    // Original unfiltered mono PCM at the source sample rate, used for waveform display.
    std::vector<float> original_mono_pcm;

    // Cached onset-function debug from the most recent detect_low_freq_cycles
    // call. Populated for visualisation; mutable so the detector can stay
    // const. Empty until the first run.
    mutable std::vector<float> last_onset_fn;
    mutable std::vector<float> last_threshold;
    mutable int last_onset_frame_lo = 0;
    mutable int last_onset_frame_hi = 0;

    // Cached track-fundamental debug from the most recent track_fundamental
    // call: per-frame harmonic-mean observation (dB) along the tracked path,
    // the seed peak, and the stop threshold. Lets the editor visualise why
    // tracking terminated where it did (energy below seed_peak - 20 dB).
    mutable std::vector<float> last_track_obs;
    mutable float last_track_seed_peak = 0.f;
    mutable float last_track_stop_thresh = 0.f;
    mutable int last_track_frame_lo = 0;
    mutable int last_track_frame_hi = -1;
    mutable int last_track_start_frame = 0;

    // Current display transform settings (applied to produce `image`).
    float display_db_min = -80;
    float display_db_max = 0;
    float gamma_value = 1.0f;
    bool per_frame_normalize = false;
    bool spectral_whiten = false;
    bool log_hz = true;

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
        bool p_spectral_whiten,
        bool p_log_hz);

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
    // stop_drop_db sets the gap threshold: frames whose harmonic-mean obs
    // drops more than stop_drop_db below the seed peak are tagged as gaps in
    // the output (Vector2(time, -1) sentinel) instead of terminating the walk,
    // so tracking can resume when the signal returns. With a sketch hint
    // present, the walk follows the sketch through the gap.
    godot::PackedVector2Array track_fundamental(
        float start_time_s,
        godot::PackedVector2Array harmonic_picks,
        float bandwidth_hz,
        float max_slope_hz_per_sec,
        float transition_penalty,
        godot::PackedVector2Array sketch_hint,
        float sketch_weight,
        float stop_drop_db) const;

    // STFT bin-masking reconstruction.
    godot::Ref<godot::AudioStreamWAV> reconstruct_isolated(
        godot::PackedVector2Array tracked_curve,
        float bandwidth_hz) const;

    // Time-domain onset detection for very low fundamentals (sub-100 Hz)
    // where FFT bin resolution is coarser than the fundamental and
    // Viterbi tracking on harmonics is unreliable. Detects peaks in a
    // mid-band spectral-energy envelope, enforces a refractory period of
    // 1 / hz_max between onsets, and returns one (time_seconds, hz) per
    // detected cycle, where hz = 1 / inter-onset interval. sensitivity is
    // 0..1 (higher = more onsets pass the adaptive threshold). sketch_hint,
    // if non-empty, rejects onsets whose IOI-derived hz lies outside [0.5x,
    // 2x] of the sketched hz at that time (gap sentinels y<=0 supported).
    godot::PackedVector2Array detect_low_freq_cycles(
        float start_time_s,
        float end_time_s,
        float hz_min,
        float hz_max,
        float sensitivity,
        godot::PackedVector2Array sketch_hint) const;

    // Raw PCM access for grain extraction.
    const std::vector<float>& get_mono_pcm() const { return mono_pcm; }
    // GDScript-accessible version of get_mono_pcm().
    godot::PackedFloat32Array get_mono_pcm_packed() const;
    // Original unfiltered PCM at source sample rate, for waveform display.
    godot::PackedFloat32Array get_original_mono_pcm_packed() const;
    // Total duration in seconds.
    float get_total_duration() const { return static_cast<float>(num_frames) * seconds_per_frame; }
    int get_num_frames() const { return num_frames; }
    int get_num_bins() const { return num_bins; }
    const std::vector<float>& get_full_mag_db() const { return full_mag_db; }

    // Onset-detection debug snapshot from the last detect_low_freq_cycles
    // call. Returns a Dictionary with PackedFloat32Arrays "onset_fn" and
    // "threshold" (both length = frame_hi - frame_lo + 1), plus the integer
    // bounds "frame_lo" / "frame_hi" and the float "seconds_per_frame".
    // Empty Dictionary if detection has not been run.
    godot::Dictionary get_last_onset_debug() const;

    // Tracker debug snapshot from the last track_fundamental call. Returns a
    // Dictionary with PackedFloat32Array "obs" (harmonic-mean observation in
    // dB along the tracked path, length = frame_hi - frame_lo + 1), floats
    // "seed_peak", "stop_thresh", "seconds_per_frame", "analysis_db_floor",
    // and ints "frame_lo", "frame_hi", "start_frame". Empty Dictionary if
    // tracking has not been run.
    godot::Dictionary get_last_track_debug() const;
};

#endif
