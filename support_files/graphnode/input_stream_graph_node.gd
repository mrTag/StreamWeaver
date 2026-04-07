@tool
class_name InputStreamGraphNode extends GraphNode

var current_resource: StreamWeaverInputStream

@onready var name_edit = %NameEdit
@onready var resource_picker = %ResourcePicker

func _ready():
	name_edit.text_changed.connect(_on_name_changed)
	resource_picker.resource_changed.connect(_on_resource_changed)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverInputStream):
	current_resource = res
	
	name_edit.text = current_resource.input_name
	resource_picker.edited_resource = current_resource.audio_stream
	position_offset = current_resource.graph_node_position

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverInputStream:
	var res := StreamWeaverInputStream.new()
	res.input_name = "Input %d"%(audio_stream_resource.inputs.size() + 1)
	res.resource_name = res.input_name
	return res

func add_to_stream_weaver_audio_stream(audio_stream_resource: StreamWeaverAudioStream):
	if audio_stream_resource and "inputs" in audio_stream_resource:
		audio_stream_resource.inputs.append(current_resource)
		audio_stream_resource.emit_changed()

func remove_from_stream_weaver_audio_stream(audio_stream_resource: StreamWeaverAudioStream):
	if audio_stream_resource and "inputs" in audio_stream_resource:
		audio_stream_resource.inputs.erase(current_resource)
		audio_stream_resource.emit_changed()

func get_right_port_for_resource(res: Resource) -> int:
	if res == current_resource:
		return 0
	return -1

func get_resource_for_right_port(_port: int) -> Resource:
	return current_resource

func _on_name_changed(_new_name):
	if current_resource and current_resource.input_name != name_edit.text:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Input Stream Name")
		undo_redo.add_do_property(current_resource, "input_name", name_edit.text)
		undo_redo.add_do_property(current_resource, "resource_name", name_edit.text)
		undo_redo.add_undo_property(current_resource, "resource_name", current_resource.input_name)
		undo_redo.add_undo_property(current_resource, "input_name", current_resource.input_name)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_resource_changed(resource: AudioStream):
	if current_resource and current_resource.audio_stream != resource:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Input Stream Audio")
		undo_redo.add_do_property(current_resource, "audio_stream", resource)
		undo_redo.add_undo_property(current_resource, "audio_stream", current_resource.audio_stream)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from:Vector2, to:Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Input Stream Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()

func _update_ui():
	if current_resource:
		if name_edit.text != current_resource.input_name:
			name_edit.text = current_resource.input_name
		if resource_picker.edited_resource != current_resource.audio_stream:
			resource_picker.edited_resource = current_resource.audio_stream
		current_resource.emit_changed()
