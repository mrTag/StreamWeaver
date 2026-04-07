@tool
class_name ParameterFollowGraphNode extends GraphNode

const LEFT_PORT_INPUT_PARAMETER : int = 0

var current_resource: StreamWeaverParameterFollowInput

@onready var acceleration_spin_box : SpinBox = %AccelerationSpinBox
@onready var max_speed_spin_box : SpinBox = %MaxSpeedSpinBox

func _ready():
	acceleration_spin_box.value_changed.connect(_on_acceleration_changed)
	max_speed_spin_box.value_changed.connect(_on_max_speed_changed)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverParameterFollowInput):
	current_resource = res
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverParameterFollowInput:
	var res := StreamWeaverParameterFollowInput.new()
	res.resource_name = "Parameter Follow %d" % (audio_stream_resource.parameters.size() + 1)
	res.acceleration = 1.0
	res.max_speed = 1.0
	return res

func add_to_stream_weaver_audio_stream(audio_stream_resource: StreamWeaverAudioStream):
	if audio_stream_resource and "parameters" in audio_stream_resource:
		audio_stream_resource.parameters.append(current_resource)
		audio_stream_resource.emit_changed()

func remove_from_stream_weaver_audio_stream(audio_stream_resource: StreamWeaverAudioStream):
	if audio_stream_resource and "parameters" in audio_stream_resource:
		audio_stream_resource.parameters.erase(current_resource)
		audio_stream_resource.emit_changed()

func get_right_port_for_resource(res: Resource) -> int:
	if res == current_resource:
		return 0
	return -1

func get_resource_for_right_port(_port: int) -> Resource:
	print("get_resource_for_right_port")
	return current_resource

func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {
		LEFT_PORT_INPUT_PARAMETER: Array()
	}
	if current_resource.input_parameter:
		connections[LEFT_PORT_INPUT_PARAMETER].append(current_resource.input_parameter)
	return connections

func connect_to(other: GraphNode, other_port: int, this_port: int, only_check:bool=false) -> bool:
	if not other.has_method("get_resource_for_right_port"):
		return false
	var res = other.get_resource_for_right_port(other_port)
	
	if this_port == LEFT_PORT_INPUT_PARAMETER:
		if res is StreamWeaverParameter:
			if current_resource.input_parameter == res:
				return false
			if not only_check:
				current_resource.input_parameter = res
			return true
	return false

func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)

func disconnect_from(other: GraphNode, other_port: int, this_port: int):
	if not other.has_method("get_resource_for_right_port"):
		return
	var res = other.get_resource_for_right_port(other_port)
	
	if this_port == LEFT_PORT_INPUT_PARAMETER:
		if res == current_resource.input_parameter:
			current_resource.input_parameter = null

func _on_acceleration_changed(value: float):
	if current_resource and current_resource.acceleration != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Follow Acceleration")
		undo_redo.add_do_property(current_resource, "acceleration", value)
		undo_redo.add_undo_property(current_resource, "acceleration", current_resource.acceleration)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_max_speed_changed(value: float):
	if current_resource and current_resource.max_speed != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Follow Max Speed")
		undo_redo.add_do_property(current_resource, "max_speed", value)
		undo_redo.add_undo_property(current_resource, "max_speed", current_resource.max_speed)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from: Vector2, to: Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Parameter Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()

func _update_ui():
	if current_resource:
		if acceleration_spin_box.value != current_resource.acceleration:
			acceleration_spin_box.value = current_resource.acceleration
		if max_speed_spin_box.value != current_resource.max_speed:
			max_speed_spin_box.value = current_resource.max_speed
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
		current_resource.emit_changed()
