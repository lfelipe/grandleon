# SPDX-License-Identifier: MIT
# Explicit-network acceptance: run against a staged full Godot application.
# A fresh --test-root isolates both downloads and genuine campaign checkpoints.
extends "native_campaign_play.gd"

const Catalog = preload("res://scripts/campaign_catalog.gd")

var checks := 0
var work := ""
var base_save_root := ""

func _init() -> void:
	call_deferred("_run")

func expect(condition: bool, message: String) -> bool:
	checks += 1
	if not condition:
		push_error(message)
		quit(1)
	return condition

func _run() -> void:
	var url := ""
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--catalog-url="):
			url = argument.trim_prefix("--catalog-url=")
		elif argument.begins_with("--test-root="):
			work = ProjectSettings.globalize_path(argument.trim_prefix("--test-root="))
	if not expect(not url.is_empty() and not work.is_empty(), "Explicit catalogue URL and isolated test root are required"):
		return
	if not expect(not DirAccess.dir_exists_absolute(work), "Test root must be new; existing downloads/saves must not be reused"):
		return
	DirAccess.make_dir_recursive_absolute(work)
	base_save_root = work.path_join("campaigns")
	var library = Catalog.new(work.path_join("library"), url)
	root.add_child(library)
	var refreshed: Dictionary = await library.refresh()
	if not expect(refreshed.ok, "Actual public catalogue must load: " + JSON.stringify(refreshed)):
		return
	var entry: Dictionary = {}
	for candidate: Dictionary in library.entries():
		if candidate.id == "steel-and-cedar":
			entry = candidate
	if not expect(not entry.is_empty() and entry.available, "Compatible Steel and Cedar must be discoverable"):
		return
	var installed: Dictionary = await library.install(entry)
	if not expect(installed.ok, "Actual release package must download and validate: " + JSON.stringify(installed)):
		return
	var launch: Dictionary = library.read_launch(entry)
	if not expect(launch.ok and launch.package.size() == 55584, "Installed native package must be playable"):
		return
	var inspector = ClassDB.instantiate("GrandleonSession")
	var metadata: Dictionary = inspector.inspect_campaign(launch.package, launch.campaign)
	if not expect(metadata.ok, "Downloaded native campaign must inspect successfully"):
		return
	# Seed a genuine pre-existing built-in checkpoint for the same game. The
	# downloaded copy must never share or replace this save directory.
	var builtin = ClassDB.instantiate("GrandleonSession")
	if not expect(builtin.start_package(launch.package, launch.campaign, 1, base_save_root, "en"), "Built-in campaign checkpoint fixture must start normally"):
		return
	var builtin_play: Dictionary = await _play_to_checkpoint(builtin)
	if not builtin_play.get("ok", false):
		return
	builtin.stop()
	var builtin_directory := base_save_root.path_join(metadata.gameId)
	var builtin_before := _file_hashes(builtin_directory)
	if not expect(not builtin_before.is_empty(), "Built-in fixture must contain real persisted save bytes"):
		return
	var player_script = load("res://scripts/main.gd")
	var downloaded_root: String = player_script._scoped_campaign_save_root(base_save_root, launch.save_scope)
	if not expect(downloaded_root.begins_with(base_save_root + "/downloaded/"), "Downloaded campaign must use the actual player's separate save scope"):
		return
	var session = ClassDB.instantiate("GrandleonSession")
	if not expect(session.start_package(launch.package, launch.campaign, 1, downloaded_root, "en"), "Downloaded native campaign must start"):
		return
	var played: Dictionary = await _play_to_checkpoint(session)
	if not played.get("ok", false):
		return
	var moved: bool = int(played.moves) > 0
	session.stop()
	if not expect(_file_hashes(builtin_directory) == builtin_before, "Downloaded play must not change built-in save bytes"):
		return
	library.queue_free()
	await process_frame
	# No refresh is called: installation metadata, validation and play must work
	# without another request. HTTP failure/offline refresh has separate tests.
	var offline = Catalog.new(work.path_join("library"), url)
	root.add_child(offline)
	var cached: Dictionary = {}
	for candidate: Dictionary in offline.entries():
		if candidate.id == entry.id and candidate.source_url == entry.source_url:
			cached = candidate
	if not expect(not cached.is_empty() and cached.installed, "Fresh catalogue instance must expose installed campaign from disk"):
		return
	var cached_launch: Dictionary = offline.read_launch(cached)
	if not expect(cached_launch.ok and cached_launch.save_scope == launch.save_scope, "Offline launch must preserve campaign identity and save scope"):
		return
	if not expect(offline.find_children("*", "HTTPRequest").is_empty(), "Offline listing and launch must create no network request"):
		return
	var resumed_session = ClassDB.instantiate("GrandleonSession")
	if not expect(resumed_session.start_package(cached_launch.package, cached_launch.campaign, 1, downloaded_root, "en"), "Offline campaign checkpoint must reopen"):
		return
	var resumed := false
	var company := false
	var deadline := Time.get_ticks_msec() + 15000
	while not company and Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in resumed_session.poll():
			if event.type == "campaign_begun": resumed = event.resumed
			elif event.type == "management": company = true
	if not expect(resumed and company and resumed_session.checkpoint_slots()[0].exists, "Offline native session must resume the genuine saved checkpoint"):
		return
	if not expect(resumed_session.submit_management("proceed"), "Resumed checkpoint must proceed normally"):
		return
	var resumed_battle: Dictionary = await _battle(resumed_session)
	if not expect(resumed_battle.get("width", 0) == 14 and resumed_battle.get("height", 0) == 9, "Offline checkpoint must continue into authored Deepwood, not replay Bell Orchard"):
		return
	resumed_session.stop()
	if not expect(_file_hashes(builtin_directory) == builtin_before, "Offline resume must leave built-in saves untouched"):
		return
	var result := {"passed": true, "checks": checks, "catalogue": url, "campaign": launch.campaign,
		"package_bytes": launch.package.size(), "player_mode": launch.playerMode,
		"ordinary_movement": moved, "ordinary_battle_won": true, "battle_commands": played.commands, "offline_checkpoint_resumed": resumed,
		"built_in_save_unchanged": true, "save_scope": launch.save_scope,
		"resumed_map": [resumed_battle.width, resumed_battle.height]}
	var file := FileAccess.open(work.path_join("result.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(result, "\t") + "\n")
	file.close()
	offline.queue_free()
	await process_frame
	print("CAMPAIGN_DOWNLOAD_PLAY_PASS " + JSON.stringify(result))
	quit()

func _battle(session) -> Dictionary:
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type in ["battle_begins", "snapshot"] and not event.snapshot.get("deploying", false):
				return event.snapshot
			elif event.type == "deployment_begins": session.submit_intent("begin")
			elif event.type == "session_failed":
				expect(false, "Native campaign failed: " + str(event.get("message", "")))
				return {}
	return {}

func _file_hashes(directory: String) -> Dictionary:
	var result := {}
	for file: String in DirAccess.get_files_at(directory):
		result[file] = FileAccess.get_sha256(directory.path_join(file))
	return result
