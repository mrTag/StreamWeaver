@tool
class_name InputTriggerGraphNode extends GraphNode

var current_resource: StreamWeaverTriggerInput

@onready var name_edit = %NameEdit

func _ready():
	name_edit.text_changed.connect(_on_name_changed)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverTrigger):
	current_resource = res
	
	name_edit.text = current_resource.trigger_name
	position_offset = current_resource.graph_node_position

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverTriggerInput:
	var res := StreamWeaverTriggerInput.new()
	res.trigger_name = "Trigger %d"%(audio_stream_resource.triggers.size() + 1)
	res.resource_name = res.trigger_name
	return res

func add_to_stream_weaver_audio_stream(audio_stream_resource: StreamWeaverAudioStream):
	if audio_stream_resource and "triggers" in audio_stream_resource:
		audio_stream_resource.triggers.append(current_resource)
		audio_stream_resource.emit_changed()

func remove_from_stream_weaver_audio_stream(audio_stream_resource: StreamWeaverAudioStream):
	if audio_stream_resource and "triggers" in audio_stream_resource:
		audio_stream_resource.triggers.erase(current_resource)
		audio_stream_resource.emit_changed()

func get_right_port_for_resource(res: Resource) -> int:
	if res == current_resource:
		return 0
	return -1

func get_resource_for_right_port(_port: int) -> Resource:
	return current_resource

func _on_name_changed(_new_name):
	if current_resource and current_resource.trigger_name != name_edit.text:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Trigger Name")
		undo_redo.add_do_property(current_resource, "trigger_name", name_edit.text)
		undo_redo.add_do_property(current_resource, "resource_name", name_edit.text)
		undo_redo.add_undo_property(current_resource, "resource_name", current_resource.trigger_name)
		undo_redo.add_undo_property(current_resource, "trigger_name", current_resource.trigger_name)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from:Vector2, to:Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Trigger Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()

func _update_ui():
	if current_resource:
		if name_edit.text != current_resource.trigger_name:
			name_edit.text = current_resource.trigger_name
		current_resource.emit_changed()
