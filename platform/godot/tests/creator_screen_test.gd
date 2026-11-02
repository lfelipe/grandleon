# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://creator.tscn")
	var screen = packed.instantiate()
	root.add_child(screen)
	current_scene = screen
	await process_frame
	_expect("starter action focused", screen.first_button.has_focus(), true)
	_expect("project library available", screen.library.ready(), true)
	screen._create_starter()
	await process_frame
	var projects: Array[Dictionary] = screen.library.list_projects()
	_expect("starter project created", projects.size(), 1)
	if projects.size() == 1:
		var loaded: Dictionary = screen.library.load_source(str(projects[0].id))
		_expect("starter project opens", loaded.ok, true)
		if loaded.ok:
			var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
			var compiled: Dictionary = compiler.compile_source_json(loaded.source)
			_expect("starter project compiles", compiled.ok, true)
			_expect("starter has a campaign", loaded.project.campaigns.is_empty(), false)
	_expect("creator reports creation", screen.status_label.text, "CREATED  /  Untitled Adventure")
	if projects.size() == 1:
		screen._play_project(str(projects[0].id))
		await process_frame
		await process_frame
		_expect("draft opens in play scene", current_scene.name, "PC")
		if current_scene.name == "PC":
			_expect("draft campaign selected", current_scene.campaign_key, "demo_campaign")
			_expect("draft session started", current_scene.session != null, true)

	if failures == 0:
		print("creator screen smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
