# SPDX-License-Identifier: MIT
extends SceneTree

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	# Exercise the actual mobile controls after autoloads exist, without a
	# network catalog request or a forced Android OS feature on the desktop.
	var fixture := GDScript.new()
	fixture.source_code = "extends \"res://scripts/home_menu.gd\"\nfunc _ready() -> void: pass\nfunc _process(_delta: float) -> void: pass\n"
	assert(fixture.reload() == OK)
	var home = fixture.new()
	root.add_child(home)
	home._build_mobile_interface()
	await process_frame
	assert(not home.mobile_settings.visible, "Secondary settings start closed")
	assert(not home.language_select.is_visible_in_tree(), "Language choices belong in Settings")
	var creator_found := false
	for node in home.find_children("*", "Button", true, false):
		if node.text == root.get_node("Localization").text("home.create_edit"):
			creator_found = node.is_visible_in_tree()
	assert(creator_found, "Phone retains the creator entry point")
	home.mobile_settings_button.pressed.emit()
	await process_frame
	assert(home.mobile_settings.visible and home.language_select.is_visible_in_tree())
	assert(home.mobile_settings_back.has_focus(), "Settings take keyboard/controller focus")
	var sliders: Array = home.mobile_settings.find_children("*", "HSlider", true, false)
	assert(sliders.size() == 2, "Music and effects controls are inside Settings")
	home.mobile_settings_back.pressed.emit()
	assert(not home.mobile_settings.visible and home.mobile_settings_button.has_focus())
	home.queue_free()
	await process_frame
	print("MOBILE_HOME_PASS creator_visible=true contextual_settings=true focus_return=true")
	root.get_node("PCAudio").request_quit()
