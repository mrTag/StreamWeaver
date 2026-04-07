@tool
class_name ParameterWindowGraphNode extends GraphNode

const LEFT_PORT_INPUT_PARAMETER : int = 0

var current_resource: StreamWeaverParameterWindow

@onready var min_value_spin_box : SpinBox = %MinValueSpinBox
@onready var max_value_spin_box : SpinBox = %MaxValueSpinBox
@onready var value_interpolation_window_spin_box : SpinBox = %ValueInterpolationWindowSpinBox
@onready var interpolation_type_option_button : OptionButton = %InterpolationTypeOptionButton

func _ready():
	min_value_spin_box.value_changed.connect(_on_min_value_changed)
	max_value_spin_box.value_changed.connect(_on_max_value_changed)
	value_interpolation_window_spin_box.value_changed.connect(_on_value_interpolation_window_changed)
	interpolation_type_option_button.item_selected.connect(_on_interpolation_type_selected)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverParameterWindow):
	current_resource = res
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverParameterWindow:
	var res := StreamWeaverParameterWindow.new()
	res.resource_name = "Parameter Window %d" % (audio_stream_resource.parameters.size() + 1)
	res.min_value = 0.0
	res.max_value = 1.0
	res.value_interpolation_window = 0.1
	res.interpolation_type = 0 # Linear
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

func _on_min_value_changed(value: float):
	if current_resource and current_resource.min_value != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Window Min Value")
		undo_redo.add_do_property(current_resource, "min_value", value)
		undo_redo.add_undo_property(current_resource, "min_value", current_resource.min_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_max_value_changed(value: float):
	if current_resource and current_resource.max_value != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Window Max Value")
		undo_redo.add_do_property(current_resource, "max_value", value)
		undo_redo.add_undo_property(current_resource, "max_value", current_resource.max_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_value_interpolation_window_changed(value: float):
	if current_resource and current_resource.value_interpolation_window != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Window Interpolation Window")
		undo_redo.add_do_property(current_resource, "value_interpolation_window", value)
		undo_redo.add_undo_property(current_resource, "value_interpolation_window", current_resource.value_interpolation_window)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_interpolation_type_selected(index: int):
	if current_resource and current_resource.interpolation_type != index:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Window Interpolation Type")
		undo_redo.add_do_property(current_resource, "interpolation_type", index)
		undo_redo.add_undo_property(current_resource, "interpolation_type", current_resource.interpolation_type)
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
		if min_value_spin_box.value != current_resource.min_value:
			min_value_spin_box.value = current_resource.min_value
		if max_value_spin_box.value != current_resource.max_value:
			max_value_spin_box.value = current_resource.max_value
		if value_interpolation_window_spin_box.value != current_resource.value_interpolation_window:
			value_interpolation_window_spin_box.value = current_resource.value_interpolation_window
		if interpolation_type_option_button.selected != current_resource.interpolation_type:
			interpolation_type_option_button.selected = current_resource.interpolation_type
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
		current_resource.emit_changed()
