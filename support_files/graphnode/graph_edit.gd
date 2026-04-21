@tool
extends GraphEdit

# Register your node types here
# Key = display name in menu
# Value = PackedScene (GraphNode-based)
var node_types := {
	"Input Stream": preload("res://addons/streamweaver/graphnode/input_stream.tscn"),
	"Input Parameter": preload("res://addons/streamweaver/graphnode/input_parameter.tscn"),
	"Input Trigger": preload("res://addons/streamweaver/graphnode/input_trigger.tscn"),
	"Conditional Parameter Trigger": preload("res://addons/streamweaver/graphnode/conditional_parameter_trigger.tscn"),
	"Conditional Range Trigger": preload("res://addons/streamweaver/graphnode/conditional_parameter_range_trigger.tscn"),
	"Metronome Trigger": preload("res://addons/streamweaver/graphnode/metronome_trigger.tscn"),
	"Delay Trigger": preload("res://addons/streamweaver/graphnode/delay_trigger.tscn"),
	"Output Randomize": preload("res://addons/streamweaver/graphnode/output_randomize.tscn"),
	"Output Looping": preload("res://addons/streamweaver/graphnode/output_loop.tscn"),
	"Output Granular Sweep": preload("res://addons/streamweaver/graphnode/output_granular_linear_sweep.tscn"),
	"Output Granular Database": preload("res://addons/streamweaver/graphnode/output_granular_database.tscn"),
	"Parameter Add": preload("res://addons/streamweaver/graphnode/parameter_add.tscn"),
	"Parameter Multiply": preload("res://addons/streamweaver/graphnode/parameter_multiply.tscn"),
	"Parameter Follow": preload("res://addons/streamweaver/graphnode/parameter_follow.tscn"),
	"Parameter Window": preload("res://addons/streamweaver/graphnode/parameter_window.tscn"),
	"Parameter Remap": preload("res://addons/streamweaver/graphnode/parameter_remap.tscn")
}
func _create_resource_for_node_type(node_type_name: String) -> Resource:
	match node_type_name:
		"Input Stream":
			return InputStreamGraphNode.create_new_resource(current_resource)
		"Input Parameter":
			return InputParameterGraphNode.create_new_resource(current_resource)
		"Input Trigger":
			return InputTriggerGraphNode.create_new_resource(current_resource)
		"Conditional Parameter Trigger":
			return ConditionalParameterTriggerGraphNode.create_new_resource(current_resource)
		"Conditional Range Trigger":
			return ConditionalParameterRangeTriggerGraphNode.create_new_resource(current_resource)
		"Metronome Trigger":
			return MetronomeTriggerGraphNode.create_new_resource(current_resource)
		"Delay Trigger":
			return DelayTriggerGraphNode.create_new_resource(current_resource)
		"Output Randomize":
			return OutputRandomizeGraphNode.create_new_resource(current_resource)
		"Output Looping":
			return OutputLoopingGraphNode.create_new_resource(current_resource)
		"Output Granular Sweep":
			return OutputGranularLinearSweepGraphNode.create_new_resource(current_resource)
		"Output Granular Database":
			return OutputGranularDatabaseGraphNode.create_new_resource(current_resource)
		"Parameter Add":
			return ParameterAddGraphNode.create_new_resource(current_resource)
		"Parameter Multiply":
			return ParameterMultiplyGraphNode.create_new_resource(current_resource)
		"Parameter Follow":
			return ParameterFollowGraphNode.create_new_resource(current_resource)
		"Parameter Window":
			return ParameterWindowGraphNode.create_new_resource(current_resource)
		"Parameter Remap":
			return ParameterRemapGraphNode.create_new_resource(current_resource)
	return null

func _get_node_type_for_resource(res: Resource) -> String:
	if res is StreamWeaverInputStream: return "Input Stream"
	if res is StreamWeaverParameterInput: return "Input Parameter"
	if res is StreamWeaverTriggerInput: return "Input Trigger"
	if res is StreamWeaverTriggerConditionalParameter: return "Conditional Parameter Trigger"
	if res is StreamWeaverTriggerConditionalParameterRange: return "Conditional Range Trigger"
	if res is StreamWeaverTriggerMetronome: return "Metronome Trigger"
	if res is StreamWeaverTriggerDelay: return "Delay Trigger"
	if res is StreamWeaverOutputRandomize: return "Output Randomize"
	if res is StreamWeaverOutputLooping: return "Output Looping"
	if res is StreamWeaverOutputGranularLinearSweep: return "Output Granular Sweep"
	if res is StreamWeaverOutputGranularDatabase: return "Output Granular Database"
	if res is StreamWeaverParameterAdd: return "Parameter Add"
	if res is StreamWeaverParameterMultiply: return "Parameter Multiply"
	if res is StreamWeaverParameterFollowInput: return "Parameter Follow"
	if res is StreamWeaverParameterWindow: return "Parameter Window"
	if res is StreamWeaverParameterRemap: return "Parameter Remap"
	return ""

func _find_node_by_resource(res: Resource) -> GraphNode:
	for child in get_children():
		if child.has_method("get_right_port_for_resource") and child.get_right_port_for_resource(res) != -1:
			return child
		if "current_resource" in child and child.current_resource == res:
			return child
	return null

var popup_menu: PopupMenu
var click_position := Vector2.ZERO
var hovered_node: GraphNode = null
var current_resource: StreamWeaverAudioStream

const DELETE_NODE_ID = 1000

func _ready():
	# Create context menu
	popup_menu = PopupMenu.new()
	get_tree().root.add_child(popup_menu)

	var id := 0
	for name in node_types.keys():
		popup_menu.add_item(name, id)
		id += 1

	popup_menu.id_pressed.connect(_on_popup_id_pressed)
	
	connection_request.connect(_on_connection_request)
	disconnection_request.connect(_on_disconnect_request)

func _exit_tree() -> void:
	if popup_menu:
		popup_menu.queue_free()
		popup_menu = null


func _gui_input(event):
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			# Save graph-space position (important!)
			click_position = get_local_mouse_position()
			
			# Check if hovering a node
			hovered_node = null
			for child in get_children():
				if child is GraphNode:
					var rect = Rect2(child.position_offset * zoom - scroll_offset, child.size * zoom)
					if rect.has_point(click_position):
						hovered_node = child
						break
			
			# Prepare context menu
			popup_menu.clear()
			var id := 0
			for name in node_types.keys():
				popup_menu.add_item("Add " + name, id)
				id += 1
			
			if hovered_node:
				popup_menu.add_separator()
				var node_name := hovered_node.name
				if "current_resource" in hovered_node:
					var res_name : String = hovered_node.current_resource.resource_name
					if res_name != "":
						node_name = res_name
				popup_menu.add_item("Delete " + str(node_name), DELETE_NODE_ID)
			
			popup_menu.popup()
			popup_menu.set_position(get_window().position + Vector2i(get_global_mouse_position()))


func _on_popup_id_pressed(id):
	if id == DELETE_NODE_ID:
		if hovered_node:
			var res = hovered_node.get("current_resource")
			var orginal_node_name := hovered_node.name
			var connections := get_connection_list_from_node(hovered_node.name)
			if res:
				var undo_redo = EditorInterface.get_editor_undo_redo()
				undo_redo.create_action("Delete Node")
				undo_redo.add_do_method(self, "disconnect_connections", connections)
				undo_redo.add_do_method(self, "_remove_resource_and_node", res)
				undo_redo.add_undo_method(self, "_add_resource_and_node", res, orginal_node_name)
				undo_redo.add_undo_method(self, "recreate_connections", connections)
				undo_redo.commit_action()
		return

	var node_type_name = node_types.keys()[id]
	var position_offset = (click_position + scroll_offset) / zoom
	
	var res: Resource = _create_resource_for_node_type(node_type_name)
	
	if res:
		res.graph_node_position = position_offset
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Add " + node_type_name)
		undo_redo.add_do_method(self, "_add_resource_and_node", res, "")
		undo_redo.add_undo_method(self, "_remove_resource_and_node", res)
		undo_redo.commit_action()

func _add_resource_and_node(res: Resource, node_name: String = ""):
	var node_type = _get_node_type_for_resource(res)
	if node_type == "": return
	
	var scene: PackedScene = node_types[node_type]	
	var node_instance = scene.instantiate()
	add_child(node_instance)
	if node_name == "":
		node_instance.name = res.resource_name
	else:
		node_instance.name = node_name
	node_instance.initialize(res)
	if current_resource:
		if node_instance.has_method("add_to_stream_weaver_audio_stream"):
			node_instance.add_to_stream_weaver_audio_stream(current_resource)
		current_resource.emit_changed()

func _remove_resource_and_node(res: Resource):
	for child in get_children():
		if child is GraphNode and child.has_method("initialize"):
			var child_res = child.get("current_resource")
			
			if child_res == res:
				if child.has_method("remove_from_stream_weaver_audio_stream"):
					child.remove_from_stream_weaver_audio_stream(current_resource)
				remove_child(child)
				child.queue_free()
				break
	if current_resource:
		current_resource.emit_changed()

func recreate_connections(connections: Array):
	for connection in connections:
		var node_from = get_node(String(connection["from_node"]))
		var node_to = get_node(String(connection["to_node"]))
		if node_from and node_to:
			node_to.connect_to(node_from, connection["from_port"], connection["to_port"])
			connect_node(connection["from_node"], connection["from_port"], connection["to_node"], connection["to_port"])

func disconnect_connections(connections: Array):
	for connection in connections:
		var node_from = get_node(String(connection["from_node"]))
		var node_to = get_node(String(connection["to_node"]))
		if node_from and node_to:
			node_to.disconnect_from(node_from, connection["from_port"], connection["to_port"])
			disconnect_node(connection["from_node"], connection["from_port"], connection["to_node"], connection["to_port"])

func _on_connection_request(from_node: StringName, from_port: int, to_node: StringName, to_port: int):
	var node_from = get_node(String(from_node))
	var node_to = get_node(String(to_node))
	
	if node_to.has_method("can_connect_to") and node_to.can_connect_to(node_from, from_port, to_port):
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Connect Nodes")
		undo_redo.add_do_method(node_to, "connect_to", node_from, from_port, to_port)
		undo_redo.add_do_method(self, "connect_node", from_node, from_port, to_node, to_port)
		undo_redo.add_undo_method(node_to, "disconnect_from", node_from, from_port, to_port)
		undo_redo.add_undo_method(self, "disconnect_node", from_node, from_port, to_node, to_port)
		undo_redo.commit_action()

func _on_disconnect_request(from_node: StringName, from_port: int, to_node: StringName, to_port: int):
	var node_from = get_node(String(from_node))
	var node_to = get_node(String(to_node))
	
	if node_to.has_method("disconnect_from"):
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Disconnect Nodes")
		undo_redo.add_do_method(node_to, "disconnect_from", node_from, from_port, to_port)
		undo_redo.add_do_method(self, "disconnect_node", from_node, from_port, to_node, to_port)
		undo_redo.add_undo_method(node_to, "connect_to", node_from, from_port, to_port)
		undo_redo.add_undo_method(self, "connect_node", from_node, from_port, to_node, to_port)
		undo_redo.commit_action()


func edit_streamweaver_resource(streamweaver_resource: StreamWeaverAudioStream):
	current_resource = streamweaver_resource
	
	# Clear existing nodes
	for child in get_children():
		if child is GraphNode:
			remove_child(child)
			child.queue_free()
	
	if current_resource == null:
		return
	
	var nodes_to_load = {
		"Input Stream": current_resource.inputs,
		"Input Parameter": current_resource.parameters,
		"Input Trigger": current_resource.triggers,
		"Conditional Parameter Trigger": current_resource.triggers,
		"Conditional Range Trigger": current_resource.triggers,
		"Metronome Trigger": current_resource.triggers,
		"Delay Trigger": current_resource.triggers,
		"Output Randomize": current_resource.outputs,
		"Output Looping": current_resource.outputs,
		"Output Granular Sweep": current_resource.outputs,
		"Output Granular Database": current_resource.outputs,
		"Parameter Add": current_resource.parameters,
		"Parameter Multiply": current_resource.parameters,
		"Parameter Follow": current_resource.parameters,
		"Parameter Window": current_resource.parameters,
		"Parameter Remap": current_resource.parameters
	}
	
	var all_nodes : Array[GraphNode] = []
	
	for type_name in nodes_to_load:
		var scene: PackedScene = node_types[type_name]
		var counter : int = 0
		for res in nodes_to_load[type_name]:
			if _get_node_type_for_resource(res) != type_name:
				continue
			var node_instance = scene.instantiate()
			if res.resource_name == "":
				node_instance.name = res.resource_name
			else:
				node_instance.name = "%s %d" % [ type_name, counter]
			add_child(node_instance)
			node_instance.initialize(res)
			all_nodes.append(node_instance)
			counter += 1
	
	# Restore connections
	for node_to in all_nodes:
		if node_to.has_method("get_left_port_connections"):
			var connections : Dictionary[int,Array] = node_to.get_left_port_connections()
			for port in connections.keys():
				for connection_resource in connections[port]:
					var node_from = _find_node_by_resource(connection_resource)
					if not node_from or not node_from.has_method("get_right_port_for_resource"):
						continue
					var port_from = node_from.get_right_port_for_resource(connection_resource)
					connect_node(node_from.name, port_from, node_to.name, port)
