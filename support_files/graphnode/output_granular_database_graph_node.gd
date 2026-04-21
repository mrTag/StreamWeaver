@tool
class_name OutputGranularDatabaseGraphNode extends GraphNode

# Port 0 is always the volume multiplier. Axis parameter ports start at 1 and
# are built dynamically based on the picked grains database's axes.
const LEFT_PORT_VOLUME_PARAMETER : int = 0
const AXIS_PARAM_PORT_OFFSET : int = 1
const AXIS_SLOT_COLOR := Color(0.81960785, 0.57254905, 0, 1)
const AXIS_SLOT_TYPE : int = 1

var current_resource: StreamWeaverOutputGranularDatabase
var _axis_labels: Array[Label] = []

@onready var name_edit : LineEdit = %NameEdit
@onready var base_volume : SpinBox = %BaseVolume
@onready var pitch_correction : CheckBox = %PitchCorrection
@onready var no_repeat_count : SpinBox = %NoRepeatCount
@onready var candidate_pool_size : SpinBox = %CandidatePoolSize
@onready var search_param_smoothing : SpinBox = %SearchParamSmoothing
@onready var shortlist_score_window : SpinBox = %ShortlistScoreWindow
@onready var continuity_bias : SpinBox = %ContinuityBias
@onready var random_selection_span : SpinBox = %RandomSelectionSpan
@onready var random_walk_step : SpinBox = %RandomWalkStep
@onready var random_walk_damping : SpinBox = %RandomWalkDamping
@onready var preferred_match_bias : SpinBox = %PreferredMatchBias
@onready var database_picker : EditorResourcePicker = %DatabasePicker
@onready var database_label : Label = %DatabaseLabel
@onready var volume_label : Label = %VolumeMultiplierLabel

func _ready():
	name_edit.text_changed.connect(_on_name_changed)
	base_volume.value_changed.connect(_on_base_volume_changed)
	pitch_correction.toggled.connect(_on_pitch_correction_toggled)
	no_repeat_count.value_changed.connect(_on_no_repeat_count_changed)
	candidate_pool_size.value_changed.connect(_on_candidate_pool_size_changed)
	search_param_smoothing.value_changed.connect(_on_search_param_smoothing_changed)
	shortlist_score_window.value_changed.connect(_on_shortlist_score_window_changed)
	continuity_bias.value_changed.connect(_on_continuity_bias_changed)
	random_selection_span.value_changed.connect(_on_random_selection_span_changed)
	random_walk_step.value_changed.connect(_on_random_walk_step_changed)
	random_walk_damping.value_changed.connect(_on_random_walk_damping_changed)
	preferred_match_bias.value_changed.connect(_on_preferred_match_bias_changed)
	database_picker.resource_changed.connect(_on_database_changed)
	dragged.connect(_on_dragged)


func initialize(res: StreamWeaverOutputGranularDatabase):
	current_resource = res
	_rebuild_axis_slots()
	_update_ui()


static func create_new_resource(audio_stream_resource: StreamWeaverAudioStream) -> StreamWeaverOutputGranularDatabase:
	var res := StreamWeaverOutputGranularDatabase.new()
	res.output_name = "Output Granular Database %d" % (audio_stream_resource.outputs.size() + 1)
	res.resource_name = res.output_name
	return res


func add_to_stream_weaver_audio_stream(audio_stream_resource: Resource):
	if audio_stream_resource and "outputs" in audio_stream_resource:
		audio_stream_resource.outputs.append(current_resource)
		audio_stream_resource.emit_changed()


func remove_from_stream_weaver_audio_stream(audio_stream_resource: Resource):
	if audio_stream_resource and "outputs" in audio_stream_resource:
		audio_stream_resource.outputs.erase(current_resource)
		audio_stream_resource.emit_changed()


func get_left_port_connections() -> Dictionary[int, Array]:
	var connections : Dictionary[int, Array] = {}
	connections[LEFT_PORT_VOLUME_PARAMETER] = Array()
	if current_resource.volume_multiplier:
		connections[LEFT_PORT_VOLUME_PARAMETER].append(current_resource.volume_multiplier)

	var axis_params = current_resource.axis_parameters
	for i in range(axis_params.size()):
		var port := AXIS_PARAM_PORT_OFFSET + i
		connections[port] = Array()
		if axis_params[i] != null:
			connections[port].append(axis_params[i])
	return connections


func connect_to(other: GraphNode, other_port: int, this_port: int, only_check: bool = false) -> bool:
	if not other.has_method("get_resource_for_right_port"):
		return false
	var res = other.get_resource_for_right_port(other_port)
	if not (res is StreamWeaverParameter):
		return false

	if this_port == LEFT_PORT_VOLUME_PARAMETER:
		if current_resource.volume_multiplier:
			return false
		if not only_check:
			current_resource.volume_multiplier = res
		return true

	var axis_idx := this_port - AXIS_PARAM_PORT_OFFSET
	var axis_params = current_resource.axis_parameters
	if axis_idx < 0 or axis_idx >= axis_params.size():
		return false
	if axis_params[axis_idx] != null:
		return false
	if not only_check:
		axis_params[axis_idx] = res
		current_resource.axis_parameters = axis_params
	return true


func can_connect_to(other: GraphNode, other_port: int, this_port: int) -> bool:
	return connect_to(other, other_port, this_port, true)


func disconnect_from(other: GraphNode, other_port: int, this_port: int):
	if not other.has_method("get_resource_for_right_port"):
		return
	var res = other.get_resource_for_right_port(other_port)

	if this_port == LEFT_PORT_VOLUME_PARAMETER:
		if res == current_resource.volume_multiplier:
			current_resource.volume_multiplier = null
		return

	var axis_idx := this_port - AXIS_PARAM_PORT_OFFSET
	var axis_params = current_resource.axis_parameters
	if axis_idx >= 0 and axis_idx < axis_params.size() and res == axis_params[axis_idx]:
		axis_params[axis_idx] = null
		current_resource.axis_parameters = axis_params


func _on_name_changed(_new_name):
	if current_resource and current_resource.output_name != name_edit.text:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Granular Database Name")
		undo_redo.add_do_property(current_resource, "output_name", name_edit.text)
		undo_redo.add_do_property(current_resource, "resource_name", name_edit.text)
		undo_redo.add_undo_property(current_resource, "resource_name", current_resource.output_name)
		undo_redo.add_undo_property(current_resource, "output_name", current_resource.output_name)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()


func _on_base_volume_changed(value: float):
	if current_resource and current_resource.base_volume_db != value:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Change Output Base Volume")
		undo_redo.add_do_property(current_resource, "base_volume_db", value)
		undo_redo.add_undo_property(current_resource, "base_volume_db", current_resource.base_volume_db)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()


func _on_pitch_correction_toggled(pressed: bool):
	if current_resource and current_resource.pitch_correction_enabled != pressed:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Toggle Pitch Correction")
		undo_redo.add_do_property(current_resource, "pitch_correction_enabled", pressed)
		undo_redo.add_undo_property(current_resource, "pitch_correction_enabled", current_resource.pitch_correction_enabled)
		undo_redo.add_do_method(self, "_update_ui")
		undo_redo.add_undo_method(self, "_update_ui")
		undo_redo.commit_action()


func _commit_numeric_property(action_name: String, property_name: StringName, value):
	if current_resource == null:
		return
	if current_resource.get(property_name) == value:
		return
	var undo_redo = EditorInterface.get_editor_undo_redo()
	undo_redo.create_action(action_name)
	undo_redo.add_do_property(current_resource, property_name, value)
	undo_redo.add_undo_property(current_resource, property_name, current_resource.get(property_name))
	undo_redo.add_do_method(self, "_update_ui")
	undo_redo.add_undo_method(self, "_update_ui")
	undo_redo.commit_action()


func _on_no_repeat_count_changed(value: float):
	_commit_numeric_property("Change No-Repeat Count", "no_repeat_count", int(value))


func _on_candidate_pool_size_changed(value: float):
	_commit_numeric_property("Change Candidate Pool Size", "candidate_pool_size", int(value))


func _on_search_param_smoothing_changed(value: float):
	_commit_numeric_property("Change Search Param Smoothing", "search_param_smoothing", value)


func _on_shortlist_score_window_changed(value: float):
	_commit_numeric_property("Change Shortlist Score Window", "shortlist_score_window", value)


func _on_continuity_bias_changed(value: float):
	_commit_numeric_property("Change Continuity Bias", "continuity_bias", value)


func _on_random_selection_span_changed(value: float):
	_commit_numeric_property("Change Random Selection Span", "random_selection_span", int(value))


func _on_random_walk_step_changed(value: float):
	_commit_numeric_property("Change Random Walk Step", "random_walk_step", value)


func _on_random_walk_damping_changed(value: float):
	_commit_numeric_property("Change Random Walk Damping", "random_walk_damping", value)


func _on_preferred_match_bias_changed(value: float):
	_commit_numeric_property("Change Preferred Match Bias", "preferred_match_bias", value)


func _on_database_changed(resource):
	if current_resource == null:
		return
	if current_resource.grains_database == resource:
		return

	var new_db : StreamWeaverGrainsDatabase = resource as StreamWeaverGrainsDatabase
	var old_db := current_resource.grains_database
	var old_axis_params = current_resource.axis_parameters.duplicate()

	# Build a new axis_parameters array sized to the new database, preserving
	# existing entries where the axis index still exists.
	var new_num_axes : int = new_db.get_num_axes() if new_db else 0
	var new_axis_params : Array[StreamWeaverParameter] = []
	for i in range(new_num_axes):
		if i < old_axis_params.size():
			new_axis_params.append(old_axis_params[i])
		else:
			new_axis_params.append(null)

	var undo_redo = EditorInterface.get_editor_undo_redo()
	undo_redo.create_action("Change Grains Database")
	undo_redo.add_do_property(current_resource, "grains_database", new_db)
	undo_redo.add_do_property(current_resource, "axis_parameters", new_axis_params)
	undo_redo.add_undo_property(current_resource, "axis_parameters", old_axis_params)
	undo_redo.add_undo_property(current_resource, "grains_database", old_db)
	undo_redo.add_do_method(self, "_rebuild_axis_slots_and_reconnect")
	undo_redo.add_do_method(self, "_update_ui")
	undo_redo.add_undo_method(self, "_rebuild_axis_slots_and_reconnect")
	undo_redo.add_undo_method(self, "_update_ui")
	undo_redo.commit_action()


func _on_dragged(_from: Vector2, to: Vector2):
	if current_resource and current_resource.graph_node_position != to:
		var undo_redo = EditorInterface.get_editor_undo_redo()
		undo_redo.create_action("Move Output Granular Database Node")
		undo_redo.add_do_property(current_resource, "graph_node_position", to)
		undo_redo.add_undo_property(current_resource, "graph_node_position", current_resource.graph_node_position)
		undo_redo.add_do_property(self, "position_offset", to)
		undo_redo.add_undo_property(self, "position_offset", current_resource.graph_node_position)
		undo_redo.commit_action()


func _rebuild_axis_slots():
	# Remove previously added dynamic axis labels so the base scene's slot
	# layout is the only thing left before we append new labels.
	for label in _axis_labels:
		if is_instance_valid(label):
			remove_child(label)
			label.queue_free()
	_axis_labels.clear()

	if current_resource == null:
		return

	var db : StreamWeaverGrainsDatabase = current_resource.grains_database
	var num_axes : int = db.get_num_axes() if db else 0

	for i in range(num_axes):
		var label := Label.new()
		label.text = "%s Parameter" % db.get_axis_name(i)
		add_child(label)
		_axis_labels.append(label)
		var slot_idx := label.get_index()
		set_slot(slot_idx, true, AXIS_SLOT_TYPE, AXIS_SLOT_COLOR,
				false, 0, Color(1, 1, 1, 1), null, null, true)


func _rebuild_axis_slots_and_reconnect():
	var parent_edit := get_parent()
	var graph_edit : GraphEdit = parent_edit if parent_edit is GraphEdit else null

	# Drop all connections touching this node before the slot layout changes,
	# since port indices will shift.
	if graph_edit:
		var to_drop : Array = graph_edit.get_connection_list_from_node(name)
		for conn in to_drop:
			graph_edit.disconnect_node(conn.from_node, conn.from_port, conn.to_node, conn.to_port)

	_rebuild_axis_slots()

	# Re-establish connections that are still valid against the new slot layout.
	if graph_edit:
		var valid := get_left_port_connections()
		for port in valid.keys():
			for connection_resource in valid[port]:
				var from_node : GraphNode = graph_edit._find_node_by_resource(connection_resource)
				if from_node == null or not from_node.has_method("get_right_port_for_resource"):
					continue
				var from_port : int = from_node.get_right_port_for_resource(connection_resource)
				graph_edit.connect_node(from_node.name, from_port, name, port)


func _update_ui():
	if current_resource:
		if name_edit.text != current_resource.output_name:
			name_edit.text = current_resource.output_name

		if base_volume.value != current_resource.base_volume_db:
			base_volume.value = current_resource.base_volume_db

		pitch_correction.set_pressed_no_signal(current_resource.pitch_correction_enabled)
		if no_repeat_count.value != current_resource.no_repeat_count:
			no_repeat_count.value = current_resource.no_repeat_count
		if candidate_pool_size.value != current_resource.candidate_pool_size:
			candidate_pool_size.value = current_resource.candidate_pool_size
		if search_param_smoothing.value != current_resource.search_param_smoothing:
			search_param_smoothing.value = current_resource.search_param_smoothing
		if shortlist_score_window.value != current_resource.shortlist_score_window:
			shortlist_score_window.value = current_resource.shortlist_score_window
		if continuity_bias.value != current_resource.continuity_bias:
			continuity_bias.value = current_resource.continuity_bias
		if random_selection_span.value != current_resource.random_selection_span:
			random_selection_span.value = current_resource.random_selection_span
		if random_walk_step.value != current_resource.random_walk_step:
			random_walk_step.value = current_resource.random_walk_step
		if random_walk_damping.value != current_resource.random_walk_damping:
			random_walk_damping.value = current_resource.random_walk_damping
		if preferred_match_bias.value != current_resource.preferred_match_bias:
			preferred_match_bias.value = current_resource.preferred_match_bias

		if database_picker.edited_resource != current_resource.grains_database:
			database_picker.edited_resource = current_resource.grains_database

		var db = current_resource.grains_database
		if db and db.get_grain_count() > 0:
			database_label.text = "Database: %d grains, %d axes" % [db.get_grain_count(), db.get_num_axes()]
		elif db:
			database_label.text = "Database: (empty)"
		else:
			database_label.text = "Database: (none)"

		if position_offset != current_resource.graph_node_position:
			position_offset = current_resource.graph_node_position

		current_resource.emit_changed()
