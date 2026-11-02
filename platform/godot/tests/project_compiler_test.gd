# SPDX-License-Identifier: MIT
extends SceneTree

var failures := 0


func _init() -> void:
	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	if compiler == null:
		_fail("GrandleonProjectCompiler is not registered")
		quit(1)
		return
	_expect("canonical schema", compiler.supported_schema(), "1.2.0")

	var malformed: Dictionary = compiler.compile_source_json("{")
	_expect("malformed source refused", malformed.ok, false)
	_expect("refusal returns no package", malformed.package.size(), 0)
	_expect("one source diagnostic", malformed.diagnostics.size(), 1)
	if malformed.diagnostics.size() == 1:
		_expect("source phase", malformed.diagnostics[0].phase, "source")
		_expect("source code", malformed.diagnostics[0].code, "invalid_json")

	var source_path := _source_project_path()
	if source_path.is_empty():
		_fail("missing --source-project argument")
	else:
		var source := FileAccess.get_file_as_string(source_path)
		if source.is_empty():
			_fail("source project is empty or unreadable: %s" % source_path)
		else:
			var compiled: Dictionary = compiler.compile_source_json(source)
			_expect("demo project compiles", compiled.ok, true)
			_expect("demo has no diagnostics", compiled.diagnostics.size(), 0)
			if compiled.package.size() < 64:
				_fail("compiled package is unexpectedly small: %d" % compiled.package.size())
			else:
				var session = ClassDB.instantiate("GrandleonSession")
				if session == null:
					_fail("GrandleonSession is not registered")
				elif not session.start_package(compiled.package, "demo_campaign"):
					_fail("compiled package did not start: %s" % session.last_error())
				else:
					session.stop()

	if failures == 0:
		print("project compiler smoke passed")
	quit(1 if failures > 0 else 0)


func _source_project_path() -> String:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--source-project="):
			return argument.trim_prefix("--source-project=")
	return ""


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_fail("%s: expected %s, got %s" % [label, expected, actual])


func _fail(message: String) -> void:
	failures += 1
	push_error(message)
