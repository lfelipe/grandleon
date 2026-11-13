# SPDX-License-Identifier: MIT
extends SceneTree
func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	# Load the real picker after autoloads exist, without starting an unrelated
	# battle/music scene in this control-layout regression.
	var fixture := GDScript.new()
	fixture.source_code = "extends \"res://scripts/main.gd\"\nfunc _ready() -> void: pass\nfunc _process(_delta: float) -> void: pass\n"
	assert(fixture.reload() == OK)
	var player = fixture.new()
	root.add_child(player)
	player._show_game_language_picker({}, PackedByteArray(),
		{"source": "en", "languages": ["en", "es", "pt-BR"]})
	var panel: PanelContainer = player.game_language_picker_layer.get_child(1)
	for dimensions in [Vector2i(1920, 1080), Vector2i(2400, 1600), Vector2i(1280, 720)]:
		root.size = dimensions
		await process_frame
		await process_frame
		var bounds := Rect2(Vector2.ZERO, Vector2(dimensions))
		assert(bounds.encloses(panel.get_global_rect()), "Language choices must remain on screen")
		assert(panel.get_global_rect().get_center().distance_to(bounds.get_center()) < 2,
			"CanvasLayer popup must be centered after resize")
	var scroll: ScrollContainer = panel.get_child(0).get_child(0)
	var choices: VBoxContainer = scroll.get_child(0)
	for _index in 16:
		var button := Button.new()
		button.custom_minimum_size.y = 80
		button.text = "Additional language"
		choices.add_child(button)
	await process_frame
	await process_frame
	assert(panel.size.y <= root.size.y - 48, "Long language lists must scroll within the screen")
	var last: Button = choices.get_child(choices.get_child_count() - 1)
	last.grab_focus()
	await process_frame
	await process_frame
	assert(scroll.scroll_vertical > 0, "Keyboard focus must reveal the last language")
	player.queue_free()
	await process_frame
	# Focus changes play the normal UI sound. Let the audio mixer finish it
	# before tearing down the autoload players at process exit.
	await create_timer(0.15).timeout
	print("Game language popup: actual CanvasLayer centering, tablet/phone resize and long-list focus scrolling PASS")
	quit()
