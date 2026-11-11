# SPDX-License-Identifier: MIT
extends SceneTree

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var source := FileAccess.get_file_as_string("res://content/templates/starter_campaign.json")
	var compiled: Dictionary = compiler.compile_source_json(source)
	if not bool(compiled.get("ok", false)):
		_fail("Unicode-path fixture must compile through the actual native compiler")
		return
	var directory := ProjectSettings.globalize_path("user://native-path-regression/revisão 保存")
	if DirAccess.make_dir_recursive_absolute(directory) != OK:
		_fail("Unicode package directory could not be created")
		return
	var package_path := directory.path_join("campaign.gpk")
	var file := FileAccess.open(package_path, FileAccess.WRITE)
	if file == null:
		_fail("Godot could not write the Unicode-path package")
		return
	file.store_buffer(compiled.package)
	file.close()
	var save_root := directory.path_join("save revisão 保存")
	var session = ClassDB.instantiate("GrandleonSession")
	if not session.start(package_path, "first_adventure", 1, save_root, "en"):
		_fail("native Unicode package/save paths refused: " + str(session.last_error()))
		return
	if not DirAccess.dir_exists_absolute(save_root) or DirAccess.get_directories_at(save_root).is_empty():
		session.stop()
		_fail("native save storage must use the exact requested Unicode directory")
		return
	var saw_company := false
	var deadline := Time.get_ticks_msec() + 10000
	while not saw_company and Time.get_ticks_msec() < deadline:
		await process_frame
		for event: Dictionary in session.poll():
			if event.type == "management":
				saw_company = true
			elif event.type == "session_failed":
				session.stop()
				_fail("native Unicode campaign failed: " + str(event.get("message", "")))
				return
	session.stop()
	if not saw_company:
		_fail("native Unicode campaign did not publish its real company")
		return
	print("native Unicode package/save path integration passed (%s)" % OS.get_name())
	quit()

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
