#include "StreamWeaver.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"

#include "profiling.h"

using namespace godot;

// -------------------- StreamWeaverInputStream --------------------
void StreamWeaverInputStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_input_name"), &StreamWeaverInputStream::GetInputName);
	ClassDB::bind_method(D_METHOD("set_input_name", "input_name"), &StreamWeaverInputStream::SetInputName);
	ClassDB::bind_method(D_METHOD("get_audio_stream"), &StreamWeaverInputStream::GetAudioStream);
	ClassDB::bind_method(D_METHOD("set_audio_stream", "audio_stream"), &StreamWeaverInputStream::SetAudioStream);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "input_name"), "set_input_name", "get_input_name");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "audio_stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_audio_stream", "get_audio_stream");
}

// -------------------- ParameterCondition --------------------
void ParameterCondition::_bind_methods() {

}

// -------------------- ParameterConditionComparison --------------------
void ParameterConditionComparison::_bind_methods() {
	BIND_ENUM_CONSTANT(EQ);
	BIND_ENUM_CONSTANT(LT);
	BIND_ENUM_CONSTANT(GT);
	BIND_ENUM_CONSTANT(LTE);
	BIND_ENUM_CONSTANT(GTE);
	BIND_ENUM_CONSTANT(NEQ);

	ClassDB::bind_method(D_METHOD("get_parameter_name"), &ParameterConditionComparison::GetParameterName);
	ClassDB::bind_method(D_METHOD("set_parameter_name", "parameter_name"), &ParameterConditionComparison::SetParameterName);
	ClassDB::bind_method(D_METHOD("get_comparison_type"), &ParameterConditionComparison::GetComparisonType);
	ClassDB::bind_method(D_METHOD("set_comparison_type", "comparison_type"), &ParameterConditionComparison::SetComparisonType);
	ClassDB::bind_method(D_METHOD("get_value"), &ParameterConditionComparison::GetValue);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &ParameterConditionComparison::SetValue);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "parameter_name"), "set_parameter_name", "get_parameter_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "comparison_type", PROPERTY_HINT_ENUM, "EQ,LT,GT,LTE,GTE,NEQ"), "set_comparison_type", "get_comparison_type");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
}

// -------------------- ParameterConditionRange --------------------
void ParameterConditionRange::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_parameter_name"), &ParameterConditionRange::GetParameterName);
	ClassDB::bind_method(D_METHOD("set_parameter_name", "parameter_name"), &ParameterConditionRange::SetParameterName);
	ClassDB::bind_method(D_METHOD("get_min_value"), &ParameterConditionRange::GetMinValue);
	ClassDB::bind_method(D_METHOD("set_min_value", "min_value"), &ParameterConditionRange::SetMinValue);
	ClassDB::bind_method(D_METHOD("get_max_value"), &ParameterConditionRange::GetMaxValue);
	ClassDB::bind_method(D_METHOD("set_max_value", "max_value"), &ParameterConditionRange::SetMaxValue);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "parameter_name"), "set_parameter_name", "get_parameter_name");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min_value", "get_min_value");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max_value", "get_max_value");
}

// -------------------- StreamWeaverOutput --------------------
void StreamWeaverOutput::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_conditions"), &StreamWeaverOutput::GetConditions);
	ClassDB::bind_method(D_METHOD("set_conditions", "conditions"), &StreamWeaverOutput::SetConditions);
	ClassDB::bind_method(D_METHOD("get_triggered_by"), &StreamWeaverOutput::GetTriggeredBy);
	ClassDB::bind_method(D_METHOD("set_triggered_by", "triggered_by"), &StreamWeaverOutput::SetTriggeredBy);
	ClassDB::bind_method(D_METHOD("get_output_name"), &StreamWeaverOutput::GetOutputName);
	ClassDB::bind_method(D_METHOD("set_output_name", "output_name"), &StreamWeaverOutput::SetOutputName);
	
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "output_name"), "set_output_name", "get_output_name");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "conditions", PROPERTY_HINT_ARRAY_TYPE, "ParameterCondition"), "set_conditions", "get_conditions");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "triggered_by"), "set_triggered_by", "get_triggered_by");
}

bool StreamWeaverOutput::should_trigger(StringName on_trigger, const HashMap<StringName, float> &parameters) {
	PROFILE_FUNCTION();
	if (on_trigger != triggered_by)
		return false;
	for (int i = 0; i < conditions.size(); ++i) {
		const auto condition = cast_to<ParameterCondition>(conditions[i]);
		for (auto param : parameters) {
			if (!condition->check(param.key, param.value)) {
				return false;
			}
		}
	}
	return true;
}

// -------------------- StreamWeaverOutputRandomize --------------------
void StreamWeaverOutputRandomize::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_randomize_pitch"), &StreamWeaverOutputRandomize::GetRandomizePitch);
	ClassDB::bind_method(D_METHOD("set_randomize_pitch", "randomize_pitch"), &StreamWeaverOutputRandomize::SetRandomizePitch);
	ClassDB::bind_method(D_METHOD("get_randomize_volume"), &StreamWeaverOutputRandomize::GetRandomizeVolume);
	ClassDB::bind_method(D_METHOD("set_randomize_volume", "randomize_volume"), &StreamWeaverOutputRandomize::SetRandomizeVolume);
	ClassDB::bind_method(D_METHOD("get_input_streams"), &StreamWeaverOutputRandomize::GetInputStreams);
	ClassDB::bind_method(D_METHOD("set_input_streams", "input_streams"), &StreamWeaverOutputRandomize::SetInputStreams);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "randomize_pitch"), "set_randomize_pitch", "get_randomize_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "randomize_volume"), "set_randomize_volume", "get_randomize_volume");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "input_streams", PROPERTY_HINT_ARRAY_TYPE, "StringName"), "set_input_streams", "get_input_streams");
}

class ParameterizedOutputRandomizeRuntimeInstance : public StreamWeaverOutputRuntimeInstanceBase {
public:
	float randomize_pitch;
	float randomize_volume;
	Ref<RandomNumberGenerator> randomizer;
	LocalVector<const StreamWeaverInputStream*> inputs;
	int last_played_index = 0;

	struct RuntimeInputData {
		Ref<AudioStreamPlayback> playback;
		float pitch;
		float volume;
	};
	LocalVector<RuntimeInputData> currently_playing_inputs;
	void triggered() override {
		if (inputs.is_empty())
			return;
		RuntimeInputData runtime_input {};
		int input_index = randomizer->randi_range(0, inputs.size()-1);
		// super simple way to not play the same sound twice in a row:
		if (input_index == last_played_index && inputs.size() > 1) {
			input_index = (input_index + 1) % inputs.size();
		}
		last_played_index = input_index;
	    runtime_input.playback = inputs[input_index]->GetAudioStream()->instantiate_playback();
	    runtime_input.playback->start();
		runtime_input.pitch = randomizer->randf_range(-randomize_pitch, randomize_pitch);
		runtime_input.volume = randomizer->randf_range(-randomize_volume, randomize_volume);
		currently_playing_inputs.push_back(runtime_input);
	}

	bool mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
		if (currently_playing_inputs.is_empty()) {
			return false;
		}
		PROFILE_FUNCTION();
		int current_input_index = 0;
		while (current_input_index < currently_playing_inputs.size()) {
			auto& cpi = currently_playing_inputs[current_input_index];
			auto mixed_input = cpi.playback->mix_audio(cpi.pitch, p_frames);
		    int num_frames_mixed = Math::min( p_frames, static_cast<int32_t>( mixed_input.size() ) );
			for (int i = 0; i < num_frames_mixed; i++) {
				p_buffer[i].left += mixed_input[i].x;
				p_buffer[i].right += mixed_input[i].y;
			}

			if (num_frames_mixed < p_frames) {
				currently_playing_inputs.remove_at(current_input_index);
			}
			else {
				++current_input_index;
			}
		}
		return true;
	}
};

StreamWeaverOutputRuntimeInstanceBase *StreamWeaverOutputRandomize::create_runtime_instance(const StreamWeaverAudioStreamPlayback &from_playback) {
	PROFILE_FUNCTION();
	auto* instance = new ParameterizedOutputRandomizeRuntimeInstance();
	instance->randomize_pitch = randomize_pitch;
	instance->randomize_volume = randomize_volume;
	for (int i = 0; i < input_streams.size(); ++i) {
		StringName input_stream_name = input_streams[i];
		for (int input_index=0; input_index < from_playback.GetParent().GetInputs().size(); ++input_index) {
			const auto input = cast_to<StreamWeaverInputStream>(from_playback.GetParent().GetInputs()[input_index]);
			if (input->GetInputName() == input_stream_name) {
				instance->inputs.push_back(input);
				break;
			}
		}
	}
	static int random_seed = 54631;
	random_seed += 24462;
	instance->randomizer.instantiate();
	instance->randomizer->set_seed(random_seed);
	return instance;
}

void StreamWeaverOutputRandomize::release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) {
	delete dynamic_cast<ParameterizedOutputRandomizeRuntimeInstance*>(instance);
}

// -------------------- StreamWeaverOutputLooping --------------------

void StreamWeaverOutputLooping::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_input_stream"), &StreamWeaverOutputLooping::GetInputStream);
	ClassDB::bind_method(D_METHOD("set_input_stream", "input_stream"), &StreamWeaverOutputLooping::SetInputStream);

	ClassDB::bind_method(D_METHOD("get_modifying_parameter_name"), &StreamWeaverOutputLooping::GetModifyingParameterName);
	ClassDB::bind_method(D_METHOD("set_modifying_parameter_name", "modifying_parameter_name"), &StreamWeaverOutputLooping::SetModifyingParameterName);

	ClassDB::bind_method(D_METHOD("get_min_volume"), &StreamWeaverOutputLooping::GetMinVolume);
	ClassDB::bind_method(D_METHOD("set_min_volume", "min_volume"), &StreamWeaverOutputLooping::SetMinVolume);

	ClassDB::bind_method(D_METHOD("get_max_volume"), &StreamWeaverOutputLooping::GetMaxVolume);
	ClassDB::bind_method(D_METHOD("set_max_volume", "max_volume"), &StreamWeaverOutputLooping::SetMaxVolume);

	ClassDB::bind_method(D_METHOD("get_parameter_value_min_volume"), &StreamWeaverOutputLooping::GetParameterValueMinVolume);
	ClassDB::bind_method(D_METHOD("set_parameter_value_min_volume", "parameter_value_min_volume"), &StreamWeaverOutputLooping::SetParameterValueMinVolume);

	ClassDB::bind_method(D_METHOD("get_parameter_value_max_volume"), &StreamWeaverOutputLooping::GetParameterValueMaxVolume);
	ClassDB::bind_method(D_METHOD("set_parameter_value_max_volume", "parameter_value_max_volume"), &StreamWeaverOutputLooping::SetParameterValueMaxVolume);

	ClassDB::bind_method(D_METHOD("get_min_pitch"), &StreamWeaverOutputLooping::GetMinPitch);
	ClassDB::bind_method(D_METHOD("set_min_pitch", "min_pitch"), &StreamWeaverOutputLooping::SetMinPitch);

	ClassDB::bind_method(D_METHOD("get_max_pitch"), &StreamWeaverOutputLooping::GetMaxPitch);
	ClassDB::bind_method(D_METHOD("set_max_pitch", "max_pitch"), &StreamWeaverOutputLooping::SetMaxPitch);

	ClassDB::bind_method(D_METHOD("get_parameter_value_min_pitch"), &StreamWeaverOutputLooping::GetParameterValueMinPitch);
	ClassDB::bind_method(D_METHOD("set_parameter_value_min_pitch", "parameter_value_min_pitch"), &StreamWeaverOutputLooping::SetParameterValueMinPitch);

	ClassDB::bind_method(D_METHOD("get_parameter_value_max_pitch"), &StreamWeaverOutputLooping::GetParameterValueMaxPitch);
	ClassDB::bind_method(D_METHOD("set_parameter_value_max_pitch", "parameter_value_max_pitch"), &StreamWeaverOutputLooping::SetParameterValueMaxPitch);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "input_stream"), "set_input_stream", "get_input_stream");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "modifying_parameter_name"), "set_modifying_parameter_name", "get_modifying_parameter_name");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_volume"), "set_min_volume", "get_min_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_volume"), "set_max_volume", "get_max_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "parameter_value_min_volume"), "set_parameter_value_min_volume", "get_parameter_value_min_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "parameter_value_max_volume"), "set_parameter_value_max_volume", "get_parameter_value_max_volume");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_pitch"), "set_min_pitch", "get_min_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_pitch"), "set_max_pitch", "get_max_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "parameter_value_min_pitch"), "set_parameter_value_min_pitch", "get_parameter_value_min_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "parameter_value_max_pitch"), "set_parameter_value_max_pitch", "get_parameter_value_max_pitch");
}

class ParameterizedOutputLoopingRuntimeInstance : public StreamWeaverOutputRuntimeInstanceBase {
public:
	StreamWeaverInputStream* input;
	int last_played_index = 0;

	void triggered() override {

	}

	bool mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {

		return true;
	}
};

StreamWeaverOutputRuntimeInstanceBase *StreamWeaverOutputLooping::create_runtime_instance(const StreamWeaverAudioStreamPlayback &from_playback) {
	auto* instance = new ParameterizedOutputLoopingRuntimeInstance();

	return instance;
}

void StreamWeaverOutputLooping::release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) {
	delete dynamic_cast<ParameterizedOutputLoopingRuntimeInstance *>(instance);
}

// -------------------- StreamWeaverAudioStream --------------------
void StreamWeaverAudioStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_parameters"), &StreamWeaverAudioStream::GetParameters);
	ClassDB::bind_method(D_METHOD("set_parameters", "parameters"), &StreamWeaverAudioStream::SetParameters);
	ClassDB::bind_method(D_METHOD("get_triggers"), &StreamWeaverAudioStream::GetTriggers);
	ClassDB::bind_method(D_METHOD("set_triggers", "triggers"), &StreamWeaverAudioStream::SetTriggers);
	ClassDB::bind_method(D_METHOD("get_inputs"), &StreamWeaverAudioStream::GetInputs);
	ClassDB::bind_method(D_METHOD("set_inputs", "inputs"), &StreamWeaverAudioStream::SetInputs);
	ClassDB::bind_method(D_METHOD("get_outputs"), &StreamWeaverAudioStream::GetOutputs);
	ClassDB::bind_method(D_METHOD("set_outputs", "outputs"), &StreamWeaverAudioStream::SetOutputs);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "parameters", PROPERTY_HINT_ARRAY_TYPE, "StringName"), "set_parameters", "get_parameters");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "triggers", PROPERTY_HINT_ARRAY_TYPE, "StringName"), "set_triggers", "get_triggers");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "inputs", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverInputStream"), "set_inputs", "get_inputs");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "outputs", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverOutput"), "set_outputs", "get_outputs");
}

godot::Ref<godot::AudioStreamPlayback> StreamWeaverAudioStream::_instantiate_playback() const {
	godot::Ref<StreamWeaverAudioStreamPlayback> playback;
	playback.instantiate();
	playback->initialize({this});
	return playback;
}

godot::String StreamWeaverAudioStream::_get_stream_name() const {
	return "StreamWeaverAudioStream";
}

// -------------------- StreamWeaverAudioStreamPlayback --------------------
void StreamWeaverAudioStreamPlayback::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parameter", "parameter_name", "parameter_value"), &StreamWeaverAudioStreamPlayback::set_parameter);
	ClassDB::bind_method(D_METHOD("trigger", "trigger_name"), &StreamWeaverAudioStreamPlayback::trigger);
}

StreamWeaverAudioStreamPlayback::~StreamWeaverAudioStreamPlayback() = default;

void StreamWeaverAudioStreamPlayback::initialize(godot::Ref<StreamWeaverAudioStream> parent) {
	PROFILE_FUNCTION();
	parent_stream = parent;
	for (int i = 0; i < parent->GetParameters().size(); ++i) {
		parameters[parent->GetParameters()[i]] = 0;
	}
	for (int i = 0; i < parent->GetOutputs().size(); ++i) {
		const auto output = cast_to<StreamWeaverOutput>(parent->GetOutputs()[i]);
		if (output == nullptr) {
			continue;
		}
		runtime_outputs.push_back(
			output->create_runtime_instance(*this));
	}
}

void StreamWeaverAudioStreamPlayback::set_parameter(godot::StringName parameter_name, float value) {
	parameters[parameter_name] = value;
}

void StreamWeaverAudioStreamPlayback::trigger(godot::StringName trigger_name) {
	for (int i = 0; i < parent_stream->GetOutputs().size(); ++i) {
		const auto output = cast_to<StreamWeaverOutput>(parent_stream->GetOutputs()[i]);
		if (output->should_trigger(trigger_name, parameters)) {
			runtime_outputs[i]->triggered();
		}
	}
}

void StreamWeaverAudioStreamPlayback::_start(double p_from_pos) {
	active = true;
}

void StreamWeaverAudioStreamPlayback::_stop() {
	active = false;
}

bool StreamWeaverAudioStreamPlayback::_is_playing() const {
	return active;
}

int32_t StreamWeaverAudioStreamPlayback::_mix(godot::AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	PROFILE_FUNCTION();
	if (!active) {
		return 0;
	}
	for (int i = 0; i < p_frames; ++i) {
		p_buffer[i].left = 0;
		p_buffer[i].right = 0;
	}
	for (auto runtime_output : runtime_outputs) {
		runtime_output->mix_output_into_buffer(p_buffer, p_frames);
	}
	return p_frames;
}
