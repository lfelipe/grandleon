# SPDX-License-Identifier: MIT
extends SceneTree

const ProjectLibrary = preload("res://scripts/creator/project_library.gd")
const CampaignReadiness = preload("res://scripts/creator/campaign_readiness.gd")
const StandaloneExporter = preload("res://scripts/creator/standalone_exporter.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var shell := ProjectSettings.globalize_path("user://fake-player-shell")
	var exports := ProjectSettings.globalize_path("user://fake-game-exports")
	_make_fake_shell(shell)
	var library = ProjectLibrary.new("user://standalone-projects")
	var template := FileAccess.get_file_as_string(
		"res://content/templates/starter_campaign.json"
	)
	var created: Dictionary = library.create_project("Exported Adventure", template)
	_expect("standalone fixture project is created", created.ok, true)
	if not created.ok:
		quit(1)
		return
	var exporter = StandaloneExporter.new(library, shell, exports)
	_expect("clean installed player shell is available", exporter.linux_availability().ok, true)
	var unsafe_exporter = StandaloneExporter.new(library, shell, shell.path_join("exports"))
	var unsafe: Dictionary = unsafe_exporter.linux_availability()
	_expect("an export directory inside the player shell is refused", unsafe.ok, false)
	_expect("unsafe export directory refusal is specific", str(unsafe.code), "export_directory_unsafe")

	var policy_project: Dictionary = JSON.parse_string(str(created.source))
	var policy_campaign: Dictionary = policy_project.campaigns[0]
	policy_campaign.flow.nodes[1].placements[1].erase("player")
	policy_campaign.playerMode = "one"
	_expect(
		"one-player export does not require a Player 2 character",
		CampaignReadiness.playable_export(policy_project).ok,
		true
	)
	for mode: String in ["two", "choice"]:
		policy_campaign.playerMode = mode
		var readiness: Dictionary = CampaignReadiness.playable_export(policy_project)
		_expect("%s-player policy requires a Player 2 character" % mode, readiness.ok, false)
		_expect(
			"%s-player policy names the incomplete battle" % mode,
			readiness.gaps,
			["Hold the Crossing"]
		)
	policy_campaign.playerMode = "choice"
	var incomplete_source := JSON.stringify(_integer_numbers(policy_project), "  ", true) + "\n"
	var incomplete_saved: Dictionary = library.save_source(str(created.id), incomplete_source)
	_expect(
		"incomplete two-player fixture remains valid editable source: %s" % JSON.stringify(incomplete_saved),
		incomplete_saved.ok,
		true
	)
	var blocked: Dictionary = exporter.export_linux_player(str(created.id))
	_expect("Linux export refuses a battle without Player 2", blocked.ok, false)
	_expect(
		"Linux export returns the player ownership reason",
		str(blocked.get("code", "")),
		"player_two_ownership_missing"
	)
	_expect(
		"Linux export tells the author which battle to fix",
		str(blocked.get("message", "")).contains("Hold the Crossing"),
		true
	)
	_expect(
		"complete source is restored after the negative export",
		library.save_source(str(created.id), str(created.source)).ok,
		true
	)
	var built: Dictionary = exporter.export_linux_player(str(created.id))
	_expect("player-only Linux game is exported", built.ok, true)
	if built.ok:
		var output := str(built.path)
		_expect("export has a player-only marker", FileAccess.file_exists(output.path_join("PLAYER-ONLY")), true)
		_expect("export preserves the tested launcher", FileAccess.file_exists(output.path_join("launch-pc.sh")), true)
		_expect("export preserves runtime licenses", FileAccess.file_exists(output.path_join("licenses/NOTICE.txt")), true)
		_expect("export removes runtime saves", DirAccess.dir_exists_absolute(output.path_join("runtime")), false)
		_expect("export removes local controls", FileAccess.file_exists(output.path_join("controls.local.json")), false)
		_expect("export removes creator scenes", FileAccess.file_exists(output.path_join("project/creator.tscn")), false)
		_expect("export removes Game Settings", FileAccess.file_exists(output.path_join("project/game_settings.tscn")), false)
		_expect("export removes source templates", DirAccess.dir_exists_absolute(output.path_join("project/content/templates")), false)
		_expect("export removes creator implementation", FileAccess.file_exists(output.path_join("project/scripts/creator/creator_screen.gd")), false)
		_expect("export removes creator readiness rules", FileAccess.file_exists(output.path_join("project/scripts/creator/campaign_readiness.gd")), false)
		_expect("export removes Game Settings code", FileAccess.file_exists(output.path_join("project/scripts/creator/game_settings.gd")), false)
		_expect("export removes console builder host", FileAccess.file_exists(output.path_join("project/scripts/creator/console_builder.gd")), false)
		var loaded: Dictionary = library.load_source(str(created.id))
		var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
		var compiled: Dictionary = compiler.compile_source_json(str(loaded.source))
		_expect(
			"export replaces the built-in campaign package",
			FileAccess.get_file_as_bytes(output.path_join("project/game.gpk")),
			compiled.package
		)
		var campaign_id := str(loaded.project.campaigns[0].id)
		_expect(
			"export selects the authored campaign",
			FileAccess.get_file_as_string(output.path_join("DEFAULT-CAMPAIGN")).strip_edges(),
			campaign_id
		)
		var info := FileAccess.get_file_as_string(output.path_join("BUILD-INFO.txt"))
		_expect("export report disables creator", info.contains("creatorIncluded=no"), true)
		_expect("export report names portable game identity", info.contains("exportedGameId=%s" % str(loaded.project.gameId)), true)
		_expect("export report names the package hash", info.contains("packageSha256=%s" % str(built.package_sha256)), true)
		_expect("export report contains no source shell path", info.contains(shell), false)
		_expect("export result does not expose creator", bool(built.creator_included), false)
	var second: Dictionary = exporter.export_linux_player(str(created.id))
	_expect("a second export receives a new directory", second.ok and str(second.name) != str(built.name), true)

	_write(shell.path_join("BUILD-INFO.txt"), "sourceTree=dirty\n")
	var refused: Dictionary = exporter.linux_availability()
	_expect("an unclean player shell is refused", refused.ok, false)
	_expect("unclean shell refusal is specific", str(refused.code), "player_shell_unverified")
	var real_shell := _argument("--real-shell=")
	if not real_shell.is_empty():
		var real_exporter = StandaloneExporter.new(
			library, real_shell, "user://real-shell-exports"
		)
		_expect("packaged release shell is exportable", real_exporter.linux_availability().ok, true)
		var real_build: Dictionary = real_exporter.export_linux_player(str(created.id))
		_expect("packaged release shell produces a standalone game", real_build.ok, true)
		if real_build.ok:
			var real_path := str(real_build.path)
			_expect(
				"real standalone keeps the runtime binary",
				FileAccess.file_exists(real_path.path_join("godot")),
				true
			)
			_expect(
				"real standalone strips its creator scene",
				FileAccess.file_exists(real_path.path_join("project/creator.tscn")),
				false
			)
			var real_info := FileAccess.get_file_as_string(
				real_path.path_join("BUILD-INFO.txt")
			)
			_expect(
				"real standalone carries the authored package hash",
				real_info.contains("packageSha256=%s" % str(real_build.package_sha256)),
				true
			)

	if failures == 0:
		print("standalone exporter smoke passed")
	quit(1 if failures > 0 else 0)


func _argument(prefix: String) -> String:
	for value: String in OS.get_cmdline_user_args():
		if value.begins_with(prefix):
			return value.trim_prefix(prefix)
	return ""


func _make_fake_shell(root_path: String) -> void:
	_write(root_path.path_join("BUILD-INFO.txt"), "Grandleon Linux PC bundle\nreleaseVersion=0.3.0\nsourceRevision=test\nsourceTree=clean\ndefaultCampaign=old\ncreatorIncluded=yes\n")
	_write(root_path.path_join("godot"), "fake runtime\n")
	_write(root_path.path_join("launch-pc.sh"), "#!/usr/bin/env bash\n")
	_write(root_path.path_join("control-pc.sh"), "#!/usr/bin/env bash\n")
	_write(root_path.path_join("licenses/NOTICE.txt"), "test license\n")
	_write(root_path.path_join("project/project.godot"), "[application]\n")
	_write(root_path.path_join("project/bin/libgrandleon_godot.so"), "fake extension\n")
	_write(root_path.path_join("project/game.gpk"), "old package\n")
	_write(root_path.path_join("project/creator.tscn"), "creator\n")
	_write(root_path.path_join("project/game_settings.tscn"), "settings\n")
	_write(root_path.path_join("project/scripts/creator/creator_screen.gd"), "creator\n")
	_write(root_path.path_join("project/scripts/creator/campaign_readiness.gd"), "readiness\n")
	_write(root_path.path_join("project/scripts/creator/game_settings.gd"), "settings\n")
	_write(root_path.path_join("project/scripts/creator/console_builder.gd"), "console builder\n")
	_write(root_path.path_join("project/scripts/creator/standalone_exporter.gd"), "exporter\n")
	_write(root_path.path_join("project/content/templates/starter_campaign.json"), "{}\n")
	_write(root_path.path_join("runtime/data/private.save"), "private\n")
	_write(root_path.path_join("controls.local.json"), "{\"private\":true}\n")


func _write(path: String, value: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		failures += 1
		push_error("could not write standalone fixture: %s" % path.get_file())
		return
	file.store_string(value)
	file.close()


func _integer_numbers(value: Variant) -> Variant:
	if value is Dictionary:
		var dictionary: Dictionary = {}
		for key: Variant in value:
			dictionary[key] = _integer_numbers(value[key])
		return dictionary
	if value is Array:
		var array: Array = []
		for entry: Variant in value:
			array.append(_integer_numbers(entry))
		return array
	if value is float and value == floor(value):
		return int(value)
	return value


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])
