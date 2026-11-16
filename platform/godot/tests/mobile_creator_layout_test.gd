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
	var covered := Rect2(100, 400, 600, 126)
	var pan := Layout.keyboard_pan_delta(covered, 394, 12)
	check(covered.end.y - pan <= 382, "An overlay IME must leave the entire focused field visible")
	check(Layout.keyboard_pan_delta(Rect2(100, 50, 600, 126), 394, 12) == 0, "Visible fields must not cause an accumulating keyboard offset")
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
	var rich := RichTextLabel.new()
	rich.bbcode_enabled = true
	rich.text = "[b]Bold[/b] [i]Italic[/i] [code]Code[/code]"
	contents.add_child(rich)
	var normal := Layout.metrics(2.625)
	Layout.apply_tree(screen, normal)
	await process_frame
	await process_frame
	for variant: String in ["normal_font_size", "bold_font_size", "italics_font_size", "bold_italics_font_size", "mono_font_size"]:
		check(rich.get_theme_font_size(variant) >= 37, "Every rich-text style must remain readable at phone density: " + variant)
	check(first.mouse_filter == Control.MOUSE_FILTER_PASS, "Form buttons must allow native scroll gestures to reach their container")
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
	var adapter = load("res://scripts/creator/mobile_creator_layout.gd").new()
	adapter.screen = screen
	screen.add_child(adapter)
	adapter._apply()
	await process_frame
	last.grab_focus()
	scroll.ensure_control_visible(last)
	await process_frame
	var inserted := Label.new()
	inserted.text = "Additional guidance above the focused action"
	inserted.custom_minimum_size.y = 500
	contents.add_child(inserted)
	contents.move_child(inserted, 0)
	for frame in 8: await process_frame
	check(scroll.get_global_rect().grow(1).encloses(last.get_global_rect()), "A late form reflow keeps the focused action visible: last=%s viewport=%s focus=%s" % [last.get_global_rect(), scroll.get_global_rect(), root.gui_get_focus_owner()])
	adapter.queue_free()
	await process_frame
	var home = load("res://scripts/home_menu.gd").new()
	home.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	home.catalog = load("res://scripts/campaign_catalog.gd").new("user://mobile-home-layout-" + str(Time.get_ticks_usec()), "")
	root.add_child(home)
	await process_frame
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
	var hub = load("res://scripts/creator/creator_screen.gd").new()
	root.add_child(hub)
	await process_frame
	hub._layout_mobile_hub(true)
	Layout.apply_tree(hub, normal)
	await process_frame
	await process_frame
	check(hub.hub_projects_panel.position.x == 80 and hub.hub_projects_panel.size.x == 1760, "Compact creator gives the project list the full canvas width")
	check(not hub.hub_actions.vertical and not hub.hub_explanation.visible, "Compact creator uses a short action row")
	for action: Control in hub.hub_actions.get_children():
		if action is Button:
			check(hub.hub_actions_panel.get_global_rect().encloses(action.get_global_rect()), "Compact creator actions must stay inside their panel")
	hub._layout_mobile_hub(false)
	await process_frame
	check(hub.hub_actions.vertical and hub.hub_projects_panel.position.x == 680, "Tablet layout restores the two-column hub")
	hub.queue_free()
	root.remove_child(home)
	var detached_adapter = load("res://scripts/creator/mobile_creator_layout.gd").new()
	detached_adapter.screen = home
	detached_adapter._apply()
	detached_adapter.free()
	home.queue_free()
	screen.queue_free()
	await process_frame
	print("mobile creator density/scroll/keyboard-resize layout: ", "PASS" if failures == 0 else "FAIL")
	root.get_node("PCAudio").request_quit(0 if failures == 0 else 2)
