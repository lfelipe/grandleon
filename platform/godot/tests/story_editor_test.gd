# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var library = ProjectLibrary.new()
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_battle.json"
	)
	var created: Dictionary = library.create_project("Authored Story", template)
	_expect("starter project created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var creator_context = root.get_node("CreatorContext")
	creator_context.open_project(created.id)
	var packed: PackedScene = load("res://story_editor.tscn")
	var editor = packed.instantiate()
	root.add_child(editor)
	current_scene = editor
	await process_frame
	await process_frame

	_expect("project opens in story creator", editor.project_id, created.id)
	_expect("starter dialogues are listed", editor.dialogues.is_empty(), false)
	_expect("dialogue selector is populated", editor.dialogue_select.item_count > 0, true)
	_expect("dialogue selector receives controller focus", editor.dialogue_select.has_focus(), true)
	var before := _parse(editor.source)
	var original_text := str(before.dialogues[0].lines[0].text)
	var second_text := str(before.dialogues[0].lines[1].text)
	editor.dialogue_text.text = "The relay answers through portable story source."
	editor._apply_line()
	await process_frame
	var edited := _parse(editor.source)
	_expect(
		"selected dialogue line changes",
		str(edited.dialogues[0].lines[0].text),
		"The relay answers through portable story source."
	)
	_expect(
		"neighbouring dialogue line is preserved",
		str(edited.dialogues[0].lines[1].text),
		second_text
	)
	_expect("dialogue edit marks project dirty", editor.dirty, true)
	_expect("dialogue edit creates undo history", editor.undo_stack.size(), 1)
	editor._undo()
	_expect(
		"dialogue undo restores original text",
		str(_parse(editor.source).dialogues[0].lines[0].text),
		original_text
	)
	editor._redo()
	_expect(
		"dialogue redo restores edited text",
		str(_parse(editor.source).dialogues[0].lines[0].text),
		"The relay answers through portable story source."
	)
	editor._save()
	_expect("story save clears dirty state", editor.dirty, false)
	var stored: Dictionary = library.load_source(created.id)
	_expect("saved story reopens", stored.ok, true)
	if stored.ok:
		_expect(
			"saved dialogue text reopens",
			str(stored.project.dialogues[0].lines[0].text),
			"The relay answers through portable story source."
		)
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		_expect(
			"saved story still compiles",
			compiler.compile_source_json(stored.source).ok,
			true
		)

	var screenshot_path := _argument_path("--screenshot=")
	if not screenshot_path.is_empty():
		for _frame in 12:
			await process_frame
		var texture := get_root().get_texture()
		var image := texture.get_image() if texture != null else null
		if image == null:
			failures += 1
			push_error("story editor capture needs a rendering display")
		else:
			image.save_png(screenshot_path)
			print("story editor capture saved: %s" % screenshot_path)
	if failures == 0:
		print("story editor smoke passed")
	quit(1 if failures > 0 else 0)


func _parse(value: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(value)
	return parsed if parsed is Dictionary else {}


func _argument_path(prefix: String) -> String:
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with(prefix):
			return argument.trim_prefix(prefix)
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
