# SPDX-License-Identifier: MIT
# Run against a staged app with -- --catalog-test-url=http://127.0.0.1:PORT/catalog.json
extends SceneTree
var failed := false
var localization: Node

func _initialize() -> void:
	call_deferred("_run")

func check(value: bool, message: String) -> void:
	if not value:
		failed = true
		push_error(message)

func _find_button(node: Node, text: String) -> Button:
	for child: Node in node.get_children():
		if child is Button and child.text == text: return child
		var result := _find_button(child, text)
		if result != null: return result
	return null

func _campaign_button(node: Node, id: String, text: String) -> Button:
	for child: Node in node.get_children():
		if child is Button and child.text == text and child.get_meta("campaign_id", "") == id: return child
		var result := _campaign_button(child, id, text)
		if result != null: return result
	return null

func _idle(browser: Control) -> void:
	var deadline := Time.get_ticks_msec() + 20000
	while browser.busy and Time.get_ticks_msec() < deadline:
		await process_frame
	check(not browser.busy, "Catalogue request exceeded bounded test wait")

func _run() -> void:
	localization = root.get_node("Localization")
	var url := ""
	for arg: String in OS.get_cmdline_user_args():
		if arg.begins_with("--catalog-test-url="): url = arg.trim_prefix("--catalog-test-url=")
	if url.is_empty():
		push_error("An explicit local catalogue URL is required")
		quit(2)
		return
	var catalog = load("res://scripts/campaign_catalog.gd").new("user://catalog-ui-test-" + str(Time.get_ticks_usec()), "")
	var home = load(get_script().resource_path.get_base_dir().path_join("campaign_browser_home.gd")).new()
	home.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	home.catalog = catalog
	root.add_child(home)
	await process_frame
	await _idle(home.browser)
	check(home.browse_button != null, "Hub must expose Browse campaigns")
	home.browse_button.pressed.emit()
	await _idle(home.browser)
	home.browser.source_input.text = "http://example.com/unsafe.json"
	_find_button(home.browser, localization.text("catalog.add")).pressed.emit()
	check(home.browser.status.text == localization.text("catalog.error.invalid_url"), "Invalid source must have a human error")
	home.browser.source_input.text = url
	_find_button(home.browser, localization.text("catalog.add")).pressed.emit()
	await _idle(home.browser)
	var details: String = home.browser._entry_details({"playerMode": "choice", "languages": ["en", "es", "pt-BR"]})
	check(details.contains(localization.text("catalog.mode.choice")) and details.contains("EN, ES, PT-BR"), "Cards must show player choice and readable language codes")
	check(home.browser._entry_details({"playerMode": "two", "languages": ["es"]}).contains(localization.text("catalog.mode.two")), "Fixed two-player cards must identify their mode")
	check(home.browser._source_name({"source_url": "https://raw.githubusercontent.com/alice/campaigns/main/catalog.json"}).contains("alice/campaigns"), "Source fallback must identify repository owner")
	check(home.browser._source_name({"source_url": "https://example.org/catalog.json", "source_title": "My campaigns"}).contains("My campaigns"), "Cards must preserve human catalogue title")
	var unavailable := 0
	for entry: Dictionary in catalog.entries():
		if not entry.available: unavailable += 1
	check(unavailable == 1, "Unsupported custom artwork must stay unavailable")
	check(catalog.entries().size() == 3, "Visible Add catalogue button must refresh actual HTTP metadata")
	var download := _campaign_button(home.browser, "steel", localization.text("catalog.download"))
	check(download != null and not download.disabled, "Compatible native campaign must offer download")
	if download != null:
		download.pressed.emit()
		await _idle(home.browser)
	for arg: String in OS.get_cmdline_user_args():
		if arg.begins_with("--catalog-test-capture="):
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png(arg.trim_prefix("--catalog-test-capture="))
	var play := _find_button(home.browser, localization.text("catalog.play"))
	var unavailable_download := _campaign_button(home.browser, "custom", localization.text("catalog.download"))
	check(unavailable_download != null and unavailable_download.disabled, "Unsupported custom artwork must disable Download")
	check(play != null, "Verified native package must offer Play")
	if play != null: play.pressed.emit()
	check(home.player_choice_overlay.visible, "Downloaded campaign must reuse real player choice")
	check(not home.pending_save_scope.is_empty(), "Downloaded launch must isolate its saves")
	var offline = load("res://scripts/campaign_catalog.gd").new(catalog.storage_root, "")
	root.add_child(offline)
	check(offline.sources().size() == 1, "Added sources must persist")
	var installed := false
	for entry: Dictionary in offline.entries():
		if entry.installed:
			installed = bool(offline.read_launch(entry).get("ok", false))
	check(installed, "Cached campaign must launch without a refresh or network")
	offline.queue_free()
	var scope: String = home.pending_save_scope
	home._choose_players(1)
	check(home.observed_launch.players == 1 and home.observed_launch.save_scope == scope, "Player choice must stage selected count and source scope")
	var two_download := _campaign_button(home.browser, "fixed-two", localization.text("catalog.download"))
	check(two_download != null and not two_download.disabled, "Fixed two-player campaign must offer download")
	if two_download != null: two_download.pressed.emit()
	await _idle(home.browser)
	var two_play := _campaign_button(home.browser, "fixed-two", localization.text("catalog.play"))
	check(two_play != null, "Actual authored two-player package must verify and install")
	if two_play != null: two_play.pressed.emit()
	check(not home.player_choice_overlay.visible and home.observed_launch.players == 2, "Fixed two-player campaign must bypass choice and stage two players")
	var context = root.get_node("LaunchContext")
	context.stage_package(PackedByteArray([1]), "test", false, 1, "", "", "es", scope)
	var staged: Dictionary = context.take_package()
	check(staged.save_scope == scope and staged.language == "es", "LaunchContext must preserve optional scope and language")
	context.stage_package(PackedByteArray([1]), "test")
	check(context.take_package().save_scope.is_empty(), "Existing launches must keep unscoped saves")
	var main_script = load("res://scripts/main.gd")
	var save_root := ProjectSettings.globalize_path("user://campaigns")
	check(main_script._scoped_campaign_save_root(save_root, scope).begins_with(save_root + "/downloaded/"), "Downloaded saves must stay under campaign root")
	check(main_script._scoped_campaign_save_root(save_root, "") == save_root, "Built-in save root must stay unchanged")
	check(not main_script._scoped_campaign_save_root(save_root, "../../escape").contains(".."), "Scope must never escape save root")
	var player_only = load("res://scripts/home_menu.gd").new()
	player_only.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	player_only.creator_enabled = false
	root.add_child(player_only)
	check(player_only.browser == null and player_only.catalog == null, "Standalone player must not browse or refresh catalogues")
	player_only.queue_free()
	home.queue_free()
	await process_frame
	await process_frame
	print("campaign browser real HTTP + GUI + native package integration: ", "FAIL" if failed else "PASS")
	quit(2 if failed else 0)
