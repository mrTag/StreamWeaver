@tool
class_name OutputGranularLinearSweepGraphNode extends GraphNode

# ports are not slots! port indices are only the enabled slots counted from 0.
const LEFT_PORT_INPUT_STREAM : int = 0
const LEFT_PORT_SWEEPING_PARAMETER : int = 1


var current_resource: StreamWeaverOutputGranularLinearSweep

@onready var name_edit : LineEdit = %NameEdit
@onready var min_parameter_value : SpinBox = %MinParameterValue
@onready var max_parameter_value : SpinBox = %MaxParameterValue
@onready var min_grain_size : SpinBox = %MinGrainSize
@onready var max_grain_size : SpinBox = %MaxGrainSize
@onready var grain_jitter_percentage : SpinBox = %GrainJitterPercentage

func _ready():
	name_edit.text_changed.connect(_on_name_changed)
	min_parameter_value.value_changed.connect(_on_min_parameter_value_changed)
	max_parameter_value.value_changed.connect(_on_max_parameter_value_changed)
	min_grain_size.value_changed.connect(_on_min_grain_size_changed)
	max_grain_size.value_changed.connect(_on_max_grain_size_changed)
	grain_jitter_percentage.value_changed.connect(_on_grain_jitter_percentage_changed)
	dragged.connect(_on_dragged)


func initialize(res: StreamWeaverOutputGranularLinearSweep):
	current_resource = res
	
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverOutputGranularLinearSweep:
	var res := StreamWeaverOutputGranularLinearSweep.new()
	res.output_name = "Output Granular Linear Sweep %d"%(audio_stream_resource.outputs.size() + 1)
	res.resource_name = res.output_name
	return res

func add_to_stream_weaver_audio_stream(audio_stream_resource: Resource):
	if audio_stream_resource and "outputs" in audio_stream_resource:
		audio_stream_resource.outputs.append(current_resource)
		audio_stream_resource.emit_changed()

func remove_from_stream_weaver_audio_stream(audio_stream_resource: Resource):
	if audio_stream_resource and "outputs" in audio_stream_resource:
		audio_stream_resource.outputs.erase(current_resource)
		audio_stream_resource.emit_changed()

# this is for the initialization of all connections
# (one side should be enough and we choose left!)
func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {
		LEFT_PORT_INPUT_STREAM: Array(),
		LEFT_PORT_SWEEPING_PARAMETER: Array()
	}
	if current_resource.input_stream:
		connections[LEFT_PORT_INPUT_STREAM].append(current_resource.input_stream)
	if current_resource.sweeping_parameter:
		connections[LEFT_PORT_SWEEPING_PARAMETER].append(current_resource.sweeping_parameter)
	return connections

# this is always evaluated on the input port!
func connect_to(other: GraphNode, other_port: int, this_port: int, only_check:bool=false) -> bool:
	if this_port == LEFT_PORT_INPUT_STREAM:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res is StreamWeaverInputStream:
				if current_resource.input_stream:
					return false
				if not only_check:
					current_resource.input_stream = res
				return true
	elif this_port == LEFT_PORT_SWEEPING_PARAMETER:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res is StreamWeaverParameter:
				if current_resource.sweeping_parameter:
					return false
				if not only_check:
					current_resource.sweeping_parameter = res
				return true
		
	return false

func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)

func disconnect_from(other: GraphNode, other_port: int, this_port: int):
	if this_port == LEFT_PORT_INPUT_STREAM:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res and res == current_resource.input_stream:
				current_resource.input_stream = null
	elif this_port == LEFT_PORT_SWEEPING_PARAMETER:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res == current_resource.sweeping_parameter:
				current_resource.sweeping_parameter = null

func _on_name_changed(_new_name):
	if current_resource and current_resource.output_name != name_edit.text:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Granular Linear Sweep Name")
		undo_redo.add_do_property(current_resource, "output_name", name_edit.text)
		undo_redo.add_do_property(current_resource, "resource_name", name_edit.text)
		undo_redo.add_undo_property(current_resource, "resource_name", current_resource.output_name)
		undo_redo.add_undo_property(current_resource, "output_name", current_resource.output_name)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_min_parameter_value_changed(value: float):
	if current_resource and current_resource.min_parameter_value != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Min Parameter Value")
		undo_redo.add_do_property(current_resource, "min_parameter_value", value)
		undo_redo.add_undo_property(current_resource, "min_parameter_value", current_resource.min_parameter_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_max_parameter_value_changed(value: float):
	if current_resource and current_resource.max_parameter_value != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Max Parameter Value")
		undo_redo.add_do_property(current_resource, "max_parameter_value", value)
		undo_redo.add_undo_property(current_resource, "max_parameter_value", current_resource.max_parameter_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_min_grain_size_changed(value: float):
	if current_resource and current_resource.min_grain_size_milliseconds != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Min Grain Size")
		undo_redo.add_do_property(current_resource, "min_grain_size_milliseconds", value)
		undo_redo.add_undo_property(current_resource, "min_grain_size_milliseconds", current_resource.min_grain_size_milliseconds)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_max_grain_size_changed(value: float):
	if current_resource and current_resource.max_grain_size_milliseconds != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Max Grain Size")
		undo_redo.add_do_property(current_resource, "max_grain_size_milliseconds", value)
		undo_redo.add_undo_property(current_resource, "max_grain_size_milliseconds", current_resource.max_grain_size_milliseconds)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_grain_jitter_percentage_changed(value: float):
	if current_resource and current_resource.grain_jitter_percentage != value / 100.0:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Grain Jitter Percentage")
		undo_redo.add_do_property(current_resource, "grain_jitter_percentage", value / 100.0)
		undo_redo.add_undo_property(current_resource, "grain_jitter_percentage", current_resource.grain_jitter_percentage * 100.0)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from: Vector2, to: Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Output Granular Linear Sweep Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()

func _update_ui():
	if current_resource:
		if name_edit.text != current_resource.output_name:
			name_edit.text = current_resource.output_name
		
		if min_parameter_value.value != current_resource.min_parameter_value:
			min_parameter_value.value = current_resource.min_parameter_value
		if max_parameter_value.value != current_resource.max_parameter_value:
			max_parameter_value.value = current_resource.max_parameter_value
		if min_grain_size.value != current_resource.min_grain_size_milliseconds:
			min_grain_size.value = current_resource.min_grain_size_milliseconds
		if max_grain_size.value != current_resource.max_grain_size_milliseconds:
			max_grain_size.value = current_resource.max_grain_size_milliseconds
		if grain_jitter_percentage.value != current_resource.grain_jitter_percentage:
			grain_jitter_percentage.value = current_resource.grain_jitter_percentage * 100.0
		
		# Important: update position_offset if it changed
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
			
		current_resource.emit_changed()
