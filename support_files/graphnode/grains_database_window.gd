@tool
extends Window

# Editor tool window for inspecting and editing a StreamWeaverGrainsDatabase.
#
# Three pages (wizard flow, not tabs):
#   - Overview: browse grains, filter/sort, audition, delete, save.
#   - Analysis & Parameters: audio input, FFT analysis, fundamental
#     tracking, parameter axis definition.
#   - Extract Grains: choose FFT Automatic or Manual extraction; parameters
#     from the Analysis page are assigned automatically.

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
const DEFAULT_STOP_DROP_DB := 20.0

const PICK_HIT_RADIUS_PX := 10.0

# --- Editor state: database --------------------------------------------------

var _grains_database: StreamWeaverGrainsDatabase
var _db_path := ""
var _dirty := false

# Sources listed in the database (snapshot from db.source_paths).
var _source_paths: PackedStringArray

# Filter / sort state for the overview list.
# Source filter ids: -2 = all, -1 = (unknown), 0..N-1 = source_paths index.
var _filter_source := -2
var _filter_energy := 0      # 0 = all, 1 = low-energy only, 2 = high-energy only
var _filter_hz_min := 0.0
var _filter_hz_max := 0.0    # 0 = no upper bound
var _filter_axis_min: PackedFloat32Array
var _filter_axis_max: PackedFloat32Array
var _sort_key := 0           # 0=index, 1=time, 2=Hz, 3=source, 4..=axis i-3
var _sort_descending := false

# Indices of grains currently displayed in _grain_list (after filter+sort).
var _filtered_indices: PackedInt32Array

# --- Editor state: import / extraction (existing, scoped to import tab) ------

var _fft: StreamWeaverFFT
var _stream: AudioStream

# Each pick: { "order": int, "hz": float }. Reference time is _picks[0]'s
# implicit time; new picks snap their x to the first pick's time.
var _picks: Array = []
var _reference_time := -1.0
var _dragging_pick_index := -1
var _selected_pick_index := -1

# Free-form sketch over the spectrogram, used as a hint for the second tracker
# pass.
var _sketch_points: PackedVector2Array
var _sketching := false
var _sketch_mode := false

var _tracked_curve: PackedVector2Array

# Threading state for background analysis.
var _analysis_thread: Thread
var _analysis_result: StreamWeaverFFT
var _analyzing := false
var _dot_count := 0
var _dot_timer := 0.0

# --- UI: header --------------------------------------------------------------

var _db_picker: EditorResourcePicker
var _title_label: Label
var _new_btn: Button
var _save_btn: Button
var _save_as_btn: Button
var _compact_btn: Button
var _compact_on_save_check: CheckBox

# --- UI: page-stack navigation -----------------------------------------------

const PAGE_OVERVIEW = 0
const PAGE_ANALYSIS = 1
const PAGE_EXTRACT  = 2
var _current_page: int = 0
var _page_container: Control
var _overview_panel: VBoxContainer
var _analysis_panel: VBoxContainer
var _extract_panel: VBoxContainer
var _analysis_continue_btn: Button

# --- UI: extract page --------------------------------------------------------

var _extract_method: int = 0  # 0 = FFT Automatic, 1 = Manual, 2 = Semi-Automatic
var _fft_extract_panel: VBoxContainer
var _manual_extract_panel: VBoxContainer
var _extract_method_fft_btn: Button
var _extract_method_manual_btn: Button
var _extract_method_semiauto_btn: Button

# --- UI: overview tab --------------------------------------------------------

var _overview_summary_label: RichTextLabel
var _overview_axes_box: VBoxContainer
var _overview_sources_label: Label
var _compression_opt: OptionButton

var _filter_source_opt: OptionButton
var _filter_energy_opt: OptionButton
var _filter_hz_min_spin: SpinBox
var _filter_hz_max_spin: SpinBox
var _filter_axes_box: VBoxContainer
var _filter_axis_rows: Array = [] # { "min": SpinBox, "max": SpinBox }

var _sort_key_opt: OptionButton
var _sort_dir_opt: OptionButton

var _grain_list: ItemList
var _grain_player: AudioStreamPlayer

var _grain_detail_label: RichTextLabel
var _grain_waveform: Control
var _grain_play_btn: Button
var _grain_delete_btn: Button

var _bulk_delete_selected_btn: Button
var _bulk_delete_filtered_btn: Button
var _bulk_delete_low_energy_btn: Button

var _seq_play_btn: Button
var _seq_status_label: Label

# Grain currently shown in the detail pane (single, even if multi-selected).
var _detail_grain_index := -1

# Sequential playback state (_seq_pos indexes _filtered_indices).
var _seq_playing := false
var _seq_paused  := false
var _seq_pos     := 0

# --- UI: import tab (existing controls) --------------------------------------

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
var _log_hz_check: CheckBox
var _autofit_btn: Button

var _bandwidth_spin: SpinBox
var _max_slope_spin: SpinBox
var _transition_penalty_spin: SpinBox
var _stop_drop_spin: SpinBox
var _tracker_params_row: VBoxContainer
var _track_btn: Button
var _play_btn: Button
var _status_label: Label

# Low Hz mode: time-domain onset detection for sub-100 Hz fundamentals.
var _low_hz_mode := false
var _low_hz_mode_check: CheckBox
var _low_hz_row: HBoxContainer
var _low_hz_min_spin: SpinBox
var _low_hz_max_spin: SpinBox
var _sensitivity_spin: SpinBox
var _picks_panel: VBoxContainer

var _picks_list: ItemList
var _order_spin: SpinBox
var _delete_pick_btn: Button
var _clear_picks_btn: Button
var _sketch_mode_btn: Button
var _clear_sketch_btn: Button
var _sketch_weight_spin: SpinBox

var _spectrogram_view: Control
var _overlay: Control
var _player: AudioStreamPlayer

var _hover_pos := Vector2(-1, -1)

# --- Spectrogram preview playback state ---
var _spec_playback_playing: bool = false

# Extraction parameters (now in _fft_extract_panel on extract page)
var _cycles_spin: SpinBox
var _crossfade_spin: SpinBox
var _energy_thresh_spin: SpinBox
var _import_btn: Button
var _extract_status_label: Label

# Parameter axis definitions (import-side editor)
var _axis_container: VBoxContainer
var _add_axis_btn: Button
var _axis_rows: Array = []

var _grain_boundaries: PackedVector2Array

# --- UI: auto extraction spectrogram + region selector -------------------------

var _auto_spec_ctrl: Control    # draws spectrogram texture
var _auto_overlay_ctrl: Control # draws region highlight + handles input
var _extract_region_start := -1.0  # -1 = no selection (use full curve)
var _extract_region_end   := -1.0
var _extract_region_dragging := false

# --- Manual extraction tab state -----------------------------------------------

# Sorted ascending list of grain-boundary times in seconds.
var _manual_markers: PackedFloat64Array = PackedFloat64Array()
# Visible time window for zoom/pan.
var _manual_view_start: float = 0.0
var _manual_view_end: float   = 1.0
# Dragging / selection state for markers.
var _manual_dragging_marker_idx: int = -1
var _manual_selected_marker_idx: int = -1

# --- Semi-automatic extraction state ---
# Number of markers to auto-place/propagate ahead of the currently dragged marker.
var _semiauto_lookahead: int = 10
# Snapshot of all marker positions taken at the start of a drag (used for first-marker shift).
var _semiauto_drag_initial_markers: PackedFloat64Array
# Original time of the dragged marker at drag start (used for first-marker delta).
var _semiauto_drag_start_t: float = 0.0
# Dragging state for param-lane keyframes.
var _manual_dragging_kf_axis_data = null  # axis_data dict of the axis being dragged, or null
var _manual_dragging_kf_idx: int = -1
# Undo stack: snapshots of _manual_markers before each edit.
var _manual_undo_stack: Array = []
# Index of the grain region (between markers) the mouse is currently hovering over; -1 = none.
var _manual_hovered_grain_idx: int = -1
# Panning state (middle-mouse drag).
var _manual_panning: bool = false
var _manual_pan_start_pos: Vector2 = Vector2.ZERO
var _manual_pan_start_view: Vector2 = Vector2.ZERO  # (view_start, view_end)
# Cached PCM for waveform strip: original full-bandwidth mono, from _fft.
var _manual_waveform_pcm: PackedFloat32Array
var _manual_waveform_pcm_rate: float = 0.0

# --- Grain preview (pre-extraction) state ---
var _preview_player: AudioStreamPlayer
var _preview_timer: Timer
var _preview_grain_idx: int = -1  # index into _manual_markers; -1 = not previewing

# --- Analysis spectrogram zoom/pan state ----------------------------------------

# Visible time window for the analysis spectrogram zoom/pan.
var _analysis_view_start: float = 0.0
var _analysis_view_end: float   = 1.0
# Panning state (middle-mouse drag) for the analysis spectrogram.
var _analysis_panning: bool = false
var _analysis_pan_start_pos: Vector2 = Vector2.ZERO
var _analysis_pan_start_view: Vector2 = Vector2.ZERO  # (view_start, view_end)

# --- UI: manual extraction panel -----------------------------------------------

var _manual_status_label: Label
# The spec/overlay area sits inside a clipping panel.
var _manual_spec_ctrl: Control   # draws spectrogram via draw_texture_rect_region
var _manual_overlay: Control     # captures input, draws markers
# Semi-automatic lookahead row (shown only in semi-auto mode).
var _semiauto_lookahead_row: HBoxContainer
var _semiauto_lookahead_label: Label
var _manual_spec_texture: ImageTexture
var _manual_waveform_ctrl: Control
var _manual_waveform_check: CheckBox
var _manual_waveform_gain_spin: SpinBox
var _manual_snap_check: CheckBox
var _manual_energy_spin: SpinBox
var _manual_import_btn: Button
var _manual_extract_status_label: Label
var _manual_info_label: Label         # "Analyze audio in Import tab first"


# =============================================================================
# Construction
# =============================================================================

func _ready() -> void:
	title = "StreamWeaver Grain Database Editor"
	size = Vector2i(1320, 880)
	min_size = Vector2i(900, 600)
	close_requested.connect(_on_close_requested)

	var root := VBoxContainer.new()
	root.anchor_right = 1.0
	root.anchor_bottom = 1.0
	root.offset_left = 8
	root.offset_top = 8
	root.offset_right = -8
	root.offset_bottom = -8
	add_child(root)

	_build_header(root)

	_page_container = Control.new()
	_page_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_page_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(_page_container)

	_overview_panel = VBoxContainer.new()
	_overview_panel.anchor_right = 1.0
	_overview_panel.anchor_bottom = 1.0
	_page_container.add_child(_overview_panel)
	_build_overview_tab(_overview_panel)

	_analysis_panel = VBoxContainer.new()
	_analysis_panel.anchor_right = 1.0
	_analysis_panel.anchor_bottom = 1.0
	_analysis_panel.visible = false
	_page_container.add_child(_analysis_panel)
	_build_analysis_page(_analysis_panel)

	_extract_panel = VBoxContainer.new()
	_extract_panel.anchor_right = 1.0
	_extract_panel.anchor_bottom = 1.0
	_extract_panel.visible = false
	_page_container.add_child(_extract_panel)
	_build_extract_page(_extract_panel)

	_grain_player = AudioStreamPlayer.new()
	add_child(_grain_player)
	_grain_player.finished.connect(_on_grain_player_finished)

	_preview_player = AudioStreamPlayer.new()
	add_child(_preview_player)

	_preview_timer = Timer.new()
	_preview_timer.one_shot = true
	_preview_timer.timeout.connect(_on_preview_timer_timeout)
	add_child(_preview_timer)

	_set_database(null)


func _build_header(root: VBoxContainer) -> void:
	var row := HBoxContainer.new()
	root.add_child(row)

	row.add_child(_make_label("Database:"))
	_db_picker = EditorResourcePicker.new()
	_db_picker.base_type = "StreamWeaverGrainsDatabase"
	_db_picker.custom_minimum_size.x = 280
	_db_picker.resource_changed.connect(_on_db_picker_changed)
	row.add_child(_db_picker)

	_title_label = Label.new()
	_title_label.text = "(no database open)"
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(_title_label)

	_new_btn = Button.new()
	_new_btn.text = "New…"
	_new_btn.pressed.connect(_on_new_database_pressed)
	row.add_child(_new_btn)

	_save_btn = Button.new()
	_save_btn.text = "Save"
	_save_btn.disabled = true
	_save_btn.pressed.connect(_on_save_pressed)
	row.add_child(_save_btn)

	_save_as_btn = Button.new()
	_save_as_btn.text = "Save As…"
	_save_as_btn.disabled = true
	_save_as_btn.pressed.connect(_on_save_as_pressed)
	row.add_child(_save_as_btn)

	_compact_btn = Button.new()
	_compact_btn.text = "Compact"
	_compact_btn.tooltip_text = "Rewrite the PCM pool with only the windowed audio of surviving grains, dropping any orphaned samples from deleted grains."
	_compact_btn.disabled = true
	_compact_btn.pressed.connect(_on_compact_pressed)
	row.add_child(_compact_btn)

	_compact_on_save_check = CheckBox.new()
	_compact_on_save_check.text = "Compact on save"
	_compact_on_save_check.button_pressed = true
	row.add_child(_compact_on_save_check)


# =============================================================================
# Overview tab
# =============================================================================

func _build_overview_tab(parent: VBoxContainer) -> void:
	parent.add_theme_constant_override("separation", 6)

	# --- Extract action -------------------------------------------------
	var extract_action_btn := Button.new()
	extract_action_btn.text = "Extract Grains from Audio File…"
	extract_action_btn.pressed.connect(func():
		_reset_analysis_state()
		_reset_extraction_state()
		_navigate_to(PAGE_ANALYSIS)
	)
	parent.add_child(extract_action_btn)
	parent.add_child(HSeparator.new())

	# --- Summary --------------------------------------------------------
	var summary_panel := PanelContainer.new()
	parent.add_child(summary_panel)
	var summary_box := VBoxContainer.new()
	summary_panel.add_child(summary_box)
	_overview_summary_label = RichTextLabel.new()
	_overview_summary_label.bbcode_enabled = true
	_overview_summary_label.fit_content = true
	_overview_summary_label.scroll_active = false
	_overview_summary_label.custom_minimum_size.y = 50
	summary_box.add_child(_overview_summary_label)

	_overview_sources_label = Label.new()
	_overview_sources_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	summary_box.add_child(_overview_sources_label)

	# --- Compression mode ---
	var compression_row := HBoxContainer.new()
	summary_box.add_child(compression_row)
	compression_row.add_child(_make_label("PCM compression:"))
	_compression_opt = OptionButton.new()
	_compression_opt.add_item("Float32 – full quality", 0)
	_compression_opt.add_item("Int16 – half size", 1)
	_compression_opt.add_item("µ-law 8-bit – quarter size", 2)
	_compression_opt.item_selected.connect(_on_compression_mode_selected)
	compression_row.add_child(_compression_opt)

	# --- Axes -----------------------------------------------------------
	parent.add_child(_make_section_header("Parameter axes"))
	_overview_axes_box = VBoxContainer.new()
	parent.add_child(_overview_axes_box)

	# --- Filters --------------------------------------------------------
	parent.add_child(HSeparator.new())
	parent.add_child(_make_section_header("Filters"))

	var filter_row1 := HBoxContainer.new()
	parent.add_child(filter_row1)

	filter_row1.add_child(_make_label("Source:"))
	_filter_source_opt = OptionButton.new()
	_filter_source_opt.item_selected.connect(_on_filter_source_changed)
	filter_row1.add_child(_filter_source_opt)

	filter_row1.add_child(_make_label("  Energy:"))
	_filter_energy_opt = OptionButton.new()
	_filter_energy_opt.add_item("All", 0)
	_filter_energy_opt.add_item("Low-energy only", 1)
	_filter_energy_opt.add_item("High-energy only", 2)
	_filter_energy_opt.item_selected.connect(_on_filter_energy_changed)
	filter_row1.add_child(_filter_energy_opt)

	filter_row1.add_child(_make_label("  Hz min:"))
	_filter_hz_min_spin = _make_spin(0.0, 48000.0, 1.0, 0.0)
	_filter_hz_min_spin.value_changed.connect(func(_v): _on_filter_changed())
	filter_row1.add_child(_filter_hz_min_spin)
	filter_row1.add_child(_make_label("  max:"))
	_filter_hz_max_spin = _make_spin(0.0, 48000.0, 1.0, 0.0)
	_filter_hz_max_spin.tooltip_text = "0 = no upper bound"
	_filter_hz_max_spin.value_changed.connect(func(_v): _on_filter_changed())
	filter_row1.add_child(_filter_hz_max_spin)

	_filter_axes_box = VBoxContainer.new()
	parent.add_child(_filter_axes_box)

	# --- Sort -----------------------------------------------------------
	var sort_row := HBoxContainer.new()
	parent.add_child(sort_row)
	sort_row.add_child(_make_label("Sort by:"))
	_sort_key_opt = OptionButton.new()
	_sort_key_opt.item_selected.connect(_on_sort_changed)
	sort_row.add_child(_sort_key_opt)
	_sort_dir_opt = OptionButton.new()
	_sort_dir_opt.add_item("Asc", 0)
	_sort_dir_opt.add_item("Desc", 1)
	_sort_dir_opt.item_selected.connect(_on_sort_changed)
	sort_row.add_child(_sort_dir_opt)

	# --- Sequential playback --------------------------------------------
	var seq_row := HBoxContainer.new()
	parent.add_child(seq_row)
	_seq_play_btn = Button.new()
	_seq_play_btn.text = "▶ Play All"
	_seq_play_btn.tooltip_text = "Play filtered grains from the selected item downward (Space to pause/resume)"
	_seq_play_btn.pressed.connect(_on_seq_play_pressed)
	seq_row.add_child(_seq_play_btn)
	var seq_stop_btn := Button.new()
	seq_stop_btn.text = "■ Stop"
	seq_stop_btn.pressed.connect(_on_seq_stop)
	seq_row.add_child(seq_stop_btn)
	_seq_status_label = Label.new()
	_seq_status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	seq_row.add_child(_seq_status_label)

	# --- Grain list + detail pane ---------------------------------------
	parent.add_child(HSeparator.new())

	var split := HSplitContainer.new()
	split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	parent.add_child(split)

	var list_panel := PanelContainer.new()
	list_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(list_panel)
	var list_box := VBoxContainer.new()
	list_panel.add_child(list_box)
	list_box.add_child(_make_label("Grains (multi-select with Ctrl/Shift)"))
	_grain_list = ItemList.new()
	_grain_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_grain_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_grain_list.select_mode = ItemList.SELECT_MULTI
	_grain_list.item_selected.connect(_on_grain_list_item_selected)
	_grain_list.multi_selected.connect(_on_grain_list_multi_selected)
	_grain_list.item_activated.connect(_on_grain_list_item_activated)
	list_box.add_child(_grain_list)

	var detail_panel := PanelContainer.new()
	detail_panel.custom_minimum_size.x = 320
	detail_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(detail_panel)
	var detail_box := VBoxContainer.new()
	detail_panel.add_child(detail_box)
	detail_box.add_child(_make_section_header("Selected grain"))

	_grain_waveform = Control.new()
	_grain_waveform.custom_minimum_size = Vector2(280, 80)
	_grain_waveform.draw.connect(_on_grain_waveform_draw)
	detail_box.add_child(_grain_waveform)

	_grain_detail_label = RichTextLabel.new()
	_grain_detail_label.bbcode_enabled = true
	_grain_detail_label.fit_content = true
	_grain_detail_label.scroll_active = false
	_grain_detail_label.custom_minimum_size.y = 100
	detail_box.add_child(_grain_detail_label)

	var detail_btn_row := HBoxContainer.new()
	detail_box.add_child(detail_btn_row)
	_grain_play_btn = Button.new()
	_grain_play_btn.text = "▶ Play"
	_grain_play_btn.disabled = true
	_grain_play_btn.pressed.connect(_on_grain_play_pressed)
	detail_btn_row.add_child(_grain_play_btn)
	_grain_delete_btn = Button.new()
	_grain_delete_btn.text = "Delete"
	_grain_delete_btn.disabled = true
	_grain_delete_btn.pressed.connect(_on_grain_delete_pressed)
	detail_btn_row.add_child(_grain_delete_btn)

	# --- Bulk actions ---------------------------------------------------
	parent.add_child(HSeparator.new())
	var bulk_row := HBoxContainer.new()
	parent.add_child(bulk_row)
	bulk_row.add_child(_make_label("Bulk:"))
	_bulk_delete_selected_btn = Button.new()
	_bulk_delete_selected_btn.text = "Delete selected"
	_bulk_delete_selected_btn.pressed.connect(_on_bulk_delete_selected)
	bulk_row.add_child(_bulk_delete_selected_btn)
	_bulk_delete_filtered_btn = Button.new()
	_bulk_delete_filtered_btn.text = "Delete filtered (visible)"
	_bulk_delete_filtered_btn.pressed.connect(_on_bulk_delete_filtered)
	bulk_row.add_child(_bulk_delete_filtered_btn)
	_bulk_delete_low_energy_btn = Button.new()
	_bulk_delete_low_energy_btn.text = "Delete all low-energy"
	_bulk_delete_low_energy_btn.pressed.connect(_on_bulk_delete_low_energy)
	bulk_row.add_child(_bulk_delete_low_energy_btn)


# =============================================================================
# Analysis & Parameters page
# =============================================================================

func _build_analysis_page(root: VBoxContainer) -> void:
	# --- Page header ---
	var page_header := HBoxContainer.new()
	root.add_child(page_header)
	var back_btn := Button.new()
	back_btn.text = "← Overview"
	back_btn.pressed.connect(func(): _navigate_to(PAGE_OVERVIEW))
	page_header.add_child(back_btn)
	var page_title := Label.new()
	page_title.text = "  Analysis & Parameters"
	page_title.add_theme_font_size_override("font_size", 14)
	page_header.add_child(page_title)

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

	_low_hz_mode_check = CheckBox.new()
	_low_hz_mode_check.text = "Low Hz mode"
	_low_hz_mode_check.tooltip_text = "Time-domain onset detection for sub-100 Hz fundamentals (impulse trains, slow LFOs)."
	_low_hz_mode_check.toggled.connect(_on_low_hz_mode_toggled)
	row1b.add_child(_low_hz_mode_check)

	# Low Hz mode controls (hidden by default).
	_low_hz_row = HBoxContainer.new()
	_low_hz_row.visible = false
	root.add_child(_low_hz_row)

	_low_hz_row.add_child(_make_label("Hz min:"))
	_low_hz_min_spin = _make_spin(0.5, 200, 0.5, 5.0)
	_low_hz_min_spin.step = 0.5
	_low_hz_row.add_child(_low_hz_min_spin)

	_low_hz_row.add_child(_make_label("  Hz max:"))
	_low_hz_max_spin = _make_spin(1, 200, 1, 100.0)
	_low_hz_row.add_child(_low_hz_max_spin)

	_low_hz_row.add_child(_make_label("  Sensitivity:"))
	_sensitivity_spin = _make_spin(0.0, 1.0, 0.05, 0.5)
	_sensitivity_spin.step = 0.05
	_sensitivity_spin.tooltip_text = "Higher = more onsets pass the adaptive threshold."
	_low_hz_row.add_child(_sensitivity_spin)

	_low_hz_row.add_child(_make_label("  (sketch hint constrains detected cycles to ±octave of the sketched Hz)"))

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

	_log_hz_check = CheckBox.new()
	_log_hz_check.text = "Log Hz"
	_log_hz_check.button_pressed = true
	_log_hz_check.toggled.connect(_on_log_hz_toggled)
	row2.add_child(_log_hz_check)

	_autofit_btn = Button.new()
	_autofit_btn.text = "Auto-fit"
	_autofit_btn.pressed.connect(_on_autofit_pressed)
	row2.add_child(_autofit_btn)

	var zoom_fit_btn := Button.new()
	zoom_fit_btn.text = "Zoom Fit"
	zoom_fit_btn.tooltip_text = "Reset spectrogram zoom to show the full audio duration."
	zoom_fit_btn.pressed.connect(_analysis_zoom_fit)
	row2.add_child(zoom_fit_btn)

	# --- Row 3: status label -----------------------------------------
	_status_label = Label.new()
	_status_label.text = "Open or create a database, then pick an AudioStream and Analyze."
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	_status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.add_child(_status_label)

	# --- Spectrogram + side panel ------------------------------------
	var split := HBoxContainer.new()
	split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split)

	# Left column: spectrogram + parameter axes (same width, aligned).
	var left_col := VBoxContainer.new()
	left_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left_col.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(left_col)

	var frame := PanelContainer.new()
	frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	frame.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left_col.add_child(frame)

	var view_holder := Control.new()
	view_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	view_holder.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view_holder.clip_contents = true
	frame.add_child(view_holder)

	_spectrogram_view = Control.new()
	_spectrogram_view.anchor_right = 1.0
	_spectrogram_view.anchor_bottom = 1.0
	_spectrogram_view.mouse_filter = Control.MOUSE_FILTER_PASS
	_spectrogram_view.draw.connect(_on_analysis_spec_draw)
	_spectrogram_view.resized.connect(_spectrogram_view.queue_redraw)
	view_holder.add_child(_spectrogram_view)

	_overlay = Control.new()
	_overlay.anchor_right = 1.0
	_overlay.anchor_bottom = 1.0
	_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	_overlay.gui_input.connect(_on_overlay_gui_input)
	_overlay.draw.connect(_on_overlay_draw)
	_overlay.mouse_exited.connect(_on_overlay_mouse_exited)
	view_holder.add_child(_overlay)

	# Side panel: 3-phase Track Fundamental operation.
	var side := VBoxContainer.new()
	side.custom_minimum_size.x = 240
	side.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(side)

	# --- Phase 1: Pick Harmonics ---
	_picks_panel = VBoxContainer.new()
	_picks_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	side.add_child(_picks_panel)

	_picks_panel.add_child(_make_section_header("1. Pick Harmonics"))
	_picks_list = ItemList.new()
	_picks_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_picks_list.custom_minimum_size.y = 100
	_picks_list.item_selected.connect(_on_pick_list_selected)
	_picks_panel.add_child(_picks_list)

	var order_row := HBoxContainer.new()
	_picks_panel.add_child(order_row)
	order_row.add_child(_make_label("Order:"))
	_order_spin = _make_spin(1, 64, 1, 1)
	_order_spin.value_changed.connect(_on_order_spin_changed)
	order_row.add_child(_order_spin)

	_delete_pick_btn = Button.new()
	_delete_pick_btn.text = "Delete pick"
	_delete_pick_btn.pressed.connect(_on_delete_pick_pressed)
	_picks_panel.add_child(_delete_pick_btn)

	_clear_picks_btn = Button.new()
	_clear_picks_btn.text = "Clear picks"
	_clear_picks_btn.pressed.connect(_on_clear_picks_pressed)
	_picks_panel.add_child(_clear_picks_btn)

	# --- Phase 2: Sketch Fundamental ---
	side.add_child(HSeparator.new())
	side.add_child(_make_section_header("2. Sketch Fundamental"))

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

	# --- Phase 3: Track Fundamental ---
	side.add_child(HSeparator.new())
	side.add_child(_make_section_header("3. Track Fundamental"))

	_tracker_params_row = VBoxContainer.new()
	side.add_child(_tracker_params_row)

	var tp_row_a := HBoxContainer.new()
	_tracker_params_row.add_child(tp_row_a)
	tp_row_a.add_child(_make_label("BW (Hz):"))
	_bandwidth_spin = _make_spin(1, 5000, 1, DEFAULT_BANDWIDTH_HZ)
	_bandwidth_spin.custom_minimum_size.x = 60
	tp_row_a.add_child(_bandwidth_spin)
	tp_row_a.add_child(_make_label("  Max slope:"))
	_max_slope_spin = _make_spin(10, 100000, 10, DEFAULT_MAX_SLOPE)
	_max_slope_spin.custom_minimum_size.x = 70
	tp_row_a.add_child(_max_slope_spin)

	var tp_row_b := HBoxContainer.new()
	_tracker_params_row.add_child(tp_row_b)
	tp_row_b.add_child(_make_label("Penalty:"))
	_transition_penalty_spin = _make_spin(0.0, 100.0, 0.5, DEFAULT_TRANSITION_PENALTY)
	_transition_penalty_spin.step = 0.5
	_transition_penalty_spin.custom_minimum_size.x = 60
	tp_row_b.add_child(_transition_penalty_spin)
	tp_row_b.add_child(_make_label("  Stop drop:"))
	_stop_drop_spin = _make_spin(1.0, 120.0, 1.0, DEFAULT_STOP_DROP_DB)
	_stop_drop_spin.step = 1.0
	_stop_drop_spin.custom_minimum_size.x = 60
	_stop_drop_spin.tooltip_text = "Frames whose harmonic-mean obs falls more than this many dB below the seed peak are tagged as gaps. With a sketch hint present, tracking follows the sketch through gaps and resumes when energy returns."
	tp_row_b.add_child(_stop_drop_spin)

	_track_btn = Button.new()
	_track_btn.text = "Track fundamental"
	_track_btn.disabled = true
	_track_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_track_btn.pressed.connect(_on_track_pressed)
	side.add_child(_track_btn)

	_play_btn = Button.new()
	_play_btn.text = "Play isolated"
	_play_btn.disabled = true
	_play_btn.pressed.connect(_on_play_pressed)
	side.add_child(_play_btn)

	_player = AudioStreamPlayer.new()
	add_child(_player)
	_player.finished.connect(func(): _spec_playback_playing = false)

	# --- Parameter axes (inside left_col so they align with the spectrogram) ---
	left_col.add_child(HSeparator.new())
	left_col.add_child(_make_section_header("Parameter Axes"))
	left_col.add_child(_make_label("Axis definitions lock once first grains are imported. Calibration stays editable per import."))
	_axis_container = VBoxContainer.new()
	left_col.add_child(_axis_container)

	_add_axis_btn = Button.new()
	_add_axis_btn.text = "Add Parameter Axis"
	_add_axis_btn.pressed.connect(_on_add_axis_pressed)
	left_col.add_child(_add_axis_btn)

	# --- Continue button ---
	root.add_child(HSeparator.new())
	_analysis_continue_btn = Button.new()
	_analysis_continue_btn.text = "Continue to Extract Grains →"
	_analysis_continue_btn.disabled = true
	_analysis_continue_btn.pressed.connect(_on_analysis_continue_pressed)
	root.add_child(_analysis_continue_btn)

	_refresh_picks_list()


# =============================================================================
# Extract Grains page
# =============================================================================

func _build_extract_page(root: VBoxContainer) -> void:
	root.add_theme_constant_override("separation", 4)

	# --- Page header ---
	var page_header := HBoxContainer.new()
	root.add_child(page_header)
	var back_btn := Button.new()
	back_btn.text = "← Analysis"
	back_btn.pressed.connect(func(): _navigate_to(PAGE_ANALYSIS))
	page_header.add_child(back_btn)
	var page_title := Label.new()
	page_title.text = "  Extract Grains"
	page_title.add_theme_font_size_override("font_size", 14)
	page_header.add_child(page_title)

	# --- Method selector ---
	var method_row := HBoxContainer.new()
	root.add_child(method_row)
	_extract_method_fft_btn = Button.new()
	_extract_method_fft_btn.text = "FFT Automatic"
	_extract_method_fft_btn.toggle_mode = true
	_extract_method_fft_btn.button_pressed = true
	_extract_method_fft_btn.pressed.connect(func(): _on_extract_method_selected(0))
	method_row.add_child(_extract_method_fft_btn)
	_extract_method_manual_btn = Button.new()
	_extract_method_manual_btn.text = "Manual"
	_extract_method_manual_btn.toggle_mode = true
	_extract_method_manual_btn.button_pressed = false
	_extract_method_manual_btn.pressed.connect(func(): _on_extract_method_selected(1))
	method_row.add_child(_extract_method_manual_btn)
	_extract_method_semiauto_btn = Button.new()
	_extract_method_semiauto_btn.text = "Semi-Automatic"
	_extract_method_semiauto_btn.toggle_mode = true
	_extract_method_semiauto_btn.button_pressed = false
	_extract_method_semiauto_btn.pressed.connect(func(): _on_extract_method_selected(2))
	method_row.add_child(_extract_method_semiauto_btn)

	# --- FFT Automatic panel ---
	_fft_extract_panel = VBoxContainer.new()
	root.add_child(_fft_extract_panel)

	var fft_extract_row := HBoxContainer.new()
	_fft_extract_panel.add_child(fft_extract_row)

	fft_extract_row.add_child(_make_label("Pitch marks / cycle step:"))
	_cycles_spin = _make_spin(2, 6, 1, 3)
	_cycles_spin.editable = false
	_cycles_spin.tooltip_text = "Legacy input kept for compatibility. PSOLA extraction now emits one unit per cycle."
	fft_extract_row.add_child(_cycles_spin)

	fft_extract_row.add_child(_make_label("  Window overlap:"))
	_crossfade_spin = _make_spin(0.25, 1.0, 0.25, 0.5)
	_crossfade_spin.step = 0.25
	_crossfade_spin.editable = false
	_crossfade_spin.tooltip_text = "Legacy input kept for compatibility. Runtime now uses Hann-windowed PSOLA overlap-add instead of grain crossfades."
	fft_extract_row.add_child(_crossfade_spin)

	fft_extract_row.add_child(_make_label("  Energy threshold (dB):"))
	_energy_thresh_spin = _make_spin(-120, 0, 1, -40)
	fft_extract_row.add_child(_energy_thresh_spin)

	# --- Auto extraction spectrogram + region selector ---
	var auto_spec_panel := PanelContainer.new()
	auto_spec_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	auto_spec_panel.custom_minimum_size.y = 180
	_fft_extract_panel.add_child(auto_spec_panel)

	var auto_spec_holder := Control.new()
	auto_spec_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	auto_spec_holder.size_flags_vertical   = Control.SIZE_EXPAND_FILL
	auto_spec_holder.clip_contents = true
	auto_spec_panel.add_child(auto_spec_holder)

	_auto_spec_ctrl = Control.new()
	_auto_spec_ctrl.anchor_right  = 1.0
	_auto_spec_ctrl.anchor_bottom = 1.0
	_auto_spec_ctrl.mouse_filter  = Control.MOUSE_FILTER_IGNORE
	_auto_spec_ctrl.draw.connect(_on_auto_spec_draw)
	auto_spec_holder.add_child(_auto_spec_ctrl)

	_auto_overlay_ctrl = Control.new()
	_auto_overlay_ctrl.anchor_right  = 1.0
	_auto_overlay_ctrl.anchor_bottom = 1.0
	_auto_overlay_ctrl.mouse_filter  = Control.MOUSE_FILTER_STOP
	_auto_overlay_ctrl.draw.connect(_on_auto_overlay_draw)
	_auto_overlay_ctrl.gui_input.connect(_on_auto_overlay_input)
	_auto_overlay_ctrl.mouse_exited.connect(func(): _auto_overlay_ctrl.queue_redraw())
	auto_spec_holder.add_child(_auto_overlay_ctrl)

	var fft_extract_btn_row := HBoxContainer.new()
	_fft_extract_panel.add_child(fft_extract_btn_row)

	_import_btn = Button.new()
	_import_btn.text = "Extract Grains into Database"
	_import_btn.pressed.connect(_on_import_pressed)
	fft_extract_btn_row.add_child(_import_btn)

	var clear_region_btn := Button.new()
	clear_region_btn.text = "Clear Region"
	clear_region_btn.pressed.connect(func():
		_extract_region_start = -1.0
		_extract_region_end   = -1.0
		_auto_overlay_ctrl.queue_redraw()
	)
	fft_extract_btn_row.add_child(clear_region_btn)

	_extract_status_label = Label.new()
	_extract_status_label.text = ""
	_fft_extract_panel.add_child(_extract_status_label)

	# --- Manual panel ---
	_manual_extract_panel = VBoxContainer.new()
	_manual_extract_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_manual_extract_panel.visible = false
	root.add_child(_manual_extract_panel)

	# Toolbar
	var toolbar := HBoxContainer.new()
	_manual_extract_panel.add_child(toolbar)

	_manual_info_label = Label.new()
	_manual_info_label.text = "Analyze audio in the Analysis page first."
	_manual_info_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(_manual_info_label)

	var zoom_fit_btn := Button.new()
	zoom_fit_btn.text = "Zoom Fit"
	zoom_fit_btn.pressed.connect(_manual_zoom_fit)
	toolbar.add_child(zoom_fit_btn)

	_manual_snap_check = CheckBox.new()
	_manual_snap_check.text = "Snap to zero-crossing"
	_manual_snap_check.button_pressed = true
	toolbar.add_child(_manual_snap_check)

	var undo_btn := Button.new()
	undo_btn.text = "Undo"
	undo_btn.pressed.connect(_manual_undo)
	toolbar.add_child(undo_btn)

	_semiauto_lookahead_row = HBoxContainer.new()
	_semiauto_lookahead_row.visible = false
	toolbar.add_child(_semiauto_lookahead_row)
	var la_lbl := Label.new()
	la_lbl.text = "  Lookahead:"
	_semiauto_lookahead_row.add_child(la_lbl)
	_semiauto_lookahead_label = Label.new()
	_semiauto_lookahead_label.text = str(_semiauto_lookahead)
	_semiauto_lookahead_row.add_child(_semiauto_lookahead_label)

	# Spectrogram + overlay area
	var spec_panel := PanelContainer.new()
	spec_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spec_panel.size_flags_vertical   = Control.SIZE_EXPAND_FILL
	spec_panel.custom_minimum_size.y = 200
	_manual_extract_panel.add_child(spec_panel)

	var spec_holder := Control.new()
	spec_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spec_holder.size_flags_vertical   = Control.SIZE_EXPAND_FILL
	spec_holder.clip_contents = true
	spec_panel.add_child(spec_holder)

	_manual_spec_ctrl = Control.new()
	_manual_spec_ctrl.anchor_right  = 1.0
	_manual_spec_ctrl.anchor_bottom = 1.0
	_manual_spec_ctrl.mouse_filter  = Control.MOUSE_FILTER_IGNORE
	_manual_spec_ctrl.draw.connect(_on_manual_spec_draw)
	spec_holder.add_child(_manual_spec_ctrl)

	_manual_overlay = Control.new()
	_manual_overlay.anchor_right  = 1.0
	_manual_overlay.anchor_bottom = 1.0
	_manual_overlay.mouse_filter  = Control.MOUSE_FILTER_STOP
	_manual_overlay.focus_mode    = Control.FOCUS_ALL
	_manual_overlay.gui_input.connect(_on_manual_overlay_gui_input)
	_manual_overlay.draw.connect(_on_manual_overlay_draw)
	_manual_overlay.mouse_exited.connect(func():
		_manual_hovered_grain_idx = -1
		_manual_overlay.queue_redraw())
	spec_holder.add_child(_manual_overlay)

	# Waveform strip
	var waveform_row := HBoxContainer.new()
	_manual_extract_panel.add_child(waveform_row)

	_manual_waveform_check = CheckBox.new()
	_manual_waveform_check.text = "Show waveform"
	_manual_waveform_check.toggled.connect(func(p: bool):
		_manual_waveform_ctrl.visible = p
		if p: _manual_rebuild_waveform_pcm()
	)
	waveform_row.add_child(_manual_waveform_check)

	waveform_row.add_child(_make_label("  Gain:"))
	_manual_waveform_gain_spin = _make_spin(1.0, 32.0, 0.5, 1.0)
	_manual_waveform_gain_spin.custom_minimum_size.x = 60
	_manual_waveform_gain_spin.tooltip_text = "Vertical amplitude multiplier for waveform display. Increase for quiet audio."
	_manual_waveform_gain_spin.value_changed.connect(func(_v: float): _manual_waveform_ctrl.queue_redraw())
	waveform_row.add_child(_manual_waveform_gain_spin)

	_manual_waveform_ctrl = Control.new()
	_manual_waveform_ctrl.custom_minimum_size.y = 56
	_manual_waveform_ctrl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_manual_waveform_ctrl.visible = false
	_manual_waveform_ctrl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_manual_waveform_ctrl.draw.connect(_on_manual_waveform_draw)
	_manual_extract_panel.add_child(_manual_waveform_ctrl)

	# Extraction settings
	_manual_extract_panel.add_child(HSeparator.new())

	var extract_row := HBoxContainer.new()
	_manual_extract_panel.add_child(extract_row)

	extract_row.add_child(_make_label("Energy threshold (dB):"))
	_manual_energy_spin = _make_spin(-120, 0, 1, -40)
	extract_row.add_child(_manual_energy_spin)

	_manual_import_btn = Button.new()
	_manual_import_btn.text = "Extract Manual Grains"
	_manual_import_btn.disabled = true
	_manual_import_btn.pressed.connect(_on_manual_import_pressed)
	extract_row.add_child(_manual_import_btn)

	_manual_extract_status_label = Label.new()
	_manual_extract_status_label.text = ""
	_manual_extract_panel.add_child(_manual_extract_status_label)

	_manual_status_label = Label.new()
	_manual_status_label.text = "Click on spectrogram to add markers. Right-click removes. Ctrl+Z to undo. ←/→ navigate markers, Shift+←/→ move 1 ms, Ctrl+Shift+←/→ move 10 ms."
	_manual_extract_panel.add_child(_manual_status_label)


func _navigate_to(page: int) -> void:
	_current_page = page
	_overview_panel.visible = (page == PAGE_OVERVIEW)
	_analysis_panel.visible = (page == PAGE_ANALYSIS)
	_extract_panel.visible  = (page == PAGE_EXTRACT)
	match page:
		PAGE_ANALYSIS: _on_analysis_page_entered()
		PAGE_EXTRACT:  _on_extract_page_entered()


func _on_analysis_page_entered() -> void:
	_refresh_analysis_continue_btn()
	if _fft != null and _analysis_view_end <= _analysis_view_start:
		_analysis_zoom_fit()
	if _fft != null and _manual_view_end <= _manual_view_start:
		_manual_zoom_fit()
	_spectrogram_view.queue_redraw()
	_overlay.queue_redraw()
	_manual_redraw_lanes()
	_refresh_axis_previews()


func _on_extract_page_entered() -> void:
	_reset_extraction_state()
	_on_extract_method_selected(_extract_method)
	if _auto_spec_ctrl != null:
		_auto_spec_ctrl.queue_redraw()
	if _auto_overlay_ctrl != null:
		_auto_overlay_ctrl.queue_redraw()


func _reset_extraction_state() -> void:
	_extract_region_start = -1.0
	_extract_region_end   = -1.0
	_extract_region_dragging = false
	if _extract_status_label:
		_extract_status_label.text = ""
	if _manual_extract_status_label:
		_manual_extract_status_label.text = ""
	_manual_markers = PackedFloat64Array()
	_manual_undo_stack = []
	_manual_selected_marker_idx = -1
	_manual_dragging_marker_idx = -1
	_semiauto_drag_initial_markers = PackedFloat64Array()
	_semiauto_drag_start_t = 0.0


func _reset_analysis_state() -> void:
	_fft = null
	_tracked_curve = PackedVector2Array()
	_grain_boundaries = PackedVector2Array()
	_picks.clear()
	_reference_time = -1.0
	_selected_pick_index = -1
	_dragging_pick_index = -1
	_sketch_points = PackedVector2Array()
	for ad in _axis_rows:
		ad["keyframes_arr"] = []
	_manual_spec_texture = null
	if _spectrogram_view:
		_spectrogram_view.queue_redraw()
	if _status_label:
		_status_label.text = "Pick an AudioStream and click Analyze."
	if _overlay:
		_overlay.queue_redraw()
	if _auto_spec_ctrl:
		_auto_spec_ctrl.queue_redraw()
	if _manual_spec_ctrl:
		_manual_spec_ctrl.queue_redraw()
	_refresh_analysis_continue_btn()


func _on_extract_method_selected(method: int) -> void:
	_extract_method = method
	_fft_extract_panel.visible    = (method == 0)
	_manual_extract_panel.visible = (method == 1 or method == 2)
	_extract_method_fft_btn.button_pressed      = (method == 0)
	_extract_method_manual_btn.button_pressed   = (method == 1)
	_extract_method_semiauto_btn.button_pressed = (method == 2)
	_semiauto_lookahead_row.visible = (method == 2)
	if method == 1:
		_manual_status_label.text = "Click on spectrogram to add markers. Right-click removes. Ctrl+Z to undo. ←/→ navigate markers, Shift+←/→ move 1 ms, Ctrl+Shift+←/→ move 10 ms."
	elif method == 2:
		_manual_status_label.text = "Click to place first marker; markers auto-fill. Drag to adjust (propagates forward). Ctrl+drag = no propagation. Scroll wheel during drag = adjust lookahead."
	if method == 1 or method == 2:
		_manual_refresh_info()
		if _manual_waveform_check.button_pressed and _manual_waveform_pcm.is_empty():
			_manual_rebuild_waveform_pcm()
		_manual_spec_ctrl.queue_redraw()
		_manual_overlay.queue_redraw()
		_manual_waveform_ctrl.queue_redraw()
		if _fft != null and _manual_view_end <= _manual_view_start:
			_manual_zoom_fit()


func _refresh_analysis_continue_btn() -> void:
	if _analysis_continue_btn == null:
		return
	_analysis_continue_btn.disabled = (_fft == null or _axis_rows.is_empty())


func _on_analysis_continue_pressed() -> void:
	_navigate_to(PAGE_EXTRACT)


func _manual_refresh_info() -> void:
	if _fft == null:
		_manual_info_label.text = "Analyze audio in the Analysis page first."
		_manual_import_btn.disabled = true
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	if _manual_view_end <= _manual_view_start:
		_manual_zoom_fit()
	var n_markers := _manual_markers.size()
	var n_grains  := maxi(n_markers - 1, 0)
	_manual_info_label.text = "%d markers  /  %d grain%s  |  view: %.2f – %.2f s  (total %.2f s)" % [
		n_markers, n_grains, "s" if n_grains != 1 else "", _manual_view_start, _manual_view_end, total_dur]
	_manual_import_btn.disabled = (_grains_database == null or n_markers < 2)


func _manual_zoom_fit() -> void:
	if _fft == null:
		return
	_manual_view_start = 0.0
	_manual_view_end   = _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	_manual_spec_ctrl.queue_redraw()
	_manual_overlay.queue_redraw()
	_manual_redraw_lanes()
	_manual_refresh_info()


# --- Waveform strip helpers ----------------------------------------------------

func _manual_rebuild_waveform_pcm() -> void:
	if _fft == null:
		return
	_manual_waveform_pcm = _fft.get_original_mono_pcm_packed()
	_manual_waveform_pcm_rate = _fft.get_sample_rate()
	_manual_waveform_ctrl.queue_redraw()


func _on_manual_waveform_draw() -> void:
	var ctrl := _manual_waveform_ctrl
	var sz := ctrl.size
	if sz.x <= 1 or sz.y <= 1 or _fft == null:
		return
	ctrl.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.06, 0.06, 0.08, 1.0))
	if _manual_waveform_pcm.is_empty() or _manual_waveform_pcm_rate <= 0:
		return
	var t_start := _manual_view_start
	var t_end   := _manual_view_end
	if t_end <= t_start:
		return
	var rate    := _manual_waveform_pcm_rate
	var n_pcm   := _manual_waveform_pcm.size()
	var s_start := clampi(int(t_start * rate), 0, n_pcm - 1)
	var s_end   := clampi(int(t_end   * rate), 0, n_pcm - 1)
	if s_end <= s_start:
		return
	var n_pix  := int(sz.x)
	var gain   := _manual_waveform_gain_spin.value
	var mid_y  := sz.y * 0.5
	var half_h := sz.y * 0.45
	var span   := float(s_end - s_start)
	var color  := Color(0.4, 0.9, 0.5, 0.85)
	# Draw a vertical min/max bar per pixel column so peaks are never missed.
	# Stride through samples to cap inner iterations at ~8 regardless of zoom level.
	for xi in n_pix:
		var si_lo := s_start + int(float(xi)     / float(n_pix) * span)
		var si_hi := s_start + int(float(xi + 1) / float(n_pix) * span)
		si_lo = clampi(si_lo, 0, n_pcm - 1)
		si_hi = clampi(si_hi, 0, n_pcm - 1)
		var step  := max(1, (si_hi - si_lo) / 8)
		var amp_min: float = _manual_waveform_pcm[si_lo]
		var amp_max: float = amp_min
		var si := si_lo
		while si <= si_hi:
			var s: float = _manual_waveform_pcm[si]
			if s < amp_min: amp_min = s
			if s > amp_max: amp_max = s
			si += step
		var y_top    := mid_y - clampf(amp_max * gain, -1.0, 1.0) * half_h
		var y_bottom := mid_y - clampf(amp_min * gain, -1.0, 1.0) * half_h
		if y_bottom - y_top < 1.0:
			y_bottom = y_top + 1.0
		ctrl.draw_line(Vector2(xi, y_top), Vector2(xi, y_bottom), color, 1.0)


# --- Spectrogram drawing -------------------------------------------------------

func _on_manual_spec_draw() -> void:
	var ctrl := _manual_spec_ctrl
	var sz   := ctrl.size
	ctrl.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.05, 0.05, 0.07, 1.0))
	if _fft == null or _manual_spec_texture == null:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	if total_dur <= 0:
		return
	var img_w := float(_manual_spec_texture.get_width())
	var img_h := float(_manual_spec_texture.get_height())
	var px_start := (_manual_view_start / total_dur) * img_w
	var px_end   := (_manual_view_end   / total_dur) * img_w
	px_start = clampf(px_start, 0, img_w)
	px_end   = clampf(px_end,   0, img_w)
	if px_end <= px_start:
		return
	var src := Rect2(px_start, 0, px_end - px_start, img_h)
	var dst := Rect2(Vector2.ZERO, sz)
	ctrl.draw_texture_rect_region(_manual_spec_texture, dst, src)

	# Draw time ruler ticks.
	var tick_step := _manual_nice_tick_step(_manual_view_end - _manual_view_start)
	if tick_step > 0:
		var font   := ThemeDB.fallback_font
		var t_first := ceilf(_manual_view_start / tick_step) * tick_step
		var t := t_first
		while t <= _manual_view_end + tick_step * 0.01:
			var x := _manual_time_to_px(t, sz.x)
			ctrl.draw_line(Vector2(x, 0), Vector2(x, 8), Color(1, 1, 1, 0.5), 1.0)
			ctrl.draw_string(font, Vector2(x + 2, 18), "%.2fs" % t,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color(1, 1, 1, 0.7))
			t += tick_step


func _manual_nice_tick_step(span: float) -> float:
	if span <= 0:
		return 0.0
	var raw := span / 6.0
	var mag := pow(10.0, floor(log(raw) / log(10.0)))
	for m in [1.0, 2.0, 5.0, 10.0]:
		if mag * m >= raw:
			return mag * m
	return mag * 10.0


# --- Overlay drawing -----------------------------------------------------------

func _on_manual_overlay_draw() -> void:
	var ctrl := _manual_overlay
	var sz   := ctrl.size
	if sz.x <= 0 or sz.y <= 0 or _fft == null:
		return

	# Highlight the hovered grain region (Ctrl+Space hint).
	if _manual_hovered_grain_idx >= 0 and _manual_hovered_grain_idx + 1 < _manual_markers.size() \
			and _manual_hovered_grain_idx != _preview_grain_idx:
		var px0 := _manual_time_to_px(float(_manual_markers[_manual_hovered_grain_idx]), sz.x)
		var px1 := _manual_time_to_px(float(_manual_markers[_manual_hovered_grain_idx + 1]), sz.x)
		ctrl.draw_rect(Rect2(px0, 0, px1 - px0, sz.y), Color(1.0, 1.0, 1.0, 0.07))

	# Highlight the currently-previewing grain region in cyan.
	if _preview_grain_idx >= 0 and _preview_grain_idx + 1 < _manual_markers.size():
		var px0 := _manual_time_to_px(float(_manual_markers[_preview_grain_idx]), sz.x)
		var px1 := _manual_time_to_px(float(_manual_markers[_preview_grain_idx + 1]), sz.x)
		ctrl.draw_rect(Rect2(px0, 0, px1 - px0, sz.y), Color(0.4, 1.0, 0.9, 0.22))
		# Playback cursor within the previewing region.
		if _preview_player.is_playing():
			var pos := _preview_player.get_playback_position()
			var start_s := float(_manual_markers[_preview_grain_idx])
			var px_cursor := _manual_time_to_px(start_s + pos, sz.x)
			ctrl.draw_line(Vector2(px_cursor, 0), Vector2(px_cursor, sz.y),
				Color(0.4, 1.0, 0.9, 0.9), 2.0)

	# Marker vertical lines + triangle handle at top.
	var n := _manual_markers.size()
	for i in n:
		var t  := float(_manual_markers[i])
		var x  := _manual_time_to_px(t, sz.x)
		var is_sel := (i == _manual_selected_marker_idx)
		var line_col := Color(1.0, 0.9, 0.2, 0.95) if is_sel else Color(1.0, 0.55, 0.1, 0.85)
		ctrl.draw_line(Vector2(x, 0), Vector2(x, sz.y), line_col, 1.5)
		# Triangle handle.
		var tri := PackedVector2Array([
			Vector2(x, 0), Vector2(x - 6, -8), Vector2(x + 6, -8)])
		ctrl.draw_colored_polygon(tri, line_col)
		# Grain number + duration label between consecutive markers.
		if i < n - 1:
			var gx := (_manual_time_to_px(float(_manual_markers[i]), sz.x) +
					   _manual_time_to_px(float(_manual_markers[i + 1]), sz.x)) * 0.5
			ctrl.draw_string(ThemeDB.fallback_font, Vector2(gx - 6, 16),
				str(i + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color(1, 1, 1, 0.7))
			var dur_ms := int(round((_manual_markers[i + 1] - _manual_markers[i]) * 1000.0))
			ctrl.draw_string(ThemeDB.fallback_font, Vector2(gx - 6, 28),
				str(dur_ms) + " ms", HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color(1, 1, 1, 0.55))

	# Playback position cursor.
	if _spec_playback_playing and _player.is_playing():
		var play_t := _player.get_playback_position()
		var play_x := _manual_time_to_px(play_t, sz.x)
		ctrl.draw_line(Vector2(play_x, 0), Vector2(play_x, sz.y), Color(0.2, 0.9, 1.0, 0.9), 2.0)


# --- Overlay input (zoom, pan, marker add/move/remove, keyboard navigation) ----

func _on_manual_overlay_gui_input(event: InputEvent) -> void:
	if _fft == null:
		return

	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton

		# Wheel: adjust lookahead during semi-auto drag, otherwise zoom.
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP or mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			if _extract_method == 2 and _manual_dragging_marker_idx >= 0:
				var delta := 1 if mb.button_index == MOUSE_BUTTON_WHEEL_UP else -1
				_semiauto_lookahead = clampi(_semiauto_lookahead + delta, 0, 50)
				_semiauto_lookahead_label.text = str(_semiauto_lookahead)
				_semiauto_propagate(_manual_dragging_marker_idx)
				_manual_overlay.queue_redraw()
				_manual_spec_ctrl.queue_redraw()
				_manual_redraw_lanes()
				_manual_refresh_info()
				return
			var sz_x := _manual_overlay.size.x
			var cursor_t := _manual_px_to_time(mb.position.x, sz_x)
			var zoom_in  := mb.button_index == MOUSE_BUTTON_WHEEL_UP
			var factor   := 0.8 if zoom_in else 1.25
			var new_span := (_manual_view_end - _manual_view_start) * factor
			var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
			new_span = clampf(new_span, 0.05, total_dur)
			_manual_view_start = cursor_t - (cursor_t - _manual_view_start) * factor
			_manual_view_end   = _manual_view_start + new_span
			_manual_view_start = maxf(_manual_view_start, 0.0)
			_manual_view_end   = minf(_manual_view_end, total_dur)
			_manual_spec_ctrl.queue_redraw()
			_manual_overlay.queue_redraw()
			_manual_waveform_ctrl.queue_redraw()
			_manual_redraw_lanes()
			_manual_refresh_info()
			return

		# Pan: middle-mouse press/release.
		if mb.button_index == MOUSE_BUTTON_MIDDLE:
			if mb.pressed:
				_manual_panning = true
				_manual_pan_start_pos  = mb.position
				_manual_pan_start_view = Vector2(_manual_view_start, _manual_view_end)
			else:
				_manual_panning = false
			return

		# Left-button: add marker or start drag.
		if mb.button_index == MOUSE_BUTTON_LEFT and mb.pressed:
			_manual_overlay.grab_focus()
			var sz_x := _manual_overlay.size.x
			var t    := _manual_px_to_time(mb.position.x, sz_x)
			var hit  := _manual_find_marker_near(mb.position.x, sz_x)
			if hit >= 0:
				_manual_dragging_marker_idx = hit
				_manual_selected_marker_idx = hit
				if _extract_method == 2:
					_semiauto_drag_start_t = float(_manual_markers[hit])
					_semiauto_drag_initial_markers = _manual_markers.duplicate()
				_manual_overlay.queue_redraw()
			else:
				_manual_push_undo()
				t = _manual_snap(t)
				_manual_insert_marker(t)
				_manual_dragging_marker_idx = _manual_selected_marker_idx
				if _extract_method == 2:
					_semiauto_drag_start_t = float(_manual_markers[_manual_dragging_marker_idx])
					_semiauto_drag_initial_markers = _manual_markers.duplicate()
					if _manual_markers.size() == 1:
						_semiauto_autofill(t)
						_semiauto_drag_initial_markers = _manual_markers.duplicate()
				_manual_refresh_info()
			return

		if mb.button_index == MOUSE_BUTTON_LEFT and not mb.pressed:
			if _manual_dragging_marker_idx >= 0:
				_manual_dragging_marker_idx = -1
				_manual_sort_markers()
			_manual_overlay.queue_redraw()
			return

		# Right-click: remove marker.
		if mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			var sz_x := _manual_overlay.size.x
			var hit  := _manual_find_marker_near(mb.position.x, sz_x)
			if hit >= 0:
				_manual_push_undo()
				_manual_markers.remove_at(hit)
				if _manual_selected_marker_idx >= _manual_markers.size():
					_manual_selected_marker_idx = _manual_markers.size() - 1
				_manual_overlay.queue_redraw()
				_manual_refresh_info()
			return

	if event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		if _manual_panning:
			var sz_x := _manual_overlay.size.x
			if sz_x > 0:
				var dt := -(mm.position.x - _manual_pan_start_pos.x) / sz_x * \
						  (_manual_pan_start_view.y - _manual_pan_start_view.x)
				var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
				var span := _manual_pan_start_view.y - _manual_pan_start_view.x
				_manual_view_start = clampf(_manual_pan_start_view.x + dt, 0.0, total_dur - span)
				_manual_view_end   = _manual_view_start + span
				_manual_spec_ctrl.queue_redraw()
				_manual_overlay.queue_redraw()
				_manual_waveform_ctrl.queue_redraw()
				_manual_redraw_lanes()
				_manual_refresh_info()
			return
		if _manual_dragging_marker_idx >= 0:
			var sz_x := _manual_overlay.size.x
			# In semi-auto mode, pass the pre-drag interval as the period hint so
			# the dragged marker snaps within period/8 rather than the 10 ms fallback
			# (which spans multiple cycles for any pitch above ~100 Hz).
			var drag_hint_s := -1.0
			if _extract_method == 2 and _semiauto_drag_initial_markers.size() > _manual_dragging_marker_idx:
				var di := _manual_dragging_marker_idx
				if di > 0:
					drag_hint_s = float(_semiauto_drag_initial_markers[di]) \
							- float(_semiauto_drag_initial_markers[di - 1])
				elif _semiauto_drag_initial_markers.size() > 1:
					drag_hint_s = float(_semiauto_drag_initial_markers[1]) \
							- float(_semiauto_drag_initial_markers[0])
			var t := _manual_snap(_manual_px_to_time(mm.position.x, sz_x), drag_hint_s)
			_manual_markers[_manual_dragging_marker_idx] = t
			if _extract_method == 2 and not mm.ctrl_pressed:
				_semiauto_propagate(_manual_dragging_marker_idx)
			_manual_overlay.queue_redraw()
			_manual_spec_ctrl.queue_redraw()
			_manual_redraw_lanes()
			_manual_refresh_info()
			return
		# Update hovered grain region for Ctrl+Space preview.
		var sz_x := _manual_overlay.size.x
		var t := _manual_px_to_time(mm.position.x, sz_x)
		var gi := _find_grain_region_at(t)
		if gi != _manual_hovered_grain_idx:
			_manual_hovered_grain_idx = gi
			_manual_overlay.queue_redraw()

	# Keyboard: undo, navigate markers, move selected marker.
	if event is InputEventKey:
		var ke := event as InputEventKey
		if not ke.pressed:
			return
		if ke.keycode == KEY_Z and ke.ctrl_pressed and not ke.shift_pressed:
			_manual_undo()
			return
		var n_markers := _manual_markers.size()
		if ke.keycode == KEY_LEFT and not ke.shift_pressed and not ke.ctrl_pressed:
			if n_markers > 0 and _manual_selected_marker_idx > 0:
				_manual_selected_marker_idx -= 1
				_manual_scroll_to_selected()
				_manual_overlay.queue_redraw()
		elif ke.keycode == KEY_RIGHT and not ke.shift_pressed and not ke.ctrl_pressed:
			if n_markers > 0 and _manual_selected_marker_idx < n_markers - 1:
				_manual_selected_marker_idx += 1
				_manual_scroll_to_selected()
				_manual_overlay.queue_redraw()
		elif ke.keycode == KEY_LEFT and ke.shift_pressed:
			if _manual_selected_marker_idx >= 0 and _manual_selected_marker_idx < n_markers:
				var step := 0.010 if ke.ctrl_pressed else 0.001
				var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
				_manual_push_undo()
				var moved_t := clampf(float(_manual_markers[_manual_selected_marker_idx]) - step, 0.0, total_dur)
				_manual_markers[_manual_selected_marker_idx] = moved_t
				_manual_sort_markers()
				_manual_selected_marker_idx = _manual_find_marker_value(moved_t)
				_manual_overlay.queue_redraw()
				_manual_spec_ctrl.queue_redraw()
				_manual_redraw_lanes()
				_manual_refresh_info()
		elif ke.keycode == KEY_RIGHT and ke.shift_pressed:
			if _manual_selected_marker_idx >= 0 and _manual_selected_marker_idx < n_markers:
				var step := 0.010 if ke.ctrl_pressed else 0.001
				var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
				_manual_push_undo()
				var moved_t := clampf(float(_manual_markers[_manual_selected_marker_idx]) + step, 0.0, total_dur)
				_manual_markers[_manual_selected_marker_idx] = moved_t
				_manual_sort_markers()
				_manual_selected_marker_idx = _manual_find_marker_value(moved_t)
				_manual_overlay.queue_redraw()
				_manual_spec_ctrl.queue_redraw()
				_manual_redraw_lanes()
				_manual_refresh_info()


# --- Auto extraction spectrogram drawing and input ----------------------------

func _on_auto_spec_draw() -> void:
	var ctrl := _auto_spec_ctrl
	var sz   := ctrl.size
	ctrl.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.05, 0.05, 0.07, 1.0))
	if _fft == null or _manual_spec_texture == null:
		ctrl.draw_string(ThemeDB.fallback_font, Vector2(8, sz.y * 0.5),
			"Analyze audio first.", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(0.7, 0.7, 0.7, 0.7))
		return
	ctrl.draw_texture_rect(_manual_spec_texture, Rect2(Vector2.ZERO, sz), false)
	# Draw tracked curve as a yellow polyline.
	if _tracked_curve.size() >= 2:
		var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
		if total_dur > 0:
			var yr := float(_fft.get_y_resolution())
			var min_hz := _fft.pixel_y_to_hz(int(yr) - 1)
			var max_hz := _fft.pixel_y_to_hz(0)
			var hz_range := max_hz - min_hz
			var pts := PackedVector2Array()
			for pt in _tracked_curve:
				if pt.y <= 0:
					continue
				var x := pt.x / total_dur * sz.x
				var norm_y := 1.0 - clampf((pt.y - min_hz) / maxf(hz_range, 1.0), 0.0, 1.0)
				pts.append(Vector2(x, norm_y * sz.y))
			if pts.size() >= 2:
				ctrl.draw_polyline(pts, Color(1.0, 0.9, 0.1, 0.85), 1.5)


func _on_auto_overlay_draw() -> void:
	var ctrl := _auto_overlay_ctrl
	var sz   := ctrl.size
	if sz.x <= 0 or sz.y <= 0 or _fft == null:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	if total_dur <= 0:
		return

	# Draw selection region.
	var r_start := _extract_region_start
	var r_end   := _extract_region_end
	if _extract_region_dragging:
		# During drag, r_end may be before r_start — display both cases.
		pass
	if r_start >= 0.0 and r_end >= 0.0 and r_start != r_end:
		var t0 := minf(r_start, r_end)
		var t1 := maxf(r_start, r_end)
		var x0 := t0 / total_dur * sz.x
		var x1 := t1 / total_dur * sz.x
		# Fill.
		ctrl.draw_rect(Rect2(x0, 0, x1 - x0, sz.y), Color(0.3, 0.6, 1.0, 0.25))
		# Edge lines.
		ctrl.draw_line(Vector2(x0, 0), Vector2(x0, sz.y), Color(0.4, 0.7, 1.0, 0.9), 2.0)
		ctrl.draw_line(Vector2(x1, 0), Vector2(x1, sz.y), Color(0.4, 0.7, 1.0, 0.9), 2.0)
		# Time labels.
		var font := ThemeDB.fallback_font
		ctrl.draw_string(font, Vector2(x0 + 3, 14), "%.2fs" % t0,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color(0.8, 0.9, 1.0, 1.0))
		ctrl.draw_string(font, Vector2(x1 + 3, 14), "%.2fs" % t1,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color(0.8, 0.9, 1.0, 1.0))

	# Mouse cursor time line (from global mouse position).
	var local_mouse := ctrl.get_local_mouse_position()
	if local_mouse.x >= 0 and local_mouse.x <= sz.x:
		ctrl.draw_line(Vector2(local_mouse.x, 0), Vector2(local_mouse.x, sz.y),
			Color(1.0, 1.0, 1.0, 0.3), 1.0)
		var t_cursor := local_mouse.x / sz.x * total_dur
		ctrl.draw_string(ThemeDB.fallback_font, Vector2(local_mouse.x + 3, sz.y - 6),
			"%.2fs" % t_cursor, HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color(1, 1, 1, 0.6))

	# Playback position cursor.
	if _spec_playback_playing and _player.is_playing():
		var play_t := _player.get_playback_position()
		var play_x := play_t / total_dur * sz.x
		ctrl.draw_line(Vector2(play_x, 0), Vector2(play_x, sz.y), Color(0.2, 0.9, 1.0, 0.9), 2.0)


func _on_auto_overlay_input(event: InputEvent) -> void:
	if _fft == null:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	if total_dur <= 0:
		return
	var sz_x := _auto_overlay_ctrl.size.x
	if sz_x <= 0:
		return

	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.pressed:
				_extract_region_start = clampf(mb.position.x / sz_x * total_dur, 0.0, total_dur)
				_extract_region_end   = _extract_region_start
				_extract_region_dragging = true
				_auto_overlay_ctrl.queue_redraw()
			else:
				_extract_region_dragging = false
				# Swap so start < end.
				if _extract_region_end < _extract_region_start:
					var tmp := _extract_region_start
					_extract_region_start = _extract_region_end
					_extract_region_end   = tmp
				# Treat a zero-size selection as a seek click: start playback
				# from the clicked time so the user can audition any spot.
				if absf(_extract_region_end - _extract_region_start) < 1e-4:
					var seek_t := clampf(mb.position.x / sz_x * total_dur, 0.0, total_dur)
					_extract_region_start = -1.0
					_extract_region_end   = -1.0
					if _stream and _player.stream:
						_player.play(seek_t)
						_spec_playback_playing = true
				_auto_overlay_ctrl.queue_redraw()
		elif mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			_extract_region_start = -1.0
			_extract_region_end   = -1.0
			_extract_region_dragging = false
			_auto_overlay_ctrl.queue_redraw()

	elif event is InputEventMouseMotion:
		if _extract_region_dragging:
			_extract_region_end = clampf(
				(event as InputEventMouseMotion).position.x / sz_x * total_dur, 0.0, total_dur)
			_auto_overlay_ctrl.queue_redraw()
		else:
			_auto_overlay_ctrl.queue_redraw()


# --- Marker helpers ------------------------------------------------------------

func _manual_time_to_px(t: float, width: float) -> float:
	var span := _manual_view_end - _manual_view_start
	if span <= 0:
		return 0.0
	return (t - _manual_view_start) / span * width


func _manual_px_to_time(px: float, width: float) -> float:
	if width <= 0:
		return _manual_view_start
	return _manual_view_start + px / width * (_manual_view_end - _manual_view_start)


func _manual_find_marker_near(px: float, width: float, radius_px: float = 8.0) -> int:
	var best_idx := -1
	var best_d   := radius_px
	for i in _manual_markers.size():
		var mx := _manual_time_to_px(float(_manual_markers[i]), width)
		var d  := absf(mx - px)
		if d < best_d:
			best_d   = d
			best_idx = i
	return best_idx


func _manual_insert_marker(t: float) -> void:
	var total_dur := 0.0
	if _fft != null:
		total_dur = _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	t = clampf(t, 0.0, total_dur)
	# Sorted insertion.
	var insert_at := _manual_markers.size()
	for i in _manual_markers.size():
		if float(_manual_markers[i]) > t:
			insert_at = i
			break
	_manual_markers.insert(insert_at, t)
	_manual_selected_marker_idx = insert_at
	_manual_overlay.queue_redraw()
	_manual_spec_ctrl.queue_redraw()
	_manual_redraw_lanes()


func _manual_sort_markers() -> void:
	var arr := Array(_manual_markers)
	arr.sort()
	_manual_markers = PackedFloat64Array(arr)


func _manual_snap(t: float, hint_period_s: float = -1.0) -> float:
	if not _manual_snap_check.button_pressed:
		return t
	if _manual_waveform_pcm.is_empty() or _manual_waveform_pcm_rate <= 0:
		_manual_rebuild_waveform_pcm()
	if _manual_waveform_pcm.is_empty():
		return t
	var rate    := _manual_waveform_pcm_rate
	var si      := int(round(t * rate))
	# Use a period-aware snap window (period/8) when the caller knows the grain
	# interval, otherwise fall back to a fixed ~10 ms window.  The old approach
	# scaled with view span and could span multiple cycles, causing markers to
	# snap to the wrong zero-crossing and producing unequal grain intervals →
	# mismatched Hann window sizes at playback → phasing artifacts.
	var snap_win: int
	if hint_period_s > 0.0:
		snap_win = maxi(4, int(hint_period_s * rate / 8.0))
	else:
		snap_win = maxi(4, int(rate * 0.01))
	var best_si  := si
	var best_d   := snap_win + 1
	var n_pcm    := _manual_waveform_pcm.size()
	for s in range(-snap_win, snap_win + 1):
		var idx := si + s
		if idx <= 0 or idx + 1 >= n_pcm:
			continue
		# Upward zero-crossing.
		if _manual_waveform_pcm[idx] <= 0.0 and _manual_waveform_pcm[idx + 1] > 0.0:
			var d := abs(s)
			if d < best_d:
				best_d  = d
				best_si = idx
	return float(best_si) / rate


func _manual_push_undo() -> void:
	_manual_undo_stack.append(_manual_markers.duplicate())
	if _manual_undo_stack.size() > 64:
		_manual_undo_stack.pop_front()


func _manual_undo() -> void:
	if _manual_undo_stack.is_empty():
		return
	_manual_markers = _manual_undo_stack.pop_back()
	_manual_selected_marker_idx = -1
	_manual_overlay.queue_redraw()
	_manual_spec_ctrl.queue_redraw()
	_manual_redraw_lanes()
	_manual_refresh_info()


# Find the index of the marker whose value is closest to t (within 1e-9 s tolerance).
func _manual_find_marker_value(t: float) -> int:
	for i in _manual_markers.size():
		if absf(float(_manual_markers[i]) - t) < 1e-9:
			return i
	return -1


# Pan the view so the selected marker is visible.
func _manual_scroll_to_selected() -> void:
	if _fft == null or _manual_selected_marker_idx < 0 or _manual_selected_marker_idx >= _manual_markers.size():
		return
	var t := float(_manual_markers[_manual_selected_marker_idx])
	if t >= _manual_view_start and t <= _manual_view_end:
		return
	var span := _manual_view_end - _manual_view_start
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	_manual_view_start = clampf(t - span * 0.5, 0.0, maxf(0.0, total_dur - span))
	_manual_view_end   = _manual_view_start + span
	_manual_spec_ctrl.queue_redraw()
	_manual_waveform_ctrl.queue_redraw()
	_manual_redraw_lanes()


# --- Semi-automatic helpers ----------------------------------------------------

# Fill _semiauto_lookahead markers after from_t at 100 ms intervals.
func _semiauto_autofill(from_t: float) -> void:
	if _fft == null:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	var interval := 0.100
	for k in range(1, _semiauto_lookahead + 1):
		var t := from_t + interval * k
		if t > total_dur:
			break
		t = _manual_snap(t, interval)
		_manual_markers.append(t)
	_manual_sort_markers()
	_manual_overlay.queue_redraw()
	_manual_spec_ctrl.queue_redraw()
	_manual_redraw_lanes()


# Propagate the interval established by dragging marker at dragged_idx forward
# to _semiauto_lookahead subsequent markers.  Creates or removes markers at the
# tail so the lookahead window is always exactly _semiauto_lookahead markers long.
func _semiauto_propagate(dragged_idx: int) -> void:
	if _fft == null:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	var n := _manual_markers.size()

	if dragged_idx == 0:
		# First marker: shift all following markers by the same delta.
		if _semiauto_drag_initial_markers.size() == n:
			var delta := float(_manual_markers[0]) - _semiauto_drag_start_t
			for i in range(1, n):
				_manual_markers[i] = float(_semiauto_drag_initial_markers[i]) + delta
		return

	# General case: new interval = gap between marker before and dragged marker.
	var new_interval := float(_manual_markers[dragged_idx]) - float(_manual_markers[dragged_idx - 1])
	if new_interval <= 0.0:
		return

	# Compute target count of markers to maintain after dragged_idx.
	var target_after := _semiauto_lookahead

	# Resize the array so exactly target_after markers follow dragged_idx.
	var desired_size := dragged_idx + 1 + target_after
	while _manual_markers.size() > desired_size:
		_manual_markers.remove_at(_manual_markers.size() - 1)
	while _manual_markers.size() < desired_size:
		_manual_markers.append(0.0)  # placeholder, filled below

	# Place each lookahead marker.
	for k in range(1, target_after + 1):
		var t := float(_manual_markers[dragged_idx]) + new_interval * k
		t = clampf(t, 0.0, total_dur)
		t = _manual_snap(t, new_interval)
		_manual_markers[dragged_idx + k] = t


# --- Automation lanes ----------------------------------------------------------

const _LANE_HEIGHT := 64
const _LANE_KF_RADIUS := 5.0


func _manual_redraw_lanes() -> void:
	for ad in _axis_rows:
		if ad.has("lane_ctrl") and ad["lane_ctrl"] != null:
			ad["lane_ctrl"].queue_redraw()


func _manual_kf_to_lane_pos(kf_t: float, kf_v: float, ax_min: float, ax_max: float,
		lane_sz: Vector2) -> Vector2:
	var x := _analysis_time_to_px(kf_t, lane_sz.x)
	var range_v := ax_max - ax_min
	var norm_v  := (kf_v - ax_min) / maxf(range_v, 1e-6)
	var y := lane_sz.y * (1.0 - clampf(norm_v, 0, 1))
	return Vector2(x, y)


func _on_manual_lane_draw(lane: Control, axis_data: Dictionary) -> void:
	var ax_min: float = axis_data["min"].value
	var ax_max: float = axis_data["max"].value
	var kfs: Array = axis_data["keyframes_arr"]
	var sz := lane.size
	lane.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.07, 0.07, 0.10, 1.0))
	lane.draw_line(Vector2(0, sz.y * 0.5), Vector2(sz.x, sz.y * 0.5),
		Color(0.3, 0.3, 0.35, 0.4), 1.0)

	if kfs.is_empty():
		return

	# Interpolation polyline (every pixel).
	var pts := PackedVector2Array()
	var n_px := int(sz.x)
	pts.resize(n_px)
	for xi in n_px:
		var t := _analysis_px_to_time(float(xi), sz.x)
		var v := _manual_kf_lerp(kfs, t, ax_min, ax_max)
		pts[xi] = Vector2(xi, sz.y * (1.0 - clampf((v - ax_min) / max(ax_max - ax_min, 1e-6), 0, 1)))
	if pts.size() >= 2:
		lane.draw_polyline(pts, Color(0.3, 0.8, 1.0, 0.8), 1.5)

	# Keyframe dots.
	for ki in kfs.size():
		var kf: Dictionary = kfs[ki]
		var pos := _manual_kf_to_lane_pos(kf.t, kf.v, ax_min, ax_max, sz)
		var is_drag := (is_same(axis_data, _manual_dragging_kf_axis_data) and ki == _manual_dragging_kf_idx)
		var col := Color(1.0, 0.9, 0.2, 1.0) if is_drag else Color(0.4, 0.85, 1.0, 1.0)
		lane.draw_circle(pos, _LANE_KF_RADIUS, col)
		lane.draw_circle(pos, _LANE_KF_RADIUS, Color(0, 0, 0, 0.8), false, 1.5)


func _manual_kf_lerp(kfs: Array, t: float, ax_min: float, ax_max: float) -> float:
	if kfs.is_empty():
		return (ax_min + ax_max) * 0.5
	if t <= float(kfs[0].t):
		return float(kfs[0].v)
	if t >= float(kfs[kfs.size() - 1].t):
		return float(kfs[kfs.size() - 1].v)
	for i in kfs.size() - 1:
		var t0 := float(kfs[i].t)
		var t1 := float(kfs[i + 1].t)
		if t >= t0 and t <= t1:
			var frac := (t - t0) / maxf(t1 - t0, 1e-9)
			return lerp(float(kfs[i].v), float(kfs[i + 1].v), frac)
	return float(kfs[kfs.size() - 1].v)


func _on_manual_lane_input(event: InputEvent, lane: Control, axis_data: Dictionary) -> void:
	var ax_min: float = axis_data["min"].value
	var ax_max: float = axis_data["max"].value
	var kfs: Array = axis_data["keyframes_arr"]
	var sz := lane.size

	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT and mb.pressed:
			var hit := _manual_find_kf_near(kfs, mb.position, ax_min, ax_max, sz)
			if hit >= 0:
				_manual_dragging_kf_axis_data = axis_data
				_manual_dragging_kf_idx       = hit
			else:
				var t := _analysis_px_to_time(mb.position.x, sz.x)
				var range_v := ax_max - ax_min
				var v := ax_min + (1.0 - mb.position.y / sz.y) * range_v
				v = clampf(v, ax_min, ax_max)
				_manual_kf_insert(kfs, t, v)
				_manual_dragging_kf_axis_data = axis_data
				_manual_dragging_kf_idx       = _manual_kf_find_at(kfs, t)
			lane.queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_LEFT and not mb.pressed:
			_manual_dragging_kf_axis_data = null
			_manual_dragging_kf_idx       = -1
			lane.queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			var hit := _manual_find_kf_near(kfs, mb.position, ax_min, ax_max, sz)
			if hit >= 0:
				kfs.remove_at(hit)
				lane.queue_redraw()
			return

	if event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		if is_same(axis_data, _manual_dragging_kf_axis_data) and _manual_dragging_kf_idx >= 0:
			var ki  := _manual_dragging_kf_idx
			if ki < kfs.size():
				var t := _analysis_px_to_time(mm.position.x, sz.x)
				var range_v := ax_max - ax_min
				var v := ax_min + (1.0 - mm.position.y / sz.y) * range_v
				kfs[ki] = { "t": t, "v": clampf(v, ax_min, ax_max) }
				kfs.sort_custom(func(a, b): return float(a.t) < float(b.t))
				_manual_dragging_kf_idx = _manual_kf_find_at(kfs, t)
				lane.queue_redraw()
				lane.tooltip_text = "Value: %.3f" % clampf(v, ax_min, ax_max)
		else:
			var t := _analysis_px_to_time(mm.position.x, sz.x)
			var v := _manual_kf_lerp(kfs, t, ax_min, ax_max)
			lane.tooltip_text = "Value: %.3f" % v


func _manual_find_kf_near(kfs: Array, pos: Vector2, ax_min: float, ax_max: float,
		sz: Vector2) -> int:
	var best_i := -1
	var best_d := _LANE_KF_RADIUS * 2.0
	for ki in kfs.size():
		var kp := _manual_kf_to_lane_pos(float(kfs[ki].t), float(kfs[ki].v), ax_min, ax_max, sz)
		var d  := pos.distance_to(kp)
		if d < best_d:
			best_d = d
			best_i = ki
	return best_i


func _manual_kf_insert(kfs: Array, t: float, v: float) -> void:
	for ki in kfs.size():
		if is_equal_approx(float(kfs[ki].t), t):
			kfs[ki].v = v
			return
	kfs.append({ "t": t, "v": v })
	kfs.sort_custom(func(a, b): return float(a.t) < float(b.t))


func _manual_kf_find_at(kfs: Array, t: float) -> int:
	for ki in kfs.size():
		if is_equal_approx(float(kfs[ki].t), t):
			return ki
	return kfs.size() - 1


# --- Import action (manual) ----------------------------------------------------

func _on_manual_import_pressed() -> void:
	if _grains_database == null:
		_manual_extract_status_label.text = "Open or create a database first."
		return
	if _fft == null:
		_manual_extract_status_label.text = "Analyze audio in the Analysis page first."
		return
	if _manual_markers.size() < 2:
		_manual_extract_status_label.text = "Place at least 2 markers."
		return
	if _stream == null:
		_manual_extract_status_label.text = "No AudioStream loaded (analyze in the Analysis page)."
		return

	_manual_extract_status_label.text = "Importing..."
	await get_tree().process_frame

	var axis_configs := _build_axis_configs()
	var source_path := _stream.resource_path if _stream != null else ""
	var added: PackedInt32Array = _grains_database.append_from_manual_markers(
		_fft,
		_stream,
		source_path,
		_manual_markers,
		axis_configs,
		_manual_energy_spin.value,
		_tracked_curve)

	if added.size() == 0:
		_manual_extract_status_label.text = "Import produced 0 grains. (See output for errors.)"
	else:
		_manual_extract_status_label.text = "Imported %d grain%s (database total: %d)." % [
			added.size(), "s" if added.size() != 1 else "", _grains_database.get_grain_count()]
		_mark_dirty()
		_refresh_overview()
		_navigate_to(PAGE_ANALYSIS)
		return
	_manual_refresh_info()


# =============================================================================
# Common UI helpers
# =============================================================================

func _make_label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l


func _make_section_header(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 14)
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


func _process(delta: float) -> void:
	if _analyzing:
		_dot_timer += delta
		if _dot_timer >= 0.4:
			_dot_timer -= 0.4
			_dot_count = (_dot_count % 3) + 1
			_status_label.text = "Analyzing" + ".".repeat(_dot_count)
		if _analysis_thread and not _analysis_thread.is_alive():
			_finish_analysis()

	if _spec_playback_playing:
		if _player.is_playing():
			match _current_page:
				PAGE_ANALYSIS:
					_overlay.queue_redraw()
				PAGE_EXTRACT:
					if _extract_method == 0:
						_auto_overlay_ctrl.queue_redraw()
					else:
						_manual_overlay.queue_redraw()
		else:
			_spec_playback_playing = false

	if _preview_player and _preview_player.is_playing() and _manual_overlay:
		_manual_overlay.queue_redraw()


# =============================================================================
# Database management
# =============================================================================

func _on_db_picker_changed(res: Resource) -> void:
	# Avoid re-entry: setting the picker to the same resource we just opened
	# would otherwise force a refresh.
	var db := res as StreamWeaverGrainsDatabase
	if db == _grains_database:
		return
	_set_database(db)
	if db != null and db.resource_path != "":
		_db_path = db.resource_path


func _on_new_database_pressed() -> void:
	var dialog := FileDialog.new()
	dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dialog.filters = PackedStringArray(["*.res ; Godot Binary Resource", "*.tres ; Godot Text Resource (legacy)"])
	dialog.access = FileDialog.ACCESS_RESOURCES
	dialog.current_file = "grains_database.res"
	dialog.file_selected.connect(func(path: String):
		var fresh := StreamWeaverGrainsDatabase.new()
		var err := ResourceSaver.save(fresh, path)
		if err != OK:
			_status_label.text = "New database save failed: error %d" % err
			dialog.queue_free()
			return
		var loaded := load(path) as StreamWeaverGrainsDatabase
		_db_path = path
		_set_database(loaded)
		_db_picker.edited_resource = loaded
		_status_label.text = "Created %s" % path
		dialog.queue_free()
	)
	dialog.canceled.connect(func(): dialog.queue_free())
	add_child(dialog)
	dialog.popup_centered(Vector2i(700, 500))


func _on_save_pressed() -> void:
	if _grains_database == null:
		return
	if _db_path.is_empty():
		_on_save_as_pressed()
		return
	_save_to_path(_db_path)


func _on_save_as_pressed() -> void:
	if _grains_database == null:
		return
	var dialog := FileDialog.new()
	dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dialog.filters = PackedStringArray(["*.res ; Godot Binary Resource", "*.tres ; Godot Text Resource (legacy)"])
	dialog.access = FileDialog.ACCESS_RESOURCES
	dialog.current_file = "grains_database.res" if _db_path.is_empty() else _db_path.get_file()
	dialog.file_selected.connect(func(path: String):
		_db_path = path
		_save_to_path(path)
		dialog.queue_free()
	)
	dialog.canceled.connect(func(): dialog.queue_free())
	add_child(dialog)
	dialog.popup_centered(Vector2i(700, 500))


func _save_to_path(path: String) -> void:
	if _compact_on_save_check.button_pressed and not _grains_database.is_compact():
		_grains_database.compact_pcm_pool()
	var err := ResourceSaver.save(_grains_database, path)
	if err == OK:
		_dirty = false
		_refresh_header()
		_refresh_overview()
		_status_label.text = "Saved to %s" % path
	else:
		_status_label.text = "Save failed: error %d" % err


func _on_compact_pressed() -> void:
	if _grains_database == null:
		return
	_grains_database.compact_pcm_pool()
	_mark_dirty()
	_refresh_overview()
	_status_label.text = "Compacted PCM pool."


func _on_compression_mode_selected(index: int) -> void:
	if _grains_database == null:
		return
	var new_mode: int = _compression_opt.get_item_id(index)
	if new_mode == _grains_database.get_compression_mode():
		return
	# If there is existing PCM data, warn the user that this is destructive.
	if _grains_database.get_pcm_pool_byte_count() > 0:
		var mode_names := ["Float32", "Int16", "µ-law 8-bit"]
		var from_name: String = mode_names[_grains_database.get_compression_mode()]
		var to_name: String = mode_names[new_mode]
		var dlg := ConfirmationDialog.new()
		dlg.title = "Re-compress PCM data?"
		dlg.dialog_text = (
			"This will re-encode all %d grains from %s to %s.\n\n"
			% [_grains_database.get_grain_count(), from_name, to_name]
			+ "Downsampling (especially to µ-law 8-bit) permanently reduces audio quality. "
			+ "This cannot be undone without re-extracting from the source files.\n\n"
			+ "Proceed?"
		)
		dlg.confirmed.connect(func():
			_grains_database.recompress_pcm_pool(new_mode)
			_mark_dirty()
			_refresh_overview()
			_status_label.text = "PCM re-compressed to %s." % to_name
			dlg.queue_free()
		)
		dlg.canceled.connect(func():
			# Revert the dropdown to the current (unchanged) mode.
			_compression_opt.selected = _grains_database.get_compression_mode()
			dlg.queue_free()
		)
		add_child(dlg)
		dlg.popup_centered()
	else:
		# Empty pool — just set the mode for future extractions.
		_grains_database.set_compression_mode(new_mode)
		_mark_dirty()
		_status_label.text = "Compression mode set to %s (applies to future extractions)." % \
			_compression_opt.get_item_text(index)


func _set_database(db: StreamWeaverGrainsDatabase) -> void:
	_grains_database = db
	if db == null:
		_db_path = ""
	else:
		_db_path = db.resource_path
	_dirty = false
	_detail_grain_index = -1
	_filtered_indices = PackedInt32Array()
	_on_seq_stop()

	# Reset transient extraction state when switching databases.
	_picks.clear()
	_reference_time = -1.0
	_selected_pick_index = -1
	_sketch_points = PackedVector2Array()
	# Reset manual extraction state.
	_manual_markers = PackedFloat64Array()
	for ad in _axis_rows:
		ad["keyframes_arr"] = []
	_manual_undo_stack = []
	_manual_selected_marker_idx = -1
	_tracked_curve = PackedVector2Array()
	_grain_boundaries = PackedVector2Array()
	_refresh_analysis_continue_btn()
	if _overlay:
		_overlay.queue_redraw()

	_refresh_header()
	_rebuild_axis_editor_from_db()
	_refresh_overview()


func _mark_dirty() -> void:
	if _dirty:
		return
	_dirty = true
	_refresh_header()


func _refresh_header() -> void:
	var has_db := _grains_database != null
	_save_btn.disabled = not has_db
	_save_as_btn.disabled = not has_db
	_compact_btn.disabled = not has_db
	if not has_db:
		_title_label.text = "(no database open)"
		return
	var path_label := _db_path if not _db_path.is_empty() else "(unsaved)"
	_title_label.text = ("%s%s" % [path_label, "  *" if _dirty else ""])


# =============================================================================
# Overview refresh
# =============================================================================

func _refresh_overview() -> void:
	_refresh_overview_summary()
	_refresh_overview_axes()
	_refresh_overview_sources()
	_refresh_filter_controls()
	_refresh_sort_controls()
	_recompute_filtered_indices()
	_refresh_grain_list()
	_refresh_grain_detail()
	_grain_waveform.queue_redraw()


func _refresh_overview_summary() -> void:
	if _grains_database == null:
		_overview_summary_label.text = "[i]Open or create a database to begin.[/i]"
		_compression_opt.disabled = true
		return
	var db := _grains_database
	var count := db.get_grain_count()
	var rate := db.get_sample_rate()
	var pool_mb := db.get_pcm_pool_byte_count() * 1.0 / (1024.0 * 1024.0)
	var orphan_bytes := db.orphan_pcm_bytes()
	var orphan_mb := orphan_bytes / (1024.0 * 1024.0)
	var orphan_str := ""
	if orphan_bytes > 0:
		orphan_str = "  [color=#ff8a4a](%.2f MB orphan)[/color]" % orphan_mb
	_overview_summary_label.text = (
		"Grains: [b]%d[/b]   Sample rate: %d Hz   PCM pool: %.2f MB%s"
		% [count, int(rate), pool_mb, orphan_str])
	# Sync the compression dropdown without triggering the signal.
	_compression_opt.disabled = false
	_compression_opt.selected = db.get_compression_mode()


func _refresh_overview_axes() -> void:
	for c in _overview_axes_box.get_children():
		c.queue_free()
	if _grains_database == null:
		_overview_axes_box.add_child(_make_label("(no database open)"))
		return
	var db := _grains_database
	var n := db.get_num_axes()
	if n == 0:
		var l := _make_label("No axes defined yet — define on first import.")
		_overview_axes_box.add_child(l)
		return
	for i in n:
		var axis_name := String(db.get_axis_name(i))
		var lo := db.get_axis_min(i)
		var hi := db.get_axis_max(i)
		var derived: bool = db.get_axis_derived(i)
		var kind := "derived(Hz)" if derived else "manual"
		var l := _make_label("  • %s   [%f .. %f]   %s" % [axis_name, lo, hi, kind])
		_overview_axes_box.add_child(l)


func _refresh_overview_sources() -> void:
	if _grains_database == null:
		_overview_sources_label.text = ""
		return
	var db := _grains_database
	_source_paths = db.get_source_paths()
	# Tally per source id (-1 = unknown).
	var counts := {}
	var ids := db.get_grain_source_ids()
	for i in ids.size():
		var sid: int = ids[i]
		counts[sid] = int(counts.get(sid, 0)) + 1
	var parts: Array[String] = []
	if counts.has(-1):
		parts.append("(unknown): %d" % int(counts[-1]))
	for i in _source_paths.size():
		var c := int(counts.get(i, 0))
		if c == 0:
			continue
		parts.append("%s: %d" % [_source_paths[i].get_file(), c])
	if parts.is_empty():
		_overview_sources_label.text = "Sources: (none)"
	else:
		_overview_sources_label.text = "Sources: " + ", ".join(parts)


func _refresh_filter_controls() -> void:
	# Source dropdown
	var prev_source := _filter_source
	_filter_source_opt.clear()
	_filter_source_opt.add_item("All", -2)
	if _grains_database != null:
		_filter_source_opt.add_item("(unknown)", -1)
		for i in _source_paths.size():
			_filter_source_opt.add_item(_source_paths[i].get_file(), i)
	# Restore selection if it still exists in the dropdown.
	for i in _filter_source_opt.item_count:
		if _filter_source_opt.get_item_id(i) == prev_source:
			_filter_source_opt.select(i)
			break
	# If we couldn't restore, default to "All".
	if _filter_source_opt.get_selected_id() != prev_source:
		_filter_source = -2
		_filter_source_opt.select(0)

	# Per-axis filter rows
	for c in _filter_axes_box.get_children():
		c.queue_free()
	_filter_axis_rows.clear()
	if _grains_database == null:
		return
	var n := _grains_database.get_num_axes()
	# Resize filter arrays preserving previous values where possible.
	var new_min := PackedFloat32Array()
	var new_max := PackedFloat32Array()
	new_min.resize(n)
	new_max.resize(n)
	for i in n:
		var lo := _grains_database.get_axis_min(i)
		var hi := _grains_database.get_axis_max(i)
		new_min[i] = lo if i >= _filter_axis_min.size() else _filter_axis_min[i]
		new_max[i] = hi if i >= _filter_axis_max.size() else _filter_axis_max[i]
	_filter_axis_min = new_min
	_filter_axis_max = new_max

	for i in n:
		var row := HBoxContainer.new()
		_filter_axes_box.add_child(row)
		row.add_child(_make_label("  %s:" % String(_grains_database.get_axis_name(i))))
		var lo_spin := _make_spin(-1e9, 1e9, 0.01, _filter_axis_min[i])
		lo_spin.step = 0.01
		var hi_spin := _make_spin(-1e9, 1e9, 0.01, _filter_axis_max[i])
		hi_spin.step = 0.01
		var idx := i
		var on_lo := func(v: float) -> void: _on_filter_axis_value_changed(idx, true, v)
		var on_hi := func(v: float) -> void: _on_filter_axis_value_changed(idx, false, v)
		lo_spin.value_changed.connect(on_lo)
		hi_spin.value_changed.connect(on_hi)
		row.add_child(lo_spin)
		row.add_child(_make_label(".."))
		row.add_child(hi_spin)
		_filter_axis_rows.append({"min": lo_spin, "max": hi_spin})


func _on_filter_axis_value_changed(idx: int, is_min: bool, v: float) -> void:
	if is_min and idx < _filter_axis_min.size():
		_filter_axis_min[idx] = v
	elif not is_min and idx < _filter_axis_max.size():
		_filter_axis_max[idx] = v
	_on_filter_changed()


func _refresh_sort_controls() -> void:
	var prev := _sort_key
	_sort_key_opt.clear()
	_sort_key_opt.add_item("Index", 0)
	_sort_key_opt.add_item("Time", 1)
	_sort_key_opt.add_item("Hz", 2)
	_sort_key_opt.add_item("Source", 3)
	if _grains_database != null:
		var n := _grains_database.get_num_axes()
		for i in n:
			_sort_key_opt.add_item(String(_grains_database.get_axis_name(i)), 4 + i)
	# Restore prior key if still present.
	var found := false
	for i in _sort_key_opt.item_count:
		if _sort_key_opt.get_item_id(i) == prev:
			_sort_key_opt.select(i)
			found = true
			break
	if not found:
		_sort_key = 0
		_sort_key_opt.select(0)
	_sort_dir_opt.select(1 if _sort_descending else 0)


func _on_filter_source_changed(idx: int) -> void:
	_filter_source = _filter_source_opt.get_item_id(idx)
	_on_filter_changed()


func _on_filter_energy_changed(idx: int) -> void:
	_filter_energy = _filter_energy_opt.get_item_id(idx)
	_on_filter_changed()


func _on_filter_changed() -> void:
	_filter_hz_min = _filter_hz_min_spin.value
	_filter_hz_max = _filter_hz_max_spin.value
	_recompute_filtered_indices()
	_refresh_grain_list()


func _on_sort_changed(_idx: int) -> void:
	_sort_key = _sort_key_opt.get_selected_id()
	_sort_descending = _sort_dir_opt.get_selected_id() == 1
	_recompute_filtered_indices()
	_refresh_grain_list()


func _grain_passes_filter(meta: Dictionary) -> bool:
	if _filter_source != -2:
		if int(meta.get("source_id", -1)) != _filter_source:
			return false
	if _filter_energy == 1 and not bool(meta.get("low_energy", false)):
		return false
	if _filter_energy == 2 and bool(meta.get("low_energy", false)):
		return false
	var hz: float = meta.get("fundamental_hz", 0.0)
	if _filter_hz_min > 0.0 and hz < _filter_hz_min:
		return false
	if _filter_hz_max > 0.0 and hz > _filter_hz_max:
		return false
	var params: PackedFloat32Array = meta.get("params", PackedFloat32Array())
	for i in params.size():
		if i >= _filter_axis_min.size():
			break
		if params[i] < _filter_axis_min[i] or params[i] > _filter_axis_max[i]:
			return false
	return true


func _grain_sort_value(meta: Dictionary) -> float:
	if _sort_key == 0:
		return float(meta.get("center_sample", 0))
	if _sort_key == 1:
		return float(meta.get("original_time_s", 0.0))
	if _sort_key == 2:
		return float(meta.get("fundamental_hz", 0.0))
	if _sort_key == 3:
		return float(int(meta.get("source_id", -1)))
	var axis_index := _sort_key - 4
	var params: PackedFloat32Array = meta.get("params", PackedFloat32Array())
	if axis_index >= 0 and axis_index < params.size():
		return params[axis_index]
	return 0.0


func _recompute_filtered_indices() -> void:
	_filtered_indices = PackedInt32Array()
	if _grains_database == null:
		return
	var count := _grains_database.get_grain_count()
	var keep: Array[int] = []
	var values: Array[float] = []
	for i in count:
		var meta := _grains_database.get_grain_metadata(i)
		if not _grain_passes_filter(meta):
			continue
		keep.append(i)
		values.append(_grain_sort_value(meta))
	# Sort indices by their values.
	var order := range(keep.size())
	var cmp_asc := func(a, b): return values[a] < values[b]
	var cmp_desc := func(a, b): return values[a] > values[b]
	order.sort_custom(cmp_desc if _sort_descending else cmp_asc)
	_filtered_indices.resize(order.size())
	for i in order.size():
		_filtered_indices[i] = keep[order[i]]


func _refresh_grain_list() -> void:
	_grain_list.clear()
	if _grains_database == null:
		return
	for i in _filtered_indices.size():
		var grain_idx := _filtered_indices[i]
		var meta := _grains_database.get_grain_metadata(grain_idx)
		_grain_list.add_item(_format_grain_row(grain_idx, meta))
		_grain_list.set_item_metadata(i, grain_idx)
	if _detail_grain_index >= 0:
		# Re-select the detail grain in the new list, if still present.
		for i in _filtered_indices.size():
			if _filtered_indices[i] == _detail_grain_index:
				_grain_list.select(i)
				break


func _format_grain_row(grain_idx: int, meta: Dictionary) -> String:
	var t: float = meta.get("original_time_s", 0.0)
	var hz: float = meta.get("fundamental_hz", 0.0)
	var src_id: int = meta.get("source_id", -1)
	var src := "?"
	if src_id >= 0 and src_id < _source_paths.size():
		src = _source_paths[src_id].get_file()
	var energy_tag := "  E" if meta.get("low_energy", false) else ""
	var params: PackedFloat32Array = meta.get("params", PackedFloat32Array())
	var pstr := ""
	for p in params:
		pstr += "  %.2f" % p
	return "#%d  t=%.3fs  %.1fHz%s  src:%s%s" % [grain_idx, t, hz, pstr, src, energy_tag]


# =============================================================================
# Grain detail / audition / delete
# =============================================================================

func _on_grain_list_item_selected(idx: int) -> void:
	if idx < 0 or idx >= _filtered_indices.size():
		return
	_detail_grain_index = _filtered_indices[idx]
	_refresh_grain_detail()
	_grain_waveform.queue_redraw()


func _on_grain_list_multi_selected(idx: int, selected: bool) -> void:
	# Use the most recently toggled-on row as the detail grain.
	if selected and idx >= 0 and idx < _filtered_indices.size():
		_detail_grain_index = _filtered_indices[idx]
		_refresh_grain_detail()
		_grain_waveform.queue_redraw()


func _on_grain_list_item_activated(idx: int) -> void:
	if idx < 0 or idx >= _filtered_indices.size():
		return
	_detail_grain_index = _filtered_indices[idx]
	_refresh_grain_detail()
	_play_detail_grain()


func _refresh_grain_detail() -> void:
	var has_grain := _grains_database != null and _detail_grain_index >= 0
	_grain_play_btn.disabled = not has_grain
	_grain_delete_btn.disabled = not has_grain
	if not has_grain:
		_grain_detail_label.text = "[i]Select a grain to inspect.[/i]"
		return
	var meta := _grains_database.get_grain_metadata(_detail_grain_index)
	if meta.is_empty():
		_grain_detail_label.text = "[i](grain index out of range)[/i]"
		return
	var src_id: int = meta.get("source_id", -1)
	var src := "(unknown)"
	if src_id >= 0 and src_id < _source_paths.size():
		src = _source_paths[src_id]
	var params: PackedFloat32Array = meta.get("params", PackedFloat32Array())
	var lines := PackedStringArray()
	lines.append("[b]#%d[/b]" % _detail_grain_index)
	lines.append("time:    %.4f s" % float(meta.get("original_time_s", 0.0)))
	lines.append("f0:      %.2f Hz   period: %d samples" % [
		float(meta.get("fundamental_hz", 0.0)),
		int(meta.get("period_samples", 0))])
	lines.append("window:  %d samples" % int(meta.get("window_size", 0)))
	lines.append("low_energy: %s" % str(meta.get("low_energy", false)))
	for i in params.size():
		var axis_name := String(_grains_database.get_axis_name(i))
		lines.append("%s: %f" % [axis_name, params[i]])
	lines.append("source: %s" % src)
	_grain_detail_label.text = "\n".join(lines)


func _on_grain_play_pressed() -> void:
	_play_detail_grain()


func _on_seq_play_pressed() -> void:
	if _seq_playing and not _seq_paused:
		_seq_paused = true
		_grain_player.stop()
		_seq_play_btn.text = "▶ Resume"
	elif _seq_playing and _seq_paused:
		_seq_paused = false
		_seq_play_btn.text = "⏸ Pause"
		var sel := _grain_list.get_selected_items()
		if not sel.is_empty():
			_seq_pos = sel[0]
		_play_seq_grain()
	else:
		_start_seq_playback()


func _start_seq_playback() -> void:
	if _grains_database == null or _filtered_indices.is_empty():
		return
	var sel := _grain_list.get_selected_items()
	_seq_pos = sel[0] if not sel.is_empty() else 0
	_seq_playing = true
	_seq_paused  = false
	_seq_play_btn.text = "⏸ Pause"
	_play_seq_grain()


func _play_seq_grain() -> void:
	if not _seq_playing or _seq_paused:
		return
	if _seq_pos >= _filtered_indices.size():
		_on_seq_stop()
		return
	var grain_index := _filtered_indices[_seq_pos]
	_seq_status_label.text = "%d / %d" % [_seq_pos + 1, _filtered_indices.size()]
	_grain_list.deselect_all()
	_grain_list.select(_seq_pos)
	_grain_list.ensure_current_is_visible()
	_detail_grain_index = grain_index
	_refresh_grain_detail()
	_grain_waveform.queue_redraw()
	_seq_pos += 1
	_play_detail_grain()


func _on_grain_player_finished() -> void:
	if _seq_playing and not _seq_paused:
		_play_seq_grain()


func _on_seq_stop() -> void:
	_seq_playing = false
	_seq_paused  = false
	if _seq_play_btn:
		_seq_play_btn.text = "▶ Play All"
	if _seq_status_label:
		_seq_status_label.text = ""
	if _grain_player:
		_grain_player.stop()


func _preview_grain_region(start_s: float, end_s: float, grain_idx: int = -1) -> void:
	if not _grains_database or not _stream:
		return
	var bytes: PackedByteArray = _grains_database.get_source_pcm_slice(_stream, start_s, end_s)
	if bytes.is_empty():
		return
	var wav := AudioStreamWAV.new()
	wav.data = bytes
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = int(_grains_database.get_stored_sample_rate())
	wav.stereo = false
	_preview_player.stop()
	_preview_timer.stop()
	_preview_player.stream = wav
	_preview_player.play()
	_preview_grain_idx = grain_idx
	_preview_timer.start(end_s - start_s)
	if _manual_overlay:
		_manual_overlay.queue_redraw()


func _on_preview_timer_timeout() -> void:
	_preview_player.stop()
	_preview_grain_idx = -1
	if _manual_overlay:
		_manual_overlay.queue_redraw()


func _find_grain_region_at(t: float) -> int:
	for i in range(_manual_markers.size() - 1):
		if t >= _manual_markers[i] and t < _manual_markers[i + 1]:
			return i
	return -1


func _input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	if event.keycode != KEY_SPACE:
		return
	get_viewport().set_input_as_handled()
	match _current_page:
		PAGE_OVERVIEW:
			_on_seq_play_pressed()
		PAGE_ANALYSIS, PAGE_EXTRACT:
			if _current_page == PAGE_EXTRACT and _extract_method in [1, 2] \
					and event.ctrl_pressed and _manual_hovered_grain_idx >= 0:
				var i := _manual_hovered_grain_idx
				if i + 1 < _manual_markers.size():
					_preview_grain_region(_manual_markers[i], _manual_markers[i + 1], i)
					return
			_toggle_spec_playback()


func _toggle_spec_playback() -> void:
	if _fft == null or _stream == null:
		return
	if _spec_playback_playing or _player.is_playing():
		_player.stop()
		_spec_playback_playing = false
		return
	var t := _get_spec_hover_time()
	if t < 0.0:
		t = 0.0
	_player.stream = _stream
	_player.play(t)
	_spec_playback_playing = true


func _get_spec_hover_time() -> float:
	match _current_page:
		PAGE_ANALYSIS:
			if _hover_pos.x >= 0:
				return _overlay_to_time_hz(_hover_pos).x
		PAGE_EXTRACT:
			if _fft == null:
				return -1.0
			var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
			if _extract_method == 0:
				var sz_x := _auto_overlay_ctrl.size.x
				if sz_x <= 0 or total_dur <= 0:
					return -1.0
				return clampf(_auto_overlay_ctrl.get_local_mouse_position().x / sz_x * total_dur, 0.0, total_dur)
			else:
				return _manual_px_to_time(_manual_overlay.get_local_mouse_position().x, _manual_overlay.size.x)
	return -1.0


func _play_detail_grain() -> void:
	if _grains_database == null or _detail_grain_index < 0:
		return
	var pcm := _grains_database.get_grain_pcm_windowed(_detail_grain_index)
	if pcm.size() < 2:
		_status_label.text = "Grain audio is empty."
		return
	var rate := int(_grains_database.get_sample_rate())
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	# Convert float pcm [-1,1] to int16 LE.
	var bytes := PackedByteArray()
	bytes.resize(pcm.size() * 2)
	for i in pcm.size():
		var s: int = clampi(int(round(pcm[i] * 32767.0)), -32768, 32767)
		var lo: int = s & 0xff
		var hi: int = (s >> 8) & 0xff
		bytes[i * 2] = lo
		bytes[i * 2 + 1] = hi
	wav.data = bytes
	wav.mix_rate = rate
	wav.stereo = false
	_grain_player.stream = wav
	_grain_player.play()


func _on_grain_delete_pressed() -> void:
	if _grains_database == null or _detail_grain_index < 0:
		return
	if _grains_database.delete_grain(_detail_grain_index):
		_mark_dirty()
		_detail_grain_index = -1
		_refresh_overview()


func _on_bulk_delete_selected() -> void:
	if _grains_database == null:
		return
	var sel := _grain_list.get_selected_items()
	if sel.is_empty():
		return
	var indices := PackedInt32Array()
	for row in sel:
		if row >= 0 and row < _filtered_indices.size():
			indices.append(_filtered_indices[row])
	if indices.is_empty():
		return
	_confirm_delete(indices.size(), func():
		_grains_database.delete_grains(indices)
		_mark_dirty()
		_detail_grain_index = -1
		_refresh_overview())


func _on_bulk_delete_filtered() -> void:
	if _grains_database == null or _filtered_indices.is_empty():
		return
	var indices := _filtered_indices.duplicate()
	_confirm_delete(indices.size(), func():
		_grains_database.delete_grains(indices)
		_mark_dirty()
		_detail_grain_index = -1
		_refresh_overview())


func _on_bulk_delete_low_energy() -> void:
	if _grains_database == null:
		return
	var to_delete := PackedInt32Array()
	for i in _grains_database.get_grain_count():
		var meta := _grains_database.get_grain_metadata(i)
		if bool(meta.get("low_energy", false)):
			to_delete.append(i)
	if to_delete.is_empty():
		_status_label.text = "No low-energy grains to delete."
		return
	_confirm_delete(to_delete.size(), func():
		_grains_database.delete_grains(to_delete)
		_mark_dirty()
		_detail_grain_index = -1
		_refresh_overview())


func _confirm_delete(count: int, on_yes: Callable) -> void:
	var dlg := ConfirmationDialog.new()
	dlg.dialog_text = "Delete %d grain%s? PCM stays in the pool until you Compact or Save (with Compact on save)." % [
		count, "" if count == 1 else "s"]
	dlg.confirmed.connect(func():
		on_yes.call()
		dlg.queue_free())
	dlg.canceled.connect(func(): dlg.queue_free())
	add_child(dlg)
	dlg.popup_centered()


func _on_grain_waveform_draw() -> void:
	var size_v: Vector2 = _grain_waveform.size
	if size_v.x <= 1 or size_v.y <= 1:
		return
	_grain_waveform.draw_rect(Rect2(Vector2.ZERO, size_v), Color(0.05, 0.05, 0.07, 1.0))
	_grain_waveform.draw_line(Vector2(0, size_v.y * 0.5), Vector2(size_v.x, size_v.y * 0.5),
		Color(0.3, 0.3, 0.35, 0.7), 1.0)
	if _grains_database == null or _detail_grain_index < 0:
		return
	var pcm := _grains_database.get_grain_pcm_windowed(_detail_grain_index)
	var n := pcm.size()
	if n < 2:
		return
	var pts := PackedVector2Array()
	pts.resize(n)
	var step := size_v.x / float(n - 1)
	for i in n:
		var y := size_v.y * 0.5 - pcm[i] * size_v.y * 0.45
		pts[i] = Vector2(i * step, y)
	_grain_waveform.draw_polyline(pts, Color(0.4, 0.85, 1.0, 0.95), 1.0)


# =============================================================================
# Import flow
# =============================================================================

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
	if _low_hz_mode:
		_track_btn.disabled = not enabled or _fft == null
	else:
		_track_btn.disabled = not enabled or _picks.is_empty() or _fft == null
	_play_btn.disabled = not enabled or _tracked_curve.size() == 0


func _on_low_hz_mode_toggled(pressed: bool) -> void:
	_low_hz_mode = pressed
	_low_hz_row.visible = pressed
	_tracker_params_row.visible = not pressed
	_picks_panel.visible = not pressed
	_track_btn.text = "Detect cycles" if pressed else "Track fundamental"
	_play_btn.visible = not pressed
	_play_btn.disabled = pressed or _tracked_curve.size() == 0
	_track_btn.disabled = (_fft == null) or (not pressed and _picks.is_empty())
	_tracked_curve = PackedVector2Array()
	_overlay.queue_redraw()


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
	_tracked_curve = PackedVector2Array()
	_refresh_picks_list()
	_overlay.queue_redraw()
	_set_controls_enabled(true)
	_status_label.text = "Analyzed: %d x %d frames  bin=%.2fHz  decimation=%dx  (click spectrogram to add harmonic picks)" \
		% [_fft.get_x_resolution(), _fft.get_y_resolution(), _fft.get_bin_hz(), _fft.get_decimation_factor()]
	_analysis_zoom_fit()
	_manual_zoom_fit()  # set view immediately so param lanes work before navigating away
	_refresh_analysis_continue_btn()


func _refresh_texture() -> void:
	if _fft == null:
		return
	var img := _fft.get_image()
	if img == null:
		return
	var tex := ImageTexture.create_from_image(img)
	_manual_spec_texture = tex
	_spectrogram_view.queue_redraw()
	_manual_spec_ctrl.queue_redraw()


# --- Analysis spectrogram zoom helpers -----------------------------------------

func _analysis_time_to_px(t: float, width: float) -> float:
	var span := _analysis_view_end - _analysis_view_start
	if span <= 0:
		return 0.0
	return (t - _analysis_view_start) / span * width


func _analysis_px_to_time(px: float, width: float) -> float:
	if width <= 0:
		return _analysis_view_start
	return _analysis_view_start + px / width * (_analysis_view_end - _analysis_view_start)


func _analysis_zoom_fit() -> void:
	_analysis_view_start = 0.0
	_analysis_view_end = _fft.get_x_resolution() * _fft.get_seconds_per_frame() if _fft != null else 1.0
	_spectrogram_view.queue_redraw()
	_overlay.queue_redraw()
	_refresh_axis_previews()


func _on_analysis_spec_draw() -> void:
	var ctrl := _spectrogram_view
	var sz := ctrl.size
	ctrl.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.05, 0.05, 0.07, 1.0))
	if sz.x <= 0 or sz.y <= 0 or _manual_spec_texture == null or _fft == null:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()
	if total_dur <= 0:
		return
	var img_w := float(_manual_spec_texture.get_width())
	var img_h := float(_manual_spec_texture.get_height())
	var px_start := (_analysis_view_start / total_dur) * img_w
	var px_end   := (_analysis_view_end   / total_dur) * img_w
	px_start = clampf(px_start, 0.0, img_w)
	px_end   = clampf(px_end,   0.0, img_w)
	if px_end <= px_start:
		return
	var src := Rect2(px_start, 0.0, px_end - px_start, img_h)
	ctrl.draw_texture_rect_region(_manual_spec_texture, Rect2(Vector2.ZERO, sz), src)


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
		_whiten_check.button_pressed,
		_log_hz_check.button_pressed)
	_refresh_texture()
	_overlay.queue_redraw()


func _on_log_hz_toggled(_pressed: bool) -> void:
	_apply_display_settings()


# --- Coordinate helpers ----------------------------------------------

func _overlay_to_time_hz(local_pos: Vector2) -> Vector2:
	var size_v: Vector2 = _overlay.size
	if size_v.x <= 0 or size_v.y <= 0 or _fft == null:
		return Vector2(-1, -1)
	var t := _analysis_px_to_time(local_pos.x, size_v.x)
	var v := clampf(local_pos.y / size_v.y, 0.0, 1.0)
	var py := int(v * _fft.get_y_resolution())
	return Vector2(t, _fft.pixel_y_to_hz(py))


func _time_hz_to_overlay(t: float, hz: float) -> Vector2:
	var size_v: Vector2 = _overlay.size
	var yr := _fft.get_y_resolution()
	if yr <= 0:
		return Vector2.ZERO
	var sy := size_v.y / float(yr)
	var x := _analysis_time_to_px(t, size_v.x)
	var py := _fft.hz_to_pixel_y(hz)
	return Vector2(x, (py + 0.5) * sy)


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
	if _low_hz_mode:
		_track_btn.disabled = _fft == null
	else:
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

	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame()

	# Scroll-wheel zoom (zoom toward cursor position).
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP or mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			var cursor_t := _analysis_px_to_time(mb.position.x, _overlay.size.x)
			var zoom_in := mb.button_index == MOUSE_BUTTON_WHEEL_UP
			var factor := 0.8 if zoom_in else 1.25
			var new_span := (_analysis_view_end - _analysis_view_start) * factor
			new_span = clampf(new_span, 0.05, total_dur)
			_analysis_view_start = cursor_t - (cursor_t - _analysis_view_start) * factor
			_analysis_view_end   = _analysis_view_start + new_span
			if _analysis_view_start < 0.0:
				_analysis_view_start = 0.0
				_analysis_view_end   = new_span
			if _analysis_view_end > total_dur:
				_analysis_view_end   = total_dur
				_analysis_view_start = maxf(0.0, total_dur - new_span)
			_spectrogram_view.queue_redraw()
			_overlay.queue_redraw()
			_refresh_axis_previews()
			return

	# Middle-mouse pan.
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_MIDDLE:
			if mb.pressed:
				_analysis_panning = true
				_analysis_pan_start_pos  = mb.position
				_analysis_pan_start_view = Vector2(_analysis_view_start, _analysis_view_end)
			else:
				_analysis_panning = false
			return

	if event is InputEventMouseMotion and _analysis_panning:
		var mm := event as InputEventMouseMotion
		var span := _analysis_pan_start_view.y - _analysis_pan_start_view.x
		var dx := mm.position.x - _analysis_pan_start_pos.x
		var dt := -dx / _overlay.size.x * span
		_analysis_view_start = clampf(_analysis_pan_start_view.x + dt, 0.0, total_dur - span)
		_analysis_view_end   = _analysis_view_start + span
		_spectrogram_view.queue_redraw()
		_overlay.queue_redraw()
		_refresh_axis_previews()
		return

	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			if _sketch_mode:
				_sketching = true
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
		_hover_pos = event.position
		_overlay.queue_redraw()
		if _sketching:
			var th := _overlay_to_time_hz(event.position)
			if th.x >= 0:
				_insert_point_into_sketch(th)
			return
		if _dragging_pick_index >= 0:
			var th := _overlay_to_time_hz(event.position)
			if th.y > 0:
				_picks[_dragging_pick_index].hz = th.y
				_refresh_picks_list()


func _on_overlay_mouse_exited() -> void:
	_hover_pos = Vector2(-1, -1)
	_overlay.queue_redraw()


# --- Sketch hint flattening ------------------------------------------

func _get_sketch_hint() -> PackedVector2Array:
	if _sketch_points.size() < 1:
		return PackedVector2Array()
	var result := _sketch_points.duplicate()
	result.sort()
	return result


# --- Tracking + playback ---------------------------------------------

func _on_track_pressed() -> void:
	if _fft == null:
		return
	if _low_hz_mode:
		_detect_low_freq_cycles()
		return
	if _picks.is_empty():
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
		_sketch_weight_spin.value,
		_stop_drop_spin.value)

	if _tracked_curve.size() == 0:
		_status_label.text = "Tracking failed: no peak at picks."
		_play_btn.disabled = true
	else:
		var gap_count := 0
		for i in _tracked_curve.size():
			if _tracked_curve[i].y <= 0:
				gap_count += 1
		var tracked_count := _tracked_curve.size() - gap_count
		var has_sketch := _sketch_points.size() >= 1
		_status_label.text = "Tracked %d points from %.3fs to %.3fs (%d harmonic picks%s%s)" % [
			tracked_count,
			_tracked_curve[0].x,
			_tracked_curve[_tracked_curve.size() - 1].x,
			_picks.size(),
			"  +sketch (%d pts)" % [_sketch_points.size()] if has_sketch else "",
			"  %d gap frame%s" % [gap_count, "s" if gap_count != 1 else ""] if gap_count > 0 else ""]
		_play_btn.disabled = false
		_grain_boundaries = PackedVector2Array()
	_overlay.queue_redraw()
	_refresh_axis_previews()


func _detect_low_freq_cycles() -> void:
	_status_label.text = "Detecting cycles..."
	await get_tree().process_frame

	var stream_length := _stream.get_length() if _stream != null else 0.0
	var sketch_hint := _get_sketch_hint()
	_tracked_curve = _fft.detect_low_freq_cycles(
		0.0,
		stream_length,
		_low_hz_min_spin.value,
		_low_hz_max_spin.value,
		_sensitivity_spin.value,
		sketch_hint)

	if _tracked_curve.size() < 2:
		_status_label.text = "No cycles detected. Try raising sensitivity or widening the Hz range."
		_play_btn.disabled = true
	else:
		var hz_first: float = _tracked_curve[0].y
		var hz_last: float = _tracked_curve[_tracked_curve.size() - 1].y
		_status_label.text = "Detected %d cycles from %.3fs to %.3fs (Hz: %.2f .. %.2f)" % [
			_tracked_curve.size(),
			_tracked_curve[0].x,
			_tracked_curve[_tracked_curve.size() - 1].x,
			min(hz_first, hz_last),
			max(hz_first, hz_last)]
		_play_btn.disabled = true
		_grain_boundaries = PackedVector2Array()
	_overlay.queue_redraw()
	_refresh_axis_previews()


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

	# Sketch polyline.
	if _sketch_points.size() >= 2:
		var spts := PackedVector2Array()
		spts.resize(_sketch_points.size())
		for i in _sketch_points.size():
			spts[i] = _time_hz_to_overlay(_sketch_points[i].x, _sketch_points[i].y)
		_overlay.draw_polyline(spts, Color(0.2, 0.9, 1.0, 0.85), 1.5)

	# Tracked fundamental.
	if _tracked_curve.size() >= 2:
		var seg := PackedVector2Array()
		for i in _tracked_curve.size():
			var p: Vector2 = _tracked_curve[i]
			if p.y > 0:
				seg.push_back(_time_hz_to_overlay(p.x, p.y))
			else:
				if seg.size() >= 2:
					_overlay.draw_polyline(seg, Color(0.3, 1.0, 0.4, 0.95), 2.0)
				seg = PackedVector2Array()
		if seg.size() >= 2:
			_overlay.draw_polyline(seg, Color(0.3, 1.0, 0.4, 0.95), 2.0)

		var unique_orders := {}
		for entry in _picks:
			unique_orders[int(entry.order)] = true
		for k in unique_orders.keys():
			if k <= 1:
				continue
			var hseg := PackedVector2Array()
			for i in _tracked_curve.size():
				var p2: Vector2 = _tracked_curve[i]
				if p2.y > 0:
					hseg.push_back(_time_hz_to_overlay(p2.x, p2.y * k))
				else:
					if hseg.size() >= 2:
						_overlay.draw_polyline(hseg, Color(1.0, 0.3, 0.9, 0.45), 1.0)
					hseg = PackedVector2Array()
			if hseg.size() >= 2:
				_overlay.draw_polyline(hseg, Color(1.0, 0.3, 0.9, 0.45), 1.0)

	# Low Hz mode: vertical ticks at each detected cycle onset.
	if _low_hz_mode and _tracked_curve.size() >= 1:
		for i in _tracked_curve.size():
			var p3: Vector2 = _tracked_curve[i]
			var tx: float = _time_hz_to_overlay(p3.x, _fft.get_display_min_hz()).x
			_overlay.draw_line(
				Vector2(tx, 0), Vector2(tx, size_v.y),
				Color(0.3, 1.0, 0.4, 0.45), 1.0)

	# Tracker debug strip (normal mode).
	if not _low_hz_mode and _fft != null:
		var tdebug: Dictionary = _fft.get_last_track_debug()
		if tdebug.size() > 0:
			var t_obs: PackedFloat32Array = tdebug["obs"]
			var t_stop: float = tdebug["stop_thresh"]
			var t_seed: float = tdebug["seed_peak"]
			var t_lo: int = tdebug["frame_lo"]
			var t_hi: int = tdebug["frame_hi"]
			var t_start: int = tdebug["start_frame"]
			var t_spf: float = tdebug["seconds_per_frame"]
			var t_floor: float = tdebug["analysis_db_floor"]
			var t_n: int = t_obs.size()
			if t_n >= 2:
				var v_lo: float = t_floor
				var v_hi: float = max(t_seed + 3.0, t_stop + 3.0)
				var v_range: float = max(1.0, v_hi - v_lo)

				var t_strip_h: float = size_v.y * 0.20
				var t_strip_top: float = size_v.y - t_strip_h
				_overlay.draw_rect(
					Rect2(Vector2(0, t_strip_top), Vector2(size_v.x, t_strip_h)),
					Color(0, 0, 0, 0.30))

				var thresh_y: float = t_strip_top + t_strip_h - ((t_stop - v_lo) / v_range) * t_strip_h
				_overlay.draw_line(
					Vector2(0, thresh_y), Vector2(size_v.x, thresh_y),
					Color(1.0, 0.4, 0.4, 0.75), 1.0)
				var seed_y: float = t_strip_top + t_strip_h - ((t_seed - v_lo) / v_range) * t_strip_h
				_overlay.draw_line(
					Vector2(0, seed_y), Vector2(size_v.x, seed_y),
					Color(1.0, 1.0, 0.4, 0.40), 1.0)

				var t_pts := PackedVector2Array()
				t_pts.resize(t_n)
				for i in t_n:
					var f: int = t_lo + i
					var t: float = float(f) * t_spf
					var x: float = _time_hz_to_overlay(t, _fft.get_display_min_hz()).x
					var y: float = t_strip_top + t_strip_h - ((t_obs[i] - v_lo) / v_range) * t_strip_h
					t_pts[i] = Vector2(x, y)
				_overlay.draw_polyline(t_pts, Color(0.4, 0.8, 1.0, 0.85), 1.0)

				var sx_seed: float = _time_hz_to_overlay(float(t_start) * t_spf, _fft.get_display_min_hz()).x
				_overlay.draw_line(Vector2(sx_seed, t_strip_top), Vector2(sx_seed, size_v.y),
					Color(1.0, 1.0, 0.4, 0.80), 1.0)
				var sx_lo: float = _time_hz_to_overlay(float(t_lo) * t_spf, _fft.get_display_min_hz()).x
				_overlay.draw_line(Vector2(sx_lo, t_strip_top), Vector2(sx_lo, size_v.y),
					Color(1.0, 0.6, 0.2, 0.85), 1.5)
				var sx_hi: float = _time_hz_to_overlay(float(t_hi) * t_spf, _fft.get_display_min_hz()).x
				_overlay.draw_line(Vector2(sx_hi, t_strip_top), Vector2(sx_hi, size_v.y),
					Color(1.0, 0.6, 0.2, 0.85), 1.5)

				var t_font := ThemeDB.fallback_font
				var t_fs := 10
				var caption := "track obs (dB)  seed=%.1f  stop=%.1f  span=%.2fs" % [
					t_seed, t_stop, float(t_hi - t_lo) * t_spf]
				_overlay.draw_string(t_font, Vector2(4, t_strip_top + 12),
					caption, HORIZONTAL_ALIGNMENT_LEFT, -1, t_fs, Color(1, 1, 1, 0.85))

	# Low Hz debug strip.
	if _low_hz_mode and _fft != null:
		var debug: Dictionary = _fft.get_last_onset_debug()
		if debug.size() > 0:
			var onset_arr: PackedFloat32Array = debug["onset_fn"]
			var thresh_arr: PackedFloat32Array = debug["threshold"]
			var frame_lo: int = debug["frame_lo"]
			var spf: float = debug["seconds_per_frame"]
			var npts: int = onset_arr.size()
			if npts >= 2 and npts == thresh_arr.size():
				var max_v: float = 1e-6
				for v in onset_arr:
					if v > max_v: max_v = v
				for v in thresh_arr:
					if v > max_v: max_v = v

				var strip_h: float = size_v.y * 0.20
				var strip_top: float = size_v.y - strip_h
				_overlay.draw_rect(
					Rect2(Vector2(0, strip_top), Vector2(size_v.x, strip_h)),
					Color(0, 0, 0, 0.30))

				var onset_pts := PackedVector2Array()
				onset_pts.resize(npts)
				var thresh_pts := PackedVector2Array()
				thresh_pts.resize(npts)
				for i in npts:
					var f: int = frame_lo + i
					var t: float = float(f) * spf
					var x: float = _time_hz_to_overlay(t, _fft.get_display_min_hz()).x
					var oy: float = strip_top + strip_h - (onset_arr[i] / max_v) * strip_h
					var ty: float = strip_top + strip_h - (thresh_arr[i] / max_v) * strip_h
					onset_pts[i] = Vector2(x, oy)
					thresh_pts[i] = Vector2(x, ty)
				_overlay.draw_polyline(onset_pts, Color(0.4, 0.8, 1.0, 0.75), 1.0)
				_overlay.draw_polyline(thresh_pts, Color(1.0, 0.4, 0.4, 0.75), 1.0)

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

	# Grain boundaries.
	if _grain_boundaries.size() > 0:
		for i in _grain_boundaries.size():
			var gb: Vector2 = _grain_boundaries[i]
			var x_start: float = _time_hz_to_overlay(gb.x, _fft.get_display_min_hz()).x
			var x_end: float = _time_hz_to_overlay(gb.y, _fft.get_display_min_hz()).x
			_overlay.draw_line(
				Vector2(x_start, 0), Vector2(x_start, size_v.y),
				Color(1.0, 0.5, 0.1, 0.4), 1.0)
			var y_top := 2.0
			_overlay.draw_line(
				Vector2(x_start, y_top), Vector2(x_end, y_top),
				Color(1.0, 0.5, 0.1, 0.3), 2.0)

	# Playback position cursor.
	if _spec_playback_playing and _player.is_playing():
		var play_t := _player.get_playback_position()
		var play_x := _time_hz_to_overlay(play_t, _fft.get_display_min_hz()).x
		_overlay.draw_line(Vector2(play_x, 0), Vector2(play_x, size_v.y), Color(0.2, 0.9, 1.0, 0.9), 2.0)

	# Hover readout.
	if _hover_pos.x >= 0:
		var hov_th: Vector2 = _overlay_to_time_hz(_hover_pos)
		if hov_th.x >= 0 and hov_th.y > 0:
			var h_font := ThemeDB.fallback_font
			var h_fs := 12
			var h_label := "%.3f s   %.1f Hz" % [hov_th.x, hov_th.y]
			var h_size: Vector2 = h_font.get_string_size(h_label, HORIZONTAL_ALIGNMENT_LEFT, -1, h_fs)
			var pad := Vector2(4, 2)
			var anchor: Vector2 = _hover_pos + Vector2(12, -6)
			if anchor.x + h_size.x + pad.x * 2 > size_v.x:
				anchor.x = _hover_pos.x - 12 - h_size.x - pad.x * 2
			if anchor.y - h_size.y - pad.y < 0:
				anchor.y = _hover_pos.y + 16 + h_size.y
			var bg_rect := Rect2(
				Vector2(anchor.x - pad.x, anchor.y - h_size.y - pad.y),
				h_size + pad * 2)
			_overlay.draw_rect(bg_rect, Color(0, 0, 0, 0.70))
			_overlay.draw_string(h_font, anchor, h_label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, h_fs, Color(1, 1, 1, 1))


# =============================================================================
# Parameter axis editor (import-side)
# =============================================================================

# Set to true while we rebuild the axis editor from a database whose axes are
# already locked. Prevents user-edit handlers from scribbling state.
var _axes_locked := false


func _on_add_axis_pressed() -> void:
	if _axes_locked:
		return
	_add_axis_row("", true, 0.0, 1.0, 100.0, 0.0, 1000.0, 1.0)
	_refresh_analysis_continue_btn()


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
	name_edit.editable = not _axes_locked
	top_row.add_child(name_edit)

	var derived_check := CheckBox.new()
	derived_check.text = "From fundamental"
	derived_check.button_pressed = derived
	derived_check.disabled = _axes_locked
	top_row.add_child(derived_check)

	top_row.add_child(_make_label("  Min:"))
	var min_spin := _make_spin(-1000000, 1000000, 1, min_val)
	min_spin.custom_minimum_size.x = 80
	min_spin.editable = not _axes_locked
	top_row.add_child(min_spin)

	top_row.add_child(_make_label("  Max:"))
	var max_spin := _make_spin(-1000000, 1000000, 1, max_val)
	max_spin.custom_minimum_size.x = 80
	max_spin.editable = not _axes_locked
	top_row.add_child(max_spin)

	var remove_btn := Button.new()
	remove_btn.text = "X"
	remove_btn.disabled = _axes_locked
	remove_btn.pressed.connect(func(): _remove_axis_row(row))
	top_row.add_child(remove_btn)

	# Calibration row.
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

	# Fundamental frequency preview lane (read-only; shows _tracked_curve → parameter space).
	var preview_ctrl := Control.new()
	preview_ctrl.custom_minimum_size = Vector2(0, 44)
	preview_ctrl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	preview_ctrl.visible = derived
	preview_ctrl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(preview_ctrl)

	# Interactive automation lane (visible when not derived from fundamental).
	var lane_ctrl := Control.new()
	lane_ctrl.custom_minimum_size = Vector2(0, _LANE_HEIGHT)
	lane_ctrl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lane_ctrl.mouse_filter = Control.MOUSE_FILTER_STOP
	lane_ctrl.tooltip_text = "Click to add keyframes, drag to move, right-click to remove."
	lane_ctrl.visible = not derived
	row.add_child(lane_ctrl)

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
		"keyframes_arr": [],
		"preview_ctrl": preview_ctrl,
		"lane_ctrl": lane_ctrl,
	}
	_axis_rows.append(axis_data)

	preview_ctrl.draw.connect(func():
		_draw_fundamental_preview(preview_ctrl,
			cal_a_hz_spin, cal_a_val_spin, cal_b_hz_spin, cal_b_val_spin,
			min_spin, max_spin))

	lane_ctrl.draw.connect(func(): _on_manual_lane_draw(lane_ctrl, axis_data))
	lane_ctrl.gui_input.connect(func(ev): _on_manual_lane_input(ev, lane_ctrl, axis_data))

	# Refresh the preview whenever calibration values change.
	cal_a_hz_spin.value_changed.connect(func(_v): preview_ctrl.queue_redraw())
	cal_a_val_spin.value_changed.connect(func(_v): preview_ctrl.queue_redraw())
	cal_b_hz_spin.value_changed.connect(func(_v): preview_ctrl.queue_redraw())
	cal_b_val_spin.value_changed.connect(func(_v): preview_ctrl.queue_redraw())

	derived_check.toggled.connect(func(pressed: bool):
		cal_row.visible = pressed
		preview_ctrl.visible = pressed
		lane_ctrl.visible = not pressed)


func _remove_axis_row(row: VBoxContainer) -> void:
	if _axes_locked:
		return
	for i in _axis_rows.size():
		if _axis_rows[i].row == row:
			_axis_rows.remove_at(i)
			break
	row.queue_free()
	_refresh_analysis_continue_btn()


func _draw_fundamental_preview(ctrl: Control, cal_a_hz: SpinBox, cal_a_val: SpinBox,
		cal_b_hz: SpinBox, cal_b_val: SpinBox, ax_min_spin: SpinBox, ax_max_spin: SpinBox) -> void:
	var sz := ctrl.size
	ctrl.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.07, 0.07, 0.10, 1.0))
	ctrl.draw_line(Vector2(0, sz.y * 0.5), Vector2(sz.x, sz.y * 0.5),
		Color(0.3, 0.3, 0.35, 0.5), 1.0)
	if _tracked_curve.size() < 2 or sz.x <= 0:
		return
	var total_dur := _fft.get_x_resolution() * _fft.get_seconds_per_frame() if _fft != null else 1.0
	if total_dur <= 0:
		return
	var hz_a    := float(cal_a_hz.value)
	var val_a   := float(cal_a_val.value)
	var hz_b    := float(cal_b_hz.value)
	var val_b   := float(cal_b_val.value)
	var ax_min  := float(ax_min_spin.value)
	var ax_max  := float(ax_max_spin.value)
	var hz_range := hz_b - hz_a
	var val_range := val_b - val_a
	var pts := PackedVector2Array()
	for pt in _tracked_curve:
		if pt.y <= 0:
			continue  # skip gap frames
		var hz := pt.y
		var param_val: float
		if absf(hz_range) < 1e-9:
			param_val = val_a
		else:
			param_val = val_a + (hz - hz_a) / hz_range * val_range
		param_val = clampf(param_val, ax_min, ax_max)
		var x := _analysis_time_to_px(pt.x, sz.x)
		var norm := (param_val - ax_min) / maxf(ax_max - ax_min, 1e-6)
		pts.append(Vector2(x, sz.y * (1.0 - clampf(norm, 0, 1))))
	if pts.size() >= 2:
		ctrl.draw_polyline(pts, Color(0.3, 1.0, 0.5, 0.85), 1.5)


func _refresh_axis_previews() -> void:
	for ad in _axis_rows:
		if ad.has("preview_ctrl") and ad["preview_ctrl"] != null:
			ad["preview_ctrl"].queue_redraw()
		if ad.has("lane_ctrl") and ad["lane_ctrl"] != null:
			ad["lane_ctrl"].queue_redraw()


func _rebuild_axis_editor_from_db() -> void:
	# Wipe existing rows (and their UI), then re-create from the database's
	# axes. If the database has axes, the editor goes into locked mode.
	_axis_rows.clear()
	if _axis_container:
		for c in _axis_container.get_children():
			c.queue_free()
	_axes_locked = false
	if _grains_database == null:
		if _add_axis_btn:
			_add_axis_btn.disabled = false
		return
	var n := _grains_database.get_num_axes()
	if n == 0:
		if _add_axis_btn:
			_add_axis_btn.disabled = false
		return
	_axes_locked = true
	if _add_axis_btn:
		_add_axis_btn.disabled = true
	for i in n:
		var axis_name := String(_grains_database.get_axis_name(i))
		var derived: bool = _grains_database.get_axis_derived(i)
		var lo := _grains_database.get_axis_min(i)
		var hi := _grains_database.get_axis_max(i)
		# Calibration defaults: keep what the editor already shipped if no
		# user-supplied per-import calibration exists.
		_add_axis_row(axis_name, derived, lo, hi, 100.0, lo, 1000.0, hi)


func _build_axis_configs() -> Array[Dictionary]:
	var configs: Array[Dictionary] = []
	for i in _axis_rows.size():
		var axis_data = _axis_rows[i]
		var cfg := {}
		cfg["name"] = StringName(axis_data.name.text)
		cfg["min_value"] = axis_data.min.value
		cfg["max_value"] = axis_data.max.value
		cfg["derived_from_fundamental"] = axis_data.derived.button_pressed
		cfg["calibration_a"] = Vector2(axis_data.cal_a_hz.value, axis_data.cal_a_val.value)
		cfg["calibration_b"] = Vector2(axis_data.cal_b_hz.value, axis_data.cal_b_val.value)
		var kfs := PackedVector2Array()
		for kf in axis_data["keyframes_arr"]:
			kfs.append(Vector2(float(kf.t), float(kf.v)))
		cfg["keyframes"] = kfs
		configs.append(cfg)
	return configs


# =============================================================================
# Import action
# =============================================================================

func _clip_curve_to_region(curve: PackedVector2Array, t0: float, t1: float) -> PackedVector2Array:
	var result := PackedVector2Array()
	for pt in curve:
		if pt.x >= t0 and pt.x <= t1:
			result.append(pt)
	return result

func _on_import_pressed() -> void:
	if _grains_database == null:
		_extract_status_label.text = "Open or create a database first."
		return
	if _fft == null or _tracked_curve.size() < 2:
		_extract_status_label.text = "No tracked curve available. Track the fundamental first."
		return
	if _axis_rows.is_empty():
		_extract_status_label.text = "Add at least one parameter axis."
		return

	_extract_status_label.text = "Importing pitch-synchronous units..."
	await get_tree().process_frame

	var axis_configs := _build_axis_configs()
	var source_path := _stream.resource_path if _stream != null else ""

	# Clip tracked curve to the selected region (if any).
	var curve_to_use := _tracked_curve
	if _extract_region_start >= 0.0 and _extract_region_end > _extract_region_start:
		curve_to_use = _clip_curve_to_region(_tracked_curve, _extract_region_start, _extract_region_end)
		if curve_to_use.size() < 2:
			_extract_status_label.text = "Selected region contains fewer than 2 tracked points. Widen the selection or clear it."
			return

	var added: PackedInt32Array
	if _low_hz_mode:
		added = _grains_database.append_from_onsets(
			_fft,
			_stream,
			source_path,
			curve_to_use,
			axis_configs,
			_energy_thresh_spin.value)
	else:
		added = _grains_database.append_from_fft(
			_fft,
			_stream,
			source_path,
			curve_to_use,
			axis_configs,
			int(_cycles_spin.value),
			_crossfade_spin.value,
			_energy_thresh_spin.value,
			_get_sketch_hint())

	var n_added := added.size()
	if n_added == 0:
		_extract_status_label.text = "Import produced 0 units. Lower the energy threshold or broaden the tracked range. (See output for axis-validation errors.)"
	else:
		_extract_status_label.text = "Imported %d grains (database total: %d)." % [
			n_added, _grains_database.get_grain_count()]
		_mark_dirty()
		_rebuild_axis_editor_from_db()
		_refresh_overview()
		_navigate_to(PAGE_ANALYSIS)
		return

	# Visualize what was just imported.
	_grain_boundaries = PackedVector2Array()
	for i in added.size():
		var meta: Dictionary = _grains_database.get_grain_metadata(added[i])
		var time_s: float = meta.original_time_s
		var duration_s: float = float(meta.get("window_size", 0)) / _grains_database.get_sample_rate()
		_grain_boundaries.append(Vector2(time_s - duration_s * 0.5, time_s + duration_s * 0.5))
	_overlay.queue_redraw()
