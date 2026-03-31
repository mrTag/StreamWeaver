#ifndef STREAMWEAVER_H
#define STREAMWEAVER_H

#include "godot_cpp/classes/audio_stream.hpp"
#include "godot_cpp/classes/audio_stream_playback.hpp"
#include "godot_cpp/templates/hash_map.hpp"
#include "godot_cpp/templates/local_vector.hpp"

class StreamWeaverAudioStreamPlayback;

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

class StreamWeaverParameter : public godot::Resource
{
    GDCLASS(StreamWeaverParameter, Resource)

    static void _bind_methods();

    godot::StringName parameter_name;
    godot::Vector2 graph_node_position;
public:
    godot::StringName GetParameterName() const { return parameter_name; }
    void SetParameterName(godot::StringName parameterName) { parameter_name = parameterName; }

    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }
};

class StreamWeaverTrigger : public godot::Resource
{
    GDCLASS(StreamWeaverTrigger, Resource)

    static void _bind_methods();

    godot::StringName trigger_name;
    godot::Vector2 graph_node_position;
public:
    godot::StringName GetTriggerName() const { return trigger_name; }
    void SetTriggerName(godot::StringName triggerName) { trigger_name = triggerName; }
    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }
};

class ParameterCondition : public godot::Resource {
	GDCLASS(ParameterCondition, Resource)

protected:
	static void _bind_methods();

public:
	virtual bool check(godot::StringName parameterName, float parameterValue) = 0;
};

class ParameterConditionComparison : public ParameterCondition {
	GDCLASS(ParameterConditionComparison, ParameterCondition)

	static void _bind_methods();
public:
	enum Comparison {
		EQ, LT, GT, LTE, GTE, NEQ
	};
private:
	godot::Ref<StreamWeaverParameter> parameter;
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
	godot::Ref<StreamWeaverParameter> GetParameter() const { return parameter; }
	void SetParameter(godot::Ref<StreamWeaverParameter> p) { parameter = p; update_name(); }
	int GetComparisonType() const { return comparison_type; }
	void SetComparisonType(int comparisonType) { comparison_type = static_cast<Comparison>(comparisonType); update_name(); }
	float GetValue() const { return value; }
	void SetValue(float v) { this->value = v; update_name(); }
	bool check(godot::StringName parameterName, float parameterValue) override {
		if (parameter.is_valid() && parameterName == parameter->GetParameterName()) {
			switch (comparison_type) {
				case EQ: return godot::Math::is_equal_approx(parameterValue, value);
				case NEQ: return !godot::Math::is_equal_approx(parameterValue, value);
				case LT: return parameterValue < value;
				case GT: return parameterValue > value;
				case LTE: return parameterValue <= value;
				case GTE: return parameterValue >= value;
			}
		}
		return true;
	}
};
VARIANT_ENUM_CAST(ParameterConditionComparison::Comparison);


class ParameterConditionRange : public ParameterCondition {
	GDCLASS(ParameterConditionRange, ParameterCondition)

	static void _bind_methods();

	godot::Ref<StreamWeaverParameter> parameter;
	float min_value = 0;
	float max_value = 1;
	void update_name() {
		set_name(godot::vformat("%f <= %s >= %f", min_value, parameter.is_valid() ? parameter->GetParameterName() : godot::StringName("<null>"), max_value));
	}
public:
	godot::Ref<StreamWeaverParameter> GetParameter() const { return parameter; }
	void SetParameter(godot::Ref<StreamWeaverParameter> p) { parameter = p; update_name(); }
	float GetMinValue() const { return min_value; }
	void SetMinValue(float minValue) { min_value = minValue; update_name(); }
	float GetMaxValue() const { return max_value; }
	void SetMaxValue(float maxValue) { max_value = maxValue; update_name(); }
	bool check(godot::StringName parameterName, float parameterValue) override {
		if (parameter.is_valid() && parameterName == parameter->GetParameterName()) {
			return parameterValue >= min_value && parameterValue <= max_value;
		}
		return true;
	}
};

class StreamWeaverOutputRuntimeInstanceBase {
public:
	virtual ~StreamWeaverOutputRuntimeInstanceBase() = default;
	virtual void triggered() = 0;
	virtual bool mix_output_into_buffer(godot::AudioFrame *p_buffer, int32_t p_frames) =0;
};

class StreamWeaverOutput : public godot::Resource {
	GDCLASS(StreamWeaverOutput, Resource)
protected:
	static void _bind_methods();
	godot::StringName output_name;
	godot::TypedArray<ParameterCondition> conditions;
	godot::Ref<StreamWeaverTrigger> triggered_by;

    godot::Vector2 graph_node_position;

	void update_name() {
		godot::StringName trigger_name = "None";
		if (triggered_by.is_valid()) {
			trigger_name = triggered_by->GetTriggerName();
		}
		set_name(godot::vformat("%s (%s)", output_name, trigger_name));
	}
public:
	godot::StringName GetOutputName() const { return output_name; }
	void SetOutputName(godot::StringName outputName) { output_name = outputName; update_name(); }
	godot::TypedArray<ParameterCondition> GetConditions() const { return conditions; }
	void SetConditions(godot::TypedArray<ParameterCondition> c) { this->conditions = c; }
	godot::Ref<StreamWeaverTrigger> GetTriggeredBy() const { return triggered_by; }
	void SetTriggeredBy(godot::Ref<StreamWeaverTrigger> triggeredBy) { triggered_by = triggeredBy; update_name(); }

	bool should_trigger(godot::StringName trigger, const godot::HashMap<godot::StringName, float>& parameters);

	virtual StreamWeaverOutputRuntimeInstanceBase* create_runtime_instance(const StreamWeaverAudioStreamPlayback& from_playback) =0;
	virtual void release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase* instance) = 0;

    godot::Vector2 GetGraphNodePosition() const { return graph_node_position; }
    void SetGraphNodePosition(godot::Vector2 graphNodePosition) { graph_node_position = graphNodePosition; }
};

class StreamWeaverOutputRandomize : public StreamWeaverOutput {
	GDCLASS(StreamWeaverOutputRandomize, StreamWeaverOutput)

	static void _bind_methods();

	float randomize_pitch = 1;
	float randomize_volume = 1;
	godot::TypedArray<godot::Ref<StreamWeaverInputStream>> input_streams;
public:
	float GetRandomizePitch() const { return randomize_pitch; }
	void SetRandomizePitch(float randomizePitch) { randomize_pitch = randomizePitch; }
	float GetRandomizeVolume() const { return randomize_volume; }
	void SetRandomizeVolume(float randomizeVolume) { randomize_volume = randomizeVolume; }
	godot::TypedArray<godot::Ref<StreamWeaverInputStream>> GetInputStreams() const { return input_streams; }
	void SetInputStreams(godot::TypedArray<godot::Ref<StreamWeaverInputStream>> inputStreams) { input_streams = inputStreams; }

	StreamWeaverOutputRuntimeInstanceBase *create_runtime_instance(const StreamWeaverAudioStreamPlayback &from_playback) override;
	void release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) override;
};

class StreamWeaverOutputLooping : public StreamWeaverOutput {
	GDCLASS(StreamWeaverOutputLooping, StreamWeaverOutput)

	static void _bind_methods();


	godot::StringName input_stream;
	godot::Ref<StreamWeaverParameter> modifying_parameter;
	float min_volume = 0;
	float max_volume = 0;
	float parameter_value_min_volume;
	float parameter_value_max_volume;
	float min_pitch = 1;
	float max_pitch = 1;
	float parameter_value_min_pitch;
	float parameter_value_max_pitch;
public:
	godot::StringName GetInputStream() const { return input_stream; }
	void SetInputStream(godot::StringName inputStream) { input_stream = inputStream; }
	godot::Ref<StreamWeaverParameter> GetModifyingParameter() const { return modifying_parameter; }
	void SetModifyingParameter(godot::Ref<StreamWeaverParameter> modifyingParameter) { modifying_parameter = modifyingParameter; }
	float GetMinVolume() const { return min_volume; }
	void SetMinVolume(float minVolume) { min_volume = minVolume; }
	float GetMaxVolume() const { return max_volume; }
	void SetMaxVolume(float maxVolume) { max_volume = maxVolume; }
	float GetParameterValueMinVolume() const { return parameter_value_min_volume; }
	void SetParameterValueMinVolume(float parameterValueMinVolume) { parameter_value_min_volume = parameterValueMinVolume; }
	float GetParameterValueMaxVolume() const { return parameter_value_max_volume; }
	void SetParameterValueMaxVolume(float parameterValueMaxVolume) { parameter_value_max_volume = parameterValueMaxVolume; }
	float GetMinPitch() const { return min_pitch; }
	void SetMinPitch(float minPitch) { min_pitch = minPitch; }
	float GetMaxPitch() const { return max_pitch; }
	void SetMaxPitch(float maxPitch) { max_pitch = maxPitch; }
	float GetParameterValueMinPitch() const { return parameter_value_min_pitch; }
	void SetParameterValueMinPitch(float parameterValueMinPitch) { parameter_value_min_pitch = parameterValueMinPitch; }
	float GetParameterValueMaxPitch() const { return parameter_value_max_pitch; }
	void SetParameterValueMaxPitch(float parameterValueMaxPitch) { parameter_value_max_pitch = parameterValueMaxPitch; }

	StreamWeaverOutputRuntimeInstanceBase *create_runtime_instance(const StreamWeaverAudioStreamPlayback &from_playback) override;
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
	godot::Ref<godot::AudioStreamPlayback> _instantiate_playback() const override;
	godot::String _get_stream_name() const override;

	godot::TypedArray<godot::Ref<StreamWeaverParameter>> GetParameters() const { return parameters; }
	void SetParameters(godot::TypedArray<godot::Ref<StreamWeaverParameter>> p) { this->parameters = p; }
	godot::TypedArray<godot::Ref<StreamWeaverTrigger>> GetTriggers() const { return triggers; }
	void SetTriggers(godot::TypedArray<godot::Ref<StreamWeaverTrigger>> t) { this->triggers = t; }
	godot::TypedArray<godot::Ref<StreamWeaverInputStream>> GetInputs() const { return inputs; }
	void SetInputs(godot::TypedArray<godot::Ref<StreamWeaverInputStream>> inpt) { this->inputs = inpt; }
	godot::TypedArray<godot::Ref<StreamWeaverOutput>> GetOutputs() const { return outputs; }
	void SetOutputs(godot::TypedArray<godot::Ref<StreamWeaverOutput>> o) { this->outputs = o; }
};

class StreamWeaverAudioStreamPlayback : public godot::AudioStreamPlayback {
	GDCLASS(StreamWeaverAudioStreamPlayback, AudioStreamPlayback)

	static void _bind_methods();

	godot::Ref<StreamWeaverAudioStream> parent_stream;
	godot::HashMap<godot::StringName, float> parameters;
	godot::LocalVector<StreamWeaverOutputRuntimeInstanceBase*> runtime_outputs;
	bool active = false;
public:
	~StreamWeaverAudioStreamPlayback() override;
	const StreamWeaverAudioStream& GetParent() const { return *parent_stream.ptr(); }
	const godot::HashMap<godot::StringName, float>& GetCurrentParameterValues() const { return parameters; }
	float GetCurrentParameterValue(godot::StringName name) const {
		const float* value_if_there = parameters.getptr(name);
		if (value_if_there == nullptr)
			return 0;
		return *value_if_there;
	}

	void initialize(godot::Ref<StreamWeaverAudioStream> parent);
	void set_parameter(godot::StringName parameter_name, float value);
	void trigger(godot::StringName trigger);

	void _start(double p_from_pos) override;
	void _stop() override;
	bool _is_playing() const override;
	int32_t _mix(godot::AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;
};

#endif //STREAMWEAVER_H
