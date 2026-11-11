# SPDX-License-Identifier: MIT
extends SceneTree
const Layout = preload("res://scripts/creator/mobile_creator_layout.gd")
var failures := 0
func _initialize() -> void: call_deferred("_run")
func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)
func _run() -> void:
	var screen := Control.new()
	screen.size = Vector2(1920, 1080)
	root.add_child(screen)
	var panel := PanelContainer.new()
	panel.size = Vector2(700, 500)
	screen.add_child(panel)
	var contents := VBoxContainer.new()
	panel.add_child(contents)
	var first: Button
	var last: Button
	for at in 10:
		var button := Button.new()
		button.text = "Campaign " + str(at)
		button.custom_minimum_size = Vector2(400, 74)
		button.add_theme_font_size_override("font_size", 22)
		contents.add_child(button)
		if first == null: first = button
		last = button
	var normal := Layout.metrics(2.625)
	Layout.apply_tree(screen, normal)
	await process_frame
	await process_frame
	check(first.custom_minimum_size.y >= 126 and first.get_theme_font_size("font_size") >= 42, "Pixel 6 density must retain 48dp targets and 16sp button text")
	check(panel.size.y == 500 and panel.get_child(0) is ScrollContainer, "A tall form must scroll within its fixed panel")
	var scroll: ScrollContainer = panel.get_child(0)
	scroll.ensure_control_visible(last)
	await process_frame
	check(scroll.scroll_vertical > 0, "The final form action must remain reachable")
	Layout.apply_tree(screen, Layout.metrics(2.625, 0.5))
	check(first.custom_minimum_size.y >= 252 and first.get_theme_font_size("font_size") >= 84, "Keyboard-sized canvases must preserve physical touch/text size")
	Layout.apply_tree(screen, normal)
	Layout.apply_tree(screen, normal)
	check(first.custom_minimum_size.y == 126 and first.get_theme_font_size("font_size") == 42, "Repeated resize/keyboard close must restore metrics without growing fonts")
	check(panel.get_child(0) == scroll and scroll.get_child(0) == contents, "Repeated layout must preserve one scroll wrapper and original controls")
	var tile := Button.new()
	tile.set_meta("mobile_tile", true)
	tile.custom_minimum_size = Vector2(64, 64)
	contents.add_child(tile)
	Layout.apply_tree(screen, normal)
	check(tile.custom_minimum_size == Vector2(64, 64), "Overview map tiles must keep their intentional zoom geometry")
	var home = load("res://scripts/home_menu.gd").new()
	home.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	home.catalog = load("res://scripts/campaign_catalog.gd").new("user://mobile-home-layout-" + str(Time.get_ticks_usec()), "")
	root.add_child(home)
	home.home_frame.hide()
	home._build_mobile_interface()
	home._refresh_games()
	await process_frame
	await process_frame
	Layout.apply_tree(home, normal)
	await process_frame
	await process_frame
	check(home.open_campaign_button.size.y >= 126 and home.browse_button.size.y >= 126, "Touch hub actions must meet the same 48dp target")
	check(home.home_frame.get_global_rect().encloses(home.open_campaign_button.get_global_rect()) and home.home_frame.get_global_rect().encloses(home.browse_button.get_global_rect()), "Main touch actions must remain inside the hub frame")
	check(home.first_button != null and home.first_button.size.y >= 126, "Included games must remain visible touch targets")
	check(home.status_label.text == root.get_node("Localization").text("home.mobile_hint"), "Touch hub must explain tapping instead of controller buttons")
	home.queue_free()
	screen.queue_free()
	await process_frame
	print("mobile creator density/scroll/keyboard-resize layout: ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 2)
