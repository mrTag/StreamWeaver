#include "StreamWeaver.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"

#include "profiling.h"

#include <algorithm>

using namespace godot;

// -------------------- StreamWeaverInputStream --------------------
void StreamWeaverInputStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_input_name"), &StreamWeaverInputStream::GetInputName);
	ClassDB::bind_method(D_METHOD("set_input_name", "input_name"), &StreamWeaverInputStream::SetInputName);
	ClassDB::bind_method(D_METHOD("get_audio_stream"), &StreamWeaverInputStream::GetAudioStream);
	ClassDB::bind_method(D_METHOD("set_audio_stream", "audio_stream"), &StreamWeaverInputStream::SetAudioStream);
    ClassDB::bind_method(D_METHOD("get_graph_node_position"), &StreamWeaverInputStream::GetGraphNodePosition);
    ClassDB::bind_method(D_METHOD("set_graph_node_position", "graph_node_position"), &StreamWeaverInputStream::SetGraphNodePosition);


	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "input_name"), "set_input_name", "get_input_name");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "audio_stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_audio_stream", "get_audio_stream");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "graph_node_position"), "set_graph_node_position", "get_graph_node_position");
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
    memdelete(dynamic_cast<StreamWeaverParameterRemapRuntimeInstance*>(instance));
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
    memdelete(dynamic_cast<StreamWeaverParameterAddRuntimeInstance*>(instance));
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
    memdelete(dynamic_cast<StreamWeaverParameterMultiplyRuntimeInstance*>(instance));
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
                current_value += current_speed * delta_time;
            }
        } else {
            // ACCELERATE or CONSTANT SPEED
            float speed_change = acceleration * delta_time;
            current_speed += direction * speed_change;
            
            if (godot::Math::abs(current_speed) > max_speed) {
                current_speed = direction * max_speed;
            }
            current_value += current_speed * delta_time;
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
        runtime_instance->set_current_value(runtime_instance->input_parameter_runtime_instance->get_value());
    }
    runtime_instance->acceleration = acceleration;
    runtime_instance->max_speed = max_speed;
    from_playback->add_ticking(runtime_instance);
    return runtime_instance;
}

void StreamWeaverParameterFollowInput::release_runtime_instance(StreamWeaverParameterRuntimeInstance* instance)
{
    memdelete(dynamic_cast<StreamWeaverParameterFollowInputRuntimeInstance*>(instance));
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
    memdelete(dynamic_cast<StreamWeaverParameterWindowRuntimeInstance*>(instance));
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

// -------------------- StreamWeaverTriggerConditionalParameter --------------------
void StreamWeaverTriggerConditionalParameter::_bind_methods() {
	BIND_ENUM_CONSTANT(EQ);
	BIND_ENUM_CONSTANT(LT);
	BIND_ENUM_CONSTANT(GT);
	BIND_ENUM_CONSTANT(LTE);
	BIND_ENUM_CONSTANT(GTE);
	BIND_ENUM_CONSTANT(NEQ);

	ClassDB::bind_method(D_METHOD("get_parameter"), &StreamWeaverTriggerConditionalParameter::GetParameter);
	ClassDB::bind_method(D_METHOD("set_parameter", "parameter"), &StreamWeaverTriggerConditionalParameter::SetParameter);
    ClassDB::bind_method(D_METHOD("get_input_trigger"), &StreamWeaverTriggerConditionalParameter::GetInputTrigger);
    ClassDB::bind_method(D_METHOD("set_input_trigger", "input_trigger"), &StreamWeaverTriggerConditionalParameter::SetInputTrigger);
	ClassDB::bind_method(D_METHOD("get_comparison_type"), &StreamWeaverTriggerConditionalParameter::GetComparisonType);
	ClassDB::bind_method(D_METHOD("set_comparison_type", "comparison_type"), &StreamWeaverTriggerConditionalParameter::SetComparisonType);
	ClassDB::bind_method(D_METHOD("get_value"), &StreamWeaverTriggerConditionalParameter::GetValue);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &StreamWeaverTriggerConditionalParameter::SetValue);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameterInput"), "set_parameter", "get_parameter");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_trigger", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverTriggerInput"), "set_input_trigger", "get_input_trigger");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "comparison_type", PROPERTY_HINT_ENUM, "EQ,LT,GT,LTE,GTE,NEQ"), "set_comparison_type", "get_comparison_type");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
}

class StreamWeaverTriggerRuntimeInstanceConditionalParameter : public StreamWeaverTriggerRuntimeInstance, public StreamWeaverRuntimeTriggerableInterface
{
public:
    StreamWeaverParameterRuntimeInstance* parameter_runtime_instance = nullptr;
    StreamWeaverTriggerConditionalParameter::Comparison comparison_type;
    float value;

    void trigger() override {
        if (parameter_runtime_instance == nullptr) {
            return;
        }

        float parameterValue = parameter_runtime_instance->get_value();
        bool should_trigger = false;
        switch (comparison_type) {
            case StreamWeaverTriggerConditionalParameter::EQ: should_trigger = Math::is_equal_approx(parameterValue, value); break;
            case StreamWeaverTriggerConditionalParameter::NEQ: should_trigger = !Math::is_equal_approx(parameterValue, value); break;
            case StreamWeaverTriggerConditionalParameter::LT: should_trigger = parameterValue < value; break;
            case StreamWeaverTriggerConditionalParameter::GT: should_trigger = parameterValue > value; break;
            case StreamWeaverTriggerConditionalParameter::LTE: should_trigger = parameterValue <= value; break;
            case StreamWeaverTriggerConditionalParameter::GTE: should_trigger = parameterValue >= value; break;
        }

        if (should_trigger) {
            trigger_all_triggerables();
        }
    }
};

StreamWeaverTriggerRuntimeInstance* StreamWeaverTriggerConditionalParameter::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    StreamWeaverTriggerRuntimeInstanceConditionalParameter* instance = memnew(StreamWeaverTriggerRuntimeInstanceConditionalParameter);
    if (parameter.is_valid())
    {
        instance->parameter_runtime_instance = from_playback->get_parameter_runtime_instance(parameter);
    }
    if (input_trigger.is_valid())
    {
        auto input_trigger_runtime_instance = from_playback->get_trigger_runtime_instance(input_trigger);
        input_trigger_runtime_instance->add_triggerable(instance);
    }
    instance->comparison_type = comparison_type;
    instance->value = value;
    return instance;
}

void StreamWeaverTriggerConditionalParameter::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
    memdelete( dynamic_cast<StreamWeaverTriggerRuntimeInstanceConditionalParameter*>(instance));
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
        input_trigger_runtime_instance->add_triggerable(runtime_instance);
    }
    runtime_instance->min_value = min_value;
    runtime_instance->max_value = max_value;
    return runtime_instance;
}

void StreamWeaverTriggerConditionalParameterRange::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance)
{
    memdelete(dynamic_cast<StreamWeaverTriggerRuntimeInstanceConditionalParameterRange*>(instance));
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
        input_trigger_runtime_instance->add_triggerable(instance);
    }
    if (multiplier_parameter.is_valid()) {
        instance->multiplier_parameter_runtime_instance = from_playback->get_parameter_runtime_instance(multiplier_parameter);
    }
    from_playback->add_ticking(instance);
    return instance;
}

void StreamWeaverTriggerDelay::release_runtime_instance(StreamWeaverTriggerRuntimeInstance* instance) {
    memdelete(dynamic_cast<StreamWeaverTriggerRuntimeInstanceDelay*>(instance));
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
    memdelete(dynamic_cast<StreamWeaverTriggerRuntimeInstanceMetronome*>(instance));
}

// -------------------- StreamWeaverOutput --------------------
void StreamWeaverOutput::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_triggers"), &StreamWeaverOutput::GetTriggers);
	ClassDB::bind_method(D_METHOD("set_triggers", "triggers"), &StreamWeaverOutput::SetTriggers);
	ClassDB::bind_method(D_METHOD("get_output_name"), &StreamWeaverOutput::GetOutputName);
	ClassDB::bind_method(D_METHOD("set_output_name", "output_name"), &StreamWeaverOutput::SetOutputName);
    ClassDB::bind_method(D_METHOD("get_base_volume_db"), &StreamWeaverOutput::GetBaseVolumeDb);
    ClassDB::bind_method(D_METHOD("set_base_volume_db", "base_volume_db"), &StreamWeaverOutput::SetBaseVolumeDb);
    ClassDB::bind_method(D_METHOD("get_base_pitch"), &StreamWeaverOutput::GetBasePitch);
    ClassDB::bind_method(D_METHOD("set_base_pitch", "base_pitch"), &StreamWeaverOutput::SetBasePitch);
    ClassDB::bind_method(D_METHOD("get_graph_node_position"), &StreamWeaverOutput::GetGraphNodePosition);
    ClassDB::bind_method(D_METHOD("set_graph_node_position", "graph_node_position"), &StreamWeaverOutput::SetGraphNodePosition);
    ClassDB::bind_method(D_METHOD("get_volume_multiplier"), &StreamWeaverOutput::GetVolumeMultiplier);
    ClassDB::bind_method(D_METHOD("set_volume_multiplier", "volume_multiplier"), &StreamWeaverOutput::SetVolumeMultiplier);
    ClassDB::bind_method(D_METHOD("get_pitch_multiplier"), &StreamWeaverOutput::GetPitchMultiplier);
    ClassDB::bind_method(D_METHOD("set_pitch_multiplier", "pitch_multiplier"), &StreamWeaverOutput::SetPitchMultiplier);

	
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "output_name"), "set_output_name", "get_output_name");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "triggers", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverTrigger"), "set_triggers", "get_triggers");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_volume_db"), "set_base_volume_db", "get_base_volume_db");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_pitch"), "set_base_pitch", "get_base_pitch");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "volume_multiplier", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_volume_multiplier", "get_volume_multiplier");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "pitch_multiplier", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_pitch_multiplier", "get_pitch_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "graph_node_position"), "set_graph_node_position", "get_graph_node_position");
}

void StreamWeaverOutput::initialize_runtime_instance_base(StreamWeaverOutputRuntimeInstanceBase* runtime_instance, StreamWeaverAudioStreamPlayback* playback) {

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

// -------------------- StreamWeaverOutputRandomize --------------------
void StreamWeaverOutputRandomize::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_randomize_pitch"), &StreamWeaverOutputRandomize::GetRandomizePitchOffset);
	ClassDB::bind_method(D_METHOD("set_randomize_pitch", "randomize_pitch"), &StreamWeaverOutputRandomize::SetRandomizePitchOffset);
	ClassDB::bind_method(D_METHOD("get_randomize_volume"), &StreamWeaverOutputRandomize::GetRandomizeVolumeOffset);
	ClassDB::bind_method(D_METHOD("set_randomize_volume", "randomize_volume"), &StreamWeaverOutputRandomize::SetRandomizeVolumeOffset);
	ClassDB::bind_method(D_METHOD("get_input_streams"), &StreamWeaverOutputRandomize::GetInputStreams);
	ClassDB::bind_method(D_METHOD("set_input_streams", "input_streams"), &StreamWeaverOutputRandomize::SetInputStreams);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "randomize_pitch"), "set_randomize_pitch", "get_randomize_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "randomize_volume"), "set_randomize_volume", "get_randomize_volume");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "input_streams", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverInputStream"), "set_input_streams", "get_input_streams");
}

class ParameterizedOutputRandomizeRuntimeInstance : public StreamWeaverOutputRuntimeInstanceBase {
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
		RuntimeInputData runtime_input {};
		int input_index = randomizer->randi_range(0, inputs.size()-1);
		// super simple way to not play the same sound twice in a row:
		if (input_index == last_played_index && inputs.size() > 1) {
			input_index = (input_index + 1) % inputs.size();
		}
		last_played_index = input_index;
	    runtime_input.playback = inputs[input_index]->GetAudioStream()->instantiate_playback();
	    runtime_input.playback->start();
		runtime_input.pitch_offset = randomizer->randf_range(-randomize_pitch, randomize_pitch);
		runtime_input.volume_offset = randomizer->randf_range(-randomize_volume, randomize_volume);
		currently_playing_inputs.push_back(runtime_input);
	}

	bool mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
		if (currently_playing_inputs.is_empty()) {
			return false;
		}
		PROFILE_FUNCTION();
		size_t current_input_index = 0;
	    float pitch = get_pitch();
	    float volume = get_volume();
		while (current_input_index < currently_playing_inputs.size()) {
			auto& cpi = currently_playing_inputs[current_input_index];
			auto mixed_input = cpi.playback->mix_audio(pitch + cpi.pitch_offset, p_frames);
		    int num_frames_mixed = Math::min( p_frames, static_cast<int32_t>( mixed_input.size() ) );
			for (int i = 0; i < num_frames_mixed; i++) {
				p_buffer[i].left += mixed_input[i].x * (volume + cpi.volume_offset);
				p_buffer[i].right += mixed_input[i].y * (volume + cpi.volume_offset);
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

StreamWeaverOutputRuntimeInstanceBase *StreamWeaverOutputRandomize::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	PROFILE_FUNCTION();
	auto* instance = memnew(ParameterizedOutputRandomizeRuntimeInstance);
    initialize_runtime_instance_base( instance, from_playback );
	instance->randomize_pitch = randomize_pitch_offset;
	instance->randomize_volume = randomize_volume_offset;
	for (Ref<StreamWeaverInputStream> input : input_streams) {
        instance->inputs.push_back(input);
	}
	static int random_seed = 54631;
	random_seed += 24462;
	instance->randomizer.instantiate();
	instance->randomizer->set_seed(random_seed);
	return instance;
}

void StreamWeaverOutputRandomize::release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) {
	memdelete(dynamic_cast<ParameterizedOutputRandomizeRuntimeInstance*>(instance));
}

// -------------------- StreamWeaverOutputLooping --------------------

void StreamWeaverOutputLooping::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_input_stream"), &StreamWeaverOutputLooping::GetInputStream);
	ClassDB::bind_method(D_METHOD("set_input_stream", "input_stream"), &StreamWeaverOutputLooping::SetInputStream);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "input_stream"), "set_input_stream", "get_input_stream");
}

class ParameterizedOutputLoopingRuntimeInstance : public StreamWeaverOutputRuntimeInstanceBase {
public:
    Ref<AudioStreamPlayback> playback;

	void trigger() override {
        // this one will keep playing the looping audio in the background, no triggering.
	}

	bool mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
	    if (playback == nullptr)
	    {
	        return false;
	    }
	    PROFILE_FUNCTION();

	    float volume = get_volume();
	    if (volume <= 0.0001f)
	    {
	        return false;
	    }
	    float pitch = get_pitch();
	    auto mixed_input = playback->mix_audio(pitch, p_frames);
	    int num_frames_mixed = Math::min( p_frames, static_cast<int32_t>( mixed_input.size() ) );
	    for (int i = 0; i < num_frames_mixed; i++) {
	        p_buffer[i].left += mixed_input[i].x * volume;
	        p_buffer[i].right += mixed_input[i].y * volume;
	    }
        return num_frames_mixed == p_frames;
	}
};

StreamWeaverOutputRuntimeInstanceBase *StreamWeaverOutputLooping::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
	auto* instance = memnew(ParameterizedOutputLoopingRuntimeInstance);
    initialize_runtime_instance_base( instance, from_playback );
    instance->playback = input_stream->GetAudioStream()->instantiate_playback();
    if (instance->playback != nullptr)
    {
        instance->playback->start();
    }
	return instance;
}

void StreamWeaverOutputLooping::release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) {
	memdelete(dynamic_cast<ParameterizedOutputLoopingRuntimeInstance *>(instance));
}


// -------------------- StreamWeaverOutputGranularLinearSweep ------
void StreamWeaverOutputGranularLinearSweep::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_input_stream"), &StreamWeaverOutputGranularLinearSweep::GetInputStream);
    ClassDB::bind_method(D_METHOD("set_input_stream", "inputStream"), &StreamWeaverOutputGranularLinearSweep::SetInputStream);
    ClassDB::bind_method(D_METHOD("get_sweeping_parameter"), &StreamWeaverOutputGranularLinearSweep::GetSweepingParameter);
    ClassDB::bind_method(D_METHOD("set_sweeping_parameter", "sweepingParameter"), &StreamWeaverOutputGranularLinearSweep::SetSweepingParameter);
    ClassDB::bind_method(D_METHOD("get_min_parameter_value"), &StreamWeaverOutputGranularLinearSweep::GetMinParameterValue);
    ClassDB::bind_method(D_METHOD("set_min_parameter_value", "minParameterValue"), &StreamWeaverOutputGranularLinearSweep::SetMinParameterValue);
    ClassDB::bind_method(D_METHOD("get_max_parameter_value"), &StreamWeaverOutputGranularLinearSweep::GetMaxParameterValue);
    ClassDB::bind_method(D_METHOD("set_max_parameter_value", "maxParameterValue"), &StreamWeaverOutputGranularLinearSweep::SetMaxParameterValue);
    ClassDB::bind_method(D_METHOD("get_min_grain_size_milliseconds"), &StreamWeaverOutputGranularLinearSweep::GetMinGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("set_min_grain_size_milliseconds", "grainSizeMs"), &StreamWeaverOutputGranularLinearSweep::SetMinGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("get_max_grain_size_milliseconds"), &StreamWeaverOutputGranularLinearSweep::GetMaxGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("set_max_grain_size_milliseconds", "maxGrainSizeMs"), &StreamWeaverOutputGranularLinearSweep::SetMaxGrainSizeMilliseconds);
    ClassDB::bind_method(D_METHOD("get_grain_jitter_percentage"), &StreamWeaverOutputGranularLinearSweep::GetGrainJitterPercentage);
    ClassDB::bind_method(D_METHOD("set_grain_jitter_percentage", "grainJitterPercentage"), &StreamWeaverOutputGranularLinearSweep::SetGrainJitterPercentage);

    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "input_stream"), "set_input_stream", "get_input_stream");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sweeping_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameter"), "set_sweeping_parameter", "get_sweeping_parameter");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_parameter_value"), "set_min_parameter_value", "get_min_parameter_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_parameter_value"), "set_max_parameter_value", "get_max_parameter_value");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_grain_size_milliseconds"), "set_min_grain_size_milliseconds", "get_min_grain_size_milliseconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_grain_size_milliseconds"), "set_max_grain_size_milliseconds", "get_max_grain_size_milliseconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grain_jitter_percentage"), "set_grain_jitter_percentage", "get_grain_jitter_percentage");
}

class StreamWeaverOutputGranularLinearSweepRuntime : public StreamWeaverOutputRuntimeInstanceBase
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

	bool mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {
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

		return true;
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

StreamWeaverOutputRuntimeInstanceBase* StreamWeaverOutputGranularLinearSweep::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    PROFILE_FUNCTION();
    auto* instance = new StreamWeaverOutputGranularLinearSweepRuntime();
    initialize_runtime_instance_base( instance, from_playback );
    instance->sweeping_parameter = from_playback->get_parameter_runtime_instance(sweeping_parameter);
    auto playback = input_stream->GetAudioStream()->instantiate_playback();
    if (playback != nullptr)
    {
        playback->start();
        int num_samples = input_stream->GetAudioStream()->get_length() * AudioServer::get_singleton()->get_mix_rate();
        instance->grain_buffer = playback->mix_audio(1.0f, num_samples);
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

void StreamWeaverOutputGranularLinearSweep::release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase *instance) {
	memdelete(dynamic_cast<StreamWeaverOutputGranularLinearSweepRuntime*>(instance));
}

// -------------------- StreamWeaverOutputGranularDatabase --------------------

void StreamWeaverOutputGranularDatabase::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_grains_database"), &StreamWeaverOutputGranularDatabase::GetGrainsDatabase);
    ClassDB::bind_method(D_METHOD("set_grains_database", "database"), &StreamWeaverOutputGranularDatabase::SetGrainsDatabase);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "grains_database", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverGrainsDatabase"),
        "set_grains_database", "get_grains_database");

    ClassDB::bind_method(D_METHOD("get_axis_parameters"), &StreamWeaverOutputGranularDatabase::GetAxisParameters);
    ClassDB::bind_method(D_METHOD("set_axis_parameters", "params"), &StreamWeaverOutputGranularDatabase::SetAxisParameters);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "axis_parameters", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverParameter"),
        "set_axis_parameters", "get_axis_parameters");

    ClassDB::bind_method(D_METHOD("get_pitch_correction_enabled"), &StreamWeaverOutputGranularDatabase::GetPitchCorrectionEnabled);
    ClassDB::bind_method(D_METHOD("set_pitch_correction_enabled", "enabled"), &StreamWeaverOutputGranularDatabase::SetPitchCorrectionEnabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pitch_correction_enabled"),
        "set_pitch_correction_enabled", "get_pitch_correction_enabled");

    ClassDB::bind_method(D_METHOD("get_no_repeat_count"), &StreamWeaverOutputGranularDatabase::GetNoRepeatCount);
    ClassDB::bind_method(D_METHOD("set_no_repeat_count", "count"), &StreamWeaverOutputGranularDatabase::SetNoRepeatCount);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "no_repeat_count", PROPERTY_HINT_RANGE, "1,32,1"),
        "set_no_repeat_count", "get_no_repeat_count");

    ClassDB::bind_method(D_METHOD("get_candidate_pool_size"), &StreamWeaverOutputGranularDatabase::GetCandidatePoolSize);
    ClassDB::bind_method(D_METHOD("set_candidate_pool_size", "size"), &StreamWeaverOutputGranularDatabase::SetCandidatePoolSize);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "candidate_pool_size", PROPERTY_HINT_RANGE, "4,128,1"),
        "set_candidate_pool_size", "get_candidate_pool_size");

    ClassDB::bind_method(D_METHOD("get_search_param_smoothing"), &StreamWeaverOutputGranularDatabase::GetSearchParamSmoothing);
    ClassDB::bind_method(D_METHOD("set_search_param_smoothing", "smoothing"), &StreamWeaverOutputGranularDatabase::SetSearchParamSmoothing);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "search_param_smoothing", PROPERTY_HINT_RANGE, "0,1,0.01"),
        "set_search_param_smoothing", "get_search_param_smoothing");

    ClassDB::bind_method(D_METHOD("get_shortlist_score_window"), &StreamWeaverOutputGranularDatabase::GetShortlistScoreWindow);
    ClassDB::bind_method(D_METHOD("set_shortlist_score_window", "window"), &StreamWeaverOutputGranularDatabase::SetShortlistScoreWindow);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shortlist_score_window", PROPERTY_HINT_RANGE, "0,12,0.05"),
        "set_shortlist_score_window", "get_shortlist_score_window");

    ClassDB::bind_method(D_METHOD("get_continuity_bias"), &StreamWeaverOutputGranularDatabase::GetContinuityBias);
    ClassDB::bind_method(D_METHOD("set_continuity_bias", "bias"), &StreamWeaverOutputGranularDatabase::SetContinuityBias);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continuity_bias", PROPERTY_HINT_RANGE, "0,4,0.01"),
        "set_continuity_bias", "get_continuity_bias");

    ClassDB::bind_method(D_METHOD("get_random_selection_span"), &StreamWeaverOutputGranularDatabase::GetRandomSelectionSpan);
    ClassDB::bind_method(D_METHOD("set_random_selection_span", "span"), &StreamWeaverOutputGranularDatabase::SetRandomSelectionSpan);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "random_selection_span", PROPERTY_HINT_RANGE, "0,32,1"),
        "set_random_selection_span", "get_random_selection_span");

    ClassDB::bind_method(D_METHOD("get_random_walk_step"), &StreamWeaverOutputGranularDatabase::GetRandomWalkStep);
    ClassDB::bind_method(D_METHOD("set_random_walk_step", "step"), &StreamWeaverOutputGranularDatabase::SetRandomWalkStep);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_walk_step", PROPERTY_HINT_RANGE, "0,8,0.05"),
        "set_random_walk_step", "get_random_walk_step");

    ClassDB::bind_method(D_METHOD("get_random_walk_damping"), &StreamWeaverOutputGranularDatabase::GetRandomWalkDamping);
    ClassDB::bind_method(D_METHOD("set_random_walk_damping", "damping"), &StreamWeaverOutputGranularDatabase::SetRandomWalkDamping);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_walk_damping", PROPERTY_HINT_RANGE, "0,1,0.01"),
        "set_random_walk_damping", "get_random_walk_damping");

    ClassDB::bind_method(D_METHOD("get_preferred_match_bias"), &StreamWeaverOutputGranularDatabase::GetPreferredMatchBias);
    ClassDB::bind_method(D_METHOD("set_preferred_match_bias", "bias"), &StreamWeaverOutputGranularDatabase::SetPreferredMatchBias);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preferred_match_bias", PROPERTY_HINT_RANGE, "0,4,0.01"),
        "set_preferred_match_bias", "get_preferred_match_bias");
}

class StreamWeaverOutputGranularDatabaseRuntime : public StreamWeaverOutputRuntimeInstanceBase {
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
    float overlap_read_index = 0.0f;
    std::vector<float> overlap_buffer;
    PackedFloat32Array smoothed_search_params;
    int last_selected_unit = -1;
    bool has_primed_output = false;
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

    PackedFloat32Array build_search_params() const {
        int num_axes = db ? db->get_num_axes() : 0;
        PackedFloat32Array params;
        params.resize(num_axes);
        for (int i = 0; i < num_axes; ++i) {
            StreamWeaverParameterRuntimeInstance* inst = (i < static_cast<int>(axis_param_instances.size()))
                ? axis_param_instances[i] : nullptr;
            if (inst) {
                params[i] = inst->get_value();
            } else {
                params[i] = 0.5f * (db->get_axis_min(i) + db->get_axis_max(i));
            }
        }
        return params;
    }

    PackedFloat32Array get_smoothed_search_params() {
        PackedFloat32Array raw = build_search_params();
        if (smoothed_search_params.size() != raw.size()) {
            smoothed_search_params = raw;
            return smoothed_search_params;
        }

        for (int i = 0; i < raw.size(); ++i) {
            smoothed_search_params[i] = Math::lerp(smoothed_search_params[i], raw[i], search_param_smoothing);
        }
        return smoothed_search_params;
    }

    int select_new_grain(const PackedFloat32Array& params, float target_f0_hint) {
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

    float get_target_f0() const {
        if (!db) return 0.0f;
        PackedFloat32Array params = build_search_params();
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
        if (!db) return;
        PackedFloat32Array params = get_smoothed_search_params();
        float target_f0_hint = get_target_f0();
        int unit_index = select_new_grain(params, target_f0_hint);
        if (unit_index < 0) return;

        const auto& entry = db->get_grain_entry(unit_index);
        if (entry.fundamental_hz <= 0.0f) return;

        int period_samples = std::max(1, static_cast<int>(std::round(db->get_sample_rate() / entry.fundamental_hz)));
        int window_size = std::max(2, period_samples * 2);
        int half_window = window_size / 2;
        int input_start = entry.center_sample - half_window;
        const float* pcm = db->get_pcm_pool_ptr();
        int pcm_size = db->get_pcm_pool_size();
        int output_center = static_cast<int>(std::round(overlap_read_index));
        if (!has_primed_output) {
            output_center += half_window;
            has_primed_output = true;
        }

        int output_start = output_center - half_window;
        int output_end = output_start + window_size;

        if (output_end <= 0) return;
        ensure_overlap_capacity(output_end);

        for (int i = 0; i < window_size; ++i) {
            int src_index = input_start + i;
            int dst_index = output_start + i;
            if (dst_index < 0 || dst_index >= static_cast<int>(overlap_buffer.size())) continue;

            float sample = 0.0f;
            if (src_index >= 0 && src_index < pcm_size) {
                sample = pcm[src_index];
            }

            float phase = (window_size > 1) ? static_cast<float>(i) / static_cast<float>(window_size - 1) : 0.0f;
            float window = 0.5f * (1.0f - std::cos(2.0f * Math_PI * phase));
            overlap_buffer[dst_index] += sample * window;
        }

        smoothed_target_f0 = entry.fundamental_hz;
    }

    void trigger() override {
        // Continuous output, no triggering needed
    }

    bool mix_output_into_buffer(AudioFrame* p_buffer, int32_t p_frames) override {
        PROFILE_FUNCTION();
        if (!db || db->get_grain_count() == 0) return false;

        float vol = get_volume();
        ensure_overlap_capacity(p_frames + 2048);

        for (int frame_index = 0; frame_index < p_frames; ++frame_index) {
            float target_f0 = get_target_f0();
            if (target_f0 > 0.0f) {
                if (smoothed_target_f0 <= 0.0f) {
                    smoothed_target_f0 = target_f0;
                } else {
                    smoothed_target_f0 = Math::lerp(smoothed_target_f0, target_f0, 0.0015f);
                }
            }
            if (db->get_sample_rate() > 0.0f && smoothed_target_f0 > 0.0f) {
                synthesis_phase += smoothed_target_f0 / db->get_sample_rate();
                while (synthesis_phase >= 1.0f) {
                    synthesis_phase -= 1.0f;
                    trigger_unit();
                }
            }

            ensure_overlap_capacity(static_cast<int>(std::ceil(overlap_read_index)) + 2);
            int read_index = static_cast<int>(overlap_read_index);
            float sample = 0.0f;
            if (read_index >= 0 && read_index < static_cast<int>(overlap_buffer.size())) {
                sample = overlap_buffer[read_index];
                overlap_buffer[read_index] = 0.0f;
            }

            float out = sample * vol;
            p_buffer[frame_index].left += out;
            p_buffer[frame_index].right += out;
            overlap_read_index += 1.0f;
        }
        return true;
    }
};

StreamWeaverOutputRuntimeInstanceBase* StreamWeaverOutputGranularDatabase::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
    PROFILE_FUNCTION();
    if (grains_database.is_null() || grains_database->get_grain_count() == 0) return nullptr;

    auto* instance = new StreamWeaverOutputGranularDatabaseRuntime();
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

    // Set up axis parameter runtime instances. Keep them aligned to the
    // database's axis indices so a missing wiring does not shift other
    // axes' values into the wrong slot.
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

    // Initialize anti-repeat state and overlap buffer for PSOLA synthesis.
    for (int i = 0; i < StreamWeaverOutputGranularDatabaseRuntime::MAX_NO_REPEAT; ++i) {
        instance->last_played[i] = -1;
    }
    instance->overlap_buffer.resize(4096, 0.0f);
    instance->trigger_unit();

    return instance;
}

void StreamWeaverOutputGranularDatabase::release_runtime_instance(StreamWeaverOutputRuntimeInstanceBase* instance) {
    memdelete(dynamic_cast<StreamWeaverOutputGranularDatabaseRuntime*>(instance));
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

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "parameters", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverParameter"), "set_parameters", "get_parameters");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "triggers", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverTrigger"), "set_triggers", "get_triggers");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "inputs", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverInputStream"), "set_inputs", "get_inputs");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "outputs", PROPERTY_HINT_ARRAY_TYPE, "StreamWeaverOutput"), "set_outputs", "get_outputs");
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
    for (auto& [parameter, parameter_instance] : all_parameters)
    {
        parameter->release_runtime_instance( parameter_instance );
    }
    for (auto& [trigger, trigger_instance] : all_triggers)
    {
        trigger->release_runtime_instance( trigger_instance );
    }
}


StreamWeaverParameterRuntimeInstance *StreamWeaverAudioStreamPlayback::
    get_parameter_runtime_instance( Ref<StreamWeaverParameter> parameter )
{
    if (auto parameter_instance = all_parameters.getptr( parameter ))
    {
        return *parameter_instance;
    }
    auto parameter_instance = parameter->create_runtime_instance(this);
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
    if (auto trigger_instance = all_triggers.getptr( trigger ))
    {
        return *trigger_instance;
    }
    auto trigger_instance = trigger->create_runtime_instance(this);
    all_triggers.insert( trigger, trigger_instance );
    if ( auto trigger_as_input = cast_to<StreamWeaverTriggerInput>( trigger.ptr() ) )
    {
        input_triggers.insert( trigger_as_input->GetTriggerName(), trigger_instance );
    }
    return trigger_instance;
}

void StreamWeaverAudioStreamPlayback::initialize(Ref<StreamWeaverAudioStream> parent) {
	PROFILE_FUNCTION();
	parent_stream = parent;
	for (int i = 0; i < parent->GetOutputs().size(); ++i) {
		const auto output = cast_to<StreamWeaverOutput>(parent->GetOutputs()[i]);
		if (output == nullptr) {
			continue;
		}
	    auto runtime_output = output->create_runtime_instance(this);
	    for (int j = 0; j < output->GetTriggers().size(); ++j)
	    {
	        auto trigger = cast_to<StreamWeaverTrigger>(output->GetTriggers()[j]);
	        if (trigger)
	        {
	            auto trigger_runtime_instance = get_trigger_runtime_instance(trigger);
	            trigger_runtime_instance->add_triggerable( runtime_output );
	        }
	    }
		runtime_outputs.push_back(runtime_output);
	}
    last_tick_time = chrono_clock::now();
}

void StreamWeaverAudioStreamPlayback::set_parameter(StringName parameter_name, float value) {
    if ( auto parameter_runtime_instance = input_parameters.getptr( parameter_name ) ) {
		(*parameter_runtime_instance)->set_current_value(value);
    }
}

void StreamWeaverAudioStreamPlayback::trigger(StringName trigger) {
	if ( auto trigger_runtime_instance = input_triggers.getptr( trigger )) {
		(*trigger_runtime_instance)->trigger_all_triggerables();
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

int32_t StreamWeaverAudioStreamPlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	PROFILE_FUNCTION();
	if (!active) {
		return 0;
	}
    auto now = chrono_clock::now();
    float delta = fseconds(now - last_tick_time).count();
    last_tick_time = now;
    for (auto ticker : ticking_objects) {
		ticker->tick(delta);
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
