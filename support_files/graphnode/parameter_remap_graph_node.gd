@tool
class_name ParameterRemapGraphNode extends GraphNode

const LEFT_PORT_INPUT_PARAMETER : int = 0

var current_resource: StreamWeaverParameterRemap

@onready var input_range_start_spin_box : SpinBox = %InputRangeStartSpinBox
@onready var input_range_end_spin_box : SpinBox = %InputRangeEndSpinBox
@onready var output_range_start_spin_box : SpinBox = %OutputRangeStartSpinBox
@onready var output_range_end_spin_box : SpinBox = %OutputRangeEndSpinBox

func _ready():
	input_range_start_spin_box.value_changed.connect(_on_input_range_start_changed)
	input_range_end_spin_box.value_changed.connect(_on_input_range_end_changed)
	output_range_start_spin_box.value_changed.connect(_on_output_range_start_changed)
	output_range_end_spin_box.value_changed.connect(_on_output_range_end_changed)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverParameterRemap):
	current_resource = res
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverParameterRemap:
	var res := StreamWeaverParameterRemap.new()
	res.resource_name = "Parameter Remap %d" % (audio_stream_resource.parameters.size() + 1)
	res.input_range_start = 0.0
	res.input_range_end = 1.0
	res.output_range_start = 0.0
	res.output_range_end = 1.0
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

func _on_input_range_start_changed(value: float):
	if current_resource and current_resource.input_range_start != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Remap Input Range Start")
		undo_redo.add_do_property(current_resource, "input_range_start", value)
		undo_redo.add_undo_property(current_resource, "input_range_start", current_resource.input_range_start)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_input_range_end_changed(value: float):
	if current_resource and current_resource.input_range_end != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Remap Input Range End")
		undo_redo.add_do_property(current_resource, "input_range_end", value)
		undo_redo.add_undo_property(current_resource, "input_range_end", current_resource.input_range_end)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_output_range_start_changed(value: float):
	if current_resource and current_resource.output_range_start != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Remap Output Range Start")
		undo_redo.add_do_property(current_resource, "output_range_start", value)
		undo_redo.add_undo_property(current_resource, "output_range_start", current_resource.output_range_start)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_output_range_end_changed(value: float):
	if current_resource and current_resource.output_range_end != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Remap Output Range End")
		undo_redo.add_do_property(current_resource, "output_range_end", value)
		undo_redo.add_undo_property(current_resource, "output_range_end", current_resource.output_range_end)
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
		if input_range_start_spin_box.value != current_resource.input_range_start:
			input_range_start_spin_box.value = current_resource.input_range_start
		if input_range_end_spin_box.value != current_resource.input_range_end:
			input_range_end_spin_box.value = current_resource.input_range_end
		if output_range_start_spin_box.value != current_resource.output_range_start:
			output_range_start_spin_box.value = current_resource.output_range_start
		if output_range_end_spin_box.value != current_resource.output_range_end:
			output_range_end_spin_box.value = current_resource.output_range_end
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
		current_resource.emit_changed()
