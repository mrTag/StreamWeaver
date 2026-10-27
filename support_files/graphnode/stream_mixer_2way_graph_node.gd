@tool
class_name StreamMixer2WayGraphNode extends GraphNode

const LEFT_PORT_STREAM_A : int = 0
const LEFT_PORT_STREAM_B : int = 1
const LEFT_PORT_MIX_PARAM : int = 2
const RIGHT_PORT_STREAM_OUT : int = 0

var current_resource: StreamWeaverStreamMixer2Way

@onready var name_edit : LineEdit = %NameEdit
@onready var mix_value : SpinBox = %MixValue
@onready var blend_mode : OptionButton = %BlendMode

func _ready() -> void:
	if name_edit:
		name_edit.text_changed.connect(_on_name_changed)
	if mix_value:
		mix_value.value_changed.connect(_on_mix_value_changed)
	if blend_mode:
		blend_mode.item_selected.connect(_on_blend_mode_selected)
	dragged.connect(_on_dragged)

func initialize(res: StreamWeaverStreamMixer2Way) -> void:
	current_resource = res
	position_offset = res.graph_node_position
	_update_ui()

func _update_ui() -> void:
	if not current_resource:
		return
	if name_edit:
		name_edit.text = current_resource.resource_name
	if mix_value:
		mix_value.set_value_no_signal(current_resource.mix_value)
	if blend_mode:
		blend_mode.selected = current_resource.blend_mode

func _on_name_changed(new_name: String) -> void:
	if current_resource:
		current_resource.resource_name = new_name
		name = new_name

func _on_mix_value_changed(val: float) -> void:
	if current_resource:
		current_resource.mix_value = val
		current_resource.emit_changed()

func _on_blend_mode_selected(idx: int) -> void:
	if current_resource:
		current_resource.blend_mode = idx
		current_resource.emit_changed()

func _on_dragged(_from: Vector2, to: Vector2) -> void:
	if current_resource:
		current_resource.graph_node_position = to

static func create_new_resource(audio_stream: StreamWeaverAudioStream) -> StreamWeaverStreamMixer2Way:
	var res := StreamWeaverStreamMixer2Way.new()
	res.resource_name = "Mixer %d" % (audio_stream.streams.size() + 1)
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
		LEFT_PORT_STREAM_A: [],
		LEFT_PORT_STREAM_B: [],
		LEFT_PORT_MIX_PARAM: []
	}
	if current_resource:
		if current_resource.input_stream_a:
			connections[LEFT_PORT_STREAM_A].append(current_resource.input_stream_a)
		if current_resource.input_stream_b:
			connections[LEFT_PORT_STREAM_B].append(current_resource.input_stream_b)
		if current_resource.mix_parameter:
			connections[LEFT_PORT_MIX_PARAM].append(current_resource.mix_parameter)
	return connections

func connect_to(other: GraphNode, other_port: int, this_port: int, only_check: bool = false) -> bool:
	if not other.has_method("get_resource_for_right_port"):
		return false
	var res = other.get_resource_for_right_port(other_port)
	if not res or not current_resource:
		return false

	if this_port == LEFT_PORT_STREAM_A:
		if res is StreamWeaverStream and res != current_resource:
			if current_resource.input_stream_a:
				return false
			if not only_check:
				current_resource.input_stream_a = res
				current_resource.emit_changed()
			return true
	elif this_port == LEFT_PORT_STREAM_B:
		if res is StreamWeaverStream and res != current_resource:
			if current_resource.input_stream_b:
				return false
			if not only_check:
				current_resource.input_stream_b = res
				current_resource.emit_changed()
			return true
	elif this_port == LEFT_PORT_MIX_PARAM:
		if res is StreamWeaverParameter:
			if current_resource.mix_parameter:
				return false
			if not only_check:
				current_resource.mix_parameter = res
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

	if this_port == LEFT_PORT_STREAM_A:
		if current_resource.input_stream_a == res:
			current_resource.input_stream_a = null
			current_resource.emit_changed()
	elif this_port == LEFT_PORT_STREAM_B:
		if current_resource.input_stream_b == res:
			current_resource.input_stream_b = null
			current_resource.emit_changed()
	elif this_port == LEFT_PORT_MIX_PARAM:
		if current_resource.mix_parameter == res:
			current_resource.mix_parameter = null
			current_resource.emit_changed()
