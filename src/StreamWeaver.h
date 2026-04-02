#ifndef STREAMWEAVER_H
#define STREAMWEAVER_H

#include "godot_cpp/classes/audio_stream.hpp"
#include "godot_cpp/classes/audio_stream_playback.hpp"
#include "godot_cpp/templates/hash_map.hpp"
#include "godot_cpp/templates/local_vector.hpp"

using chrono_clock = std::chrono::steady_clock;
using fseconds = std::chrono::duration<float>;

class StreamWeaverAudioStreamPlayback;

class StreamWeaverTickInterface
{
public:
    virtual ~StreamWeaverTickInterface() = default;
    virtual void tick(float delta_time) = 0;
};

class StreamWeaverInputStream : public godot::Resource {
	GDCLASS(StreamWeaverInputStream, Resource)

	static void _bind_methods();

	godot::StringName input_name;
	godot::Ref<godot::AudioStream> audio_stream;

    godot::Vector2 graph_node_position;
public:
	godot::StringName GetInputName() const { return input_name; }
	void SetInputName(godot::StringName inputName) {
		input_name = inputName;
		set_name(input_name);
	}
	godot::Ref<godot::AudioStream> GetAudioStream() const { return audio_stream; }
	void SetAudioStream(godot::Ref<godot::AudioStream> audioStream) { audio_stream = audioStream; }
    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }
};

class StreamWeaverParameterRuntimeInstance
{
protected:
    float current_value = 0;
public:
    virtual ~StreamWeaverParameterRuntimeInstance() = default;
    virtual float get_value() { return current_value; }

    void set_current_value(float value) { current_value = value; }
};

class StreamWeaverParameter : public godot::Resource
{
    GDCLASS(StreamWeaverParameter, Resource)
    static void _bind_methods();
    godot::Vector2 graph_node_position;
public:
    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }

    virtual StreamWeaverParameterRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) = 0;
    virtual void release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance) = 0;
};

class StreamWeaverParameterInput : public StreamWeaverParameter
{
    GDCLASS(StreamWeaverParameterInput, StreamWeaverParameter)

    static void _bind_methods();

    godot::StringName parameter_name;
    float min_value = 0;
    float max_value = 1;
    float start_value = 0;
public:
    [[nodiscard]] godot::StringName GetParameterName() const { return parameter_name; }
    void SetParameterName(godot::StringName parameterName) { parameter_name = parameterName; }
    [[nodiscard]] float GetMinValue() const { return min_value; }
    void SetMinValue(float minValue) { min_value = minValue; }
    [[nodiscard]] float GetMaxValue() const { return max_value; }
    void SetMaxValue(float maxValue) { max_value = maxValue; }
    [[nodiscard]] float GetStartValue() const { return start_value; }
    void SetStartValue(float startValue) { start_value = startValue; }

    StreamWeaverParameterRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance) override;
};

class StreamWeaverParameterRemap : public StreamWeaverParameter
{
    GDCLASS(StreamWeaverParameterRemap, StreamWeaverParameter)
    static void _bind_methods();

    float input_range_start = 0;
    float input_range_end = 1;
    float output_range_start = 0;
    float output_range_end = 1;
    godot::Ref<StreamWeaverParameterInput> input_parameter;
public:
    [[nodiscard]] float GetInputRangeStart() const { return input_range_start; }
    void SetInputRangeStart(float inputRangeStart) { input_range_start = inputRangeStart; }
    [[nodiscard]] float GetInputRangeEnd() const { return input_range_end; }
    void SetInputRangeEnd(float inputRangeEnd) { input_range_end = inputRangeEnd; }
    [[nodiscard]] float GetOutputRangeStart() const { return output_range_start; }
    void SetOutputRangeStart(float outputRangeStart) { output_range_start = outputRangeStart; }
    [[nodiscard]] float GetOutputRangeEnd() const { return output_range_end; }
    void SetOutputRangeEnd(float outputRangeEnd) { output_range_end = outputRangeEnd; }
    [[nodiscard]] godot::Ref<StreamWeaverParameterInput> GetInputParameter() const { return input_parameter; }
    void SetInputParameter(godot::Ref<StreamWeaverParameterInput> inparam) { input_parameter = inparam; }

    StreamWeaverParameterRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance) override;
};


class StreamWeaverParameterAdd : public StreamWeaverParameter
{
    GDCLASS(StreamWeaverParameterAdd, StreamWeaverParameter)
    static void _bind_methods();

    godot::LocalVector<godot::Ref<StreamWeaverParameter>> input_parameters;
public:
    [[nodiscard]] godot::TypedArray<StreamWeaverParameter> GetInputParameters() const;
    void SetInputParameters(godot::TypedArray<StreamWeaverParameter> inputParameters);

    StreamWeaverParameterRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance) override;
};

class StreamWeaverParameterMultiply : public StreamWeaverParameter
{
    GDCLASS(StreamWeaverParameterMultiply, StreamWeaverParameter)
    static void _bind_methods();

    godot::LocalVector<godot::Ref<StreamWeaverParameter>> input_parameters;
public:
    [[nodiscard]] godot::TypedArray<StreamWeaverParameter> GetInputParameters() const;
    void SetInputParameters(godot::TypedArray<StreamWeaverParameter> inputParameters);

    StreamWeaverParameterRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance) override;
};

class StreamWeaverParameterFollowInput : public StreamWeaverParameter
{
    GDCLASS(StreamWeaverParameterFollowInput, StreamWeaverParameter)
    static void _bind_methods();

    float acceleration = 1.0;
    float max_speed = 1.0;
    godot::Ref<StreamWeaverParameter> input_parameter;
public:
    [[nodiscard]] float GetAcceleration() const { return acceleration; }
    void SetAcceleration(float p_acceleration) { acceleration = p_acceleration; }
    [[nodiscard]] float GetMaxSpeed() const { return max_speed; }
    void SetMaxSpeed(float p_max_speed) { max_speed = p_max_speed; }
    [[nodiscard]] godot::Ref<StreamWeaverParameter> GetInputParameter() const { return input_parameter; }
    void SetInputParameter(godot::Ref<StreamWeaverParameter> p_input_parameter) { input_parameter = p_input_parameter; }

    StreamWeaverParameterRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance) override;
};


class StreamWeaverRuntimeTriggerableInterface
{
public:
    virtual ~StreamWeaverRuntimeTriggerableInterface() = default;
    virtual void trigger() = 0;
};

class StreamWeaverTriggerRuntimeInstance
{
protected:
    godot::LocalVector<StreamWeaverRuntimeTriggerableInterface*> triggerables;
public:
    virtual ~StreamWeaverTriggerRuntimeInstance() = default;

    void add_triggerable(StreamWeaverRuntimeTriggerableInterface* triggerable) { triggerables.push_back(triggerable); }
    void trigger_all_triggerables()
    {
        for (auto& triggerable : triggerables) {
            triggerable->trigger();
        }
    }
};

class StreamWeaverTrigger : public godot::Resource
{
    GDCLASS(StreamWeaverTrigger, Resource)

    static void _bind_methods();
    godot::Vector2 graph_node_position;
public:
    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }

    virtual StreamWeaverTriggerRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) = 0;
    virtual void release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) = 0;
};

class StreamWeaverTriggerInput : public StreamWeaverTrigger
{
    GDCLASS(StreamWeaverTriggerInput, StreamWeaverTrigger)

    static void _bind_methods();

    godot::StringName trigger_name;
public:
    godot::StringName GetTriggerName() const { return trigger_name; }
    void SetTriggerName(godot::StringName triggerName) { trigger_name = triggerName; }

    StreamWeaverTriggerRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) override;
};

class StreamWeaverTriggerConditionalParameter : public StreamWeaverTrigger {
	GDCLASS(StreamWeaverTriggerConditionalParameter, StreamWeaverTrigger)

	static void _bind_methods();
public:
	enum Comparison {
		EQ, LT, GT, LTE, GTE, NEQ
	};
private:
    godot::Ref<StreamWeaverTrigger> input_trigger;
	godot::Ref<StreamWeaverParameterInput> parameter;
	Comparison comparison_type = EQ;
	float value = 0;

	void update_name() {
		set_name(godot::vformat("%s %s %f",
			parameter.is_valid() ? parameter->GetParameterName() : godot::StringName("<null>"),
			comparison_type == EQ ? "==" :
			comparison_type == LT ? "<" :
			comparison_type == GT ? ">" :
			comparison_type == LTE ? "<=" :
			comparison_type == GTE ? ">=" :
			"!=",
			value));
	}
public:
	[[nodiscard]] godot::Ref<StreamWeaverParameterInput> GetParameter() const { return parameter; }
	void SetParameter(godot::Ref<StreamWeaverParameterInput> p) { parameter = p; update_name(); }
    [[nodiscard]] godot::Ref<StreamWeaverTrigger> GetInputTrigger() const { return input_trigger; }
    void SetInputTrigger(godot::Ref<StreamWeaverTrigger> inputTrigger) { input_trigger = inputTrigger; }
	[[nodiscard]] int GetComparisonType() const { return comparison_type; }
	void SetComparisonType(int comparisonType) { comparison_type = static_cast<Comparison>(comparisonType); update_name(); }
	[[nodiscard]] float GetValue() const { return value; }
	void SetValue(float v) { this->value = v; update_name(); }

    StreamWeaverTriggerRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) override;
};
VARIANT_ENUM_CAST(StreamWeaverTriggerConditionalParameter::Comparison);


class StreamWeaverTriggerConditionalParameterRange : public StreamWeaverTrigger {
	GDCLASS(StreamWeaverTriggerConditionalParameterRange, StreamWeaverTrigger)

	static void _bind_methods();

	godot::Ref<StreamWeaverParameterInput> parameter;
    godot::Ref<StreamWeaverTrigger> input_trigger;
	float min_value = 0;
	float max_value = 1;
	void update_name() {
		set_name(godot::vformat("%f <= %s >= %f", min_value, parameter.is_valid() ? parameter->GetParameterName() : godot::StringName("<null>"), max_value));
	}
public:
	[[nodiscard]] godot::Ref<StreamWeaverParameterInput> GetParameter() const { return parameter; }
	void SetParameter(godot::Ref<StreamWeaverParameterInput> p) { parameter = p; update_name(); }
    [[nodiscard]] godot::Ref<StreamWeaverTrigger> GetInputTrigger() const { return input_trigger; }
    void SetInputTrigger(godot::Ref<StreamWeaverTrigger> inputTrigger) { input_trigger = inputTrigger; }
	[[nodiscard]] float GetMinValue() const { return min_value; }
	void SetMinValue(float minValue) { min_value = minValue; update_name(); }
	[[nodiscard]] float GetMaxValue() const { return max_value; }
	void SetMaxValue(float maxValue) { max_value = maxValue; update_name(); }

    StreamWeaverTriggerRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) override;
};

class StreamWeaverTriggerDelay : public StreamWeaverTrigger {
    GDCLASS(StreamWeaverTriggerDelay, StreamWeaverTrigger)
    static void _bind_methods();

    godot::Ref<StreamWeaverTrigger> input_trigger;
    float delay_time = 0;
    godot::Ref<StreamWeaverParameter> multiplier_parameter;
public:
    [[nodiscard]] godot::Ref<StreamWeaverTrigger> GetInputTrigger() const { return input_trigger; }
    void SetInputTrigger(godot::Ref<StreamWeaverTrigger> inputTrigger) { input_trigger = inputTrigger; }
    [[nodiscard]] float GetDelayTime() const { return delay_time; }
    void SetDelayTime(float delayTime) { delay_time = delayTime; }
    [[nodiscard]] godot::Ref<StreamWeaverParameter> GetMultiplierParameter() const { return multiplier_parameter; }
    void SetMultiplierParameter(godot::Ref<StreamWeaverParameter> multiplierParameter) { multiplier_parameter = multiplierParameter; }

    StreamWeaverTriggerRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) override;
};

class StreamWeaverTriggerMetronome : public StreamWeaverTrigger {
    GDCLASS(StreamWeaverTriggerMetronome, StreamWeaverTrigger)
    static void _bind_methods();

    float base_trigger_rate = 1;
    godot::Ref<StreamWeaverParameter> multiplier_parameter;
public:
    [[nodiscard]] float GetBaseTriggerRate() const { return base_trigger_rate; }
    void SetBaseTriggerRate(float baseTriggerRate) { base_trigger_rate = baseTriggerRate; }
    [[nodiscard]] godot::Ref<StreamWeaverParameter> GetMultiplierParameter() const { return multiplier_parameter; }
    void SetMultiplierParameter(godot::Ref<StreamWeaverParameter> multiplierParameter) { multiplier_parameter = multiplierParameter; }

    StreamWeaverTriggerRuntimeInstance* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
    void release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) override;
};


class StreamWeaverOutputRuntimeInstanceBase : public StreamWeaverRuntimeTriggerableInterface {
public:
	~StreamWeaverOutputRuntimeInstanceBase() override = default;
	virtual bool mix_output_into_buffer(godot::AudioFrame *p_buffer, int32_t p_frames) =0;

    float base_volume_db = 0;
    float base_pitch = 1;
    StreamWeaverParameterRuntimeInstance* volume_multiplier = nullptr;
    StreamWeaverParameterRuntimeInstance* pitch_multiplier = nullptr;

    [[nodiscard]] float get_volume() const { return godot::UtilityFunctions::db_to_linear(base_volume_db) * (volume_multiplier ? volume_multiplier->get_value() : 1); }
    [[nodiscard]] float get_pitch() const { return base_pitch * (pitch_multiplier ? pitch_multiplier->get_value() : 1); }
};

class StreamWeaverOutput : public godot::Resource {
	GDCLASS(StreamWeaverOutput, Resource)
protected:
	static void _bind_methods();
	godot::StringName output_name;
	godot::TypedArray<godot::Ref<StreamWeaverTrigger>> triggers;

    godot::Vector2 graph_node_position;

	void update_name() {
		set_name(output_name);
	}
    float base_volume_db = 0;
    float base_pitch = 1;
    godot::Ref<StreamWeaverParameter> volume_multiplier;
    godot::Ref<StreamWeaverParameter> pitch_multiplier;

    void initialize_runtime_instance_base(StreamWeaverOutputRuntimeInstanceBase* runtime_instance, StreamWeaverAudioStreamPlayback* playback);
public:
	[[nodiscard]] godot::StringName GetOutputName() const { return output_name; }
	void SetOutputName(godot::StringName outputName) { output_name = outputName; update_name(); }
	[[nodiscard]] godot::TypedArray<godot::Ref<StreamWeaverTrigger>> GetTriggers() const { return triggers; }
	void SetTriggers(godot::TypedArray<godot::Ref<StreamWeaverTrigger>> p_triggers) { triggers = p_triggers; update_name(); }

    [[nodiscard]] float GetBaseVolumeDb() const { return base_volume_db; }
    void SetBaseVolumeDb(float baseVolumeDb) { base_volume_db = baseVolumeDb; }
    [[nodiscard]] float GetBasePitch() const { return base_pitch; }
    void SetBasePitch(float basePitch) { base_pitch = basePitch; }

    [[nodiscard]] godot::Ref<StreamWeaverParameter> GetVolumeMultiplier() const { return volume_multiplier; }
    void SetVolumeMultiplier(godot::Ref<StreamWeaverParameter> volumeMultiplier) { volume_multiplier = volumeMultiplier; }
    [[nodiscard]] godot::Ref<StreamWeaverParameter> GetPitchMultiplier() const { return pitch_multiplier; }
    void SetPitchMultiplier(godot::Ref<StreamWeaverParameter> pitchMultiplier) { pitch_multiplier = pitchMultiplier; }

	virtual StreamWeaverOutputRuntimeInstanceBase* create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) =0;
	virtual void release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase* instance) = 0;

    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }
};

class StreamWeaverOutputRandomize : public StreamWeaverOutput {
	GDCLASS(StreamWeaverOutputRandomize, StreamWeaverOutput)

	static void _bind_methods();

	float randomize_pitch_offset = 0;
	float randomize_volume_offset = 0;
	godot::TypedArray<godot::Ref<StreamWeaverInputStream>> input_streams;

public:
	[[nodiscard]] float GetRandomizePitchOffset() const { return randomize_pitch_offset; }
	void SetRandomizePitchOffset(float randomizePitch) { randomize_pitch_offset = randomizePitch; }
	[[nodiscard]] float GetRandomizeVolumeOffset() const { return randomize_volume_offset; }
	void SetRandomizeVolumeOffset(float randomizeVolume) { randomize_volume_offset = randomizeVolume; }
	[[nodiscard]] godot::TypedArray<godot::Ref<StreamWeaverInputStream>> GetInputStreams() const { return input_streams; }
	void SetInputStreams(godot::TypedArray<godot::Ref<StreamWeaverInputStream>> inputStreams) { input_streams = inputStreams; }

	StreamWeaverOutputRuntimeInstanceBase *create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
	void release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) override;
};

class StreamWeaverOutputLooping : public StreamWeaverOutput {
	GDCLASS(StreamWeaverOutputLooping, StreamWeaverOutput)

	static void _bind_methods();


	godot::StringName input_stream;
	godot::Ref<StreamWeaverParameterInput> modifying_parameter;
	float min_volume = 0;
	float max_volume = 0;
	float parameter_value_min_volume;
	float parameter_value_max_volume;
	float min_pitch = 1;
	float max_pitch = 1;
	float parameter_value_min_pitch;
	float parameter_value_max_pitch;
public:
	[[nodiscard]] godot::StringName GetInputStream() const { return input_stream; }
	void SetInputStream(godot::StringName inputStream) { input_stream = inputStream; }
	[[nodiscard]] godot::Ref<StreamWeaverParameterInput> GetModifyingParameter() const { return modifying_parameter; }
	void SetModifyingParameter(godot::Ref<StreamWeaverParameterInput> modifyingParameter) { modifying_parameter = modifyingParameter; }
	[[nodiscard]] float GetMinVolume() const { return min_volume; }
	void SetMinVolume(float minVolume) { min_volume = minVolume; }
	[[nodiscard]] float GetMaxVolume() const { return max_volume; }
	void SetMaxVolume(float maxVolume) { max_volume = maxVolume; }
	[[nodiscard]] float GetParameterValueMinVolume() const { return parameter_value_min_volume; }
	void SetParameterValueMinVolume(float parameterValueMinVolume) { parameter_value_min_volume = parameterValueMinVolume; }
	[[nodiscard]] float GetParameterValueMaxVolume() const { return parameter_value_max_volume; }
	void SetParameterValueMaxVolume(float parameterValueMaxVolume) { parameter_value_max_volume = parameterValueMaxVolume; }
	[[nodiscard]] float GetMinPitch() const { return min_pitch; }
	void SetMinPitch(float minPitch) { min_pitch = minPitch; }
	[[nodiscard]] float GetMaxPitch() const { return max_pitch; }
	void SetMaxPitch(float maxPitch) { max_pitch = maxPitch; }
	[[nodiscard]] float GetParameterValueMinPitch() const { return parameter_value_min_pitch; }
	void SetParameterValueMinPitch(float parameterValueMinPitch) { parameter_value_min_pitch = parameterValueMinPitch; }
	[[nodiscard]] float GetParameterValueMaxPitch() const { return parameter_value_max_pitch; }
	void SetParameterValueMaxPitch(float parameterValueMaxPitch) { parameter_value_max_pitch = parameterValueMaxPitch; }

	StreamWeaverOutputRuntimeInstanceBase *create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) override;
	void release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) override;
};


class StreamWeaverAudioStream : public godot::AudioStream {
	GDCLASS(StreamWeaverAudioStream, AudioStream)

	friend class StreamWeaverAudioStreamPlayback;

	static void _bind_methods();

	godot::TypedArray<godot::Ref<StreamWeaverParameter>> parameters;
	godot::TypedArray<godot::Ref<StreamWeaverTrigger>> triggers;
	godot::TypedArray<godot::Ref<StreamWeaverInputStream>> inputs;
	godot::TypedArray<godot::Ref<StreamWeaverOutput>> outputs;

	int num_playbacks;

public:
	[[nodiscard]] godot::Ref<godot::AudioStreamPlayback> _instantiate_playback() const override;
	[[nodiscard]] godot::String _get_stream_name() const override;

	[[nodiscard]] godot::TypedArray<godot::Ref<StreamWeaverParameter>> GetParameters() const { return parameters; }
	void SetParameters(godot::TypedArray<godot::Ref<StreamWeaverParameter>> p) { this->parameters = p; }
	[[nodiscard]] godot::TypedArray<godot::Ref<StreamWeaverTrigger>> GetTriggers() const { return triggers; }
	void SetTriggers(godot::TypedArray<godot::Ref<StreamWeaverTrigger>> t) { this->triggers = t; }
	[[nodiscard]] godot::TypedArray<godot::Ref<StreamWeaverInputStream>> GetInputs() const { return inputs; }
	void SetInputs(godot::TypedArray<godot::Ref<StreamWeaverInputStream>> inpt) { this->inputs = inpt; }
	[[nodiscard]] godot::TypedArray<godot::Ref<StreamWeaverOutput>> GetOutputs() const { return outputs; }
	void SetOutputs(godot::TypedArray<godot::Ref<StreamWeaverOutput>> o) { this->outputs = o; }
};

class StreamWeaverAudioStreamPlayback : public godot::AudioStreamPlayback {
	GDCLASS(StreamWeaverAudioStreamPlayback, AudioStreamPlayback)

	static void _bind_methods();

	godot::Ref<StreamWeaverAudioStream> parent_stream;

	godot::HashMap<godot::StringName, StreamWeaverParameterRuntimeInstance*> input_parameters;
    godot::HashMap<godot::StringName, StreamWeaverTriggerRuntimeInstance*> input_triggers;
	godot::LocalVector<StreamWeaverOutputRuntimeInstanceBase*> runtime_outputs;
    godot::LocalVector<StreamWeaverTickInterface*> ticking_objects;
    chrono_clock::time_point last_tick_time;

    godot::HashMap<godot::Ref<StreamWeaverParameter>, StreamWeaverParameterRuntimeInstance*> all_parameters;
    godot::HashMap<godot::Ref<StreamWeaverTrigger>, StreamWeaverTriggerRuntimeInstance*> all_triggers;
	bool active = false;
public:
	~StreamWeaverAudioStreamPlayback() override;
	[[nodiscard]] const StreamWeaverAudioStream& GetParent() const { return *parent_stream.ptr(); }

    StreamWeaverParameterRuntimeInstance* get_parameter_runtime_instance(godot::Ref<StreamWeaverParameter> parameter);
    StreamWeaverTriggerRuntimeInstance* get_trigger_runtime_instance(godot::Ref<StreamWeaverTrigger> trigger);
    void add_ticking(StreamWeaverTickInterface* ticking) { ticking_objects.push_back(ticking); }

	void initialize(godot::Ref<StreamWeaverAudioStream> parent);
	void set_parameter(godot::StringName parameter_name, float value);
	void trigger(godot::StringName trigger);

	void _start(double p_from_pos) override;
	void _stop() override;
	bool _is_playing() const override;
	int32_t _mix(godot::AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;
};

#endif //STREAMWEAVER_H
