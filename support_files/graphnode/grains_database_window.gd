@tool
extends Window

# Editor tool window for building/editing a StreamWeaverGrainsDatabase.
# Pick an AudioStream, render a spectrogram via StreamWeaverFFT, click multiple
# harmonics (with their order relative to the fundamental) to constrain a
# Viterbi-based fundamental tracker, optionally sketch a hint curve to refine
# it, and audition the isolated reconstruction.

const DEFAULT_FFT_SIZE := 2048
const DEFAULT_DB_FLOOR := -80.0
const DEFAULT_Y_RESOLUTION := 512
const DEFAULT_X_RES_PER_SEC := 200.0
const DEFAULT_BANDWIDTH_HZ := 50.0

const DEFAULT_MIN_HZ := 20.0
const DEFAULT_MAX_HZ := 4000.0

const DEFAULT_DISPLAY_DB_MIN := -80.0
const DEFAULT_DISPLAY_DB_MAX := 0.0
const DEFAULT_GAMMA := 1.0

const DEFAULT_MAX_SLOPE := 2000.0
const DEFAULT_TRANSITION_PENALTY := 5.0
const DEFAULT_SKETCH_WEIGHT := 1.0

const PICK_HIT_RADIUS_PX := 10.0

var _fft: StreamWeaverFFT
var _stream: AudioStream

# Each pick: { "order": int, "hz": float }. Reference time is _picks[0]'s
# implicit time; new picks snap their x to the first pick's time.
var _picks: Array = []
var _reference_time := -1.0
var _dragging_pick_index := -1
var _selected_pick_index := -1

# Free-form sketch over the spectrogram, used as a hint for the second tracker
# pass. _sketch_points is the active (in-progress) stroke; _sketch_completed_strokes
# holds all previously committed strokes. Each stroke stores (time, hz) pairs.
var _sketch_points: PackedVector2Array
var _sketch_completed_strokes: Array[PackedVector2Array] = []
var _sketching := false
var _sketch_mode := false

var _tracked_curve: PackedVector2Array

# Threading state for background analysis.
var _analysis_thread: Thread
var _analysis_result: StreamWeaverFFT
var _analyzing := false
var _dot_count := 0
var _dot_timer := 0.0

var _stream_picker: EditorResourcePicker
var _fft_size_spin: SpinBox
var _y_res_spin: SpinBox
var _x_res_spin: SpinBox
var _db_floor_spin: SpinBox
var _min_hz_spin: SpinBox
var _max_hz_spin: SpinBox
var _analyze_btn: Button

var _display_db_min_spin: SpinBox
var _display_db_max_spin: SpinBox
var _gamma_spin: SpinBox
var _per_frame_check: CheckBox
var _whiten_check: CheckBox
var _autofit_btn: Button

var _bandwidth_spin: SpinBox
var _max_slope_spin: SpinBox
var _transition_penalty_spin: SpinBox
var _track_btn: Button
var _play_btn: Button
var _status_label: Label

var _picks_list: ItemList
var _order_spin: SpinBox
var _delete_pick_btn: Button
var _clear_picks_btn: Button
var _sketch_mode_btn: Button
var _clear_sketch_btn: Button
var _sketch_weight_spin: SpinBox

var _spectrogram_view: TextureRect
var _overlay: Control
var _player: AudioStreamPlayer

# --- Step 2: Grain Extraction UI ---
var _extract_section: VBoxContainer
var _cycles_spin: SpinBox
var _crossfade_spin: SpinBox
var _energy_thresh_spin: SpinBox
var _extract_btn: Button
var _save_db_btn: Button
var _extract_status_label: Label

# Parameter axis definitions
var _axis_container: VBoxContainer
var _add_axis_btn: Button
var _axis_rows: Array = [] # Array of { "row": HBoxContainer, "name": LineEdit, "derived": CheckBox, "min": SpinBox, "max": SpinBox, "cal_a_hz": SpinBox, "cal_a_val": SpinBox, "cal_b_hz": SpinBox, "cal_b_val": SpinBox, "keyframes": PackedVector2Array }

var _grains_database: StreamWeaverGrainsDatabase
var _grain_boundaries: PackedVector2Array # (start_time, end_time) pairs for overlay


func _ready() -> void:
	title = "StreamWeaver Grains Database"
	size = Vector2i(1320, 820)
	min_size = Vector2i(800, 560)
	close_requested.connect(_on_close_requested)

	var root := VBoxContainer.new()
	root.anchor_right = 1.0
	root.anchor_bottom = 1.0
	root.offset_left = 8
	root.offset_top = 8
	root.offset_right = -8
	root.offset_bottom = -8
	add_child(root)

	# --- Row 1: audio + analysis params ------------------------------
	var row1 := HBoxContainer.new()
	root.add_child(row1)

	row1.add_child(_make_label("Audio:"))
	_stream_picker = EditorResourcePicker.new()
	_stream_picker.base_type = "AudioStream"
	_stream_picker.custom_minimum_size.x = 220
	_stream_picker.resource_changed.connect(_on_stream_changed)
	row1.add_child(_stream_picker)

	row1.add_child(_make_label("  FFT:"))
	_fft_size_spin = _make_spin(256, 16384, 256, DEFAULT_FFT_SIZE)
	row1.add_child(_fft_size_spin)

	row1.add_child(_make_label("  Y res:"))
	_y_res_spin = _make_spin(64, 2048, 64, DEFAULT_Y_RESOLUTION)
	row1.add_child(_y_res_spin)

	row1.add_child(_make_label("  X/sec:"))
	_x_res_spin = _make_spin(20, 2000, 10, DEFAULT_X_RES_PER_SEC)
	row1.add_child(_x_res_spin)

	row1.add_child(_make_label("  dB floor:"))
	_db_floor_spin = _make_spin(-140, -20, 1, DEFAULT_DB_FLOOR)
	row1.add_child(_db_floor_spin)

	_analyze_btn = Button.new()
	_analyze_btn.text = "Analyze"
	_analyze_btn.pressed.connect(_on_analyze_pressed)
	row1.add_child(_analyze_btn)

	# --- Row 1b: Hz window -------------------------------------------
	var row1b := HBoxContainer.new()
	root.add_child(row1b)

	row1b.add_child(_make_label("Min Hz:"))
	_min_hz_spin = _make_spin(0, 48000, 10, DEFAULT_MIN_HZ)
	row1b.add_child(_min_hz_spin)

	row1b.add_child(_make_label("  Max Hz:"))
	_max_hz_spin = _make_spin(50, 48000, 50, DEFAULT_MAX_HZ)
	row1b.add_child(_max_hz_spin)

	row1b.add_child(_make_label("  (narrow window → higher resolution via zoom FFT)"))

	# --- Row 2: display transform ------------------------------------
	var row2 := HBoxContainer.new()
	root.add_child(row2)

	row2.add_child(_make_label("Display dB min:"))
	_display_db_min_spin = _make_spin(-200, 200, 1, DEFAULT_DISPLAY_DB_MIN)
	_display_db_min_spin.value_changed.connect(_on_display_settings_changed)
	row2.add_child(_display_db_min_spin)

	row2.add_child(_make_label("  max:"))
	_display_db_max_spin = _make_spin(-200, 200, 1, DEFAULT_DISPLAY_DB_MAX)
	_display_db_max_spin.value_changed.connect(_on_display_settings_changed)
	row2.add_child(_display_db_max_spin)

	row2.add_child(_make_label("  gamma:"))
	_gamma_spin = _make_spin(0.1, 5.0, 0.1, DEFAULT_GAMMA)
	_gamma_spin.step = 0.1
	_gamma_spin.value_changed.connect(_on_display_settings_changed)
	row2.add_child(_gamma_spin)

	_per_frame_check = CheckBox.new()
	_per_frame_check.text = "Per-frame normalize"
	_per_frame_check.toggled.connect(_on_display_toggle_changed)
	row2.add_child(_per_frame_check)

	_whiten_check = CheckBox.new()
	_whiten_check.text = "Spectral whiten"
	_whiten_check.toggled.connect(_on_display_toggle_changed)
	row2.add_child(_whiten_check)

	_autofit_btn = Button.new()
	_autofit_btn.text = "Auto-fit"
	_autofit_btn.pressed.connect(_on_autofit_pressed)
	row2.add_child(_autofit_btn)

	# --- Row 3: tracker params ---------------------------------------
	var row3 := HBoxContainer.new()
	root.add_child(row3)

	row3.add_child(_make_label("Bandwidth (Hz):"))
	_bandwidth_spin = _make_spin(1, 5000, 1, DEFAULT_BANDWIDTH_HZ)
	row3.add_child(_bandwidth_spin)

	row3.add_child(_make_label("  Max slope (Hz/s):"))
	_max_slope_spin = _make_spin(10, 100000, 10, DEFAULT_MAX_SLOPE)
	row3.add_child(_max_slope_spin)

	row3.add_child(_make_label("  Transition penalty:"))
	_transition_penalty_spin = _make_spin(0.0, 100.0, 0.5, DEFAULT_TRANSITION_PENALTY)
	_transition_penalty_spin.step = 0.5
	row3.add_child(_transition_penalty_spin)

	_track_btn = Button.new()
	_track_btn.text = "Track fundamental"
	_track_btn.disabled = true
	_track_btn.pressed.connect(_on_track_pressed)
	row3.add_child(_track_btn)

	_play_btn = Button.new()
	_play_btn.text = "Play isolated"
	_play_btn.disabled = true
	_play_btn.pressed.connect(_on_play_pressed)
	row3.add_child(_play_btn)

	# --- Row 4: status label -----------------------------------------
	_status_label = Label.new()
	_status_label.text = "Pick an AudioStream and press Analyze."
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	_status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.add_child(_status_label)

	# --- Spectrogram + side panel ------------------------------------
	var split := HBoxContainer.new()
	split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split)

	var frame := PanelContainer.new()
	frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	frame.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(frame)

	var view_holder := Control.new()
	view_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	view_holder.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view_holder.clip_contents = true
	frame.add_child(view_holder)

	_spectrogram_view = TextureRect.new()
	_spectrogram_view.anchor_right = 1.0
	_spectrogram_view.anchor_bottom = 1.0
	_spectrogram_view.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_spectrogram_view.stretch_mode = TextureRect.STRETCH_SCALE
	_spectrogram_view.mouse_filter = Control.MOUSE_FILTER_PASS
	view_holder.add_child(_spectrogram_view)

	_overlay = Control.new()
	_overlay.anchor_right = 1.0
	_overlay.anchor_bottom = 1.0
	_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	_overlay.gui_input.connect(_on_overlay_gui_input)
	_overlay.draw.connect(_on_overlay_draw)
	view_holder.add_child(_overlay)

	# Side panel: harmonic picks + sketch controls.
	var side := VBoxContainer.new()
	side.custom_minimum_size.x = 240
	side.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(side)

	side.add_child(_make_label("Harmonic picks"))
	_picks_list = ItemList.new()
	_picks_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_picks_list.custom_minimum_size.y = 160
	_picks_list.item_selected.connect(_on_pick_list_selected)
	side.add_child(_picks_list)

	var order_row := HBoxContainer.new()
	side.add_child(order_row)
	order_row.add_child(_make_label("Order:"))
	_order_spin = _make_spin(1, 64, 1, 1)
	_order_spin.value_changed.connect(_on_order_spin_changed)
	order_row.add_child(_order_spin)

	_delete_pick_btn = Button.new()
	_delete_pick_btn.text = "Delete pick"
	_delete_pick_btn.pressed.connect(_on_delete_pick_pressed)
	side.add_child(_delete_pick_btn)

	_clear_picks_btn = Button.new()
	_clear_picks_btn.text = "Clear picks"
	_clear_picks_btn.pressed.connect(_on_clear_picks_pressed)
	side.add_child(_clear_picks_btn)

	side.add_child(HSeparator.new())
	side.add_child(_make_label("Sketch hint"))

	_sketch_mode_btn = Button.new()
	_sketch_mode_btn.text = "Sketch fundamental"
	_sketch_mode_btn.toggle_mode = true
	_sketch_mode_btn.toggled.connect(_on_sketch_mode_toggled)
	side.add_child(_sketch_mode_btn)

	_clear_sketch_btn = Button.new()
	_clear_sketch_btn.text = "Clear sketch"
	_clear_sketch_btn.pressed.connect(_on_clear_sketch_pressed)
	side.add_child(_clear_sketch_btn)

	var sw_row := HBoxContainer.new()
	side.add_child(sw_row)
	sw_row.add_child(_make_label("Weight:"))
	_sketch_weight_spin = _make_spin(0.0, 50.0, 0.1, DEFAULT_SKETCH_WEIGHT)
	_sketch_weight_spin.step = 0.1
	sw_row.add_child(_sketch_weight_spin)

	_player = AudioStreamPlayer.new()
	add_child(_player)

	# --- Step 2: Pitch-Synchronous Unit Extraction Section ---
	_extract_section = VBoxContainer.new()
	_extract_section.visible = false
	root.add_child(_extract_section)

	_extract_section.add_child(HSeparator.new())
	var step2_header := Label.new()
	step2_header.text = "Step 2: Extract Pitch Units"
	step2_header.add_theme_font_size_override("font_size", 16)
	_extract_section.add_child(step2_header)

	# Extraction parameters row
	var extract_row := HBoxContainer.new()
	_extract_section.add_child(extract_row)

	extract_row.add_child(_make_label("Pitch marks / cycle step:"))
	_cycles_spin = _make_spin(2, 6, 1, 3)
	_cycles_spin.editable = false
	_cycles_spin.tooltip_text = "Legacy input kept for compatibility. PSOLA extraction now emits one unit per cycle."
	extract_row.add_child(_cycles_spin)

	extract_row.add_child(_make_label("  Window overlap:"))
	_crossfade_spin = _make_spin(0.25, 1.0, 0.25, 0.5)
	_crossfade_spin.step = 0.25
	_crossfade_spin.editable = false
	_crossfade_spin.tooltip_text = "Legacy input kept for compatibility. Runtime now uses Hann-windowed PSOLA overlap-add instead of grain crossfades."
	extract_row.add_child(_crossfade_spin)

	extract_row.add_child(_make_label("  Energy threshold (dB):"))
	_energy_thresh_spin = _make_spin(-120, 0, 1, -40)
	extract_row.add_child(_energy_thresh_spin)

	# Parameter axes section
	_extract_section.add_child(_make_label("Parameter Axes:"))
	_axis_container = VBoxContainer.new()
	_extract_section.add_child(_axis_container)

	_add_axis_btn = Button.new()
	_add_axis_btn.text = "Add Parameter Axis"
	_add_axis_btn.pressed.connect(_on_add_axis_pressed)
	_extract_section.add_child(_add_axis_btn)

	# Extract + Save buttons
	var extract_btn_row := HBoxContainer.new()
	_extract_section.add_child(extract_btn_row)

	_extract_btn = Button.new()
	_extract_btn.text = "Extract Grains"
	_extract_btn.pressed.connect(_on_extract_pressed)
	extract_btn_row.add_child(_extract_btn)

	_save_db_btn = Button.new()
	_save_db_btn.text = "Save Database..."
	_save_db_btn.disabled = true
	_save_db_btn.pressed.connect(_on_save_db_pressed)
	extract_btn_row.add_child(_save_db_btn)

	_extract_status_label = Label.new()
	_extract_status_label.text = ""
	_extract_section.add_child(_extract_status_label)

	_refresh_picks_list()


func _process(delta: float) -> void:
	if not _analyzing:
		return
	# Animate the status label while analysis runs in the background.
	_dot_timer += delta
	if _dot_timer >= 0.4:
		_dot_timer -= 0.4
		_dot_count = (_dot_count % 3) + 1
		_status_label.text = "Analyzing" + ".".repeat(_dot_count)
	# Check if the thread has finished.
	if _analysis_thread and not _analysis_thread.is_alive():
		_finish_analysis()


func _make_label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l


func _make_spin(min_v: float, max_v: float, step: float, value: float) -> SpinBox:
	var s := SpinBox.new()
	s.min_value = min_v
	s.max_value = max_v
	s.step = step
	s.value = value
	s.allow_greater = false
	s.allow_lesser = false
	return s


func _on_close_requested() -> void:
	hide()


func _on_stream_changed(res: Resource) -> void:
	_stream = res as AudioStream
	_status_label.text = "Ready to analyze." if _stream else "Pick an AudioStream."


func _set_controls_enabled(enabled: bool) -> void:
	_analyze_btn.disabled = not enabled
	_stream_picker.editable = enabled
	_fft_size_spin.editable = enabled
	_y_res_spin.editable = enabled
	_x_res_spin.editable = enabled
	_db_floor_spin.editable = enabled
	_min_hz_spin.editable = enabled
	_max_hz_spin.editable = enabled
	_track_btn.disabled = not enabled or _picks.is_empty() or _fft == null
	_play_btn.disabled = not enabled or _tracked_curve.size() == 0


func _on_analyze_pressed() -> void:
	if _stream == null:
		_status_label.text = "No AudioStream selected."
		return
	if _analyzing:
		return

	_analyzing = true
	_dot_count = 0
	_dot_timer = 0.0
	_status_label.text = "Analyzing..."
	_set_controls_enabled(false)

	# Capture parameters for the thread (avoid accessing UI from worker).
	var p_stream := _stream
	var p_fft_size := int(_fft_size_spin.value)
	var p_db_floor := _db_floor_spin.value
	var p_y_res := int(_y_res_spin.value)
	var p_x_res := _x_res_spin.value
	var p_min_hz := _min_hz_spin.value
	var p_max_hz := _max_hz_spin.value

	_analysis_thread = Thread.new()
	_analysis_thread.start(
		func():
			_analysis_result = StreamWeaverFFT.create_from_audio_stream(
				p_stream, p_fft_size, p_db_floor, p_y_res, p_x_res, p_min_hz, p_max_hz)
	)


func _finish_analysis() -> void:
	_analysis_thread.wait_to_finish()
	_analysis_thread = null
	_analyzing = false

	var result := _analysis_result
	_analysis_result = null

	if result == null or result.get_image() == null:
		_status_label.text = "Analysis failed (see output)."
		_set_controls_enabled(true)
		return

	_fft = result

	_per_frame_check.set_pressed_no_signal(false)
	_whiten_check.set_pressed_no_signal(false)
	_gamma_spin.set_value_no_signal(DEFAULT_GAMMA)
	_autofit_display_range()

	_picks.clear()
	_reference_time = -1.0
	_selected_pick_index = -1
	_dragging_pick_index = -1
	_sketch_points = PackedVector2Array()
	_sketch_completed_strokes.clear()
	_tracked_curve = PackedVector2Array()
	_refresh_picks_list()
	_overlay.queue_redraw()
	_set_controls_enabled(true)
	_status_label.text = "Analyzed: %d x %d frames  bin=%.2fHz  decimation=%dx  (click spectrogram to add harmonic picks)" \
		% [_fft.get_x_resolution(), _fft.get_y_resolution(), _fft.get_bin_hz(), _fft.get_decimation_factor()]


func _refresh_texture() -> void:
	if _fft == null:
		return
	var img := _fft.get_image()
	if img == null:
		return
	_spectrogram_view.texture = ImageTexture.create_from_image(img)


func _autofit_display_range() -> void:
	if _fft == null:
		return
	var range_v: Vector2 = _fft.compute_auto_display_range(
		_per_frame_check.button_pressed,
		_whiten_check.button_pressed)
	_display_db_min_spin.set_value_no_signal(range_v.x)
	_display_db_max_spin.set_value_no_signal(range_v.y)
	_apply_display_settings()


func _on_autofit_pressed() -> void:
	_autofit_display_range()


func _on_display_settings_changed(_v: float) -> void:
	_apply_display_settings()


func _on_display_toggle_changed(_pressed: bool) -> void:
	_autofit_display_range()


func _apply_display_settings() -> void:
	if _fft == null:
		return
	_fft.set_display_settings(
		_display_db_min_spin.value,
		_display_db_max_spin.value,
		_gamma_spin.value,
		_per_frame_check.button_pressed,
		_whiten_check.button_pressed)
	_refresh_texture()
	_overlay.queue_redraw()


# --- Coordinate helpers ----------------------------------------------

func _overlay_to_time_hz(local_pos: Vector2) -> Vector2:
	var size_v: Vector2 = _overlay.size
	if size_v.x <= 0 or size_v.y <= 0 or _fft == null:
		return Vector2(-1, -1)
	var u := clampf(local_pos.x / size_v.x, 0.0, 1.0)
	var v := clampf(local_pos.y / size_v.y, 0.0, 1.0)
	var px := int(u * _fft.get_x_resolution())
	var py := int(v * _fft.get_y_resolution())
	return Vector2(_fft.pixel_x_to_time(px), _fft.pixel_y_to_hz(py))


func _time_hz_to_overlay(t: float, hz: float) -> Vector2:
	var size_v: Vector2 = _overlay.size
	var xr := _fft.get_x_resolution()
	var yr := _fft.get_y_resolution()
	if xr <= 0 or yr <= 0:
		return Vector2.ZERO
	var sx := size_v.x / float(xr)
	var sy := size_v.y / float(yr)
	var px := _fft.time_to_pixel_x(t)
	var py := _fft.hz_to_pixel_y(hz)
	return Vector2((px + 0.5) * sx, (py + 0.5) * sy)


func _find_pick_near(local_pos: Vector2) -> int:
	if _picks.is_empty() or _fft == null:
		return -1
	var best := -1
	var best_d := PICK_HIT_RADIUS_PX
	for i in _picks.size():
		var p: Vector2 = _time_hz_to_overlay(_reference_time, _picks[i].hz)
		var d := p.distance_to(local_pos)
		if d <= best_d:
			best_d = d
			best = i
	return best


# --- Picks management ------------------------------------------------

func _refresh_picks_list() -> void:
	_picks_list.clear()
	for i in _picks.size():
		var entry: Dictionary = _picks[i]
		_picks_list.add_item("#%d  order %d  @ %.1f Hz" % [i + 1, entry.order, entry.hz])
	if _selected_pick_index >= 0 and _selected_pick_index < _picks.size():
		_picks_list.select(_selected_pick_index)
		_order_spin.set_value_no_signal(_picks[_selected_pick_index].order)
	_track_btn.disabled = _picks.is_empty() or _fft == null


func _add_pick(time_s: float, hz: float) -> int:
	if _picks.is_empty():
		_reference_time = time_s
	var next_order := _picks.size() + 1
	_picks.append({ "order": next_order, "hz": hz })
	var idx := _picks.size() - 1
	_selected_pick_index = idx
	_refresh_picks_list()
	_overlay.queue_redraw()
	return idx


func _on_pick_list_selected(idx: int) -> void:
	_selected_pick_index = idx
	if idx >= 0 and idx < _picks.size():
		_order_spin.set_value_no_signal(_picks[idx].order)
	_overlay.queue_redraw()


func _on_order_spin_changed(value: float) -> void:
	if _selected_pick_index < 0 or _selected_pick_index >= _picks.size():
		return
	_picks[_selected_pick_index].order = int(value)
	_refresh_picks_list()
	_overlay.queue_redraw()


func _on_delete_pick_pressed() -> void:
	if _selected_pick_index < 0 or _selected_pick_index >= _picks.size():
		return
	_picks.remove_at(_selected_pick_index)
	if _picks.is_empty():
		_reference_time = -1.0
		_selected_pick_index = -1
	else:
		_selected_pick_index = clampi(_selected_pick_index, 0, _picks.size() - 1)
	_refresh_picks_list()
	_overlay.queue_redraw()


func _on_clear_picks_pressed() -> void:
	_picks.clear()
	_reference_time = -1.0
	_selected_pick_index = -1
	_refresh_picks_list()
	_overlay.queue_redraw()


# --- Sketch ----------------------------------------------------------

func _on_sketch_mode_toggled(pressed: bool) -> void:
	_sketch_mode = pressed


func _on_clear_sketch_pressed() -> void:
	_sketch_points = PackedVector2Array()
	_sketch_completed_strokes.clear()
	_overlay.queue_redraw()


func _insert_point_into_sketch(th: Vector2, smooth_radius := 10, smooth_strength := 0.5):
	var points := _sketch_points
	var size := points.size()

	var left := 0
	var right := size - 1
	var mid := 0

	while left <= right:
		mid = (left + right) / 2
		var mid_x = points[mid].x

		if is_equal_approx(mid_x, th.x):
			# Update existing point
			points[mid].y = lerp(points[mid].y, th.y, smooth_strength)
			_apply_sketch_points_smoothing(mid, smooth_radius, smooth_strength)
			return
		elif mid_x < th.x:
			left = mid + 1
		else:
			right = mid - 1

	points.insert(left, th)

	_apply_sketch_points_smoothing(left, smooth_radius, smooth_strength)


func _apply_sketch_points_smoothing(center_idx: int, radius: int, strength: float):
	var points := _sketch_points
	var size := points.size()

	for i in range(-radius, radius + 1):
		var idx = center_idx + i
		if idx < 0 or idx >= size or i == 0:
			continue

		var falloff = 1.0 - (abs(i) / float(radius + 1))
		var influence = strength * falloff * 0.5

		points[idx].y = lerp(points[idx].y, points[center_idx].y, influence)


# --- Mouse input -----------------------------------------------------

func _on_overlay_gui_input(event: InputEvent) -> void:
	if _fft == null or _analyzing:
		return

	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			if _sketch_mode:
				_sketching = true
				# Commit the active stroke before starting a new one
				if _sketch_points.size() > 0:
					_sketch_completed_strokes.append(_sketch_points.duplicate())
					_sketch_points = PackedVector2Array()
				var th := _overlay_to_time_hz(event.position)
				if th.x >= 0:
					_insert_point_into_sketch(th)
				_overlay.queue_redraw()
				return

			var hit := _find_pick_near(event.position)
			if event.shift_pressed and hit >= 0:
				_picks.remove_at(hit)
				if _picks.is_empty():
					_reference_time = -1.0
					_selected_pick_index = -1
				else:
					_selected_pick_index = clampi(_selected_pick_index, 0, _picks.size() - 1)
				_refresh_picks_list()
				_overlay.queue_redraw()
				return

			if hit >= 0:
				_dragging_pick_index = hit
				_selected_pick_index = hit
				_refresh_picks_list()
				return

			var th2 := _overlay_to_time_hz(event.position)
			if th2.y > 0:
				_add_pick(th2.x, th2.y)
				_status_label.text = "Pick #%d  order %d  @ %.1f Hz  (ref t=%.3fs)" % [
					_picks.size(), _picks[_picks.size() - 1].order,
					_picks[_picks.size() - 1].hz, _reference_time]
		else:
			_sketching = false
			_dragging_pick_index = -1
		return

	if event is InputEventMouseMotion:
		if _sketching:
			var th := _overlay_to_time_hz(event.position)
			if th.x >= 0:
				_insert_point_into_sketch(th)
				_overlay.queue_redraw()
			return
		if _dragging_pick_index >= 0:
			var th := _overlay_to_time_hz(event.position)
			if th.y > 0:
				_picks[_dragging_pick_index].hz = th.y
				_refresh_picks_list()
				_overlay.queue_redraw()


# --- Sketch hint flattening ------------------------------------------

# Returns a flat PackedVector2Array for the C++ sketch_hz_at functions.
# Completed strokes and the active stroke are concatenated in time order,
# separated by gap sentinels (y = -1) so the C++ side can detect stroke
# boundaries and return -1 for times that fall between strokes.
func _get_sketch_hint() -> PackedVector2Array:
	var result := PackedVector2Array()
	var all_strokes: Array[PackedVector2Array] = _sketch_completed_strokes.duplicate()
	if _sketch_points.size() >= 1:
		all_strokes.append(_sketch_points)

	var non_empty: Array[PackedVector2Array] = []
	for stroke in all_strokes:
		if stroke.size() >= 1:
			non_empty.append(stroke)

	for i in non_empty.size():
		var stroke: PackedVector2Array = non_empty[i].duplicate()
		# Sort stroke points by time (x) so interpolation works correctly.
		# PackedVector2Array.sort() sorts lexicographically (x first), which is
		# correct here since x = time_s.
		stroke.sort()
		result.append_array(stroke)
		# Insert a gap sentinel after each stroke except the last.
		if i < non_empty.size() - 1:
			result.append(Vector2(stroke[-1].x + 0.0001, -1.0))
	return result


# --- Tracking + playback ---------------------------------------------

func _on_track_pressed() -> void:
	if _fft == null or _picks.is_empty():
		return
	_status_label.text = "Tracking..."
	await get_tree().process_frame

	var picks_packed := PackedVector2Array()
	picks_packed.resize(_picks.size())
	for i in _picks.size():
		picks_packed[i] = Vector2(_picks[i].hz, float(_picks[i].order))

	var sketch_hint := _get_sketch_hint()
	_tracked_curve = _fft.track_fundamental(
		_reference_time,
		picks_packed,
		_bandwidth_spin.value,
		_max_slope_spin.value,
		_transition_penalty_spin.value,
		sketch_hint,
		_sketch_weight_spin.value)

	var total_stroke_count := _sketch_completed_strokes.size() + (1 if _sketch_points.size() >= 1 else 0)
	if _tracked_curve.size() == 0:
		_status_label.text = "Tracking failed: no peak at picks."
		_play_btn.disabled = true
		_extract_section.visible = false
	else:
		_status_label.text = "Tracked %d points from %.3fs to %.3fs (%d harmonic picks%s)" % [
			_tracked_curve.size(),
			_tracked_curve[0].x,
			_tracked_curve[_tracked_curve.size() - 1].x,
			_picks.size(),
			"  +sketch (%d stroke%s)" % [total_stroke_count, "s" if total_stroke_count != 1 else ""] if total_stroke_count > 0 else ""]
		_play_btn.disabled = false
		_extract_section.visible = true
		_grain_boundaries = PackedVector2Array()
	_overlay.queue_redraw()


func _on_play_pressed() -> void:
	if _fft == null or _tracked_curve.size() == 0:
		return
	var wav := _fft.reconstruct_isolated(_tracked_curve, _bandwidth_spin.value)
	if wav == null:
		_status_label.text = "Reconstruction failed."
		return
	_player.stream = wav
	_player.play()
	_status_label.text = "Playing isolated reconstruction..."


# --- Drawing ---------------------------------------------------------

func _on_overlay_draw() -> void:
	if _fft == null:
		return
	var size_v: Vector2 = _overlay.size
	if size_v.x <= 0 or size_v.y <= 0:
		return
	var xr := _fft.get_x_resolution()
	var yr := _fft.get_y_resolution()
	if xr <= 0 or yr <= 0:
		return
	var sx := size_v.x / float(xr)
	var sy := size_v.y / float(yr)

	# Reference time vertical guide.
	if _reference_time >= 0:
		var refx := (_fft.time_to_pixel_x(_reference_time) + 0.5) * sx
		_overlay.draw_line(Vector2(refx, 0), Vector2(refx, size_v.y), Color(1, 1, 0, 0.25), 1.0)

	# Sketch polylines (cyan) — each completed stroke and the active stroke drawn separately.
	var _all_sketch_strokes: Array[PackedVector2Array] = _sketch_completed_strokes.duplicate()
	if _sketch_points.size() >= 1:
		_all_sketch_strokes.append(_sketch_points)
	for stroke in _all_sketch_strokes:
		if stroke.size() < 2:
			continue
		var spts := PackedVector2Array()
		spts.resize(stroke.size())
		for i in stroke.size():
			spts[i] = _time_hz_to_overlay(stroke[i].x, stroke[i].y)
		_overlay.draw_polyline(spts, Color(0.2, 0.9, 1.0, 0.85), 1.5)

	# Tracked fundamental (green).
	if _tracked_curve.size() >= 2:
		var pts := PackedVector2Array()
		pts.resize(_tracked_curve.size())
		for i in _tracked_curve.size():
			var p: Vector2 = _tracked_curve[i]
			pts[i] = _time_hz_to_overlay(p.x, p.y)
		_overlay.draw_polyline(pts, Color(0.3, 1.0, 0.4, 0.95), 2.0)

		# Derived harmonics (translucent magenta) for each unique picked order > 1.
		var unique_orders := {}
		for entry in _picks:
			unique_orders[int(entry.order)] = true
		for k in unique_orders.keys():
			if k <= 1:
				continue
			var hpts := PackedVector2Array()
			hpts.resize(_tracked_curve.size())
			for i in _tracked_curve.size():
				var p2: Vector2 = _tracked_curve[i]
				hpts[i] = _time_hz_to_overlay(p2.x, p2.y * k)
			_overlay.draw_polyline(hpts, Color(1.0, 0.3, 0.9, 0.45), 1.0)

	# Harmonic pick markers.
	for i in _picks.size():
		var entry: Dictionary = _picks[i]
		var c: Vector2 = _time_hz_to_overlay(_reference_time, entry.hz)
		var col := Color(1, 0.85, 0.2, 0.95) if i == _selected_pick_index else Color(1, 0.6, 0.1, 0.85)
		_overlay.draw_circle(c, 6.0, col)
		_overlay.draw_circle(c, 6.0, Color(0, 0, 0, 0.9), false, 1.5)
		var font := ThemeDB.fallback_font
		var fs := 12
		var label := "%d" % entry.order
		_overlay.draw_string(font, c + Vector2(8, 4), label, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(1, 1, 1, 1))

	# Grain boundaries (orange vertical lines).
	if _grain_boundaries.size() > 0:
		for i in _grain_boundaries.size():
			var gb: Vector2 = _grain_boundaries[i]
			var x_start: float = _time_hz_to_overlay(gb.x, _fft.get_display_min_hz()).x
			var x_end: float = _time_hz_to_overlay(gb.y, _fft.get_display_min_hz()).x
			_overlay.draw_line(
				Vector2(x_start, 0), Vector2(x_start, size_v.y),
				Color(1.0, 0.5, 0.1, 0.4), 1.0)
			# Draw a small bracket at the top connecting start to end
			var y_top := 2.0
			_overlay.draw_line(
				Vector2(x_start, y_top), Vector2(x_end, y_top),
				Color(1.0, 0.5, 0.1, 0.3), 2.0)


# --- Step 2: Parameter Axis Management ----------------------------------

func _on_add_axis_pressed() -> void:
	_add_axis_row("", true, 0.0, 1.0, 100.0, 0.0, 1000.0, 1.0)


func _add_axis_row(axis_name: String, derived: bool, min_val: float, max_val: float,
		cal_a_hz: float, cal_a_val: float, cal_b_hz: float, cal_b_val: float) -> void:
	var row := VBoxContainer.new()
	_axis_container.add_child(row)

	var top_row := HBoxContainer.new()
	row.add_child(top_row)

	top_row.add_child(_make_label("Name:"))
	var name_edit := LineEdit.new()
	name_edit.text = axis_name if axis_name != "" else "RPM"
	name_edit.custom_minimum_size.x = 80
	top_row.add_child(name_edit)

	var derived_check := CheckBox.new()
	derived_check.text = "From fundamental"
	derived_check.button_pressed = derived
	top_row.add_child(derived_check)

	top_row.add_child(_make_label("  Min:"))
	var min_spin := _make_spin(-1000000, 1000000, 1, min_val)
	min_spin.custom_minimum_size.x = 80
	top_row.add_child(min_spin)

	top_row.add_child(_make_label("  Max:"))
	var max_spin := _make_spin(-1000000, 1000000, 1, max_val)
	max_spin.custom_minimum_size.x = 80
	top_row.add_child(max_spin)

	var remove_btn := Button.new()
	remove_btn.text = "X"
	remove_btn.pressed.connect(func(): _remove_axis_row(row))
	top_row.add_child(remove_btn)

	# Calibration row (visible when "From fundamental" is checked)
	var cal_row := HBoxContainer.new()
	row.add_child(cal_row)

	cal_row.add_child(_make_label("  Cal A: Hz="))
	var cal_a_hz_spin := _make_spin(0, 48000, 1, cal_a_hz)
	cal_a_hz_spin.custom_minimum_size.x = 70
	cal_row.add_child(cal_a_hz_spin)
	cal_row.add_child(_make_label(" Val="))
	var cal_a_val_spin := _make_spin(-1000000, 1000000, 1, cal_a_val)
	cal_a_val_spin.custom_minimum_size.x = 70
	cal_row.add_child(cal_a_val_spin)

	cal_row.add_child(_make_label("  Cal B: Hz="))
	var cal_b_hz_spin := _make_spin(0, 48000, 1, cal_b_hz)
	cal_b_hz_spin.custom_minimum_size.x = 70
	cal_row.add_child(cal_b_hz_spin)
	cal_row.add_child(_make_label(" Val="))
	var cal_b_val_spin := _make_spin(-1000000, 1000000, 1, cal_b_val)
	cal_b_val_spin.custom_minimum_size.x = 70
	cal_row.add_child(cal_b_val_spin)

	cal_row.visible = derived

	derived_check.toggled.connect(func(pressed: bool): cal_row.visible = pressed)

	var axis_data := {
		"row": row,
		"name": name_edit,
		"derived": derived_check,
		"min": min_spin,
		"max": max_spin,
		"cal_a_hz": cal_a_hz_spin,
		"cal_a_val": cal_a_val_spin,
		"cal_b_hz": cal_b_hz_spin,
		"cal_b_val": cal_b_val_spin,
		"keyframes": PackedVector2Array(),
	}
	_axis_rows.append(axis_data)


func _remove_axis_row(row: VBoxContainer) -> void:
	for i in _axis_rows.size():
		if _axis_rows[i].row == row:
			_axis_rows.remove_at(i)
			break
	row.queue_free()


func _build_axis_configs() -> Array[Dictionary]:
	var configs: Array[Dictionary] = []
	for axis_data in _axis_rows:
		var cfg := {}
		cfg["name"] = StringName(axis_data.name.text)
		cfg["min_value"] = axis_data.min.value
		cfg["max_value"] = axis_data.max.value
		cfg["derived_from_fundamental"] = axis_data.derived.button_pressed
		cfg["calibration_a"] = Vector2(axis_data.cal_a_hz.value, axis_data.cal_a_val.value)
		cfg["calibration_b"] = Vector2(axis_data.cal_b_hz.value, axis_data.cal_b_val.value)
		cfg["keyframes"] = axis_data.keyframes
		configs.append(cfg)
	return configs


# --- Step 2: Extraction --------------------------------------------------

func _on_extract_pressed() -> void:
	if _fft == null or _tracked_curve.size() < 2:
		_extract_status_label.text = "No tracked curve available. Track the fundamental first."
		return

	if _axis_rows.is_empty():
		_extract_status_label.text = "Add at least one parameter axis."
		return

	_extract_status_label.text = "Extracting pitch-synchronous units..."
	await get_tree().process_frame

	var axis_configs := _build_axis_configs()

	_grains_database = StreamWeaverGrainsDatabase.new()
	_grains_database.build_from_fft(
		_fft,
		_stream,
		_tracked_curve,
		axis_configs,
		int(_cycles_spin.value),
		_crossfade_spin.value,
		_energy_thresh_spin.value,
		_get_sketch_hint())

	var count := _grains_database.get_grain_count()
	if count == 0:
		_extract_status_label.text = "Extraction produced 0 units. Try lowering the energy threshold or broadening the tracked fundamental range."
		_save_db_btn.disabled = true
		_grain_boundaries = PackedVector2Array()
	else:
		_extract_status_label.text = "Extracted %d pitch units. Ready to save." % count
		_save_db_btn.disabled = false

		# Build pitch-unit overlay data
		_grain_boundaries = PackedVector2Array()
		for i in count:
			var meta: Dictionary = _grains_database.get_grain_metadata(i)
			var time_s: float = meta.original_time_s
			var duration_s: float = float(meta.get("window_size", 0)) / _grains_database.get_sample_rate()
			_grain_boundaries.append(Vector2(time_s - duration_s * 0.5, time_s + duration_s * 0.5))

	_overlay.queue_redraw()


func _on_save_db_pressed() -> void:
	if _grains_database == null or _grains_database.get_grain_count() == 0:
		return

	var dialog := FileDialog.new()
	dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dialog.filters = PackedStringArray(["*.tres ; Godot Resource"])
	dialog.access = FileDialog.ACCESS_RESOURCES
	dialog.current_file = "grains_database.tres"
	dialog.file_selected.connect(func(path: String):
		var err := ResourceSaver.save(_grains_database, path)
		if err == OK:
			_extract_status_label.text = "Saved to %s" % path
		else:
			_extract_status_label.text = "Save failed: error %d" % err
		dialog.queue_free()
	)
	dialog.canceled.connect(func(): dialog.queue_free())
	add_child(dialog)
	dialog.popup_centered(Vector2i(600, 400))
