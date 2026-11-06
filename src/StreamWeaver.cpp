#include "StreamWeaver.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"

#include "profiling.h"

#include <algorithm>
#include <cstdio>

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
class StreamWeaverInputStreamRuntimeInstance : public StreamWeaverStreamRuntimeInstance {
public:
	Ref<AudioStream> audio_stream;
	Ref<AudioStreamPlayback> playback;
	bool is_playing = false;

	void reset() override {
		if (audio_stream.is_valid()) {
			playback = audio_stream->instantiate_playback();
			if (playback.is_valid()) {
				playback->start();
				is_playing = true;
			}
		}
	}
	float get_remaining_playback_time_s() const override {
		if (!is_playing || playback.is_null() || audio_stream.is_null()) {
			return 0.0f;
		}
		double len = audio_stream->get_length();
		if (len <= 0.0) {
			return -1.0f;
		}
		double pos = playback->get_playback_position();
		return Math::max(0.0f, float(len - pos));
	}

	int32_t process_audio(AudioFrame *p_dst_buffer, int32_t p_frames, uint32_t p_generation) override {
		if (!is_playing || playback.is_null()) {
			memset(p_dst_buffer, 0, sizeof(AudioFrame) * p_frames);
			return 0;
		}
		auto mixed = playback->mix_audio(1.0f, p_frames);
		int32_t num = Math::min(p_frames, static_cast<int32_t>(mixed.size()));
		for (int i = 0; i < num; ++i) {
			p_dst_buffer[i].left = mixed[i].x;
			p_dst_buffer[i].right = mixed[i].y;
		}
		if (num < p_frames) {
			memset(p_dst_buffer + num, 0, sizeof(AudioFrame) * (p_frames - num));
			is_playing = false;
		}
		return num;
	}
};

StreamWeaverStreamRuntimeInstance* StreamWeaverInputStream::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(StreamWeaverInputStreamRuntimeInstance);
	instance->audio_stream = audio_stream;
	return instance;
}

void StreamWeaverInputStream::release_runtime_instance(StreamWeaverStreamRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverParameter -------------------------
void StreamWeaverParameter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_graph_node_position"), &StreamWeaverParameter::GetGraphNodePosition);
    ClassDB::bind_method(D_METHOD("set_graph_node_position", "graph_node_position"), &StreamWeaverParameter::SetGraphNodePosition);

    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "graph_node_position"), "set_graph_node_position", "get_graph_node_position");
}

// -------------------- StreamWeaverParameterInput -------------------------
void StreamWeaverParameterInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_parameter_name"), &StreamWeaverParameterInput::GetParameterName);
    ClassDB::bind_method(D_METHOD("set_parameter_name", "parameter_name"), &StreamWeaverParameterInput::SetParameterName);
    ClassDB::bind_method(D_METHOD("get_min_value"), &StreamWeaverParameterInput::GetMinValue);
    ClassDB::bind_method(D_METHOD("set_min_value", "min_value"), &StreamWeaverParameterInput::SetMinValue);
    ClassDB::bind_method(D_METHOD("get_max_value"), &StreamWeaverParameterInput::GetMaxValue);
    ClassDB::bind_method(D_METHOD("set_max_value", "max_value"), &StreamWeaverParameterInput::SetMaxValue);
    ClassDB::bind_method(D_METHOD("get_start_value"), &StreamWeaverParameterInput::GetStartValue);
    ClassDB::bind_method(D_METHOD("set_start_value", "start_value"), &StreamWeaverParameterInput::SetStartValue);

    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "parameter_name"), "set_parameter_name", "get_parameter_name");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min_value", "get_min_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max_value", "get_max_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_value"), "set_start_value", "get_start_value");
}

StreamWeaverParameterRuntimeInstance *StreamWeaverParameterInput::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    StreamWeaverParameterRuntimeInstance *instance = memnew(StreamWeaverParameterRuntimeInstance);
    instance->set_current_value( start_value );
    return instance;
}

void StreamWeaverParameterInput::release_runtime_instance(
    StreamWeaverParameterRuntimeInstance *instance )
{
    if (instance) {
        memdelete(instance);
    }
}

// -------------------- StreamWeaverParameterRemap ------------------
void StreamWeaverParameterRemap::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_input_parameter"), &StreamWeaverParameterRemap::GetInputParameter);
    ClassDB::bind_method(D_METHOD("set_input_parameter", "input_parameter"), &StreamWeaverParameterRemap::SetInputParameter);
    ClassDB::bind_method(D_METHOD("get_input_range_start"), &StreamWeaverParameterRemap::GetInputRangeStart);
    ClassDB::bind_method(D_METHOD("set_input_range_start", "input_range_start"), &StreamWeaverParameterRemap::SetInputRangeStart);
    ClassDB::bind_method(D_METHOD("get_input_range_end"), &StreamWeaverParameterRemap::GetInputRangeEnd);
    ClassDB::bind_method(D_METHOD("set_input_range_end", "input_range_end"), &StreamWeaverParameterRemap::SetInputRangeEnd);
    ClassDB::bind_method(D_METHOD("get_output_range_start"), &StreamWeaverParameterRemap::GetOutputRangeStart);
    ClassDB::bind_method(D_METHOD("set_output_range_start", "output_range_start"), &StreamWeaverParameterRemap::SetOutputRangeStart);
    ClassDB::bind_method(D_METHOD("get_output_range_end"), &StreamWeaverParameterRemap::GetOutputRangeEnd);
    ClassDB::bind_method(D_METHOD("set_output_range_end", "output_range_end"), &StreamWeaverParameterRemap::SetOutputRangeEnd);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameterInput"), "set_input_parameter", "get_input_parameter");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "input_range_start"), "set_input_range_start", "get_input_range_start");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "input_range_end"), "set_input_range_end", "get_input_range_end");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "output_range_start"), "set_output_range_start", "get_output_range_start");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "output_range_end"), "set_output_range_end", "get_output_range_end");
}

class StreamWeaverParameterRemapRuntimeInstance : public StreamWeaverParameterRuntimeInstance
{
public:
    StreamWeaverParameterRuntimeInstance* input_parameter_runtime_instance = nullptr;
    float input_range_start = 0;
    float input_range_end = 0;
    float output_range_start = 0;
    float output_range_end = 0;

    float get_value() override
    {
        if (input_parameter_runtime_instance == nullptr)
        {
            return output_range_start;
        }
        float input_value = input_parameter_runtime_instance->get_value();
        float input_range_length = input_range_end - input_range_start;
        if (input_range_length == 0)
        {
            return output_range_start;
        }
        float output_range_length = output_range_end - output_range_start;
        float output_value = (input_value - input_range_start) / input_range_length * output_range_length + output_range_start;
        return Math::clamp(output_value, output_range_start, output_range_end);
    }
};

StreamWeaverParameterRuntimeInstance* StreamWeaverParameterRemap::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    auto runtime_instance = memnew(StreamWeaverParameterRemapRuntimeInstance);
    if (input_parameter.is_valid())
    {
        runtime_instance->input_parameter_runtime_instance = from_playback->get_parameter_runtime_instance(input_parameter);
    }
    runtime_instance->input_range_start = input_range_start;
    runtime_instance->input_range_end = input_range_end;
    runtime_instance->output_range_start = output_range_start;
    runtime_instance->output_range_end = output_range_end;
    return runtime_instance;
}

void StreamWeaverParameterRemap::release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance)
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverParameterAdd ------------------

void StreamWeaverParameterAdd::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_input_parameters"), &StreamWeaverParameterAdd::GetInputParameters);
    ClassDB::bind_method(D_METHOD("set_input_parameters", "input_parameters"), &StreamWeaverParameterAdd::SetInputParameters);

    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "input_parameters", PROPERTY_HINT_TYPE_STRING, "24/17:StreamWeaverParameter"), "set_input_parameters", "get_input_parameters");
}

godot::TypedArray<StreamWeaverParameter> StreamWeaverParameterAdd::GetInputParameters() const
{
    godot::TypedArray<StreamWeaverParameter> arr;
    for (const auto& param : input_parameters) {
        arr.push_back(param);
    }
    return arr;
}

void StreamWeaverParameterAdd::SetInputParameters(godot::TypedArray<StreamWeaverParameter> inputParameters)
{
    input_parameters.clear();
    for (int i = 0; i < inputParameters.size(); ++i) {
        input_parameters.push_back(inputParameters[i]);
    }
}

class StreamWeaverParameterAddRuntimeInstance : public StreamWeaverParameterRuntimeInstance
{
public:
    godot::LocalVector<StreamWeaverParameterRuntimeInstance*> input_parameter_runtime_instances;

    float get_value() override
    {
        float sum = 0;
        for (auto* instance : input_parameter_runtime_instances) {
            if (instance) {
                sum += instance->get_value();
            }
        }
        return sum;
    }
};

StreamWeaverParameterRuntimeInstance* StreamWeaverParameterAdd::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    auto runtime_instance = memnew(StreamWeaverParameterAddRuntimeInstance);
    for (const auto& param : input_parameters) {
        if (param.is_valid()) {
            runtime_instance->input_parameter_runtime_instances.push_back(from_playback->get_parameter_runtime_instance(param));
        }
    }
    return runtime_instance;
}

void StreamWeaverParameterAdd::release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance)
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverParameterMultiply ------------------

void StreamWeaverParameterMultiply::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_input_parameters"), &StreamWeaverParameterMultiply::GetInputParameters);
    ClassDB::bind_method(D_METHOD("set_input_parameters", "input_parameters"), &StreamWeaverParameterMultiply::SetInputParameters);

    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "input_parameters", PROPERTY_HINT_TYPE_STRING, "24/17:StreamWeaverParameter"), "set_input_parameters", "get_input_parameters");
}

godot::TypedArray<StreamWeaverParameter> StreamWeaverParameterMultiply::GetInputParameters() const
{
    godot::TypedArray<StreamWeaverParameter> arr;
    for (const auto& param : input_parameters) {
        arr.push_back(param);
    }
    return arr;
}

void StreamWeaverParameterMultiply::SetInputParameters(godot::TypedArray<StreamWeaverParameter> inputParameters)
{
    input_parameters.clear();
    for (int i = 0; i < inputParameters.size(); ++i) {
        input_parameters.push_back(inputParameters[i]);
    }
}

class StreamWeaverParameterMultiplyRuntimeInstance : public StreamWeaverParameterRuntimeInstance
{
public:
    godot::LocalVector<StreamWeaverParameterRuntimeInstance*> input_parameter_runtime_instances;

    float get_value() override
    {
        if (input_parameter_runtime_instances.is_empty()) return 0;
        float product = 1.0;
        for (auto* instance : input_parameter_runtime_instances) {
            if (instance) {
                product *= instance->get_value();
            }
        }
        return product;
    }
};

StreamWeaverParameterRuntimeInstance* StreamWeaverParameterMultiply::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    auto runtime_instance = memnew(StreamWeaverParameterMultiplyRuntimeInstance);
    for (const auto& param : input_parameters) {
        if (param.is_valid()) {
            runtime_instance->input_parameter_runtime_instances.push_back(from_playback->get_parameter_runtime_instance(param));
        }
    }
    return runtime_instance;
}

void StreamWeaverParameterMultiply::release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance)
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverParameterFollowInput ------------------

void StreamWeaverParameterFollowInput::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_acceleration"), &StreamWeaverParameterFollowInput::GetAcceleration);
    ClassDB::bind_method(D_METHOD("set_acceleration", "acceleration"), &StreamWeaverParameterFollowInput::SetAcceleration);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &StreamWeaverParameterFollowInput::GetMaxSpeed);
    ClassDB::bind_method(D_METHOD("set_max_speed", "max_speed"), &StreamWeaverParameterFollowInput::SetMaxSpeed);
    ClassDB::bind_method(D_METHOD("get_input_parameter"), &StreamWeaverParameterFollowInput::GetInputParameter);
    ClassDB::bind_method(D_METHOD("set_input_parameter", "input_parameter"), &StreamWeaverParameterFollowInput::SetInputParameter);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration"), "set_acceleration", "get_acceleration");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed"), "set_max_speed", "get_max_speed");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_input_parameter", "get_input_parameter");
}

class StreamWeaverParameterFollowInputRuntimeInstance : public StreamWeaverParameterRuntimeInstance, public StreamWeaverTickInterface
{
public:
    StreamWeaverParameterRuntimeInstance* input_parameter_runtime_instance = nullptr;
    float acceleration = 1.0;
    float max_speed = 1.0;
    float current_speed = 0;

    void tick(float delta_time) override
    {
        if (!input_parameter_runtime_instance) return;

        float target_value = input_parameter_runtime_instance->get_value();
        float distance = target_value - current_value;

        if (godot::Math::is_zero_approx(distance) && godot::Math::is_zero_approx(current_speed)) {
            current_value = target_value;
            return;
        }

        float direction = (distance > 0) ? 1.0f : -1.0f;
        
        // Braking distance logic: d = v^2 / (2 * a)
        // We want to start braking when |distance| <= current_speed^2 / (2 * acceleration)
        float braking_distance = (current_speed * current_speed) / (2.0f * acceleration);

        if (godot::Math::abs(distance) <= braking_distance) {
            // BRAKE
            float brake_accel = (current_speed * current_speed) / (2.0f * godot::Math::abs(distance) + 1e-5f);
            brake_accel = godot::Math::min(brake_accel, acceleration * 2.0f);
            
            float speed_change = acceleration * delta_time;
            if (speed_change > godot::Math::abs(current_speed)) {
                current_speed = 0;
                current_value = target_value;
            } else {
                current_speed -= godot::Math::sign(current_speed) * speed_change;
                // atomic<float> has no operator+= in C++17; read-add-store instead.
                current_value = current_value + current_speed * delta_time;
            }
        } else {
            // ACCELERATE or CONSTANT SPEED
            float speed_change = acceleration * delta_time;
            current_speed += direction * speed_change;
            
            if (godot::Math::abs(current_speed) > max_speed) {
                current_speed = direction * max_speed;
            }
            current_value = current_value + current_speed * delta_time;
        }
        
        // Final safety check to prevent jittering around target
        float new_distance = target_value - current_value;
        if (godot::Math::sign(distance) != godot::Math::sign(new_distance) && godot::Math::abs(current_speed) < acceleration * delta_time * 2.0f) {
             current_value = target_value;
             current_speed = 0;
        }
    }
};

StreamWeaverParameterRuntimeInstance* StreamWeaverParameterFollowInput::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    auto runtime_instance = memnew(StreamWeaverParameterFollowInputRuntimeInstance);
    if (input_parameter.is_valid()) {
        runtime_instance->input_parameter_runtime_instance = from_playback->get_parameter_runtime_instance(input_parameter);
        if (runtime_instance->input_parameter_runtime_instance) {
            runtime_instance->set_current_value(runtime_instance->input_parameter_runtime_instance->get_value());
        }
    }
    runtime_instance->acceleration = acceleration;
    runtime_instance->max_speed = max_speed;
    from_playback->add_ticking(runtime_instance);
    return runtime_instance;
}

void StreamWeaverParameterFollowInput::release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance)
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverParameterWindow ------------------------

void StreamWeaverParameterWindow::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_input_parameter"), &StreamWeaverParameterWindow::GetInputParameter);
    ClassDB::bind_method(D_METHOD("set_input_parameter", "input_parameter"), &StreamWeaverParameterWindow::SetInputParameter);
    ClassDB::bind_method(D_METHOD("get_min_value"), &StreamWeaverParameterWindow::GetMinValue);
    ClassDB::bind_method(D_METHOD("set_min_value", "min_value"), &StreamWeaverParameterWindow::SetMinValue);
    ClassDB::bind_method(D_METHOD("get_max_value"), &StreamWeaverParameterWindow::GetMaxValue);
    ClassDB::bind_method(D_METHOD("set_max_value", "max_value"), &StreamWeaverParameterWindow::SetMaxValue);
    ClassDB::bind_method(D_METHOD("get_value_interpolation_window"), &StreamWeaverParameterWindow::GetValueInterpolationWindow);
    ClassDB::bind_method(D_METHOD("set_value_interpolation_window", "value_interpolation_window"), &StreamWeaverParameterWindow::SetValueInterpolationWindow);
    ClassDB::bind_method(D_METHOD("get_interpolation_type"), &StreamWeaverParameterWindow::GetInterpolationType);
    ClassDB::bind_method(D_METHOD("set_interpolation_type", "interpolation_type"), &StreamWeaverParameterWindow::SetInterpolationType);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_input_parameter", "get_input_parameter");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min_value", "get_min_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max_value", "get_max_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value_interpolation_window"), "set_value_interpolation_window", "get_value_interpolation_window");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "interpolation_type", PROPERTY_HINT_ENUM, "Linear,Quadratic,SmoothStep,Cubic"), "set_interpolation_type", "get_interpolation_type");

    BIND_ENUM_CONSTANT(LINEAR);
    BIND_ENUM_CONSTANT(QUADRATIC);
    BIND_ENUM_CONSTANT(SMOOTHSTEP);
    BIND_ENUM_CONSTANT(CUBIC);
}

class StreamWeaverParameterWindowRuntimeInstance : public StreamWeaverParameterRuntimeInstance
{
public:
    StreamWeaverParameterRuntimeInstance* input_parameter_runtime_instance = nullptr;
    float min_value = 0.0;
    float max_value = 1.0;
    float value_interpolation_window = 0.1;
    StreamWeaverParameterWindow::InterpolationType interpolation_type = StreamWeaverParameterWindow::LINEAR;

    float interpolate(float t) const {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;

        switch (interpolation_type) {
            case StreamWeaverParameterWindow::LINEAR:
                return t;
            case StreamWeaverParameterWindow::QUADRATIC:
                return t * t;
            case StreamWeaverParameterWindow::SMOOTHSTEP:
                return t * t * (3.0f - 2.0f * t);
            case StreamWeaverParameterWindow::CUBIC:
                return t * t * t;
            default:
                return t;
        }
    }

    float get_value() override
    {
        if (!input_parameter_runtime_instance) return 0.0f;

        float input_val = input_parameter_runtime_instance->get_value();

        if (input_val < min_value || input_val > max_value) {
            return 0.0f;
        }

        float window = godot::Math::max(0.00001f, value_interpolation_window);

        if (input_val < min_value + window) {
            float t = (input_val - min_value) / window;
            return interpolate(t);
        }

        if (input_val > max_value - window) {
            float t = (max_value - input_val) / window;
            return interpolate(t);
        }

        return 1.0f;
    }
};

StreamWeaverParameterRuntimeInstance* StreamWeaverParameterWindow::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    auto runtime_instance = memnew(StreamWeaverParameterWindowRuntimeInstance);
    if (input_parameter.is_valid()) {
        runtime_instance->input_parameter_runtime_instance = from_playback->get_parameter_runtime_instance(input_parameter);
    }
    runtime_instance->min_value = min_value;
    runtime_instance->max_value = max_value;
    runtime_instance->value_interpolation_window = value_interpolation_window;
    runtime_instance->interpolation_type = interpolation_type;
    return runtime_instance;
}

void StreamWeaverParameterWindow::release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance)
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverTrigger -------------------------
void StreamWeaverTrigger::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_graph_node_position"), &StreamWeaverTrigger::GetGraphNodePosition);
    ClassDB::bind_method(D_METHOD("set_graph_node_position", "graph_node_position"), &StreamWeaverTrigger::SetGraphNodePosition);

    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "graph_node_position"), "set_graph_node_position", "get_graph_node_position");
}

// -------------------- StreamWeaverTriggerInput --------------------
void StreamWeaverTriggerInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_trigger_name"), &StreamWeaverTriggerInput::GetTriggerName);
    ClassDB::bind_method(D_METHOD("set_trigger_name", "trigger_name"), &StreamWeaverTriggerInput::SetTriggerName);

    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "trigger_name"), "set_trigger_name", "get_trigger_name");
}

StreamWeaverTriggerRuntimeInstance *StreamWeaverTriggerInput::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback)
{
    StreamWeaverTriggerRuntimeInstance *instance = memnew(StreamWeaverTriggerRuntimeInstance);
    return instance;
}

void StreamWeaverTriggerInput::release_runtime_instance(
    StreamWeaverTriggerRuntimeInstance *instance )
{
    if (instance) {
        memdelete(instance);
    }
}

// -------------------- StreamWeaverTriggerPlay --------------------
void StreamWeaverTriggerPlay::_bind_methods() {
}

StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerPlay::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    return memnew(StreamWeaverTriggerRuntimeInstance);
}

void StreamWeaverTriggerPlay::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
    if (instance) {
        memdelete(instance);
    }
}

// -------------------- StreamWeaverTriggerSub --------------------
StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerSub::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	return memnew(StreamWeaverTriggerRuntimeInstance);
}

void StreamWeaverTriggerSub::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverTriggerConditionalParameter --------------------
StreamWeaverTriggerConditionalParameter::StreamWeaverTriggerConditionalParameter() {
	false_trigger = memnew(StreamWeaverTriggerSub);
	false_trigger->set_name("False");
}

Ref<StreamWeaverTrigger> StreamWeaverTriggerConditionalParameter::GetFalseTrigger() const {
	if (false_trigger.is_null()) {
		const_cast<StreamWeaverTriggerConditionalParameter*>(this)->false_trigger = memnew(StreamWeaverTriggerSub);
		const_cast<StreamWeaverTriggerConditionalParameter*>(this)->false_trigger->set_name("False");
	}
	return false_trigger;
}

void StreamWeaverTriggerConditionalParameter::SetFalseTrigger(Ref<StreamWeaverTrigger> p_trigger) {
	false_trigger = p_trigger;
	if (false_trigger.is_null()) {
		false_trigger = memnew(StreamWeaverTriggerSub);
		false_trigger->set_name("False");
	}
}

void StreamWeaverTriggerConditionalParameter::update_name() {
	godot::String param_name = "<null>";
	if (parameter.is_valid()) {
		if (auto* input_param = cast_to<StreamWeaverParameterInput>(parameter.ptr())) {
			param_name = input_param->GetParameterName();
		} else {
			param_name = parameter->get_name();
			if (param_name.is_empty()) {
				param_name = parameter->get_class();
			}
		}
	}
	set_name(godot::vformat("%s %s %f",
		param_name,
		comparison_type == EQ ? "==" :
		comparison_type == LT ? "<" :
		comparison_type == GT ? ">" :
		comparison_type == LTE ? "<=" :
		comparison_type == GTE ? ">=" :
		"!=",
		value));
	if (false_trigger.is_valid()) {
		false_trigger->set_name(godot::vformat("%s (False)", get_name()));
	}
}

void StreamWeaverTriggerConditionalParameter::_bind_methods() {
	BIND_ENUM_CONSTANT(EQ);
	BIND_ENUM_CONSTANT(LT);
	BIND_ENUM_CONSTANT(GT);
	BIND_ENUM_CONSTANT(LTE);
	BIND_ENUM_CONSTANT(GTE);
	BIND_ENUM_CONSTANT(NEQ);

	ClassDB::bind_method(D_METHOD("get_parameter"), &StreamWeaverTriggerConditionalParameter::GetParameter);
	ClassDB::bind_method(D_METHOD("set_parameter", "parameter"), &StreamWeaverTriggerConditionalParameter::SetParameter);
	ClassDB::bind_method(D_METHOD("get_false_trigger"), &StreamWeaverTriggerConditionalParameter::GetFalseTrigger);
	ClassDB::bind_method(D_METHOD("set_false_trigger", "trigger"), &StreamWeaverTriggerConditionalParameter::SetFalseTrigger);
	ClassDB::bind_method(D_METHOD("get_comparison_type"), &StreamWeaverTriggerConditionalParameter::GetComparisonType);
	ClassDB::bind_method(D_METHOD("set_comparison_type", "comparison_type"), &StreamWeaverTriggerConditionalParameter::SetComparisonType);
	ClassDB::bind_method(D_METHOD("get_value"), &StreamWeaverTriggerConditionalParameter::GetValue);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &StreamWeaverTriggerConditionalParameter::SetValue);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_parameter", "get_parameter");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "false_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTrigger", PROPERTY_USAGE_DEFAULT), "set_false_trigger", "get_false_trigger");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "comparison_type", PROPERTY_HINT_ENUM, "EQ,LT,GT,LTE,GTE,NEQ"), "set_comparison_type", "get_comparison_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
}

class StreamWeaverTriggerRuntimeInstanceConditionalParameter : public StreamWeaverTriggerRuntimeInstance, public StreamWeaverTickInterface
{
public:
	StreamWeaverParameterRuntimeInstance* parameter_runtime_instance = nullptr;
	StreamWeaverTriggerRuntimeInstance* false_trigger_runtime_instance = nullptr;
	StreamWeaverTriggerConditionalParameter::Comparison comparison_type = StreamWeaverTriggerConditionalParameter::EQ;
	float value = 0.0f;
	bool last_condition = false;

	void reset() override {
		last_condition = false;
	}

	void tick(float delta) override {
		if (parameter_runtime_instance == nullptr) {
			return;
		}

		float parameter_value = parameter_runtime_instance->get_value();
		bool condition = false;
		switch (comparison_type) {
			case StreamWeaverTriggerConditionalParameter::EQ: condition = Math::is_equal_approx(parameter_value, value); break;
			case StreamWeaverTriggerConditionalParameter::NEQ: condition = !Math::is_equal_approx(parameter_value, value); break;
			case StreamWeaverTriggerConditionalParameter::LT: condition = parameter_value < value; break;
			case StreamWeaverTriggerConditionalParameter::GT: condition = parameter_value > value; break;
			case StreamWeaverTriggerConditionalParameter::LTE: condition = parameter_value <= value; break;
			case StreamWeaverTriggerConditionalParameter::GTE: condition = parameter_value >= value; break;
		}

		if (condition != last_condition) {
			last_condition = condition;
			if (condition) {
				trigger_all_triggerables();
			} else if (false_trigger_runtime_instance) {
				false_trigger_runtime_instance->trigger_all_triggerables();
			}
		}
	}
};

StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerConditionalParameter::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(StreamWeaverTriggerRuntimeInstanceConditionalParameter);
	if (parameter.is_valid()) {
		instance->parameter_runtime_instance = from_playback->get_parameter_runtime_instance(parameter);
	}
	if (false_trigger.is_null()) {
		false_trigger = memnew(StreamWeaverTriggerSub);
		false_trigger->set_name("False");
	}
	instance->false_trigger_runtime_instance = from_playback->get_trigger_runtime_instance(false_trigger);
	instance->comparison_type = comparison_type;
	instance->value = value;
	instance->last_condition = false;
	from_playback->add_ticking(instance);
	return instance;
}

void StreamWeaverTriggerConditionalParameter::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverTriggerFilterConditionalParameter --------------------
void StreamWeaverTriggerFilterConditionalParameter::_bind_methods() {
	BIND_ENUM_CONSTANT(EQ);
	BIND_ENUM_CONSTANT(LT);
	BIND_ENUM_CONSTANT(GT);
	BIND_ENUM_CONSTANT(LTE);
	BIND_ENUM_CONSTANT(GTE);
	BIND_ENUM_CONSTANT(NEQ);

	ClassDB::bind_method(D_METHOD("get_parameter"), &StreamWeaverTriggerFilterConditionalParameter::GetParameter);
	ClassDB::bind_method(D_METHOD("set_parameter", "parameter"), &StreamWeaverTriggerFilterConditionalParameter::SetParameter);
	ClassDB::bind_method(D_METHOD("get_input_trigger"), &StreamWeaverTriggerFilterConditionalParameter::GetInputTrigger);
	ClassDB::bind_method(D_METHOD("set_input_trigger", "input_trigger"), &StreamWeaverTriggerFilterConditionalParameter::SetInputTrigger);
	ClassDB::bind_method(D_METHOD("get_comparison_type"), &StreamWeaverTriggerFilterConditionalParameter::GetComparisonType);
	ClassDB::bind_method(D_METHOD("set_comparison_type", "comparison_type"), &StreamWeaverTriggerFilterConditionalParameter::SetComparisonType);
	ClassDB::bind_method(D_METHOD("get_value"), &StreamWeaverTriggerFilterConditionalParameter::GetValue);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &StreamWeaverTriggerFilterConditionalParameter::SetValue);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameterInput"), "set_parameter", "get_parameter");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTrigger"), "set_input_trigger", "get_input_trigger");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "comparison_type", PROPERTY_HINT_ENUM, "EQ,LT,GT,LTE,GTE,NEQ"), "set_comparison_type", "get_comparison_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
}

class StreamWeaverTriggerRuntimeInstanceFilterConditionalParameter : public StreamWeaverTriggerRuntimeInstance, public StreamWeaverRuntimeTriggerableInterface
{
public:
	StreamWeaverParameterRuntimeInstance* parameter_runtime_instance = nullptr;
	StreamWeaverTriggerFilterConditionalParameter::Comparison comparison_type = StreamWeaverTriggerFilterConditionalParameter::EQ;
	float value = 0.0f;

	void trigger() override {
		if (parameter_runtime_instance == nullptr) {
			return;
		}

		float parameter_value = parameter_runtime_instance->get_value();
		bool should_trigger = false;
		switch (comparison_type) {
			case StreamWeaverTriggerFilterConditionalParameter::EQ: should_trigger = Math::is_equal_approx(parameter_value, value); break;
			case StreamWeaverTriggerFilterConditionalParameter::NEQ: should_trigger = !Math::is_equal_approx(parameter_value, value); break;
			case StreamWeaverTriggerFilterConditionalParameter::LT: should_trigger = parameter_value < value; break;
			case StreamWeaverTriggerFilterConditionalParameter::GT: should_trigger = parameter_value > value; break;
			case StreamWeaverTriggerFilterConditionalParameter::LTE: should_trigger = parameter_value <= value; break;
			case StreamWeaverTriggerFilterConditionalParameter::GTE: should_trigger = parameter_value >= value; break;
		}

		if (should_trigger) {
			trigger_all_triggerables();
		}
	}
};

StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerFilterConditionalParameter::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(StreamWeaverTriggerRuntimeInstanceFilterConditionalParameter);
	if (parameter.is_valid()) {
		instance->parameter_runtime_instance = from_playback->get_parameter_runtime_instance(parameter);
	}
	if (input_trigger.is_valid()) {
		auto* input_trigger_runtime_instance = from_playback->get_trigger_runtime_instance(input_trigger);
		if (input_trigger_runtime_instance) {
			input_trigger_runtime_instance->add_triggerable(instance);
		}
	}
	instance->comparison_type = comparison_type;
	instance->value = value;
	return instance;
}

void StreamWeaverTriggerFilterConditionalParameter::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}
// -------------------- StreamWeaverTriggerConditionalParameterRange --------------------
void StreamWeaverTriggerConditionalParameterRange::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_parameter"), &StreamWeaverTriggerConditionalParameterRange::GetParameter);
	ClassDB::bind_method(D_METHOD("set_parameter", "parameter"), &StreamWeaverTriggerConditionalParameterRange::SetParameter);
    ClassDB::bind_method(D_METHOD("get_input_trigger"), &StreamWeaverTriggerConditionalParameterRange::GetInputTrigger);
    ClassDB::bind_method(D_METHOD("set_input_trigger", "input_trigger"), &StreamWeaverTriggerConditionalParameterRange::SetInputTrigger);
	ClassDB::bind_method(D_METHOD("get_min_value"), &StreamWeaverTriggerConditionalParameterRange::GetMinValue);
	ClassDB::bind_method(D_METHOD("set_min_value", "min_value"), &StreamWeaverTriggerConditionalParameterRange::SetMinValue);
	ClassDB::bind_method(D_METHOD("get_max_value"), &StreamWeaverTriggerConditionalParameterRange::GetMaxValue);
	ClassDB::bind_method(D_METHOD("set_max_value", "max_value"), &StreamWeaverTriggerConditionalParameterRange::SetMaxValue);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameterInput"), "set_parameter", "get_parameter");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTriggerInput"), "set_input_trigger", "get_input_trigger");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min_value", "get_min_value");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max_value", "get_max_value");
}

class StreamWeaverTriggerRuntimeInstanceConditionalParameterRange : public StreamWeaverTriggerRuntimeInstance, public StreamWeaverRuntimeTriggerableInterface {
public:
    StreamWeaverParameterRuntimeInstance* parameter_runtime_instance = nullptr;
    float min_value;
    float max_value;

    void trigger() override {
        if (parameter_runtime_instance == nullptr) {
            return;
        }

        float parameterValue = parameter_runtime_instance->get_value();
        if (parameterValue >= min_value && parameterValue <= max_value) {
            trigger_all_triggerables();
        }
    }
};

StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerConditionalParameterRange::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    auto runtime_instance = memnew(StreamWeaverTriggerRuntimeInstanceConditionalParameterRange);
    if (parameter.is_valid())
    {
        runtime_instance->parameter_runtime_instance = from_playback->get_parameter_runtime_instance(parameter);
    }
    if (input_trigger.is_valid())
    {
        auto input_trigger_runtime_instance = from_playback->get_trigger_runtime_instance(input_trigger);
        if (input_trigger_runtime_instance) {
            input_trigger_runtime_instance->add_triggerable(runtime_instance);
        }
    }
    runtime_instance->min_value = min_value;
    runtime_instance->max_value = max_value;
    return runtime_instance;
}

void StreamWeaverTriggerConditionalParameterRange::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance)
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverTriggerDelay --------------------
void StreamWeaverTriggerDelay::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_input_trigger"), &StreamWeaverTriggerDelay::GetInputTrigger);
    ClassDB::bind_method(D_METHOD("set_input_trigger", "input_trigger"), &StreamWeaverTriggerDelay::SetInputTrigger);
    ClassDB::bind_method(D_METHOD("get_delay_time"), &StreamWeaverTriggerDelay::GetDelayTime);
    ClassDB::bind_method(D_METHOD("set_delay_time", "delay_time"), &StreamWeaverTriggerDelay::SetDelayTime);
    ClassDB::bind_method(D_METHOD("get_multiplier_parameter"), &StreamWeaverTriggerDelay::GetMultiplierParameter);
    ClassDB::bind_method(D_METHOD("set_multiplier_parameter", "multiplier_parameter"), &StreamWeaverTriggerDelay::SetMultiplierParameter);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTrigger"), "set_input_trigger", "get_input_trigger");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "delay_time"), "set_delay_time", "get_delay_time");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiplier_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_multiplier_parameter", "get_multiplier_parameter");
}

class StreamWeaverTriggerRuntimeInstanceDelay : public StreamWeaverTriggerRuntimeInstance, public StreamWeaverRuntimeTriggerableInterface, public StreamWeaverTickInterface {
public:
    float delay_time = 0;
    StreamWeaverParameterRuntimeInstance* multiplier_parameter_runtime_instance = nullptr;
    godot::LocalVector<float> pending_triggers;

    void trigger() override {
        pending_triggers.push_back(delay_time);
    }

    void tick(float delta_time) override {
        if (multiplier_parameter_runtime_instance != nullptr) {
            delta_time *= multiplier_parameter_runtime_instance->get_value();
        }

        if (delta_time <= 0) {
            return;
        }

        for (int i = 0; i < pending_triggers.size(); i++) {
            pending_triggers[i] -= delta_time;
            if (pending_triggers[i] <= 0) {
                trigger_all_triggerables();
                pending_triggers.remove_at(i);
                i--;
            }
        }
    }
};

StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerDelay::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    StreamWeaverTriggerRuntimeInstanceDelay* instance = memnew(StreamWeaverTriggerRuntimeInstanceDelay);
    instance->delay_time = delay_time;
    if (input_trigger.is_valid()) {
        auto input_trigger_runtime_instance = from_playback->get_trigger_runtime_instance(input_trigger);
        if (input_trigger_runtime_instance) {
            input_trigger_runtime_instance->add_triggerable(instance);
        }
    }
    if (multiplier_parameter.is_valid()) {
        instance->multiplier_parameter_runtime_instance = from_playback->get_parameter_runtime_instance(multiplier_parameter);
    }
    from_playback->add_ticking(instance);
    return instance;
}

void StreamWeaverTriggerDelay::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverTriggerMetronome ----------
void StreamWeaverTriggerMetronome::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_base_trigger_rate"), &StreamWeaverTriggerMetronome::GetBaseTriggerRate);
    ClassDB::bind_method(D_METHOD("set_base_trigger_rate", "base_trigger_rate"), &StreamWeaverTriggerMetronome::SetBaseTriggerRate);
    ClassDB::bind_method(D_METHOD("get_multiplier_parameter"), &StreamWeaverTriggerMetronome::GetMultiplierParameter);
    ClassDB::bind_method(D_METHOD("set_multiplier_parameter", "multiplier_parameter"), &StreamWeaverTriggerMetronome::SetMultiplierParameter);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_trigger_rate"), "set_base_trigger_rate", "get_base_trigger_rate");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiplier_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_multiplier_parameter", "get_multiplier_parameter");
}

class StreamWeaverTriggerRuntimeInstanceMetronome : public StreamWeaverTriggerRuntimeInstance, public StreamWeaverTickInterface
{
public:
    ~StreamWeaverTriggerRuntimeInstanceMetronome() override = default;
    float base_trigger_rate;
    float remaining_to_next_tick = 0;
    StreamWeaverParameterRuntimeInstance* multiplier_parameter_runtime_instance = nullptr;

    void tick( float delta_time ) override
    {
        if (multiplier_parameter_runtime_instance != nullptr)
        {
            delta_time *= multiplier_parameter_runtime_instance->get_value();
        }
        if (delta_time <= 0)
            return;

        remaining_to_next_tick -= delta_time;
        while (remaining_to_next_tick <= 0)
        {
            trigger_all_triggerables();
            remaining_to_next_tick += base_trigger_rate;
        }
    }
};

StreamWeaverTriggerRuntimeInstance *StreamWeaverTriggerMetronome::create_runtime_instance(
    StreamWeaverAudioStreamPlayback *from_playback )
{
    auto runtime_instance = memnew(StreamWeaverTriggerRuntimeInstanceMetronome);
    if (multiplier_parameter.is_valid())
    {
        runtime_instance->multiplier_parameter_runtime_instance = from_playback->get_parameter_runtime_instance(multiplier_parameter);
    }
    runtime_instance->base_trigger_rate = base_trigger_rate;
    runtime_instance->remaining_to_next_tick = base_trigger_rate;

    from_playback->add_ticking( runtime_instance );
    return runtime_instance;
}

void StreamWeaverTriggerMetronome::release_runtime_instance(
    StreamWeaverTriggerRuntimeInstance *instance )
{
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverStream --------------------
void StreamWeaverStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_graph_node_position"), &StreamWeaverStream::GetGraphNodePosition);
	ClassDB::bind_method(D_METHOD("set_graph_node_position", "graph_node_position"), &StreamWeaverStream::SetGraphNodePosition);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "graph_node_position"), "set_graph_node_position", "get_graph_node_position");
}

// -------------------- StreamWeaverProducer --------------------
void StreamWeaverProducer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_triggers"), &StreamWeaverProducer::GetTriggers);
	ClassDB::bind_method(D_METHOD("set_triggers", "triggers"), &StreamWeaverProducer::SetTriggers);
	ClassDB::bind_method(D_METHOD("get_output_name"), &StreamWeaverProducer::GetOutputName);
	ClassDB::bind_method(D_METHOD("set_output_name", "output_name"), &StreamWeaverProducer::SetOutputName);
    ClassDB::bind_method(D_METHOD("get_base_volume_db"), &StreamWeaverProducer::GetBaseVolumeDb);
    ClassDB::bind_method(D_METHOD("set_base_volume_db", "base_volume_db"), &StreamWeaverProducer::SetBaseVolumeDb);
    ClassDB::bind_method(D_METHOD("get_base_pitch"), &StreamWeaverProducer::GetBasePitch);
    ClassDB::bind_method(D_METHOD("set_base_pitch", "base_pitch"), &StreamWeaverProducer::SetBasePitch);
    ClassDB::bind_method(D_METHOD("get_volume_multiplier"), &StreamWeaverProducer::GetVolumeMultiplier);
    ClassDB::bind_method(D_METHOD("set_volume_multiplier", "volume_multiplier"), &StreamWeaverProducer::SetVolumeMultiplier);
    ClassDB::bind_method(D_METHOD("get_pitch_multiplier"), &StreamWeaverProducer::GetPitchMultiplier);
    ClassDB::bind_method(D_METHOD("set_pitch_multiplier", "pitch_multiplier"), &StreamWeaverProducer::SetPitchMultiplier);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "output_name"), "set_output_name", "get_output_name");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "triggers", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverTrigger"), "set_triggers", "get_triggers");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_volume_db"), "set_base_volume_db", "get_base_volume_db");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_pitch"), "set_base_pitch", "get_base_pitch");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "volume_multiplier", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_volume_multiplier", "get_volume_multiplier");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "pitch_multiplier", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_pitch_multiplier", "get_pitch_multiplier");
}

void StreamWeaverProducer::release_runtime_instance(StreamWeaverStreamRuntimeInstance* instance) {
    memdelete(instance);
}

void StreamWeaverProducer::initialize_runtime_instance_base(StreamWeaverProducerRuntimeInstanceBase* runtime_instance, StreamWeaverAudioStreamPlayback* playback) {
    runtime_instance->playback_lock = playback->get_mix_lock();
    runtime_instance->base_volume_db = base_volume_db;
    runtime_instance->base_pitch = base_pitch;
    if (volume_multiplier.is_valid())
    {
        runtime_instance->volume_multiplier = playback->get_parameter_runtime_instance(volume_multiplier);
    }
    if (pitch_multiplier.is_valid())
    {
        runtime_instance->pitch_multiplier = playback->get_parameter_runtime_instance(pitch_multiplier);
    }
}

// -------------------- StreamWeaverStreamRandomize --------------------
void StreamWeaverStreamRandomize::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_randomize_pitch"), &StreamWeaverStreamRandomize::GetRandomizePitchOffset);
	ClassDB::bind_method(D_METHOD("set_randomize_pitch", "randomize_pitch"), &StreamWeaverStreamRandomize::SetRandomizePitchOffset);
	ClassDB::bind_method(D_METHOD("get_randomize_volume"), &StreamWeaverStreamRandomize::GetRandomizeVolumeOffset);
	ClassDB::bind_method(D_METHOD("set_randomize_volume", "randomize_volume"), &StreamWeaverStreamRandomize::SetRandomizeVolumeOffset);
	ClassDB::bind_method(D_METHOD("get_input_streams"), &StreamWeaverStreamRandomize::GetInputStreams);
	ClassDB::bind_method(D_METHOD("set_input_streams", "input_streams"), &StreamWeaverStreamRandomize::SetInputStreams);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "randomize_pitch"), "set_randomize_pitch", "get_randomize_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "randomize_volume"), "set_randomize_volume", "get_randomize_volume");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "input_streams", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverInputStream"), "set_input_streams", "get_input_streams");
}
class ParameterizedOutputRandomizeRuntimeInstance : public StreamWeaverProducerRuntimeInstanceBase {
public:
	float randomize_pitch = 1;
	float randomize_volume = 0;
	Ref<RandomNumberGenerator> randomizer;
	LocalVector<Ref<StreamWeaverInputStream>> inputs;
	int last_played_index = 0;

	struct RuntimeInputData {
		Ref<AudioStreamPlayback> playback;
		float pitch_offset;
		float volume_offset;
	};
	LocalVector<RuntimeInputData> currently_playing_inputs;
	void trigger() override {
		if (inputs.is_empty())
			return;
		int input_index = randomizer->randi_range(0, inputs.size()-1);
		// super simple way to not play the same sound twice in a row:
		if (input_index == last_played_index && inputs.size() > 1) {
			input_index = (input_index + 1) % inputs.size();
		}
		last_played_index = input_index;
		if (inputs[input_index].is_null() || inputs[input_index]->GetAudioStream().is_null()) {
			return;
		}
		auto playback = inputs[input_index]->GetAudioStream()->instantiate_playback();
		if (playback.is_null()) {
			return;
		}
		playback->start();
		RuntimeInputData runtime_input {};
		runtime_input.playback = playback;
		runtime_input.pitch_offset = randomizer->randf_range(-randomize_pitch, randomize_pitch);
		runtime_input.volume_offset = randomizer->randf_range(-randomize_volume, randomize_volume);
		currently_playing_inputs.push_back(runtime_input);
	}
	void reset() override {
		currently_playing_inputs.clear();
		trigger();
	}

	int32_t mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
		SpinLockGuard guard(*playback_lock);
		if (currently_playing_inputs.is_empty()) {
			return 0;
		}
		PROFILE_FUNCTION();
		size_t current_input_index = 0;
	    float pitch = get_pitch();
	    begin_volume_block(p_frames);
		int max_frames_mixed = 0;
		while (current_input_index < currently_playing_inputs.size()) {
			auto& cpi = currently_playing_inputs[current_input_index];
		    if (!cpi.playback.is_valid())
		    {
		        currently_playing_inputs.remove_at(current_input_index);
		        continue;
		    }
			auto mixed_input = cpi.playback->mix_audio(pitch + cpi.pitch_offset, p_frames);
		    int num_frames_mixed = Math::min( p_frames, static_cast<int32_t>( mixed_input.size() ) );
			if (num_frames_mixed > max_frames_mixed) {
				max_frames_mixed = num_frames_mixed;
			}
			for (int i = 0; i < num_frames_mixed; i++) {
				p_buffer[i].left += mixed_input[i].x * (volume_at(i) + cpi.volume_offset);
				p_buffer[i].right += mixed_input[i].y * (volume_at(i) + cpi.volume_offset);
			}

			if (num_frames_mixed < p_frames) {
				currently_playing_inputs.remove_at(current_input_index);
			}
			else {
				++current_input_index;
			}
		}
		end_volume_block(p_frames);
		return max_frames_mixed;
	}
};

StreamWeaverProducerRuntimeInstanceBase *StreamWeaverStreamRandomize::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	PROFILE_FUNCTION();
	auto* instance = memnew(ParameterizedOutputRandomizeRuntimeInstance);
    initialize_runtime_instance_base( instance, from_playback );
	instance->randomize_pitch = randomize_pitch_offset;
	instance->randomize_volume = randomize_volume_offset;
	for (Ref<StreamWeaverInputStream> input : input_streams) {
		if (input.is_valid() && input->GetAudioStream().is_valid()) {
			instance->inputs.push_back(input);
		}
	}
	static int random_seed = 54631;
	random_seed += 24462;
	instance->randomizer.instantiate();
	instance->randomizer->set_seed(random_seed);
	return instance;
}

// -------------------- StreamWeaverStreamLooping --------------------

void StreamWeaverStreamLooping::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_input_stream"), &StreamWeaverStreamLooping::GetInputStream);
	ClassDB::bind_method(D_METHOD("set_input_stream", "input_stream"), &StreamWeaverStreamLooping::SetInputStream);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_stream", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverInputStream"), "set_input_stream", "get_input_stream");
}

class ParameterizedOutputLoopingRuntimeInstance : public StreamWeaverProducerRuntimeInstanceBase {
public:
    Ref<AudioStreamPlayback> playback;

	void trigger() override {
	}

	void reset() override {
		if (playback.is_valid()) {
			playback->start();
		}
	}

	int32_t mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
	    if (!playback.is_valid())
	    {
	        return 0;
	    }
	    PROFILE_FUNCTION();

	    begin_volume_block(p_frames);
	    if (get_volume() <= 0.0001f && volume_at(0) <= 0.0001f)
	    {
	        end_volume_block(p_frames);
	        return 0;
	    }
	    float pitch = get_pitch();
	    auto mixed_input = playback->mix_audio(pitch, p_frames);
	    int num_frames_mixed = Math::min( p_frames, static_cast<int32_t>( mixed_input.size() ) );
	    for (int i = 0; i < num_frames_mixed; i++) {
	        p_buffer[i].left += mixed_input[i].x * volume_at(i);
	        p_buffer[i].right += mixed_input[i].y * volume_at(i);
	    }
	    end_volume_block(p_frames);
        return num_frames_mixed;
	}
};

StreamWeaverProducerRuntimeInstanceBase *StreamWeaverStreamLooping::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(ParameterizedOutputLoopingRuntimeInstance);
    initialize_runtime_instance_base( instance, from_playback );
    if (input_stream.is_valid() && input_stream->GetAudioStream().is_valid())
    {
        instance->playback = input_stream->GetAudioStream()->instantiate_playback();
        if (instance->playback.is_valid())
        {
            instance->playback->start();
        }
    }
	return instance;
}

// -------------------- StreamWeaverStreamGranularLinearSweep ------
void StreamWeaverStreamGranularLinearSweep::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_input_stream"), &StreamWeaverStreamGranularLinearSweep::GetInputStream);
    ClassDB::bind_method(D_METHOD("set_input_stream", "inputStream"), &StreamWeaverStreamGranularLinearSweep::SetInputStream);
    ClassDB::bind_method(D_METHOD("get_sweeping_parameter"), &StreamWeaverStreamGranularLinearSweep::GetSweepingParameter);
    ClassDB::bind_method(D_METHOD("set_sweeping_parameter", "sweepingParameter"), &StreamWeaverStreamGranularLinearSweep::SetSweepingParameter);
    ClassDB::bind_method(D_METHOD("get_min_parameter_value"), &StreamWeaverStreamGranularLinearSweep::GetMinParameterValue);
    ClassDB::bind_method(D_METHOD("set_min_parameter_value", "minParameterValue"), &StreamWeaverStreamGranularLinearSweep::SetMinParameterValue);
    ClassDB::bind_method(D_METHOD("get_max_parameter_value"), &StreamWeaverStreamGranularLinearSweep::GetMaxParameterValue);
    ClassDB::bind_method(D_METHOD("set_max_parameter_value", "maxParameterValue"), &StreamWeaverStreamGranularLinearSweep::SetMaxParameterValue);
    ClassDB::bind_method(D_METHOD("get_min_grain_size_milliseconds"), &StreamWeaverStreamGranularLinearSweep::GetMinGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("set_min_grain_size_milliseconds", "grainSizeMs"), &StreamWeaverStreamGranularLinearSweep::SetMinGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("get_max_grain_size_milliseconds"), &StreamWeaverStreamGranularLinearSweep::GetMaxGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("set_max_grain_size_milliseconds", "maxGrainSizeMs"), &StreamWeaverStreamGranularLinearSweep::SetMaxGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("get_grain_jitter_percentage"), &StreamWeaverStreamGranularLinearSweep::GetGrainJitterPercentage);
    ClassDB::bind_method(D_METHOD("set_grain_jitter_percentage", "grainJitterPercentage"), &StreamWeaverStreamGranularLinearSweep::SetGrainJitterPercentage);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_stream", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverInputStream"), "set_input_stream", "get_input_stream");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sweeping_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_sweeping_parameter", "get_sweeping_parameter");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_parameter_value"), "set_min_parameter_value", "get_min_parameter_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_parameter_value"), "set_max_parameter_value", "get_max_parameter_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_grain_size_milliseconds"), "set_min_grain_size_milliseconds", "get_min_grain_size_milliseconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_grain_size_milliseconds"), "set_max_grain_size_milliseconds", "get_max_grain_size_milliseconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grain_jitter_percentage"), "set_grain_jitter_percentage", "get_grain_jitter_percentage");
}

class StreamWeaverOutputGranularLinearSweepRuntime : public StreamWeaverProducerRuntimeInstanceBase
{
public:
    float min_parameter_value;
	float max_parameter_value;
	float min_grain_size_milliseconds = 20;
	float max_grain_size_milliseconds = 20;
	int mix_rate = 44100;
	float grain_jitter_percent = 0.01f;
	int grain_fade_window_samples = 50;
    StreamWeaverParameterRuntimeInstance* sweeping_parameter;
	Ref<RandomNumberGenerator> randomizer;
	PackedVector2Array grain_buffer;

	float total_num_samples;
	float min_grain_samples;
	float max_grain_samples;
	float total_num_grains_in_input;
	float linear_step_grains;
	float current_jitter_offset = 0.0f;
	static constexpr int NUM_NO_REPEAT_GRAINS = 8;
	int last_played_grains[NUM_NO_REPEAT_GRAINS];
	inline bool is_repeat(int grain_number) {
		for (int i = 0; i < NUM_NO_REPEAT_GRAINS; i++) { if (grain_number == last_played_grains[i]) return true; }
		return false;
	}
	void initialize_grain_calculation() {
		for (int i = 0; i < NUM_NO_REPEAT_GRAINS; i++) { last_played_grains[i] = 0; }
		total_num_samples = grain_buffer.size();
		min_grain_samples = Math::floor(min_grain_size_milliseconds * mix_rate / 1000.0f);
		max_grain_samples = Math::floor(max_grain_size_milliseconds * mix_rate / 1000.0f);
		total_num_grains_in_input = (2.0f * total_num_samples)
			/ (min_grain_samples + max_grain_samples);
		linear_step_grains = (max_grain_samples - min_grain_samples) / (total_num_grains_in_input - 1.0f);
	}
	int get_grain_number(float factor) {
		if (min_grain_samples == max_grain_samples) {
			return Math::floor(total_num_grains_in_input * factor);
		}
		float sample_index = factor * total_num_samples;
		float a = min_grain_samples - linear_step_grains / 2.0f;
		float grain_number = (-a + Math::sqrt(a*a + 2.0f * linear_step_grains * sample_index)) / linear_step_grains;
		return Math::floor(grain_number);
	}
	int get_grain_start_index(int grain_number) {
		float grain_float = static_cast<float>(grain_number);
		return Math::floor( (linear_step_grains / 2.0f) * grain_float * grain_float +
			(min_grain_samples - linear_step_grains / 2.0f) * grain_float );
	}
	int get_grain_size(int grain_number) {
		return min_grain_samples + static_cast<float>(grain_number) * linear_step_grains;
	}

	struct ActiveGrainData {
		int grain_number;
		int input_start;
		float current_frame_number;
		int number_of_samples;
		int elapsed_samples;
	};
	ActiveGrainData CurrentGrain;
	ActiveGrainData NextGrain;

	void trigger() override {
		// we just sweep forever, no triggering neccessary
	}

	int32_t mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
		PROFILE_FUNCTION();
		for (int frame_index = 0; frame_index < p_frames; frame_index++) {
			float current_volume = 1.0f;
			float next_volume = 0.0f;
			int remaining_in_current = CurrentGrain.number_of_samples - CurrentGrain.elapsed_samples;
			if (remaining_in_current == grain_fade_window_samples) {
				// start up the next grain!
				NextGrain = spawn_new_grain();
			}
			if (remaining_in_current <= grain_fade_window_samples) {
				float factor_to_next = 1.0f - Math::inverse_lerp(0.0f, grain_fade_window_samples, remaining_in_current);
				current_volume = Math::sqrt(1.0f - factor_to_next);
				next_volume = Math::sqrt(factor_to_next);
			}

			int current_buffer_index = CurrentGrain.input_start + CurrentGrain.elapsed_samples;
			p_buffer[frame_index].left += grain_buffer[current_buffer_index].x * current_volume;
			p_buffer[frame_index].right += grain_buffer[current_buffer_index].y * current_volume;
			CurrentGrain.elapsed_samples ++;
			if (next_volume > 0.0f) {
				int next_buffer_index = NextGrain.input_start + NextGrain.elapsed_samples;
				p_buffer[frame_index].left += grain_buffer[next_buffer_index].x * next_volume;
				p_buffer[frame_index].right += grain_buffer[next_buffer_index].y * next_volume;
				NextGrain.elapsed_samples ++;
			}
			if (CurrentGrain.elapsed_samples >= CurrentGrain.number_of_samples) {
				// the fade to the next has been completed, let's swap!
				CurrentGrain = NextGrain;
			}
		}

		return p_frames;
	}

	ActiveGrainData spawn_new_grain() {
		PROFILE_FUNCTION();
		float parameter_as_fraction = Math::inverse_lerp(min_parameter_value, max_parameter_value, sweeping_parameter->get_value());
		// we'll randomize the fraction a little, to get variance...
		current_jitter_offset += 0.1f * randomizer->randf_range(-grain_jitter_percent, grain_jitter_percent);
		current_jitter_offset = Math::clamp(current_jitter_offset, -grain_jitter_percent, grain_jitter_percent);
		parameter_as_fraction += current_jitter_offset;
		parameter_as_fraction = Math::clamp(parameter_as_fraction, 0.0f, 1.0f);

		int grain_number = get_grain_number(parameter_as_fraction);
		while (grain_number >= total_num_grains_in_input-1 || is_repeat(grain_number)) {
			if (grain_number >= total_num_grains_in_input-1) {
				grain_number -= 1;
			}
			else {
				grain_number += randomizer->randi_range(-2, 2);
				if (grain_number < 0) grain_number = 1;
			}
		}
		for (int i = 0; i < NUM_NO_REPEAT_GRAINS - 1; ++i) {
			last_played_grains[i] = last_played_grains[i + 1];
		}
		last_played_grains[NUM_NO_REPEAT_GRAINS - 1] = grain_number;
		int start_frame = get_grain_start_index(grain_number);
		int num_frames = get_grain_size(grain_number);

		ActiveGrainData d{};
		d.grain_number = grain_number;
		d.input_start = start_frame;
		d.current_frame_number = 0;
		d.number_of_samples = num_frames;
		d.elapsed_samples = 0;
		return d;
	}
};

StreamWeaverProducerRuntimeInstanceBase* StreamWeaverStreamGranularLinearSweep::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    PROFILE_FUNCTION();
    if (input_stream.is_null() || input_stream->GetAudioStream().is_null() || sweeping_parameter.is_null()) {
        return nullptr;
    }
    auto* instance = memnew(StreamWeaverOutputGranularLinearSweepRuntime);
    initialize_runtime_instance_base( instance, from_playback );
    instance->sweeping_parameter = from_playback->get_parameter_runtime_instance(sweeping_parameter);
    auto playback = input_stream->GetAudioStream()->instantiate_playback();
    if (playback.is_valid())
    {
        playback->start();
        int num_samples = input_stream->GetAudioStream()->get_length() * AudioServer::get_singleton()->get_mix_rate();
        if (num_samples > 0) {
            instance->grain_buffer = playback->mix_audio(1.0f, num_samples);
        }
    }
    if (instance->grain_buffer.is_empty()) {
        memdelete(instance);
        return nullptr;
    }
    instance->min_parameter_value = min_parameter_value;
    instance->max_parameter_value = max_parameter_value;
    instance->min_grain_size_milliseconds = min_grain_size_milliseconds;
    instance->max_grain_size_milliseconds = max_grain_size_milliseconds;
    instance->grain_jitter_percent = grain_jitter_percentage;
    static int random_seed = 74637;
    random_seed += 24462;
    instance->randomizer.instantiate();
    instance->randomizer->set_seed(random_seed);
    instance->initialize_grain_calculation();
    instance->CurrentGrain = instance->spawn_new_grain();

    return instance;
}

// -------------------- StreamWeaverStreamGranularDatabase --------------------

void StreamWeaverStreamGranularDatabase::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_grains_database"), &StreamWeaverStreamGranularDatabase::GetGrainsDatabase);
    ClassDB::bind_method(D_METHOD("set_grains_database", "database"), &StreamWeaverStreamGranularDatabase::SetGrainsDatabase);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "grains_database", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverGrainsDatabase"),
        "set_grains_database", "get_grains_database");

    ClassDB::bind_method(D_METHOD("get_axis_parameters"), &StreamWeaverStreamGranularDatabase::GetAxisParameters);
    ClassDB::bind_method(D_METHOD("set_axis_parameters", "params"), &StreamWeaverStreamGranularDatabase::SetAxisParameters);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "axis_parameters", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverParameter"),
        "set_axis_parameters", "get_axis_parameters");

    ClassDB::bind_method(D_METHOD("get_pitch_correction_enabled"), &StreamWeaverStreamGranularDatabase::GetPitchCorrectionEnabled);
    ClassDB::bind_method(D_METHOD("set_pitch_correction_enabled", "enabled"), &StreamWeaverStreamGranularDatabase::SetPitchCorrectionEnabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pitch_correction_enabled"),
        "set_pitch_correction_enabled", "get_pitch_correction_enabled");

    ClassDB::bind_method(D_METHOD("get_no_repeat_count"), &StreamWeaverStreamGranularDatabase::GetNoRepeatCount);
    ClassDB::bind_method(D_METHOD("set_no_repeat_count", "count"), &StreamWeaverStreamGranularDatabase::SetNoRepeatCount);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "no_repeat_count", PROPERTY_HINT_RANGE, "1,32,1"),
        "set_no_repeat_count", "get_no_repeat_count");

    ClassDB::bind_method(D_METHOD("get_candidate_pool_size"), &StreamWeaverStreamGranularDatabase::GetCandidatePoolSize);
    ClassDB::bind_method(D_METHOD("set_candidate_pool_size", "size"), &StreamWeaverStreamGranularDatabase::SetCandidatePoolSize);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "candidate_pool_size", PROPERTY_HINT_RANGE, "4,128,1"),
        "set_candidate_pool_size", "get_candidate_pool_size");

    ClassDB::bind_method(D_METHOD("get_search_param_smoothing"), &StreamWeaverStreamGranularDatabase::GetSearchParamSmoothing);
    ClassDB::bind_method(D_METHOD("set_search_param_smoothing", "smoothing"), &StreamWeaverStreamGranularDatabase::SetSearchParamSmoothing);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "search_param_smoothing", PROPERTY_HINT_RANGE, "0,1,0.01"),
        "set_search_param_smoothing", "get_search_param_smoothing");

    ClassDB::bind_method(D_METHOD("get_shortlist_score_window"), &StreamWeaverStreamGranularDatabase::GetShortlistScoreWindow);
    ClassDB::bind_method(D_METHOD("set_shortlist_score_window", "window"), &StreamWeaverStreamGranularDatabase::SetShortlistScoreWindow);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shortlist_score_window", PROPERTY_HINT_RANGE, "0,12,0.05"),
        "set_shortlist_score_window", "get_shortlist_score_window");

    ClassDB::bind_method(D_METHOD("get_continuity_bias"), &StreamWeaverStreamGranularDatabase::GetContinuityBias);
    ClassDB::bind_method(D_METHOD("set_continuity_bias", "bias"), &StreamWeaverStreamGranularDatabase::SetContinuityBias);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continuity_bias", PROPERTY_HINT_RANGE, "0,4,0.01"),
        "set_continuity_bias", "get_continuity_bias");

    ClassDB::bind_method(D_METHOD("get_random_selection_span"), &StreamWeaverStreamGranularDatabase::GetRandomSelectionSpan);
    ClassDB::bind_method(D_METHOD("set_random_selection_span", "span"), &StreamWeaverStreamGranularDatabase::SetRandomSelectionSpan);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "random_selection_span", PROPERTY_HINT_RANGE, "0,32,1"),
        "set_random_selection_span", "get_random_selection_span");

    ClassDB::bind_method(D_METHOD("get_random_walk_step"), &StreamWeaverStreamGranularDatabase::GetRandomWalkStep);
    ClassDB::bind_method(D_METHOD("set_random_walk_step", "step"), &StreamWeaverStreamGranularDatabase::SetRandomWalkStep);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_walk_step", PROPERTY_HINT_RANGE, "0,8,0.05"),
        "set_random_walk_step", "get_random_walk_step");

    ClassDB::bind_method(D_METHOD("get_random_walk_damping"), &StreamWeaverStreamGranularDatabase::GetRandomWalkDamping);
    ClassDB::bind_method(D_METHOD("set_random_walk_damping", "damping"), &StreamWeaverStreamGranularDatabase::SetRandomWalkDamping);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_walk_damping", PROPERTY_HINT_RANGE, "0,1,0.01"),
        "set_random_walk_damping", "get_random_walk_damping");
    ClassDB::bind_method(D_METHOD("get_preferred_match_bias"), &StreamWeaverStreamGranularDatabase::GetPreferredMatchBias);
    ClassDB::bind_method(D_METHOD("set_preferred_match_bias", "bias"), &StreamWeaverStreamGranularDatabase::SetPreferredMatchBias);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preferred_match_bias", PROPERTY_HINT_RANGE, "0,4,0.01"),
        "set_preferred_match_bias", "get_preferred_match_bias");
    ClassDB::bind_method(D_METHOD("get_phase_alignment_ms"), &StreamWeaverStreamGranularDatabase::GetPhaseAlignmentMs);
    ClassDB::bind_method(D_METHOD("set_phase_alignment_ms", "ms"), &StreamWeaverStreamGranularDatabase::SetPhaseAlignmentMs);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "phase_alignment_ms", PROPERTY_HINT_RANGE, "0,20,0.1"),
        "set_phase_alignment_ms", "get_phase_alignment_ms");
}

class StreamWeaverOutputGranularDatabaseRuntime : public StreamWeaverProducerRuntimeInstanceBase {
public:
    struct RankedCandidate {
        int idx = -1;
        float score = 0.0f;
    };

    StreamWeaverGrainsDatabase* db = nullptr;
    // Sized to db->get_num_axes(). Entries may be nullptr where the user
    // hasn't wired up that axis — we substitute the axis midpoint.
    LocalVector<StreamWeaverParameterRuntimeInstance*> axis_param_instances;
    bool pitch_correction_enabled = false;
    Ref<RandomNumberGenerator> rng;

    float synthesis_phase = 0.0f;
    float smoothed_target_f0 = 0.0f;
    int overlap_read_index = 0;
    std::vector<float> overlap_buffer;
    PackedFloat32Array smoothed_search_params;
    PackedFloat32Array search_params_scratch;
    int last_selected_unit = -1;
    int last_grain_output_center = -1;
    int last_grain_half_window = -1;
    float selection_jitter_state = 0.0f;
    static constexpr int MAX_NO_REPEAT = 32;
    int no_repeat_count = 8;
    int candidate_pool_size = 24;
    float search_param_smoothing = 0.08f;
    float shortlist_score_window = 1.5f;
    float continuity_bias = 0.5f;
    int random_selection_span = 3;
    float random_walk_step = 0.85f;
    float random_walk_damping = 0.6f;
    float preferred_match_bias = 0.75f;
    float phase_alignment_ms = 0.0f;
    std::vector<float> correlation_scratch;
    int last_played[MAX_NO_REPEAT] = {};

    bool is_repeat(int idx) const {
        int active_no_repeat = CLAMP(no_repeat_count, 1, MAX_NO_REPEAT);
        for (int i = 0; i < active_no_repeat; ++i) {
            if (last_played[i] == idx) return true;
        }
        return false;
    }

    void record_played(int idx) {
        int active_no_repeat = CLAMP(no_repeat_count, 1, MAX_NO_REPEAT);
        for (int i = 0; i < active_no_repeat - 1; ++i) {
            last_played[i] = last_played[i + 1];
        }
        last_played[active_no_repeat - 1] = idx;
    }

    const PackedFloat32Array& build_search_params() {
        int num_axes = db ? db->get_num_axes() : 0;
        if (search_params_scratch.size() != num_axes) {
            search_params_scratch.resize(num_axes);
        }
        for (int i = 0; i < num_axes; ++i) {
            StreamWeaverParameterRuntimeInstance* inst = (i < static_cast<int>(axis_param_instances.size()))
                ? axis_param_instances[i] : nullptr;
            if (inst) {
                search_params_scratch[i] = inst->get_value();
            } else {
                search_params_scratch[i] = 0.5f * (db->get_axis_min(i) + db->get_axis_max(i));
            }
        }
        return search_params_scratch;
    }

    const PackedFloat32Array& get_smoothed_search_params() {
        const PackedFloat32Array& raw = build_search_params();
        if (smoothed_search_params.size() != raw.size()) {
            smoothed_search_params = raw;
            return smoothed_search_params;
        }

        for (int i = 0; i < raw.size(); ++i) {
            smoothed_search_params[i] = Math::lerp(smoothed_search_params[i], raw[i], search_param_smoothing);
        }
        return smoothed_search_params;
    }

    int select_new_grain(const PackedFloat32Array& params, float target_f0_hint, int current_index) {
        PROFILE_FUNCTION();
        if (!db || db->get_grain_count() == 0) return -1;

        // Ask for more candidates than the no-repeat buffer so the
        // anti-repeat logic actually has options.
        int effective_pool_size = std::max(candidate_pool_size, no_repeat_count + 1);
        PackedInt32Array candidates = db->find_nearest_grains(params, effective_pool_size);

        std::vector<RankedCandidate> ranked_candidates;
        ranked_candidates.reserve(candidates.size());
        float last_time = -1.0f;
        if (last_selected_unit >= 0 && last_selected_unit < db->get_grain_count()) {
            last_time = db->get_grain_entry(last_selected_unit).original_time_s;
        }
        for (int i = 0; i < candidates.size(); ++i) {
            int idx = candidates[i];
            if (idx < 0 || idx >= db->get_grain_count()) continue;
            const auto& candidate = db->get_grain_entry(idx);
            float score = static_cast<float>(i);
            if (is_repeat(idx)) {
                score += 1000.0f;
            }
            if (candidate.low_energy) {
                score += 25.0f;
            }
            if (target_f0_hint > 0.0f && candidate.fundamental_hz > 0.0f) {
                score += 8.0f * std::abs(std::log2(candidate.fundamental_hz / target_f0_hint));
            }
            if (last_time >= 0.0f) {
                score += continuity_bias * std::abs(candidate.original_time_s - last_time);
            }
            if (i == current_index + 1)
            {
                score += 2000.0f;
            }
            if (rng.is_valid()) {
                score += rng->randf_range(-shortlist_score_window * 2.0f, shortlist_score_window * 2.0f);
            }
            ranked_candidates.push_back({idx, score});
        }

        if (ranked_candidates.empty()) {
            return -1;
        }

        std::sort(ranked_candidates.begin(), ranked_candidates.end(), [](const RankedCandidate& a, const RankedCandidate& b) {
            if (a.score == b.score) return a.idx < b.idx;
            return a.score < b.score;
        });

        float best_score = ranked_candidates[0].score;
        std::vector<RankedCandidate> shortlist;
        shortlist.reserve(ranked_candidates.size());
        for (const RankedCandidate& candidate : ranked_candidates) {
            if (candidate.score <= best_score + shortlist_score_window) {
                shortlist.push_back(candidate);
            }
        }
        if (shortlist.empty()) {
            shortlist.push_back(ranked_candidates[0]);
        }

        int eligible_count = static_cast<int>(shortlist.size());
        if (random_selection_span > 0) {
            eligible_count = std::min(eligible_count, random_selection_span + 1);
        }
        eligible_count = std::max(1, eligible_count);

        std::vector<RankedCandidate> eligible_candidates;
        eligible_candidates.reserve(eligible_count);
        for (int i = 0; i < eligible_count; ++i) {
            eligible_candidates.push_back(shortlist[i]);
        }

        float guard_window = std::max(0.0f, preferred_match_bias);
        if (guard_window > 0.0f) {
            std::vector<RankedCandidate> guarded_candidates;
            guarded_candidates.reserve(eligible_candidates.size());
            for (const RankedCandidate& candidate : eligible_candidates) {
                if (candidate.score <= best_score + guard_window) {
                    guarded_candidates.push_back(candidate);
                }
            }
            if (!guarded_candidates.empty()) {
                eligible_candidates.swap(guarded_candidates);
            }
        }

        int chosen = eligible_candidates[0].idx;
        if (eligible_candidates.size() > 1 && rng.is_valid()) {
            selection_jitter_state = selection_jitter_state * random_walk_damping
                + rng->randf_range(-random_walk_step, random_walk_step);

            float random_temperature = 0.35f + std::abs(selection_jitter_state);
            float total_weight = 0.0f;
            std::vector<float> weights;
            weights.reserve(eligible_candidates.size());
            for (const RankedCandidate& candidate : eligible_candidates) {
                float relative_score = std::max(0.0f, candidate.score - best_score);
                float weight = Math::exp(-relative_score / std::max(0.05f, random_temperature));
                weights.push_back(weight);
                total_weight += weight;
            }

            if (total_weight > 0.0f) {
                float pick = rng->randf() * total_weight;
                for (int i = 0; i < static_cast<int>(eligible_candidates.size()); ++i) {
                    pick -= weights[i];
                    if (pick <= 0.0f) {
                        chosen = eligible_candidates[i].idx;
                        break;
                    }
                }
            }
        } else {
            selection_jitter_state = 0.0f;
        }

        if (chosen < 0) return -1;

        record_played(chosen);
        last_selected_unit = chosen;
        return chosen;
    }

    float get_target_f0() {
        if (!db) return 0.0f;
        const PackedFloat32Array& params = build_search_params();
        if (pitch_correction_enabled) {
            float estimated = db->estimate_f0_for_params(params, std::max(8, no_repeat_count * 2));
            if (estimated > 0.0f) {
                return estimated;
            }
        }
        float sum_f0 = 0.0f;
        int count = 0;
        int active_no_repeat = CLAMP(no_repeat_count, 1, MAX_NO_REPEAT);
        for (int i = 0; i < active_no_repeat; ++i) {
            int idx = last_played[i];
            if (idx >= 0 && idx < db->get_grain_count()) {
                float f0 = db->get_grain_entry(idx).fundamental_hz;
                if (f0 > 0.0f) {
                    sum_f0 += f0;
                    ++count;
                }
            }
        }
        if (count > 0) return sum_f0 / static_cast<float>(count);
        if (db->get_grain_count() > 0) {
            return std::max(1.0f, db->get_grain_entry(0).fundamental_hz);
        }
        return 0.0f;
    }

    void ensure_overlap_capacity(int frames_needed) {
        if (frames_needed <= 0) return;
        if (static_cast<int>(overlap_buffer.size()) < frames_needed) {
            overlap_buffer.resize(frames_needed, 0.0f);
        }
    }

    void trigger_unit() {
        PROFILE_FUNCTION();
        if (!db) return;
        const PackedFloat32Array& params = get_smoothed_search_params();
        float target_f0_hint = get_target_f0();
        int unit_index = select_new_grain(params, target_f0_hint, last_selected_unit);
        if (unit_index < 0) return;

        const auto& entry = db->get_grain_entry(unit_index);
        if (entry.fundamental_hz <= 0.0f) return;

        float window_f0 = (smoothed_target_f0 > 0.0f) ? smoothed_target_f0 : entry.fundamental_hz;
        int period_samples = std::max(1, static_cast<int>(std::round(db->get_sample_rate() / window_f0)));
        int half_window = std::max(1, period_samples);

        // Asymmetric Hann window: ascending uses the previous grain's half-window
        // size so that the two grains' window values sum to exactly 1 at every
        // sample in the overlap region (COLA), even when f0 has changed and the
        // two grains have different sizes.
        int ascending_size = (last_grain_half_window > 0) ? last_grain_half_window : half_window;
        int descending_size = half_window;
        int total_window_size = ascending_size + descending_size;

        int nominal_input_start = entry.center_sample - ascending_size;
        int input_start = nominal_input_start;
        int pcm_size = db->pcm_sample_count();

        // Place the grain so its ascending half starts at the previous grain's
        // center (COLA ideal). Clamp to the current read cursor if that position
        // was already consumed, to avoid writing to zeroed slots.
        int output_start;
        if (last_grain_output_center < 0) {
            output_start = overlap_read_index;
        } else {
            output_start = std::max(last_grain_output_center, overlap_read_index);
        }

        int output_end = output_start + total_window_size;
        if (output_end <= 0) return;
        ensure_overlap_capacity(output_end);

        if (phase_alignment_ms > 0.0f && last_grain_output_center >= 0 && ascending_size > 0 &&
            output_start + ascending_size <= static_cast<int>(overlap_buffer.size())) {
            int max_shift = static_cast<int>(std::round(phase_alignment_ms * 0.001f * db->get_sample_rate()));
            max_shift = std::min(max_shift, ascending_size);
            int min_shift = std::max(-max_shift, -nominal_input_start);
            int clamped_max_shift = std::min(max_shift, pcm_size - total_window_size - nominal_input_start);

            if (min_shift <= clamped_max_shift) {
                const float* target = overlap_buffer.data() + output_start;
                float target_energy = 0.0f;
                for (int i = 0; i < ascending_size; ++i) {
                    target_energy += target[i] * target[i];
                }

                if (target_energy > 1e-7f) {
                    int cand_samples = (clamped_max_shift - min_shift) + ascending_size;
                    correlation_scratch.resize(cand_samples);
                    int cand_base = nominal_input_start + min_shift;
                    for (int i = 0; i < cand_samples; ++i) {
                        correlation_scratch[i] = db->decode_sample(cand_base + i);
                    }

                    float cand_energy = 0.0f;
                    for (int i = 0; i < ascending_size; ++i) {
                        cand_energy += correlation_scratch[i] * correlation_scratch[i];
                    }

                    int best_shift = 0;
                    float best_corr = -1e9f;
                    int num_shifts = clamped_max_shift - min_shift + 1;

                    for (int s = 0; s < num_shifts; ++s) {
                        if (s > 0) {
                            cand_energy += correlation_scratch[s + ascending_size - 1] * correlation_scratch[s + ascending_size - 1]
                                         - correlation_scratch[s - 1] * correlation_scratch[s - 1];
                            if (cand_energy < 0.0f) cand_energy = 0.0f;
                        }

                        const float* cand_ptr = correlation_scratch.data() + s;
                        float dot = 0.0f;
                        for (int i = 0; i < ascending_size; ++i) {
                            dot += target[i] * cand_ptr[i];
                        }

                        float norm = std::sqrt(cand_energy * target_energy);
                        float corr = (norm > 1e-7f) ? (dot / norm) : 0.0f;
                        if (corr > best_corr) {
                            best_corr = corr;
                            best_shift = min_shift + s;
                        }
                    }

                    if (best_corr > 0.0f) {
                        input_start = nominal_input_start + best_shift;
                    }
                }
            }
        }

        // The peak of the asymmetric window (ascending→descending boundary) is
        // the "center" tracked for the next grain's placement.
        int output_center = output_start + ascending_size;
        last_grain_output_center = output_center;
        last_grain_half_window = descending_size;


        for (int i = 0; i < total_window_size; ++i)
        {
            int src_index = input_start + i;
            int dst_index = output_start + i;
            if (dst_index < 0 || dst_index >= static_cast<int>(overlap_buffer.size())) continue;

            float sample = 0.0f;
            if (src_index >= 0 && src_index < pcm_size) {
                sample = db->decode_sample(src_index);
            }

            float window;
            if (i < ascending_size) {
                float phase = static_cast<float>(i) / static_cast<float>(ascending_size);
                window = 0.5f * (1.0f - std::cos(Math_PI * phase));
            } else {
                float phase = static_cast<float>(i - ascending_size) / static_cast<float>(descending_size);
                window = 0.5f * (1.0f + std::cos(Math_PI * phase));
            }
            overlap_buffer[dst_index] += sample * window;
        }
    }

    void trigger() override {
        // Continuous output, no triggering needed
    }

    void preseed_overlap_buffer() {
        if (!db || db->get_grain_count() == 0) return;
        const PackedFloat32Array& params = get_smoothed_search_params();
        float target_f0_hint = get_target_f0();
        int unit_index = select_new_grain(params, target_f0_hint, -1);
        if (unit_index < 0) return;

        const auto& entry = db->get_grain_entry(unit_index);
        if (entry.fundamental_hz <= 0.0f) return;

        float window_f0 = (smoothed_target_f0 > 0.0f) ? smoothed_target_f0 : entry.fundamental_hz;
        int period_samples = std::max(1, static_cast<int>(std::round(db->get_sample_rate() / window_f0)));
        int half_window = std::max(1, period_samples);
        int pcm_size = db->pcm_sample_count();

        ensure_overlap_capacity(overlap_read_index + half_window * 4);

        for (int i = 0; i < half_window; ++i) {
            int src_index = entry.center_sample + i;
            int dst_index = overlap_read_index + i;
            if (dst_index < 0 || dst_index >= static_cast<int>(overlap_buffer.size())) continue;
            float sample = 0.0f;
            if (src_index >= 0 && src_index < pcm_size) {
                sample = db->decode_sample(src_index);
            }
            float phase = static_cast<float>(i) / static_cast<float>(half_window);
            float window = 0.5f * (1.0f + std::cos(Math_PI * phase));
            overlap_buffer[dst_index] += sample * window;
        }

        last_grain_output_center = overlap_read_index;
        last_grain_half_window = half_window;

        trigger_unit();
    }

    void reset() override {
        overlap_read_index = 0;
        overlap_buffer.assign(4096, 0.0f);
        synthesis_phase = 0.0f;
        smoothed_target_f0 = 0.0f;
        last_grain_output_center = -1;
        last_grain_half_window = -1;
        last_selected_unit = -1;
        for (int i = 0; i < MAX_NO_REPEAT; ++i) {
            last_played[i] = -1;
        }
        preseed_overlap_buffer();
    }

    int32_t mix_output_into_buffer(AudioFrame* p_buffer, int32_t p_frames) override {
        PROFILE_FUNCTION();
        if (!db || db->get_grain_count() == 0) return 0;

        begin_volume_block(p_frames);

        const float sample_rate = db->get_sample_rate();
        const float inv_sample_rate = (sample_rate > 0.0f) ? 1.0f / sample_rate : 0.0f;

        float target_f0 = get_target_f0();
        if (target_f0 > 0.0f) {
            if (smoothed_target_f0 <= 0.0f) {
                smoothed_target_f0 = target_f0;
            } else {
                const float block_alpha = 1.0f - std::pow(1.0f - 0.0015f, static_cast<float>(p_frames));
                smoothed_target_f0 = Math::lerp(smoothed_target_f0, target_f0, block_alpha);
            }
        }

        const bool do_trigger = (inv_sample_rate > 0.0f && smoothed_target_f0 > 0.0f);
        const float phase_increment = do_trigger ? (smoothed_target_f0 * inv_sample_rate) : 0.0f;

        ensure_overlap_capacity(overlap_read_index + p_frames + 2048);

        for (int frame_index = 0; frame_index < p_frames; ++frame_index) {
            if (do_trigger) {
                synthesis_phase += phase_increment;
                while (synthesis_phase >= 1.0f) {
                    synthesis_phase -= 1.0f;
                    trigger_unit();
                }
            }

            // trigger_unit may have called ensure_overlap_capacity and
            // reallocated, so re-fetch the pointer each frame after a possible
            // trigger. In the common no-trigger case this is just a load.
            float* slot = overlap_buffer.data() + overlap_read_index;
            float sample = *slot;
            *slot = 0.0f;

            float out = sample * volume_at(frame_index);
            p_buffer[frame_index].left += out;
            p_buffer[frame_index].right += out;
            ++overlap_read_index;
        }
        end_volume_block(p_frames);

        // Compact the overlap buffer so it stays bounded. Without this, both
        // overlap_read_index and overlap_buffer grow for the lifetime of the
        // playback, since ensure_overlap_capacity only ever grows the vector.
        constexpr int compact_threshold = 8192;
        if (overlap_read_index > compact_threshold) {
            int read_pos = overlap_read_index;
            int buf_size = static_cast<int>(overlap_buffer.size());
            int remaining = buf_size - read_pos;
            if (remaining > 0) {
                std::copy(overlap_buffer.begin() + read_pos, overlap_buffer.end(), overlap_buffer.begin());
                overlap_buffer.resize(remaining);
            } else {
                overlap_buffer.clear();
            }
            overlap_read_index = 0;
            if (last_grain_output_center >= 0)
                last_grain_output_center -= read_pos;
        }
        return p_frames;
    }
};

StreamWeaverProducerRuntimeInstanceBase* StreamWeaverStreamGranularDatabase::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    PROFILE_FUNCTION();
    if (grains_database.is_null() || grains_database->get_grain_count() == 0) return nullptr;

    auto* instance = memnew(StreamWeaverOutputGranularDatabaseRuntime);
    initialize_runtime_instance_base(instance, from_playback);
    instance->db = grains_database.ptr();
    instance->pitch_correction_enabled = pitch_correction_enabled;
    instance->no_repeat_count = CLAMP(no_repeat_count, 1, StreamWeaverOutputGranularDatabaseRuntime::MAX_NO_REPEAT);
    instance->candidate_pool_size = std::max(candidate_pool_size, instance->no_repeat_count + 1);
    instance->search_param_smoothing = Math::clamp(search_param_smoothing, 0.0f, 1.0f);
    instance->shortlist_score_window = std::max(0.0f, shortlist_score_window);
    instance->continuity_bias = std::max(0.0f, continuity_bias);
    instance->random_selection_span = std::max(0, random_selection_span);
    instance->random_walk_step = std::max(0.0f, random_walk_step);
    instance->random_walk_damping = Math::clamp(random_walk_damping, 0.0f, 1.0f);
    instance->preferred_match_bias = std::max(0.0f, preferred_match_bias);
    instance->phase_alignment_ms = std::max(0.0f, phase_alignment_ms);

    int num_axes = grains_database->get_num_axes();
    instance->axis_param_instances.resize(num_axes);
    for (int i = 0; i < num_axes; ++i) {
        StreamWeaverParameterRuntimeInstance* runtime_inst = nullptr;
        if (i < axis_parameters.size()) {
            Ref<StreamWeaverParameter> param = axis_parameters[i];
            if (param.is_valid()) {
                runtime_inst = from_playback->get_parameter_runtime_instance(param);
            }
        }
        instance->axis_param_instances[i] = runtime_inst;
    }

    static int random_seed = 98321;
    random_seed += 17453;
    instance->rng.instantiate();
    instance->rng->set_seed(random_seed);

    instance->reset();

    return instance;
}

// -------------------- StreamWeaverStreamMixer2Way --------------------
void StreamWeaverStreamMixer2Way::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_input_stream_a"), &StreamWeaverStreamMixer2Way::GetInputStreamA);
	ClassDB::bind_method(D_METHOD("set_input_stream_a", "stream"), &StreamWeaverStreamMixer2Way::SetInputStreamA);
	ClassDB::bind_method(D_METHOD("get_input_stream_b"), &StreamWeaverStreamMixer2Way::GetInputStreamB);
	ClassDB::bind_method(D_METHOD("set_input_stream_b", "stream"), &StreamWeaverStreamMixer2Way::SetInputStreamB);
	ClassDB::bind_method(D_METHOD("get_mix_parameter"), &StreamWeaverStreamMixer2Way::GetMixParameter);
	ClassDB::bind_method(D_METHOD("set_mix_parameter", "param"), &StreamWeaverStreamMixer2Way::SetMixParameter);
	ClassDB::bind_method(D_METHOD("get_mix_value"), &StreamWeaverStreamMixer2Way::GetMixValue);
	ClassDB::bind_method(D_METHOD("set_mix_value", "value"), &StreamWeaverStreamMixer2Way::SetMixValue);
	ClassDB::bind_method(D_METHOD("get_blend_mode"), &StreamWeaverStreamMixer2Way::GetBlendMode);
	ClassDB::bind_method(D_METHOD("set_blend_mode", "mode"), &StreamWeaverStreamMixer2Way::SetBlendMode);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_stream_a", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverStream"), "set_input_stream_a", "get_input_stream_a");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_stream_b", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverStream"), "set_input_stream_b", "get_input_stream_b");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mix_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_mix_parameter", "get_mix_parameter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix_value", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_mix_value", "get_mix_value");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_mode", PROPERTY_HINT_ENUM, "Linear,Equal Power"), "set_blend_mode", "get_blend_mode");

	BIND_ENUM_CONSTANT(BLEND_LINEAR);
	BIND_ENUM_CONSTANT(BLEND_EQUAL_POWER);
}

class StreamWeaverStreamMixer2WayRuntimeInstance : public StreamWeaverStreamRuntimeInstance {
public:
	StreamWeaverStreamRuntimeInstance* runtime_a = nullptr;
	StreamWeaverStreamRuntimeInstance* runtime_b = nullptr;
	StreamWeaverParameterRuntimeInstance* mix_param = nullptr;
	float mix_value = 0.5f;
	StreamWeaverStreamMixer2Way::BlendMode blend_mode = StreamWeaverStreamMixer2Way::BLEND_EQUAL_POWER;

	float ramp_value = 0.5f;
	float ramp_step = 0.0f;
	bool ramp_initialized = false;

	void reset() override {
		if (runtime_a) runtime_a->reset();
		if (runtime_b) runtime_b->reset();
		ramp_initialized = false;
	}

	int32_t process_audio(AudioFrame *p_dst_buffer, int32_t p_frames, uint32_t p_generation) override {
		bool has_a = false, has_b = false;
		int32_t frames_a = 0, frames_b = 0;
		const AudioFrame *src_a = runtime_a ? runtime_a->pull_audio(p_frames, p_generation, has_a, &frames_a) : nullptr;
		const AudioFrame *src_b = runtime_b ? runtime_b->pull_audio(p_frames, p_generation, has_b, &frames_b) : nullptr;

		if (!has_a && !has_b) {
			memset(p_dst_buffer, 0, sizeof(AudioFrame) * p_frames);
			return 0;
		}

		float target_mix = mix_param ? Math::clamp(mix_param->get_value(), 0.0f, 1.0f) : Math::clamp(mix_value, 0.0f, 1.0f);
		if (!ramp_initialized) {
			ramp_value = target_mix;
			ramp_initialized = true;
		}
		ramp_step = (target_mix - ramp_value) / float(p_frames > 0 ? p_frames : 1);

		int32_t max_frames = Math::max(has_a ? frames_a : 0, has_b ? frames_b : 0);
		for (int32_t i = 0; i < p_frames; ++i) {
			float t = Math::clamp(ramp_value + ramp_step * float(i), 0.0f, 1.0f);
			float ga, gb;
			if (blend_mode == StreamWeaverStreamMixer2Way::BLEND_EQUAL_POWER) {
				ga = Math::cos(t * Math_PI * 0.5f);
				gb = Math::sin(t * Math_PI * 0.5f);
			} else {
				ga = 1.0f - t;
				gb = t;
			}
			float l = 0.0f, r = 0.0f;
			if (has_a && src_a && i < frames_a) {
				l += src_a[i].left * ga;
				r += src_a[i].right * ga;
			}
			if (has_b && src_b && i < frames_b) {
				l += src_b[i].left * gb;
				r += src_b[i].right * gb;
			}
			p_dst_buffer[i].left = l;
			p_dst_buffer[i].right = r;
		}
		ramp_value += ramp_step * float(p_frames);
		return max_frames;
	}
};

StreamWeaverStreamRuntimeInstance* StreamWeaverStreamMixer2Way::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(StreamWeaverStreamMixer2WayRuntimeInstance);
	if (input_stream_a.is_valid()) {
		instance->runtime_a = from_playback->get_stream_runtime_instance(input_stream_a);
	}
	if (input_stream_b.is_valid()) {
		instance->runtime_b = from_playback->get_stream_runtime_instance(input_stream_b);
	}
	if (mix_parameter.is_valid()) {
		instance->mix_param = from_playback->get_parameter_runtime_instance(mix_parameter);
	}
	instance->mix_value = mix_value;
	instance->blend_mode = blend_mode;
	return instance;
}

void StreamWeaverStreamMixer2Way::release_runtime_instance(StreamWeaverStreamRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}

// -------------------- StreamWeaverStream3Slice --------------------
void StreamWeaverStream3Slice::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_intro_stream"), &StreamWeaverStream3Slice::GetIntroStream);
	ClassDB::bind_method(D_METHOD("set_intro_stream", "stream"), &StreamWeaverStream3Slice::SetIntroStream);
	ClassDB::bind_method(D_METHOD("get_loop_stream"), &StreamWeaverStream3Slice::GetLoopStream);
	ClassDB::bind_method(D_METHOD("set_loop_stream", "stream"), &StreamWeaverStream3Slice::SetLoopStream);
	ClassDB::bind_method(D_METHOD("get_outro_stream"), &StreamWeaverStream3Slice::GetOutroStream);
	ClassDB::bind_method(D_METHOD("set_outro_stream", "stream"), &StreamWeaverStream3Slice::SetOutroStream);

	ClassDB::bind_method(D_METHOD("get_start_trigger"), &StreamWeaverStream3Slice::GetStartTrigger);
	ClassDB::bind_method(D_METHOD("set_start_trigger", "trigger"), &StreamWeaverStream3Slice::SetStartTrigger);
	ClassDB::bind_method(D_METHOD("get_stop_trigger"), &StreamWeaverStream3Slice::GetStopTrigger);
	ClassDB::bind_method(D_METHOD("set_stop_trigger", "trigger"), &StreamWeaverStream3Slice::SetStopTrigger);

	ClassDB::bind_method(D_METHOD("get_transition_mode"), &StreamWeaverStream3Slice::GetTransitionMode);
	ClassDB::bind_method(D_METHOD("set_transition_mode", "mode"), &StreamWeaverStream3Slice::SetTransitionMode);
	ClassDB::bind_method(D_METHOD("get_stop_mode"), &StreamWeaverStream3Slice::GetStopMode);
	ClassDB::bind_method(D_METHOD("set_stop_mode", "mode"), &StreamWeaverStream3Slice::SetStopMode);

	ClassDB::bind_method(D_METHOD("get_crossfade_time"), &StreamWeaverStream3Slice::GetCrossfadeTime);
	ClassDB::bind_method(D_METHOD("set_crossfade_time", "time"), &StreamWeaverStream3Slice::SetCrossfadeTime);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "intro_stream", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverStream"), "set_intro_stream", "get_intro_stream");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "loop_stream", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverStream"), "set_loop_stream", "get_loop_stream");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "outro_stream", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverStream"), "set_outro_stream", "get_outro_stream");

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "start_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTrigger"), "set_start_trigger", "get_start_trigger");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stop_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTrigger"), "set_stop_trigger", "get_stop_trigger");

	ADD_PROPERTY(PropertyInfo(Variant::INT, "transition_mode", PROPERTY_HINT_ENUM, "Sample Perfect,Crossfade"), "set_transition_mode", "get_transition_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stop_mode", PROPERTY_HINT_ENUM, "Immediate,Finish Intro"), "set_stop_mode", "get_stop_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crossfade_time"), "set_crossfade_time", "get_crossfade_time");

	BIND_ENUM_CONSTANT(TRANSITION_SAMPLE_PERFECT);
	BIND_ENUM_CONSTANT(TRANSITION_CROSSFADE);
	BIND_ENUM_CONSTANT(STOP_IMMEDIATE);
	BIND_ENUM_CONSTANT(STOP_FINISH_INTRO);
}

class StreamWeaverStream3SliceRuntimeInstance : public StreamWeaverStreamRuntimeInstance {
public:
	enum State {
		STATE_IDLE = 0,
		STATE_INTRO,
		STATE_LOOP,
		STATE_CROSSFADE,
		STATE_OUTRO,
	};

	StreamWeaverStreamRuntimeInstance* runtime_intro = nullptr;
	StreamWeaverStreamRuntimeInstance* runtime_loop = nullptr;
	StreamWeaverStreamRuntimeInstance* runtime_outro = nullptr;

	StreamWeaverStream3Slice::TransitionMode transition_mode = StreamWeaverStream3Slice::TRANSITION_SAMPLE_PERFECT;
	StreamWeaverStream3Slice::StopMode stop_mode = StreamWeaverStream3Slice::STOP_IMMEDIATE;
	float crossfade_time = 0.05f;

	State state = STATE_IDLE;
	bool stop_pending_flag = false;

	StreamWeaverStreamRuntimeInstance* crossfade_from = nullptr;
	StreamWeaverStreamRuntimeInstance* crossfade_to = nullptr;
	State crossfade_target_state = STATE_IDLE;
	int32_t crossfade_pos = 0;
	int32_t crossfade_total_frames = 1;

	AudioFrame last_written_frame{ 0.0f, 0.0f };
	bool has_written_frame = false;
	AudioFrame declick_offset{ 0.0f, 0.0f };
	int32_t declick_remaining = 0;
	bool declick_pending = false;
	static constexpr int32_t DECLICK_FRAMES = 64;

	void apply_declick_to_out(AudioFrame* out, int32_t count) {
		if (count <= 0) return;
		if (declick_pending) {
			declick_offset.left = out[0].left - last_written_frame.left;
			declick_offset.right = out[0].right - last_written_frame.right;
			declick_remaining = DECLICK_FRAMES;
			declick_pending = false;
		}
		if (declick_remaining > 0) {
			int32_t n = Math::min(count, declick_remaining);
			for (int32_t i = 0; i < n; ++i) {
				float factor = float(declick_remaining - i) / float(DECLICK_FRAMES);
				out[i].left -= declick_offset.left * factor;
				out[i].right -= declick_offset.right * factor;
			}
			declick_remaining -= n;
		}
		last_written_frame = out[count - 1];
		has_written_frame = true;
	}

	std::atomic<bool> start_triggered{ false };
	std::atomic<bool> stop_triggered{ false };
	uint32_t internal_sub_generation = 0;

	inline uint32_t next_generation(uint32_t p_generation) {
		return (p_generation << 8) | (++internal_sub_generation & 0xFF);
	}
	struct TriggerReceiver : public StreamWeaverRuntimeTriggerableInterface {
		StreamWeaverStream3SliceRuntimeInstance* parent = nullptr;
		bool is_stop = false;
		void trigger() override {
			if (is_stop) {
				parent->stop_triggered.store(true, std::memory_order_release);
			} else {
				parent->start_triggered.store(true, std::memory_order_release);
			}
		}
	};
	TriggerReceiver start_receiver;
	TriggerReceiver stop_receiver;

	void trigger() override {
		start_triggered.store(true, std::memory_order_release);
	}

	void reset() override {
		state = STATE_IDLE;
		stop_pending_flag = false;
		crossfade_pos = 0;
		crossfade_from = nullptr;
		crossfade_to = nullptr;
		has_written_frame = false;
		declick_remaining = 0;
		declick_pending = false;
		start_triggered.store(false, std::memory_order_relaxed);
		stop_triggered.store(false, std::memory_order_relaxed);
	}

	StreamWeaverStreamRuntimeInstance* active_runtime() const {
		switch (state) {
			case STATE_INTRO: return runtime_intro;
			case STATE_LOOP: return runtime_loop;
			case STATE_OUTRO: return runtime_outro;
			case STATE_CROSSFADE: return crossfade_to ? crossfade_to : crossfade_from;
			default: return nullptr;
		}
	}

	void start_crossfade(StreamWeaverStreamRuntimeInstance* from, StreamWeaverStreamRuntimeInstance* to, State target_state, float override_fade_time = -1.0f) {
		if (!from && !to) {
			state = target_state;
			crossfade_from = nullptr;
			crossfade_to = nullptr;
			return;
		}
		float rate = 44100.0f;
		if (AudioServer::get_singleton()) {
			rate = AudioServer::get_singleton()->get_mix_rate();
		}
		float dur = override_fade_time >= 0.0f ? override_fade_time : crossfade_time;
		crossfade_total_frames = Math::max(1, static_cast<int32_t>(dur * rate));
		crossfade_pos = 0;
		crossfade_from = from;
		crossfade_to = to;
		crossfade_target_state = target_state;
		state = STATE_CROSSFADE;
	}

	void begin_stop_transition() {
		StreamWeaverStreamRuntimeInstance* active = active_runtime();
		if (runtime_outro) {
			runtime_outro->reset();
			start_crossfade(active, runtime_outro, STATE_OUTRO);
		} else {
			start_crossfade(active, nullptr, STATE_IDLE);
		}
	}

	void on_intro_finished() {
		if (stop_pending_flag) {
			stop_pending_flag = false;
			if (runtime_outro) {
				runtime_outro->reset();
				state = STATE_OUTRO;
				if (has_written_frame) declick_pending = true;
			} else {
				state = STATE_IDLE;
			}
		} else if (runtime_loop) {
			runtime_loop->reset();
			state = STATE_LOOP;
			if (has_written_frame) declick_pending = true;
		} else {
			state = STATE_IDLE;
		}
	}

	void handle_triggers() {
		if (start_triggered.exchange(false, std::memory_order_acq_rel)) {
			if (state == STATE_IDLE) {
				stop_pending_flag = false;
				if (runtime_intro) {
					runtime_intro->reset();
					state = STATE_INTRO;
				} else if (runtime_loop) {
					runtime_loop->reset();
					state = STATE_LOOP;
				} else {
					state = STATE_IDLE;
				}
			} else if (state == STATE_INTRO) {
				if (stop_pending_flag) {
					stop_pending_flag = false;
				}
			} else if (state == STATE_LOOP) {
			} else if (state == STATE_CROSSFADE) {
				if (crossfade_target_state == STATE_LOOP || crossfade_target_state == STATE_INTRO) {
				} else {
					StreamWeaverStreamRuntimeInstance* target = runtime_intro ? runtime_intro : runtime_loop;
					State target_state = runtime_intro ? STATE_INTRO : STATE_LOOP;
					if (target) {
						target->reset();
						start_crossfade(active_runtime(), target, target_state, 0.02f);
					}
				}
			} else if (state == STATE_OUTRO) {
				StreamWeaverStreamRuntimeInstance* target = runtime_intro ? runtime_intro : runtime_loop;
				State target_state = runtime_intro ? STATE_INTRO : STATE_LOOP;
				if (target) {
					target->reset();
					start_crossfade(runtime_outro, target, target_state, 0.02f);
				}
			}
		}

		if (stop_triggered.exchange(false, std::memory_order_acq_rel)) {
			if (state == STATE_INTRO) {
				if (stop_mode == StreamWeaverStream3Slice::STOP_FINISH_INTRO) {
					stop_pending_flag = true;
				} else {
					begin_stop_transition();
				}
			} else if (state == STATE_LOOP || (state == STATE_CROSSFADE && (crossfade_target_state == STATE_LOOP || crossfade_target_state == STATE_INTRO))) {
				begin_stop_transition();
			}
		}
	}

	int32_t process_audio(AudioFrame *p_dst_buffer, int32_t p_frames, uint32_t p_generation) override {
		handle_triggers();

		if (state == STATE_IDLE) {
			memset(p_dst_buffer, 0, sizeof(AudioFrame) * p_frames);
			return 0;
		}

		int32_t frames_written = 0;
		while (frames_written < p_frames && state != STATE_IDLE) {
			int32_t chunk = p_frames - frames_written;
			AudioFrame *out = p_dst_buffer + frames_written;

			if (state == STATE_INTRO) {
				if (!runtime_intro) {
					on_intro_finished();
					continue;
				}
				if (transition_mode == StreamWeaverStream3Slice::TRANSITION_CROSSFADE) {
					float rem_s = runtime_intro->get_remaining_playback_time_s();
					if (rem_s >= 0.0f && rem_s <= crossfade_time) {
						if (stop_pending_flag) {
							stop_pending_flag = false;
							if (runtime_outro) {
								runtime_outro->reset();
								start_crossfade(runtime_intro, runtime_outro, STATE_OUTRO);
							} else {
								start_crossfade(runtime_intro, nullptr, STATE_IDLE);
							}
						} else if (runtime_loop) {
							runtime_loop->reset();
							start_crossfade(runtime_intro, runtime_loop, STATE_LOOP);
						} else {
							start_crossfade(runtime_intro, nullptr, STATE_IDLE);
						}
						continue;
					}
				}
				bool has_audio = false;
				int32_t produced = 0;
				const AudioFrame *src = runtime_intro->pull_audio(chunk, next_generation(p_generation), has_audio, &produced);
				int32_t to_write = Math::min(chunk, produced);
				if (has_audio && src && to_write > 0) {
					for (int32_t i = 0; i < to_write; ++i) {
						out[i] = src[i];
					}
					apply_declick_to_out(out, to_write);
					frames_written += to_write;
				}
				if (produced < chunk) {
					on_intro_finished();
				}
			} else if (state == STATE_LOOP) {
				if (!runtime_loop) {
					state = STATE_IDLE;
					break;
				}
				bool has_audio = false;
				int32_t produced = 0;
				const AudioFrame *src = runtime_loop->pull_audio(chunk, next_generation(p_generation), has_audio, &produced);
				int32_t to_write = Math::min(chunk, produced);
				if (has_audio && src && to_write > 0) {
					for (int32_t i = 0; i < to_write; ++i) {
						out[i] = src[i];
					}
					apply_declick_to_out(out, to_write);
					frames_written += to_write;
				}
				if (produced < chunk) {
					state = STATE_IDLE;
				}
			} else if (state == STATE_OUTRO) {
				if (!runtime_outro) {
					state = STATE_IDLE;
					break;
				}
				bool has_audio = false;
				int32_t produced = 0;
				const AudioFrame *src = runtime_outro->pull_audio(chunk, next_generation(p_generation), has_audio, &produced);
				int32_t to_write = Math::min(chunk, produced);
				if (has_audio && src && to_write > 0) {
					for (int32_t i = 0; i < to_write; ++i) {
						out[i] = src[i];
					}
					apply_declick_to_out(out, to_write);
					frames_written += to_write;
				}
				if (produced < chunk) {
					state = STATE_IDLE;
				}
			} else if (state == STATE_CROSSFADE) {
				int32_t remaining_fade = crossfade_total_frames - crossfade_pos;
				int32_t fade_chunk = Math::min(chunk, remaining_fade);

				bool has_from = false, has_to = false;
				int32_t produced_from = 0, produced_to = 0;
				uint32_t gen_from = next_generation(p_generation);
				uint32_t gen_to = next_generation(p_generation);
				const AudioFrame *src_from = crossfade_from ? crossfade_from->pull_audio(fade_chunk, gen_from, has_from, &produced_from) : nullptr;
				const AudioFrame *src_to = crossfade_to ? crossfade_to->pull_audio(fade_chunk, gen_to, has_to, &produced_to) : nullptr;

				for (int32_t i = 0; i < fade_chunk; ++i) {
					float t = Math::clamp(float(crossfade_pos + i) / float(crossfade_total_frames), 0.0f, 1.0f);
					float g_from = Math::cos(t * Math_PI * 0.5f);
					float g_to = Math::sin(t * Math_PI * 0.5f);

					float l = 0.0f, r = 0.0f;
					if (has_from && src_from && i < produced_from) {
						l += src_from[i].left * g_from;
						r += src_from[i].right * g_from;
					}
					if (has_to && src_to && i < produced_to) {
						l += src_to[i].left * g_to;
						r += src_to[i].right * g_to;
					}
					out[i].left = l;
					out[i].right = r;
				}
				apply_declick_to_out(out, fade_chunk);
				crossfade_pos += fade_chunk;
				frames_written += fade_chunk;

				if (crossfade_pos >= crossfade_total_frames) {
					state = crossfade_target_state;
					crossfade_from = nullptr;
					crossfade_to = nullptr;
				}
			}
		}

		if (frames_written < p_frames) {
			memset(p_dst_buffer + frames_written, 0, sizeof(AudioFrame) * (p_frames - frames_written));
		}
		return frames_written;
	}
};

StreamWeaverStreamRuntimeInstance* StreamWeaverStream3Slice::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(StreamWeaverStream3SliceRuntimeInstance);
	instance->start_receiver.parent = instance;
	instance->start_receiver.is_stop = false;
	instance->stop_receiver.parent = instance;
	instance->stop_receiver.is_stop = true;

	if (intro_stream.is_valid()) {
		instance->runtime_intro = from_playback->get_stream_runtime_instance(intro_stream);
	}
	if (loop_stream.is_valid()) {
		instance->runtime_loop = from_playback->get_stream_runtime_instance(loop_stream);
	}
	if (outro_stream.is_valid()) {
		instance->runtime_outro = from_playback->get_stream_runtime_instance(outro_stream);
	}

	if (start_trigger.is_valid()) {
		auto* trig = from_playback->get_trigger_runtime_instance(start_trigger);
		if (trig) {
			trig->add_triggerable(&instance->start_receiver);
		}
	}
	if (stop_trigger.is_valid()) {
		auto* trig = from_playback->get_trigger_runtime_instance(stop_trigger);
		if (trig) {
			trig->add_triggerable(&instance->stop_receiver);
		}
	}

	instance->transition_mode = transition_mode;
	instance->stop_mode = stop_mode;
	instance->crossfade_time = crossfade_time;
	return instance;
}

void StreamWeaverStream3Slice::release_runtime_instance(StreamWeaverStreamRuntimeInstance* instance) {
	if (instance) {
		memdelete(instance);
	}
}

StreamWeaverAudioStream::StreamWeaverAudioStream() = default;

void StreamWeaverAudioStream::ensure_play_trigger() {
    if (play_trigger.is_null()) {
        play_trigger.instantiate();
        play_trigger->set_name("Play");
        play_trigger->SetGraphNodePosition(godot::Vector2(40, 300));
    }
}

Ref<StreamWeaverTriggerPlay> StreamWeaverAudioStream::GetPlayTrigger() const {
    return play_trigger;
}

void StreamWeaverAudioStream::SetPlayTrigger(Ref<StreamWeaverTriggerPlay> p_trigger) {
    play_trigger = p_trigger;
}

// -------------------- StreamWeaverAudioStream --------------------
void StreamWeaverAudioStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_parameters"), &StreamWeaverAudioStream::GetParameters);
	ClassDB::bind_method(D_METHOD("set_parameters", "parameters"), &StreamWeaverAudioStream::SetParameters);
	ClassDB::bind_method(D_METHOD("get_triggers"), &StreamWeaverAudioStream::GetTriggers);
	ClassDB::bind_method(D_METHOD("set_triggers", "triggers"), &StreamWeaverAudioStream::SetTriggers);
	ClassDB::bind_method(D_METHOD("get_streams"), &StreamWeaverAudioStream::GetStreams);
	ClassDB::bind_method(D_METHOD("set_streams", "streams"), &StreamWeaverAudioStream::SetStreams);
	ClassDB::bind_method(D_METHOD("get_master_streams"), &StreamWeaverAudioStream::GetMasterStreams);
	ClassDB::bind_method(D_METHOD("set_master_streams", "master_streams"), &StreamWeaverAudioStream::SetMasterStreams);

	ClassDB::bind_method(D_METHOD("get_master_output_position"), &StreamWeaverAudioStream::GetMasterOutputPosition);
	ClassDB::bind_method(D_METHOD("set_master_output_position", "position"), &StreamWeaverAudioStream::SetMasterOutputPosition);
	ClassDB::bind_method(D_METHOD("get_master_volume_db"), &StreamWeaverAudioStream::GetMasterVolumeDb);
	ClassDB::bind_method(D_METHOD("set_master_volume_db", "volume_db"), &StreamWeaverAudioStream::SetMasterVolumeDb);
	ClassDB::bind_method(D_METHOD("get_master_volume_multiplier"), &StreamWeaverAudioStream::GetMasterVolumeMultiplier);
	ClassDB::bind_method(D_METHOD("set_master_volume_multiplier", "param"), &StreamWeaverAudioStream::SetMasterVolumeMultiplier);
	ClassDB::bind_method(D_METHOD("ensure_play_trigger"), &StreamWeaverAudioStream::ensure_play_trigger);
	ClassDB::bind_method(D_METHOD("get_play_trigger"), &StreamWeaverAudioStream::GetPlayTrigger);
	ClassDB::bind_method(D_METHOD("set_play_trigger", "trigger"), &StreamWeaverAudioStream::SetPlayTrigger);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "parameters", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverParameter"), "set_parameters", "get_parameters");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "triggers", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverTrigger"), "set_triggers", "get_triggers");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "streams", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverStream"), "set_streams", "get_streams");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "master_streams", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverStream"), "set_master_streams", "get_master_streams");

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "master_output_position"), "set_master_output_position", "get_master_output_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "master_volume_db"), "set_master_volume_db", "get_master_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "master_volume_multiplier", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_master_volume_multiplier", "get_master_volume_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "play_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTriggerPlay"), "set_play_trigger", "get_play_trigger");
}

Ref<AudioStreamPlayback> StreamWeaverAudioStream::_instantiate_playback() const {
	Ref<StreamWeaverAudioStreamPlayback> playback;
	playback.instantiate();
	playback->initialize({this});
	return playback;
}

String StreamWeaverAudioStream::_get_stream_name() const {
	return "StreamWeaverAudioStream";
}

// -------------------- StreamWeaverAudioStreamPlayback --------------------
void StreamWeaverAudioStreamPlayback::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parameter", "parameter_name", "parameter_value"), &StreamWeaverAudioStreamPlayback::set_parameter);
	ClassDB::bind_method(D_METHOD("trigger", "trigger"), &StreamWeaverAudioStreamPlayback::trigger);
}

StreamWeaverAudioStreamPlayback::~StreamWeaverAudioStreamPlayback()
{
    for (auto& [stream, stream_instance] : all_streams)
    {
        if (stream.is_valid() && stream_instance) {
            stream->release_runtime_instance( stream_instance );
        }
    }
    for (auto& [parameter, parameter_instance] : all_parameters)
    {
        if (parameter.is_valid() && parameter_instance) {
            parameter->release_runtime_instance( parameter_instance );
        }
    }
    for (auto& [trigger, trigger_instance] : all_triggers)
    {
        if (trigger.is_valid() && trigger_instance) {
            trigger->release_runtime_instance( trigger_instance );
        }
    }
}

StreamWeaverParameterRuntimeInstance *StreamWeaverAudioStreamPlayback::
    get_parameter_runtime_instance( Ref<StreamWeaverParameter> parameter )
{
    if (parameter.is_null()) {
        return nullptr;
    }
    if (auto parameter_instance = all_parameters.getptr( parameter ))
    {
        return *parameter_instance;
    }
    auto parameter_instance = parameter->create_runtime_instance(this);
    if (!parameter_instance) {
        return nullptr;
    }
    all_parameters.insert( parameter, parameter_instance );
    if ( auto parameter_as_input = cast_to<StreamWeaverParameterInput>( parameter.ptr() ) )
    {
        input_parameters.insert( parameter_as_input->GetParameterName(), parameter_instance );
    }
    return parameter_instance;
}

StreamWeaverTriggerRuntimeInstance *StreamWeaverAudioStreamPlayback::get_trigger_runtime_instance(
    Ref<StreamWeaverTrigger> trigger )
{
    if (trigger.is_null()) {
        return nullptr;
    }
    if (auto trigger_instance = all_triggers.getptr( trigger ))
    {
        return *trigger_instance;
    }
    auto trigger_instance = trigger->create_runtime_instance(this);
    if (!trigger_instance) {
        return nullptr;
    }
    all_triggers.insert( trigger, trigger_instance );
    if ( auto trigger_as_input = cast_to<StreamWeaverTriggerInput>( trigger.ptr() ) )
    {
        input_triggers.insert( trigger_as_input->GetTriggerName(), trigger_instance );
    }
    return trigger_instance;
}

StreamWeaverStreamRuntimeInstance *StreamWeaverAudioStreamPlayback::get_stream_runtime_instance(
    Ref<StreamWeaverStream> stream)
{
    if (stream.is_null()) {
        return nullptr;
    }
    if (auto stream_instance = all_streams.getptr(stream)) {
        return *stream_instance;
    }
    auto stream_instance = stream->create_runtime_instance(this);
    if (!stream_instance) {
        return nullptr;
    }
    all_streams.insert(stream, stream_instance);

    if (auto producer = cast_to<StreamWeaverProducer>(stream.ptr())) {
        for (int j = 0; j < producer->GetTriggers().size(); ++j) {
            auto trigger = cast_to<StreamWeaverTrigger>(producer->GetTriggers()[j]);
            if (trigger) {
                auto trigger_runtime_instance = get_trigger_runtime_instance(trigger);
                trigger_runtime_instance->add_triggerable(stream_instance);
            }
        }
    }
    return stream_instance;
}

void StreamWeaverAudioStreamPlayback::initialize(Ref<StreamWeaverAudioStream> parent) {
	PROFILE_FUNCTION();
	parent_stream = parent;

    for (int i = 0; i < parent->GetParameters().size(); ++i) {
        auto param = cast_to<StreamWeaverParameter>(parent->GetParameters()[i]);
        if (param) {
            get_parameter_runtime_instance(param);
        }
    }
    for (int i = 0; i < parent->GetTriggers().size(); ++i) {
        auto trig = cast_to<StreamWeaverTrigger>(parent->GetTriggers()[i]);
        if (trig) {
            get_trigger_runtime_instance(trig);
        }
    }
    for (int i = 0; i < parent->GetStreams().size(); ++i) {
        auto s = cast_to<StreamWeaverStream>(parent->GetStreams()[i]);
        if (s) {
            get_stream_runtime_instance(s);
        }
    }
    master_stream_runtimes.clear();
    for (int i = 0; i < parent->GetMasterStreams().size(); ++i) {
        auto s = cast_to<StreamWeaverStream>(parent->GetMasterStreams()[i]);
        if (s) {
            auto runtime = get_stream_runtime_instance(s);
            if (runtime) {
                master_stream_runtimes.push_back(runtime);
            }
        }
    }
    if (parent->GetMasterVolumeMultiplier().is_valid()) {
        master_volume_multiplier_runtime = get_parameter_runtime_instance(parent->GetMasterVolumeMultiplier());
    } else {
        master_volume_multiplier_runtime = nullptr;
    }
    master_base_volume_db = parent->GetMasterVolumeDb();
    master_volume_ramp_initialized = false;

    if (parent->GetPlayTrigger().is_valid()) {
        play_trigger_runtime = get_trigger_runtime_instance(parent->GetPlayTrigger());
    } else {
        play_trigger_runtime = nullptr;
    }
    if (dummy_playback.is_null()) {
        dummy_playback.instantiate();
    }
    last_tick_time = chrono_clock::now();
}

void StreamWeaverAudioStreamPlayback::set_parameter(StringName parameter_name, float value) {
    if ( auto parameter_runtime_instance = input_parameters.getptr( parameter_name ) ) {
		(*parameter_runtime_instance)->set_current_value(value);
    }
}

void StreamWeaverAudioStreamPlayback::trigger(StringName trigger) {
	SpinLockGuard guard(mix_lock);
	if ( auto trigger_runtime_instance = input_triggers.getptr( trigger )) {
		(*trigger_runtime_instance)->trigger_all_triggerables();
	} else if (play_trigger_runtime && (trigger == StringName("Play") || trigger == StringName("play"))) {
		play_trigger_runtime->trigger_all_triggerables();
	}
}

void StreamWeaverAudioStreamPlayback::_start(double p_from_pos) {
	SpinLockGuard guard(mix_lock);
	active = true;
    for (auto stream_runtime : master_stream_runtimes) {
        if (stream_runtime) {
            stream_runtime->reset();
        }
    }
    for (auto& [trigger, trigger_instance] : all_triggers) {
        if (trigger_instance) {
            trigger_instance->reset();
        }
    }
    if (play_trigger_runtime) {
        play_trigger_runtime->trigger_all_triggerables();
    }
}


void StreamWeaverAudioStreamPlayback::_stop() {
	active = false;
}

bool StreamWeaverAudioStreamPlayback::_is_playing() const {
	return active;
}

int32_t StreamWeaverAudioStreamPlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	PROFILE_FUNCTION();
	if (!active) {
		return 0;
	}
    auto now = chrono_clock::now();
    float delta = fseconds(now - last_tick_time).count();
    last_tick_time = now;
    {
        SpinLockGuard guard(mix_lock);
        for (auto ticker : ticking_objects) {
            ticker->tick(delta);
        }
    }

    mix_generation++;

    struct MasterAudioSource {
        const AudioFrame *src = nullptr;
        bool has_audio = false;
    };
    LocalVector<MasterAudioSource> master_sources;
    for (auto stream_runtime : master_stream_runtimes) {
        bool has_audio = false;
        const AudioFrame *src = stream_runtime->pull_audio(p_frames, mix_generation, has_audio);
        master_sources.push_back({ src, has_audio });
    }

    if (static_cast<int32_t>(mix_staging_buffer.size()) < p_frames) {
        mix_staging_buffer.resize(p_frames);
    }
    for (int i = 0; i < p_frames; ++i) {
        mix_staging_buffer[i].left = 0.0f;
        mix_staging_buffer[i].right = 0.0f;
    }

    float target_vol = UtilityFunctions::db_to_linear(master_base_volume_db) *
        (master_volume_multiplier_runtime ? master_volume_multiplier_runtime->get_value() : 1.0f);
    if (!master_volume_ramp_initialized) {
        master_volume_ramp_value = target_vol;
        master_volume_ramp_initialized = true;
    }
    master_volume_ramp_step = (target_vol - master_volume_ramp_value) / float(p_frames > 0 ? p_frames : 1);

    for (const auto& ms : master_sources) {
        if (ms.has_audio && ms.src) {
            for (int i = 0; i < p_frames; ++i) {
                float vol = master_volume_ramp_value + master_volume_ramp_step * float(i);
                mix_staging_buffer[i].left += ms.src[i].left * vol;
                mix_staging_buffer[i].right += ms.src[i].right * vol;
            }
        }
    }
    master_volume_ramp_value += master_volume_ramp_step * float(p_frames);

    if (dummy_playback.is_valid()) {
        dummy_playback->copy_src = mix_staging_buffer.ptr();
        dummy_playback->mix_audio(1.0f, p_frames);
        dummy_playback->copy_src = nullptr;
    }

    memcpy(p_buffer, mix_staging_buffer.ptr(), sizeof(AudioFrame) * p_frames);
    return p_frames;
}

// -------------------- StreamWeaverDummyPlayback --------------------
void StreamWeaverDummyPlayback::_bind_methods() {}

int32_t StreamWeaverDummyPlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	if (copy_src) {
		memcpy(p_buffer, copy_src, sizeof(AudioFrame) * p_frames);
	}
	return p_frames;
}
