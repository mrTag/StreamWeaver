@tool
class_name MasterOutputGraphNode extends GraphNode

const LEFT_PORT_STREAMS : int = 0
const LEFT_PORT_VOLUME_PARAM : int = 1

var current_audio_stream: StreamWeaverAudioStream

@onready var base_volume : SpinBox = %BaseVolume

func _ready() -> void:
	if base_volume:
		base_volume.value_changed.connect(_on_base_volume_changed)
	dragged.connect(_on_dragged)

func initialize(audio_stream: StreamWeaverAudioStream) -> void:
	current_audio_stream = audio_stream
	position_offset = audio_stream.master_output_position
	if base_volume:
		base_volume.set_value_no_signal(audio_stream.master_volume_db)

func _on_base_volume_changed(val: float) -> void:
	if current_audio_stream:
		current_audio_stream.master_volume_db = val
		current_audio_stream.emit_changed()

func _on_dragged(_from: Vector2, to: Vector2) -> void:
	if current_audio_stream:
		current_audio_stream.master_output_position = to
		current_audio_stream.emit_changed()

func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {
		LEFT_PORT_STREAMS: [],
		LEFT_PORT_VOLUME_PARAM: []
	}
	if current_audio_stream:
		for s in current_audio_stream.master_streams:
			connections[LEFT_PORT_STREAMS].append(s)
		if current_audio_stream.master_volume_multiplier:
			connections[LEFT_PORT_VOLUME_PARAM].append(current_audio_stream.master_volume_multiplier)
	return connections

func connect_to(other: GraphNode, other_port: int, this_port: int, only_check: bool = false) -> bool:
	if not other.has_method("get_resource_for_right_port"):
		return false
	var res = other.get_resource_for_right_port(other_port)
	if not res:
		return false

	if this_port == LEFT_PORT_STREAMS:
		if res is StreamWeaverStream:
			if current_audio_stream.master_streams.has(res):
				return false
			if not only_check:
				current_audio_stream.master_streams.append(res)
				current_audio_stream.emit_changed()
			return true
	elif this_port == LEFT_PORT_VOLUME_PARAM:
		if res is StreamWeaverParameter:
			if current_audio_stream.master_volume_multiplier:
				return false
			if not only_check:
				current_audio_stream.master_volume_multiplier = res
				current_audio_stream.emit_changed()
			return true
	return false

func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)

func disconnect_from(other: GraphNode, other_port: int, this_port: int) -> void:
	if not other.has_method("get_resource_for_right_port"):
		return
	var res = other.get_resource_for_right_port(other_port)
	if not res or not current_audio_stream:
		return

	if this_port == LEFT_PORT_STREAMS:
		current_audio_stream.master_streams.erase(res)
		current_audio_stream.emit_changed()
	elif this_port == LEFT_PORT_VOLUME_PARAM:
		if current_audio_stream.master_volume_multiplier == res:
			current_audio_stream.master_volume_multiplier = null
			current_audio_stream.emit_changed()
