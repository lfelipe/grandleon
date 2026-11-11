# SPDX-License-Identifier: MIT
extends SceneTree
var failed := false
func _initialize() -> void: call_deferred("_run")
func check(value: bool, message: String) -> void:
	if not value:
		failed = true
		push_error(message)
func _run() -> void:
	# Exercise the actual editor mode switch against Godot's viewport routing,
	# not a simulated scroll result. Grid-only geometry needs no tactical state.
	var editor = load(get_script().resource_path.get_base_dir().path_join("map_touch_pan_host.gd")).new()
	root.add_child(editor)
	var scroll := ScrollContainer.new()
	scroll.position = Vector2(100, 100)
	scroll.size = Vector2(500, 320)
	editor.add_child(scroll)
	editor.mobile_board_scroll = scroll
	var centre := CenterContainer.new()
	scroll.add_child(centre)
	var grid := GridContainer.new()
	grid.columns = 4
	centre.add_child(grid)
	editor.grid = grid
	for at in 32:
		var cell := Button.new()
		cell.text = str(at)
		cell.custom_minimum_size = Vector2(126, 126)
		cell.set_meta("terrain_index", at)
		grid.add_child(cell)
	await process_frame
	await process_frame
	editor._mobile_set_pan(true)
	check(grid.get_child(0).mouse_filter == Control.MOUSE_FILTER_IGNORE, "Pan must let touches pass through existing tile buttons")
	var touch := InputEventScreenTouch.new()
	touch.index = 0
	touch.position = Vector2(200, 260)
	touch.pressed = true
	root.push_input(touch, true)
	await process_frame
	var drag := InputEventScreenDrag.new()
	drag.index = 0
	drag.position = Vector2(200, 130)
	drag.relative = Vector2(0, -130)
	root.push_input(drag, true)
	await process_frame
	var moved := scroll.scroll_vertical
	touch = InputEventScreenTouch.new()
	touch.index = 0
	touch.position = drag.position
	touch.pressed = false
	root.push_input(touch, true)
	await process_frame
	check(moved > 0, "A real viewport drag over map tiles must scroll the map")
	var modal := Control.new()
	editor.add_child(modal)
	editor.more_layer = modal
	touch.pressed = true
	check(not editor._mobile_pan_input(touch), "Map panning must leave visible editor forms in control of touch input")
	modal.hide()
	editor._mobile_set_pan(false)
	check(grid.get_child(0).mouse_filter == Control.MOUSE_FILTER_STOP, "Leaving Pan must restore tile editing input")
	editor.queue_free()
	await process_frame
	print("map touch Pan viewport integration: ", "FAIL" if failed else "PASS")
	quit(2 if failed else 0)
