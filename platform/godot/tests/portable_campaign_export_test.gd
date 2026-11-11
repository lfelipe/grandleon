# SPDX-License-Identifier: MIT
extends SceneTree
const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const LocalCampaigns = preload("res://scripts/creator/local_campaigns.gd")
const PortableExporter = preload("res://scripts/creator/portable_campaign_exporter.gd")
var failed := false
func _initialize() -> void:
	call_deferred("_run")
func check(value: bool, message: String) -> void:
	if not value:
		failed = true
		push_error(message)
func _run() -> void:
	var library = ProjectLibrary.new("user://portable-author-test-" + str(Time.get_ticks_usec()) + "/projects")
	var creator = load("res://creator.tscn").instantiate()
	creator.library = library
	root.add_child(creator)
	creator._open_new_campaign()
	creator.new_campaign_title.text = "Portable Lantern Road"
	creator._create_named_campaign(false)
	var projects: Array = library.list_projects()
	check(projects.size() == 1, "Actual New Campaign must create the authored project")
	if projects.is_empty(): quit(2); return
	var id: String = projects[0].id
	root.get_node("CreatorContext").open_project(id)
	var editor = load("res://map_editor.tscn").instantiate()
	editor.library = library
	root.add_child(editor)
	editor._select_tool("brush")
	editor._select_terrain("water")
	editor._paint_cell(0)
	await process_frame
	var first: Control
	var next: Control
	for cell: Node in editor.grid.get_children():
		if cell.get_meta("terrain_index", -1) == 2: first = cell
		elif cell.get_meta("terrain_index", -1) == 3: next = cell
	check(first != null and next != null, "Real map grid must expose touch paint cells")
	if first != null and next != null:
		var before: int = editor.undo_stack.size()
		var touch := InputEventScreenTouch.new()
		touch.index = 0
		touch.pressed = true
		touch.position = first.get_global_rect().get_center()
		root.push_input(touch, true)
		await process_frame
		var drag := InputEventScreenDrag.new()
		drag.index = 0
		drag.position = next.get_global_rect().get_center()
		drag.relative = drag.position - touch.position
		root.push_input(drag, true)
		await process_frame
		touch = InputEventScreenTouch.new()
		touch.index = 0
		touch.pressed = false
		touch.position = drag.position
		root.push_input(touch, true)
		await process_frame
		var terrain: Array = editor._current_map().terrain
		check(str(terrain[2]) == "water" and str(terrain[3]) == "water", "Real touch drag must paint both traversed cells")
		check(editor.undo_stack.size() == before + 1, "A touch brush stroke must remain one undoable edit")
	check(editor._save(), "Real terrain edit must save")
	check(str(library.load_source(id).project.maps[0].terrain[0]) == "water", "Authored edit must reach disk")
	creator._open_export_center(id, "Portable Lantern Road")
	check(creator.export_campaign_button != null and not creator.export_campaign_button.disabled, "Creator must expose portable campaign export")
	var result: Dictionary = creator._export_campaign_package(id, creator.export_campaign_id)
	check(result.get("ok", false), "Actual edited project must export through readiness and native compiler")
	if not result.get("ok", false):
		quit(2)
		return
	check(FileAccess.get_file_as_bytes(result.path) == result.package, "Exported GPK must be the actual compiled artifact")
	var local_games = LocalCampaigns.new(library.root_path.get_base_dir().path_join("local-imports"))
	var local: Dictionary = local_games.import_file(result.bundle_path)
	if not local.get("ok", false):
		push_error("Local import refused: " + JSON.stringify(local))
		quit(2)
		return
	check(local.get("ok", false) and local.package == result.package, "Playable envelope must validate and import its actual native GPK")
	check(local_games.import_file(result.bundle_path).get("id", "") == local.get("id", "missing"), "Identical reimport must preserve local save namespace")
	var home = load(get_script().resource_path.get_base_dir().path_join("campaign_browser_home.gd")).new()
	home.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	home.local_campaigns = local_games
	home.catalog = load("res://scripts/campaign_catalog.gd").new(library.root_path.get_base_dir().path_join("catalog"), "")
	root.add_child(home)
	var opened: Dictionary = home._import_campaign_file(result.bundle_path)
	check(opened.get("ok", false) and home.player_choice_overlay.visible, "Real Open Campaign handler must offer authored player choice")
	home._choose_players(1)
	check(home.observed_launch.players == 1 and home.observed_launch.package == result.package and not home.observed_launch.save_scope.is_empty(), "Imported game must stage genuine bytes, chosen players and isolated saves")
	var tampered: Dictionary = result.metadata.duplicate(true)
	tampered.playerMode = "two"
	var bad := str(result.bundle_path) + ".bad.zip"
	local_games.write_bundle(bad, result.package, tampered)
	check(not local_games.import_file(bad).get("ok", false), "Descriptor mode must match native authored campaign")
	tampered = result.metadata.duplicate(true)
	tampered.assets = {"kind": "custom"}
	local_games.write_bundle(bad, result.package, tampered)
	check(local_games.import_file(bad).get("error", "") == "unsupported_assets", "Imported game must reject unsupported assets")
	tampered = result.metadata.duplicate(true)
	tampered.minimumPlayerVersion = "999.0.0"
	local_games.write_bundle(bad, result.package, tampered)
	check(local_games.import_file(bad).get("error", "") == "incompatible_player", "Future-player requirements must be rejected before launch")
	tampered = result.metadata.duplicate(true)
	tampered.sha256 = "0".repeat(64)
	local_games.write_bundle(bad, result.package, tampered)
	check(local_games.import_file(bad).get("error", "") == "checksum_mismatch", "Package digest must match the descriptor")
	var corrupt := FileAccess.get_file_as_bytes(result.bundle_path)
	corrupt[42] = corrupt[42] ^ 1
	PortableExporter.write_bytes(corrupt, bad)
	check(not local_games.import_file(bad).get("ok", false), "ZIP entry CRC must be checked before native parsing")
	var compressed := ZIPPacker.new()
	compressed.open(bad)
	compressed.start_file("campaign.json")
	compressed.write_file("{}".to_utf8_buffer())
	compressed.close_file()
	compressed.start_file("campaign.gpk")
	compressed.write_file(result.package)
	compressed.close_file()
	compressed.close()
	check(not local_games.import_file(bad).get("ok", false), "Compressed archives must fail bounded stored-ZIP preflight")
	check(result.metadata.campaign == "first_adventure" and result.metadata.assets.kind == "builtin", "Portable metadata must select authored campaign and actual asset support")
	var source: Dictionary = library.export_project(id)
	check(source.get("ok", false), "Editable archive must export independently")
	var pc = ProjectLibrary.new(library.root_path.get_base_dir().path_join("pc/projects"))
	var imported: Dictionary = pc.import_project(source.path)
	check(imported.get("ok", false), "A separate PC library must import the real source archive")
	if imported.get("ok", false):
		var compiled: Dictionary = pc.compiler.compile_source_json(pc.load_source(imported.id).source)
		check(compiled.get("ok", false) and compiled.package == result.package, "Imported project must compile byte-identical native gameplay content")
	var session = ClassDB.instantiate("GrandleonSession")
	check(session.start_package(local_games.read_launch(local).package, "first_adventure", 1, "", "en"), "Portable GPK must start genuine native gameplay")
	var saw_battle := false
	var deadline := Time.get_ticks_msec() + 10000
	while not saw_battle and Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			if event.get("type", "") == "management": session.submit_management("proceed")
			elif event.get("type", "") == "deployment_begins": session.submit_intent("begin")
			elif event.get("type", "") == "battle_begins":
				saw_battle = event.get("snapshot", {}).get("units", []).size() >= 4
	check(saw_battle, "Portable authored campaign must enter its real battle with both armies")
	session.stop()
	editor._select_terrain("water")
	editor._paint_cell(1)
	check(editor._save(), "Second authored edit must save")
	var newer: Dictionary = creator._export_campaign_package(id, creator.export_campaign_id)
	var imported_new: Dictionary = local_games.import_file(newer.bundle_path)
	check(imported_new.get("ok", false) and imported_new.id != local.id, "Changed native content must receive a distinct local save namespace")
	check(local_games.read_launch(local).package == result.package, "New import must preserve the earlier game's bytes and saves")
	DirAccess.remove_absolute(newer.bundle_path)
	check(local_games.read_launch(imported_new).get("ok", false), "Installed game must remain playable after original export document is removed")
	home.queue_free()
	var asset_path: String = library.root_path.path_join(id).path_join("assets")
	DirAccess.make_dir_recursive_absolute(asset_path)
	var file := FileAccess.open(asset_path.path_join("custom.png"), FileAccess.WRITE)
	file.store_buffer(PackedByteArray([1, 2, 3]))
	file.close()
	check(creator._export_campaign_package(id, creator.export_campaign_id).get("code", "") == "unsupported_assets", "Portable data-only export must reject unsupported custom files")
	check(library.export_project(id).get("ok", false), "Custom files must remain exportable in editable source archives")
	editor.queue_free()
	creator.queue_free()
	await process_frame
	print("portable creator edit/GPK/source archive/native gameplay integration: ", "FAIL" if failed else "PASS")
	quit(2 if failed else 0)
