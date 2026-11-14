# SPDX-License-Identifier: MIT
extends "steel_cedar_board_capture_test.gd"


func _run() -> void:
	var source_path := _argument("--source=")
	var output := _argument("--output=")
	if source_path.is_empty() or output.is_empty():
		_fail("seasonal capture requires --source and --output")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output)
	get_root().size = CAPTURE_SIZE
	var source := FileAccess.get_file_as_string(source_path)
	var themes := ["temperate", "autumn", "winter", "ashland"]
	var originals: Dictionary = {}
	for theme in themes.size():
		var authored := source.replace('"themeId": "temperate"', '"themeId": "%s"' % themes[theme])
		var path := output.path_join("source-%s.json" % themes[theme])
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_string(authored)
		file.close()
		for index in [0, 1]:
			var spec: Dictionary = MAPS[index].duplicate(true)
			spec.terrain_family = "medieval_" + themes[theme]
			var battle := await _load_real_battle(path, spec)
			if battle.is_empty():
				quit(1)
				return
			_expect("the compiled package publishes its authored season", int(battle.theme), theme)
			if theme == 0:
				originals[index] = battle.duplicate(true)
			else:
				_expect("a season changes no encounter terrain rules", battle.terrain, originals[index].terrain)
				_expect("a season changes no starting unit state", battle.snapshot.units, originals[index].snapshot.units)
			await _verify_and_capture(battle, spec, output.path_join("%s-%s.png" % [themes[theme], spec.node]))
		DirAccess.remove_absolute(path)
	_expect("all authored seasonal scenes render", captures_written, 8)
	if failures == 0:
		print("SEASONAL_TERRAIN_PASS authored_themes=4 captures=%d unchanged_rules=true" % captures_written)
	quit(1 if failures else 0)
