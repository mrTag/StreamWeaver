@tool
class_name InputParameterGraphNode extends GraphNode

var current_resource: StreamWeaverParameterInput

@onready var name_edit = %NameEdit
@onready var min_spin_box = %MinSpinBox
@onready var max_spin_box = %MaxSpinBox
@onready var start_spin_box = %StartSpinBox

func _ready():
	name_edit.text_changed.connect(_on_name_changed)
	min_spin_box.value_changed.connect(_on_min_value_changed)
	max_spin_box.value_changed.connect(_on_max_value_changed)
	start_spin_box.value_changed.connect(_on_start_value_changed)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverParameter):
	current_resource = res
	
	name_edit.text = current_resource.parameter_name
	min_spin_box.value = current_resource.min_value
	max_spin_box.value = current_resource.max_value
	start_spin_box.value = current_resource.start_value
	position_offset = current_resource.graph_node_position

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverParameterInput:
	var res := StreamWeaverParameterInput.new()
	res.parameter_name = "Parameter %d"%(audio_stream_resource.parameters.size() + 1)
	res.resource_name = res.parameter_name
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

func _on_name_changed(_new_name):
	if current_resource and current_resource.parameter_name != name_edit.text:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Name")
		undo_redo.add_do_property(current_resource, "parameter_name", name_edit.text)
		undo_redo.add_do_property(current_resource, "resource_name", name_edit.text)
		undo_redo.add_undo_property(current_resource, "resource_name", current_resource.parameter_name)
		undo_redo.add_undo_property(current_resource, "parameter_name", current_resource.parameter_name)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_min_value_changed(val: float):
	if current_resource and current_resource.min_value != val:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Min Value")
		undo_redo.add_do_property(current_resource, "min_value", val)
		undo_redo.add_undo_property(current_resource, "min_value", current_resource.min_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_max_value_changed(val: float):
	if current_resource and current_resource.max_value != val:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Max Value")
		undo_redo.add_do_property(current_resource, "max_value", val)
		undo_redo.add_undo_property(current_resource, "max_value", current_resource.max_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_start_value_changed(val: float):
	if current_resource and current_resource.start_value != val:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Parameter Start Value")
		undo_redo.add_do_property(current_resource, "start_value", val)
		undo_redo.add_undo_property(current_resource, "start_value", current_resource.start_value)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from:Vector2, to:Vector2):
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
		if name_edit.text != current_resource.parameter_name:
			name_edit.text = current_resource.parameter_name
		if min_spin_box.value != current_resource.min_value:
			min_spin_box.value = current_resource.min_value
		if max_spin_box.value != current_resource.max_value:
			max_spin_box.value = current_resource.max_value
		if start_spin_box.value != current_resource.start_value:
			start_spin_box.value = current_resource.start_value
		current_resource.emit_changed()
