@tool
class_name PlayTriggerGraphNode extends GraphNode

var current_audio_stream: StreamWeaverAudioStream

var current_resource: Resource:
	get:
		return current_audio_stream.play_trigger if current_audio_stream else null

func _ready() -> void:
	dragged.connect(_on_dragged)

func initialize(audio_stream: StreamWeaverAudioStream) -> void:
	current_audio_stream = audio_stream
	if current_audio_stream:
		current_audio_stream.ensure_play_trigger()
		if current_audio_stream.play_trigger:
			position_offset = current_audio_stream.play_trigger.graph_node_position

func _on_dragged(_from: Vector2, to: Vector2) -> void:
	if current_audio_stream and current_audio_stream.play_trigger:
		current_audio_stream.play_trigger.graph_node_position = to
		current_audio_stream.emit_changed()

func get_right_port_for_resource(res: Resource) -> int:
	if current_audio_stream and current_audio_stream.play_trigger == res:
		return 0
	return -1

func get_resource_for_right_port(port: int) -> Resource:
	if port == 0 and current_audio_stream:
		return current_audio_stream.play_trigger
	return null
