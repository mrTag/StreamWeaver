@tool
extends Control

var player: AudioStreamPlayer
var playback: StreamWeaverAudioStreamPlayback
var scroll_container: ScrollContainer
var main_vbox: VBoxContainer

func _ready():
	# Use a ScrollContainer to handle many parameters/triggers
	scroll_container = ScrollContainer.new()
	scroll_container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(scroll_container)
	
	main_vbox = VBoxContainer.new()
	main_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	main_vbox.add_theme_constant_override("separation", 20)
	
	var margin_vbox = MarginContainer.new()
	margin_vbox.add_theme_constant_override("margin_left", 20)
	margin_vbox.add_theme_constant_override("margin_right", 20)
	margin_vbox.add_theme_constant_override("margin_top", 20)
	margin_vbox.add_theme_constant_override("margin_bottom", 20)
	margin_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll_container.add_child(margin_vbox)
	margin_vbox.add_child(main_vbox)
	
	player = AudioStreamPlayer.new()
	player.process_mode = PROCESS_MODE_ALWAYS
	add_child(player)
	
	visibility_changed.connect(_on_visibility_changed)

func test_stream_weaver_stream(s:StreamWeaverAudioStream):
	if playback:
		stop_playback()
	
	if not s:
		return

	# Clear previous UI
	for child in main_vbox.get_children():
		child.queue_free()

	player.stream = s
	player.play()
	playback = player.get_stream_playback()

	# UI for Parameters
	if s.parameters.size() > 0:
		var panel = PanelContainer.new()
		panel.add_theme_stylebox_override("panel", get_theme_stylebox("panel", "Tree"))
		main_vbox.add_child(panel)
		
		var margin = MarginContainer.new()
		margin.add_theme_constant_override("margin_left", 10)
		margin.add_theme_constant_override("margin_right", 10)
		margin.add_theme_constant_override("margin_top", 10)
		margin.add_theme_constant_override("margin_bottom", 10)
		panel.add_child(margin)
		
		var inner_vbox = VBoxContainer.new()
		margin.add_child(inner_vbox)
		
		var param_label = Label.new()
		param_label.text = "Parameters"
		param_label.add_theme_font_size_override("font_size", 18)
		param_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		inner_vbox.add_child(param_label)
		
		inner_vbox.add_child(HSeparator.new())
		
		for p in s.parameters:
			if not p is StreamWeaverParameterInput:
				continue
			var hbox = HBoxContainer.new()
			var name_label = Label.new()
			name_label.text = p.parameter_name
			name_label.custom_minimum_size = Vector2(150, 0)
			hbox.add_child(name_label)
			
			var slider = HSlider.new()
			slider.min_value = p.min_value
			slider.max_value = p.max_value
			slider.step = 0.01
			slider.value = p.start_value
			slider.custom_minimum_size = Vector2(100, 0)
			slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			slider.value_changed.connect(func(val):
				if playback:
					playback.set_parameter(p.parameter_name, val)
			)
			hbox.add_child(slider)
			inner_vbox.add_child(hbox)
			
			if playback:
				playback.set_parameter(p.parameter_name, p.start_value)

	# UI for Triggers
	if s.triggers.size() > 0:
		var panel = PanelContainer.new()
		panel.add_theme_stylebox_override("panel", get_theme_stylebox("panel", "Tree"))
		main_vbox.add_child(panel)
		
		var margin = MarginContainer.new()
		margin.add_theme_constant_override("margin_left", 10)
		margin.add_theme_constant_override("margin_right", 10)
		margin.add_theme_constant_override("margin_top", 10)
		margin.add_theme_constant_override("margin_bottom", 10)
		panel.add_child(margin)
		
		var inner_vbox = VBoxContainer.new()
		margin.add_child(inner_vbox)

		var trigger_label = Label.new()
		trigger_label.text = "Triggers"
		trigger_label.add_theme_font_size_override("font_size", 18)
		trigger_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		inner_vbox.add_child(trigger_label)
		
		inner_vbox.add_child(HSeparator.new())
		
		var grid = GridContainer.new()
		grid.columns = 3
		grid.add_theme_constant_override("h_separation", 10)
		grid.add_theme_constant_override("v_separation", 10)
		grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		inner_vbox.add_child(grid)
		
		for t in s.triggers:
			if not t is StreamWeaverTriggerInput:
				continue
			var btn = Button.new()
			btn.text = t.trigger_name
			btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			btn.pressed.connect(func():
				if playback:
					print("Triggering: " + t.trigger_name)
					print("playing: " + str(playback.is_playing()))
					playback.trigger(t.trigger_name)
			)
			grid.add_child(btn)

func stop_playback():
	if player:
		player.stop()
	playback = null

func _on_visibility_changed():
	if not is_visible_in_tree():
		stop_playback()
