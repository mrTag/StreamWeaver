@tool
extends EditorPlugin

const MainPanel = preload("res://addons/streamweaver/graphnode/main_window.tscn")
const GrainsDatabaseWindow = preload("res://addons/streamweaver/graphnode/grains_database_window.gd")

var main_panel_instance
var grains_window_instance: Window

func _has_main_screen():
	return true


func _enable_plugin() -> void:
	# Add autoloads here.
	pass


func _disable_plugin() -> void:
	# Remove autoloads here.
	pass


func _enter_tree():
	main_panel_instance = MainPanel.instantiate()
	# Add the main panel to the editor's main viewport.
	EditorInterface.get_editor_main_screen().add_child(main_panel_instance)
	# Hide the main panel. Very much required.
	_make_visible(false)

	add_tool_menu_item("StreamWeaver: Grains Database Editor…", _open_grains_database_window)


func _exit_tree():
	if main_panel_instance:
		main_panel_instance.queue_free()
	if grains_window_instance:
		grains_window_instance.queue_free()
		grains_window_instance = null
	remove_tool_menu_item("StreamWeaver: Grains Database Editor…")


func _open_grains_database_window() -> void:
	if grains_window_instance == null:
		grains_window_instance = GrainsDatabaseWindow.new()
		EditorInterface.get_base_control().add_child(grains_window_instance)
	grains_window_instance.popup_centered(Vector2i(1100, 700))


func _make_visible(visible):
	print("make_visible %s (instance: %s)"%[visible, main_panel_instance])
	if main_panel_instance:
		main_panel_instance.visible = visible


func _get_plugin_name():
	return "Stream Weaver"

func _get_plugin_icon():
	return EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")

func _handles(object):
	return object is StreamWeaverAudioStream


func _edit(object):
	print("edit %s called"%object)
	main_panel_instance.edit_streamweaver_resource(object)
