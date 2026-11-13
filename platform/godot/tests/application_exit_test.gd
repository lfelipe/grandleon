# SPDX-License-Identifier: MIT
extends SceneTree

var mode := "home"

func _init() -> void:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--mode="):
			mode = argument.trim_prefix("--mode=")
	call_deferred("_run")

func _run() -> void:
	var scene: Node
	if mode == "player":
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		var compiled: Dictionary = compiler.compile_source_json(FileAccess.get_file_as_string("res://content/templates/starter_battle.json"))
		if not compiled.get("ok", false):
			_fail("starter campaign must compile")
			return
		root.get_node("LaunchContext").stage_package(compiled.package, "demo_campaign", false, 1, "", "", "en")
		scene = load("res://main.tscn").instantiate()
	else:
		scene = load("res://home.tscn").instantiate()
	root.add_child(scene)
	current_scene = scene
	if mode == "player":
		var deadline := Time.get_ticks_msec() + 30000
		while scene.snapshot.is_empty() or scene._dialogue_visible():
			if Time.get_ticks_msec() > deadline:
				_fail("player must reach a live battle before exiting")
				return
			if scene._dialogue_visible(): scene._advance_dialogue()
			if scene._management_visible(): scene._on_management_intent("proceed", 0, 0, 0)
			await process_frame
	var audio := root.get_node("PCAudio")
	if not audio.play_music("banners_afar") or not audio.play_cue("dialogue"):
		_fail("exit must exercise active music and sound effects")
		return
	await process_frame
	match mode:
		"home":
			var button := _find_button(scene, scene._t("home.exit"))
			if button == null:
				_fail("normal home Exit button must be present")
				return
			button.pressed.emit()
		"player": scene._quit_to_launcher()
		"window": root.close_requested.emit()
		_:
			_fail("unknown exit mode")
			return
	if not root.is_input_disabled() or audio.play_cue("dialogue") or audio.play_music("cedar_overture"):
		_fail("closing application must block new input and playback")
		return
	print("APPLICATION_EXIT_REQUESTED mode=%s live_music=true live_effects=true" % mode)
	# The normal application exit path must now terminate this process. The
	# caller checks its exit status and rejects resource/ObjectDB warnings.

func _find_button(node: Node, label: String) -> Button:
	if node is Button and node.text == label: return node
	for child: Node in node.get_children():
		var found := _find_button(child, label)
		if found != null: return found
	return null

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
