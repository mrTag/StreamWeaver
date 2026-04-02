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
        return output_value;
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
				p_buffer[i].left += mixed_input[i].x * volume;
				p_buffer[i].right += mixed_input[i].y * volume;
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

	ClassDB::bind_method(D_METHOD("get_modifying_parameter"), &StreamWeaverOutputLooping::GetModifyingParameter);
	ClassDB::bind_method(D_METHOD("set_modifying_parameter", "modifying_parameter"), &StreamWeaverOutputLooping::SetModifyingParameter);

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
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "modifying_parameter", PROPERTY_HINT_RESOURCE_TYPE, "StreamWeaverParameterInput"), "set_modifying_parameter", "get_modifying_parameter");

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

	void trigger() override {

	}

	bool mix_output_into_buffer(AudioFrame *p_buffer, int32_t p_frames) override {

		return true;
	}
};

StreamWeaverOutputRuntimeInstanceBase *StreamWeaverOutputLooping::create_runtime_instance(StreamWeaverAudioStreamPlayback* from_playback) {
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
