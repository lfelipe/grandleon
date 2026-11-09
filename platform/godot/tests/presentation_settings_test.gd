# SPDX-License-Identifier: MIT
extends SceneTree

const PresentationSettings = preload("res://scripts/presentation_settings.gd")
const PROFILES := ["voxel-sprites"]

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var unique := "%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]
	var path := "user://presentation-settings-test/%s/settings.json" % unique
	var settings = PresentationSettings.new(path)
	_expect("missing settings are empty", settings.load_settings(PROFILES), {})
	_expect_true(
		"valid settings save",
		settings.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 0.45, "extra_firm",
			false, "reduced", true, true, "pt-BR", 0.25
		)
	)

	var reloaded = PresentationSettings.new(path)
	var values: Dictionary = reloaded.load_settings(PROFILES)
	_expect("stored profile", values.get("graphics_profile"), "voxel-sprites")
	_expect("stored camera comfort", values.get("camera_impulse"), false)
	_expect("stored text size", values.get("text_scale"), 1.15)
	_expect("stored effects level", values.get("effects_level"), 0.45)
	_expect("music volume is stored independently", values.get("music_level"), 0.25)
	_expect("stored stick input", values.get("stick_mode"), "extra_firm")
	_expect("stored battle close-ups", values.get("battle_closeups"), false)
	_expect("stored close-up choice is explicit", values.get("battle_closeups_set"), true)
	_expect("stored cinematic motion", values.get("cinematic_motion"), "reduced")
	_expect("stored FPS choice", values.get("show_fps"), true)
	_expect("stored game language", values.get("game_language"), "pt-BR")
	_expect("valid settings have no warning", reloaded.warning(), "")

	var raw: Variant = JSON.parse_string(
		FileAccess.get_file_as_string(ProjectSettings.globalize_path(path))
	)
	_expect_true("stored settings are an object", raw is Dictionary)
	if raw is Dictionary:
		var document: Dictionary = raw
		var keys := document.keys()
		keys.sort()
		_expect(
			"settings contain only the presentation whitelist",
			keys,
			[
				"battleCloseups", "cameraImpulse", "cinematicMotion",
				"contractVersion", "effectsLevel", "gameLanguage", "graphicsProfile", "musicLevel",
				"showFps", "stickMode", "textScale"
			]
		)

	_expect_true(
		"invalid profile is refused",
		not settings.save_settings("private-machine-profile", true, 1.0, PROFILES, 1.0)
	)
	_expect(
		"refused save preserves settings",
		PresentationSettings.new(path).load_settings(PROFILES).get("graphics_profile"),
		"voxel-sprites"
	)

	_expect_true(
		"invalid effects level is refused",
		not settings.save_settings("voxel-sprites", false, 1.15, PROFILES, 1.01)
	)
	_expect_true(
		"invalid stick input is refused",
		not settings.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 1.0, "machine-specific-mode"
		)
	)
	_expect_true(
		"invalid cinematic motion is refused",
		not settings.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 1.0, "deliberate",
			true, "machine-specific-mode"
		)
	)
	_expect_true(
		"second valid save",
		settings.save_settings("voxel-sprites", true, 1.0, PROFILES, 1.0)
	)
	var absolute := ProjectSettings.globalize_path(path)
	var broken := FileAccess.open(absolute, FileAccess.WRITE)
	broken.store_string("not json\n")
	broken.close()
	var recovered = PresentationSettings.new(path)
	var recovered_values: Dictionary = recovered.load_settings(PROFILES)
	_expect(
		"previous settings recover",
		recovered_values.get("graphics_profile"),
		"voxel-sprites"
	)
	_expect("previous effects level recovers", recovered_values.get("effects_level"), 0.45)
	_expect("previous music level recovers", recovered_values.get("music_level"), 0.25)
	_expect("previous stick input recovers", recovered_values.get("stick_mode"), "extra_firm")
	_expect("previous close-up choice recovers", recovered_values.get("battle_closeups"), false)
	_expect("previous motion choice recovers", recovered_values.get("cinematic_motion"), "reduced")
	_expect("previous FPS choice recovers", recovered_values.get("show_fps"), true)
	_expect("previous game language recovers", recovered_values.get("game_language"), "pt-BR")
	_expect("recovery is coarse", recovered.warning(), "PREVIOUS DISPLAY SETTINGS RESTORED")
	var restored = PresentationSettings.new(path)
	_expect(
		"recovery persists",
		restored.load_settings(PROFILES).get("graphics_profile"),
		"voxel-sprites"
	)
	_expect("restored settings have no warning", restored.warning(), "")

	_expect_true("settings reset", recovered.reset())
	_expect("reset removes settings", PresentationSettings.new(path).load_settings(PROFILES), {})
	var legacy := FileAccess.open(absolute, FileAccess.WRITE)
	legacy.store_string(JSON.stringify({
		"contractVersion": "1.0.0",
		"graphicsProfile": "diorama",
		"cameraImpulse": true,
		"textScale": 1.0,
	}))
	legacy.close()
	_expect(
		"legacy settings default to quiet music",
		PresentationSettings.new(path).load_settings(PROFILES).get("music_level"),
		0.45
	)
	_expect(
		"removed profiles migrate to the supported presentation",
		PresentationSettings.new(path).load_settings(PROFILES).get("graphics_profile"),
		"voxel-sprites"
	)
	_expect(
		"settings written before audio default safely to full volume",
		PresentationSettings.new(path).load_settings(PROFILES).get("effects_level"),
		1.0
	)
	_expect(
		"settings written before stick modes default safely",
		PresentationSettings.new(path).load_settings(PROFILES).get("stick_mode"),
		"deliberate"
	)
	_expect(
		"settings written before close-ups default safely",
		PresentationSettings.new(path).load_settings(PROFILES).get("battle_closeups"),
		true
	)
	_expect(
		"settings written before close-ups leave the game default in charge",
		PresentationSettings.new(path).load_settings(PROFILES).get("battle_closeups_set"),
		false
	)
	_expect(
		"settings written before cinematic motion default safely",
		PresentationSettings.new(path).load_settings(PROFILES).get("cinematic_motion"),
		"full"
	)
	_expect(
		"settings written before the FPS toggle default safely",
		PresentationSettings.new(path).load_settings(PROFILES).get("show_fps"),
		false
	)
	_expect(
		"settings written before language selection follow the package default",
		PresentationSettings.new(path).load_settings(PROFILES).get("game_language"),
		""
	)
	_expect_true("legacy fixture reset", PresentationSettings.new(path).reset())

	var inherited_path := "user://presentation-settings-test/%s/inherited.json" % unique
	var inherited = PresentationSettings.new(inherited_path)
	_expect_true(
		"unrelated settings can save without claiming a close-up override",
		inherited.save_settings(
			"voxel-sprites", true, 1.0, PROFILES, 0.45, "deliberate",
			false, "full", false, false
		)
	)
	var inherited_raw: Dictionary = JSON.parse_string(
		FileAccess.get_file_as_string(ProjectSettings.globalize_path(inherited_path))
	)
	_expect("inherited settings omit a local close-up choice", inherited_raw.has("battleCloseups"), false)
	_expect(
		"reloaded inherited settings still defer to the game",
		inherited.load_settings(PROFILES).get("battle_closeups_set"),
		false
	)
	var local_true_path := "user://presentation-settings-test/%s/local-true.json" % unique
	var local_true_store = PresentationSettings.new(local_true_path)
	_expect_true(
		"explicit local true fixture saves",
		local_true_store.save_settings(
			"voxel-sprites", true, 1.0, PROFILES, 1.0, "deliberate",
			true, "full", false
		)
	)
	var local_true_values: Dictionary = local_true_store.load_settings(PROFILES)
	_expect_true("inherited fixture reset", inherited.reset())

	var compiler = ClassDB.instantiate("GrandleonProjectCompiler")
	var session = ClassDB.instantiate("GrandleonSession")
	_expect_true("package preference bridge loads", compiler != null and session != null)
	if compiler != null and session != null:
		var source := FileAccess.get_file_as_string(
			"res://content/templates/starter_campaign.json"
		)
		var historical: Dictionary = compiler.compile_source_json(source)
		var authored_off: Dictionary = compiler.compile_source_json(source.replace(
			'  "sourceLanguage": "en",',
			'  "sourceLanguage": "en",\n  "battleCloseups": false,'
		))
		_expect("historical package compiles", historical.ok, true)
		_expect("authored-off package compiles", authored_off.ok, true)
		if historical.ok and authored_off.ok:
			var historical_info: Dictionary = session.package_presentation(historical.package)
			var authored_info: Dictionary = session.package_presentation(authored_off.package)
			_expect("historical package inspection succeeds", historical_info.ok, true)
			_expect("authored package inspection succeeds", authored_info.ok, true)
			_expect("absent preference keeps historical close-ups", historical_info.battle_closeups, true)
			_expect("authored disabled default reaches Godot", authored_info.battle_closeups, false)

			var player = load("res://scripts/main.gd").new()
			player._apply_battle_closeup_precedence({}, authored_info.battle_closeups)
			_expect("authored false applies without a local choice", player.battle_closeups_enabled, false)
			player._apply_battle_closeup_precedence(
				local_true_values,
				authored_info.battle_closeups
			)
			_expect("local true overrides authored false", player.battle_closeups_enabled, true)
			player._apply_battle_closeup_precedence({}, historical_info.battle_closeups)
			_expect("historical package starts enabled in the player", player.battle_closeups_enabled, true)
			player._set_battle_closeups_argument("off")
			player._apply_battle_closeup_precedence(
				local_true_values,
				historical_info.battle_closeups
			)
			_expect("command-line close-up choice remains final", player.battle_closeups_enabled, false)
			player.free()
	_expect_true("explicit local true fixture reset", local_true_store.reset())

	var integration = PresentationSettings.new()
	_expect_true(
		"integration settings save",
		integration.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 0.45, "extra_firm",
			false, "reduced", true
		)
	)

	if failures == 0:
		print("presentation settings smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_true(label: String, value: bool) -> void:
	if not value:
		failures += 1
		push_error(label)
