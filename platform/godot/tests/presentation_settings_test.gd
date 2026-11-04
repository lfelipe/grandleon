# SPDX-License-Identifier: MIT
extends SceneTree

const PresentationSettings = preload("res://scripts/presentation_settings.gd")
const PROFILES := ["voxel-sprites"]

var failures := 0


func _init() -> void:
	var unique := "%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]
	var path := "user://presentation-settings-test/%s/settings.json" % unique
	var settings = PresentationSettings.new(path)
	_expect("missing settings are empty", settings.load_settings(PROFILES), {})
	_expect_true(
		"valid settings save",
		settings.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 0.45, "extra_firm"
		)
	)

	var reloaded = PresentationSettings.new(path)
	var values: Dictionary = reloaded.load_settings(PROFILES)
	_expect("stored profile", values.get("graphics_profile"), "voxel-sprites")
	_expect("stored camera comfort", values.get("camera_impulse"), false)
	_expect("stored text size", values.get("text_scale"), 1.15)
	_expect("stored effects level", values.get("effects_level"), 0.45)
	_expect("stored stick input", values.get("stick_mode"), "extra_firm")
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
				"cameraImpulse", "contractVersion", "effectsLevel",
				"graphicsProfile", "stickMode", "textScale"
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
		not settings.save_settings("voxel-sprites", false, 1.15, PROFILES, 0.73)
	)
	_expect_true(
		"invalid stick input is refused",
		not settings.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 1.0, "machine-specific-mode"
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
	_expect("previous stick input recovers", recovered_values.get("stick_mode"), "extra_firm")
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
	_expect_true("legacy fixture reset", PresentationSettings.new(path).reset())

	var integration = PresentationSettings.new()
	_expect_true(
		"integration settings save",
		integration.save_settings(
			"voxel-sprites", false, 1.15, PROFILES, 0.45, "extra_firm"
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
