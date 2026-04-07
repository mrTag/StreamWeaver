@tool
class_name ParameterAddGraphNode extends GraphNode

const LEFT_PORT_INPUT_PARAMETERS : int = 0

var current_resource: StreamWeaverParameterAdd

func initialize(res: StreamWeaverParameterAdd):
	current_resource = res
	_update_ui()

static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverParameterAdd:
	var res := StreamWeaverParameterAdd.new()
	res.resource_name = "Parameter Add %d" % (audio_stream_resource.parameters.size() + 1)
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

func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {
		LEFT_PORT_INPUT_PARAMETERS: Array()
	}
	for param in current_resource.input_parameters:
		connections[LEFT_PORT_INPUT_PARAMETERS].append(param)
	return connections

func connect_to(other: GraphNode, other_port: int, this_port: int, only_check:bool=false) -> bool:
	if this_port == LEFT_PORT_INPUT_PARAMETERS:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res is StreamWeaverParameter:
				if current_resource.input_parameters.has(res):
					return false
				if not only_check:
					current_resource.input_parameters.append(res)
				return true
	return false

func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)

func disconnect_from(other: GraphNode, other_port: int, this_port: int):
	if this_port == LEFT_PORT_INPUT_PARAMETERS:
		if other.has_method("get_resource_for_right_port"):
			var res = other.get_resource_for_right_port(other_port)
			if res:
				current_resource.input_parameters.erase(res)

func _update_ui():
	if current_resource:
		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position
		current_resource.emit_changed()
