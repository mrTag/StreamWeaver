@tool
class_name OutputLoopingGraphNode extends GraphNode

# ports are not slots! port indices are only the enabled slots counted from 0.
const LEFT_PORT_INPUT_STREAM : int = 0
const LEFT_PORT_VOLUME_MULT_PARAM : int = 1
const LEFT_PORT_PITCH_MULT_PARAM : int = 2


var current_resource: StreamWeaverOutputLooping

@onready var name_edit : LineEdit = %NameEdit
@onready var base_volume : SpinBox = %BaseVolume
@onready var base_pitch : SpinBox = %BasePitch

func _ready():
	name_edit.text_changed.connect(_on_name_changed)
	base_volume.value_changed.connect(_on_base_volume_changed)
	base_pitch.value_changed.connect(_on_base_pitch_changed)
	dragged.connect(_on_dragged)


func initialize(res: StreamWeaverOutputLooping):
	current_resource = res
	
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverOutputLooping:
	var res := StreamWeaverOutputLooping.new()
	res.output_name = "Output looping %d"%(audio_stream_resource.outputs.size() + 1)
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
		LEFT_PORT_VOLUME_MULT_PARAM: Array(),
		LEFT_PORT_PITCH_MULT_PARAM: Array()
	}
	connections[LEFT_PORT_INPUT_STREAM].append(current_resource.input_stream)
	if current_resource.volume_multiplier:
		connections[LEFT_PORT_VOLUME_MULT_PARAM].append(current_resource.volume_multiplier)
	if current_resource.pitch_multiplier:
		connections[LEFT_PORT_PITCH_MULT_PARAM].append(current_resource.pitch_multiplier)
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
	elif this_port == LEFT_PORT_VOLUME_MULT_PARAM:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res is StreamWeaverParameter:
				if current_resource.volume_multiplier:
					return false
				if not only_check:
					current_resource.volume_multiplier = res
				return true
	elif this_port == LEFT_PORT_PITCH_MULT_PARAM:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res is StreamWeaverParameter:
				if current_resource.pitch_multiplier:
					return false
				if not only_check:
					current_resource.pitch_multiplier = res
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
	elif this_port == LEFT_PORT_VOLUME_MULT_PARAM:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res == current_resource.volume_multiplier:
				current_resource.volume_multiplier = null
	elif this_port == LEFT_PORT_PITCH_MULT_PARAM:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res == current_resource.pitch_multiplier:
				current_resource.pitch_multiplier = null

func _on_name_changed(_new_name):
	if current_resource and current_resource.output_name != name_edit.text:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Randomize Name")
		undo_redo.add_do_property(current_resource, "output_name", name_edit.text)
		undo_redo.add_do_property(current_resource, "resource_name", name_edit.text)
		undo_redo.add_undo_property(current_resource, "resource_name", current_resource.output_name)
		undo_redo.add_undo_property(current_resource, "output_name", current_resource.output_name)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_base_pitch_changed(value: float):
	if current_resource and current_resource.base_pitch != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Base Pitch")
		undo_redo.add_do_property(current_resource, "base_pitch", value)
		undo_redo.add_undo_property(current_resource, "base_pitch", current_resource.base_pitch)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_base_volume_changed(value: float):
	if current_resource and current_resource.base_volume_db != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Base Volume")
		undo_redo.add_do_property(current_resource, "base_volume_db", value)
		undo_redo.add_undo_property(current_resource, "base_volume_db", current_resource.base_volume_db)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from: Vector2, to: Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Output Randomize Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()

func _update_ui():
	if current_resource:
		if name_edit.text != current_resource.output_name:
			name_edit.text = current_resource.output_name
		
		if base_pitch.value != current_resource.base_pitch:
			base_pitch.value = current_resource.base_pitch
		if base_volume.value != current_resource.base_volume_db:
			base_volume.value = current_resource.base_volume_db
		
		# Important: update position_offset if it changed
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
			
		current_resource.emit_changed()
