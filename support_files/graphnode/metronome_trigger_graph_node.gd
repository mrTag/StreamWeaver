@tool
class_name MetronomeTriggerGraphNode extends GraphNode

const LEFT_PORT_MULTIPLIER_PARAMETER : int = 0

var current_resource: StreamWeaverTriggerMetronome

@onready var base_trigger_rate_spin_box : SpinBox = %BaseTriggerRateSpinBox

func _ready():
	base_trigger_rate_spin_box.value_changed.connect(_on_base_trigger_rate_changed)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverTriggerMetronome):
	current_resource = res
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverTriggerMetronome:
	var res := StreamWeaverTriggerMetronome.new()
	res.resource_name = "Metronome Trigger %d" % (audio_stream_resource.triggers.size() + 1)
	res.base_trigger_rate = 1.0
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

func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {
		LEFT_PORT_MULTIPLIER_PARAMETER: Array()
	}
	if current_resource.multiplier_parameter:
		connections[LEFT_PORT_MULTIPLIER_PARAMETER].append(current_resource.multiplier_parameter)
	return connections

func connect_to(other: GraphNode, other_port: int, this_port: int, only_check:bool=false) -> bool:
	if not other.has_method("get_resource_for_right_port"):
		return false
	var res = other.get_resource_for_right_port(other_port)
	print("connect_to: ", res, " ", other_port, " ", this_port)
	if this_port == LEFT_PORT_MULTIPLIER_PARAMETER:
		if res is StreamWeaverParameter:
			if current_resource.multiplier_parameter == res:
				return false
			if not only_check:
				print("setting resource")
				current_resource.multiplier_parameter = res
			return true
	return false

func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)

func disconnect_from(other: GraphNode, other_port: int, this_port: int):
	if not other.has_method("get_resource_for_right_port"):
		return
	var res = other.get_resource_for_right_port(other_port)
	
	if this_port == LEFT_PORT_MULTIPLIER_PARAMETER:
		if res == current_resource.multiplier_parameter:
			current_resource.multiplier_parameter = null

func _on_base_trigger_rate_changed(value: float):
	if current_resource and current_resource.base_trigger_rate != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Metronome Base Trigger Rate")
		undo_redo.add_do_property(current_resource, "base_trigger_rate", value)
		undo_redo.add_undo_property(current_resource, "base_trigger_rate", current_resource.base_trigger_rate)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()

func _on_dragged(_from: Vector2, to: Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Metronome Trigger Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()

func _update_ui():
	if current_resource:
		if base_trigger_rate_spin_box.value != current_resource.base_trigger_rate:
			base_trigger_rate_spin_box.value = current_resource.base_trigger_rate
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
		current_resource.emit_changed()
