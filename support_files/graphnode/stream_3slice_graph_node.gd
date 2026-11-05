@tool
class_name Stream3SliceGraphNode extends GraphNode

const LEFT_PORT_INTRO_STREAM : int = 0
const LEFT_PORT_LOOP_STREAM : int = 1
const LEFT_PORT_OUTRO_STREAM : int = 2
const LEFT_PORT_START_TRIGGER : int = 3
const LEFT_PORT_STOP_TRIGGER : int = 4
const RIGHT_PORT_STREAM_OUT : int = 0

var current_resource: StreamWeaverStream3Slice

@onready var name_edit : LineEdit = %NameEdit
@onready var transition_mode : OptionButton = %TransitionMode
@onready var crossfade_time : SpinBox = %CrossfadeTime
@onready var stop_mode : OptionButton = %StopMode

func _ready() -> void:
	if name_edit:
		name_edit.text_changed.connect(_on_name_changed)
	if transition_mode:
		transition_mode.item_selected.connect(_on_transition_mode_selected)
	if crossfade_time:
		crossfade_time.value_changed.connect(_on_crossfade_time_changed)
	if stop_mode:
		stop_mode.item_selected.connect(_on_stop_mode_selected)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverStream3Slice) -> void:
	current_resource = res
	position_offset = res.graph_node_position
	_update_ui()

func _update_ui() -> void:
	if not current_resource:
		return
	if name_edit:
		name_edit.text = current_resource.resource_name
	if transition_mode:
		transition_mode.selected = current_resource.transition_mode
	if crossfade_time:
		crossfade_time.set_value_no_signal(current_resource.crossfade_time)
	if stop_mode:
		stop_mode.selected = current_resource.stop_mode

func _on_name_changed(new_name: String) -> void:
	if current_resource:
		current_resource.resource_name = new_name
		name = new_name

func _on_transition_mode_selected(idx: int) -> void:
	if current_resource:
		current_resource.transition_mode = idx
		current_resource.emit_changed()

func _on_crossfade_time_changed(val: float) -> void:
	if current_resource:
		current_resource.crossfade_time = val
		current_resource.emit_changed()

func _on_stop_mode_selected(idx: int) -> void:
	if current_resource:
		current_resource.stop_mode = idx
		current_resource.emit_changed()

func _on_dragged(_from: Vector2, to: Vector2) -> void:
	if current_resource:
		current_resource.graph_node_position = to

static func create_new_resource(audio_stream: StreamWeaverAudioStream) -> StreamWeaverStream3Slice:
	var res := StreamWeaverStream3Slice.new()
	res.resource_name = "3-Slice %d" % (audio_stream.streams.size() + 1)
	return res

func add_to_stream_weaver_audio_stream(audio_stream: Resource) -> void:
	if audio_stream and "streams" in audio_stream:
		audio_stream.streams.append(current_resource)
		audio_stream.emit_changed()

func remove_from_stream_weaver_audio_stream(audio_stream: Resource) -> void:
	if audio_stream and "streams" in audio_stream:
		audio_stream.streams.erase(current_resource)
		if "master_streams" in audio_stream:
			audio_stream.master_streams.erase(current_resource)
		audio_stream.emit_changed()

func get_resource_for_right_port(port: int) -> Resource:
	if port == RIGHT_PORT_STREAM_OUT:
		return current_resource
	return null

func get_right_port_for_resource(res: Resource) -> int:
	if res == current_resource:
		return RIGHT_PORT_STREAM_OUT
	return -1

func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {
		LEFT_PORT_INTRO_STREAM: [],
		LEFT_PORT_LOOP_STREAM: [],
		LEFT_PORT_OUTRO_STREAM: [],
		LEFT_PORT_START_TRIGGER: [],
		LEFT_PORT_STOP_TRIGGER: []
	}
	if current_resource:
		if current_resource.intro_stream:
			connections[LEFT_PORT_INTRO_STREAM].append(current_resource.intro_stream)
		if current_resource.loop_stream:
			connections[LEFT_PORT_LOOP_STREAM].append(current_resource.loop_stream)
		if current_resource.outro_stream:
			connections[LEFT_PORT_OUTRO_STREAM].append(current_resource.outro_stream)
		if current_resource.start_trigger:
			connections[LEFT_PORT_START_TRIGGER].append(current_resource.start_trigger)
		if current_resource.stop_trigger:
			connections[LEFT_PORT_STOP_TRIGGER].append(current_resource.stop_trigger)
	return connections

func connect_to(other: GraphNode, other_port: int, this_port: int, only_check: bool = false) -> bool:
	if not other.has_method("get_resource_for_right_port"):
		return false
	var res = other.get_resource_for_right_port(other_port)
	if not res or not current_resource:
		return false

	if this_port == LEFT_PORT_INTRO_STREAM:
		if res is StreamWeaverStream and res != current_resource:
			if current_resource.intro_stream:
				return false
			if not only_check:
				current_resource.intro_stream = res
				current_resource.emit_changed()
			return true
	elif this_port == LEFT_PORT_LOOP_STREAM:
		if res is StreamWeaverStream and res != current_resource:
			if current_resource.loop_stream:
				return false
			if not only_check:
				current_resource.loop_stream = res
				current_resource.emit_changed()
			return true
	elif this_port == LEFT_PORT_OUTRO_STREAM:
		if res is StreamWeaverStream and res != current_resource:
			if current_resource.outro_stream:
				return false
			if not only_check:
				current_resource.outro_stream = res
				current_resource.emit_changed()
			return true
	elif this_port == LEFT_PORT_START_TRIGGER:
		if res is StreamWeaverTrigger:
			if current_resource.start_trigger:
				return false
			if not only_check:
				current_resource.start_trigger = res
				current_resource.emit_changed()
			return true
	elif this_port == LEFT_PORT_STOP_TRIGGER:
		if res is StreamWeaverTrigger:
			if current_resource.stop_trigger:
				return false
			if not only_check:
				current_resource.stop_trigger = res
				current_resource.emit_changed()
			return true
	return false

func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)

func disconnect_from(other: GraphNode, other_port: int, this_port: int) -> void:
	if not other.has_method("get_resource_for_right_port"):
		return
	var res = other.get_resource_for_right_port(other_port)
	if not res or not current_resource:
		return

	if this_port == LEFT_PORT_INTRO_STREAM:
		if current_resource.intro_stream == res:
			current_resource.intro_stream = null
			current_resource.emit_changed()
	elif this_port == LEFT_PORT_LOOP_STREAM:
		if current_resource.loop_stream == res:
			current_resource.loop_stream = null
			current_resource.emit_changed()
	elif this_port == LEFT_PORT_OUTRO_STREAM:
		if current_resource.outro_stream == res:
			current_resource.outro_stream = null
			current_resource.emit_changed()
	elif this_port == LEFT_PORT_START_TRIGGER:
		if current_resource.start_trigger == res:
			current_resource.start_trigger = null
			current_resource.emit_changed()
	elif this_port == LEFT_PORT_STOP_TRIGGER:
		if current_resource.stop_trigger == res:
			current_resource.stop_trigger = null
			current_resource.emit_changed()
