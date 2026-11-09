# SPDX-License-Identifier: MIT
extends SceneTree

const PCAudioScript = preload("res://scripts/pc_audio.gd")

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var audio = PCAudioScript.new()
	root.add_child(audio)
	await process_frame
	var expected := [
		"ability", "arcane", "attack", "blade", "bow", "cancel", "confirm", "defeat", "dialogue",
		"endure", "focus", "healing", "heavy", "impact", "item", "miss", "move", "refuse",
		"restore", "talk", "victory",
	]
	_expect("complete default cue set", audio.available_cues(), expected)
	for cue: String in expected:
		_expect_true("%s has audible duration" % cue, audio.cue_length_msec(cue) >= 40)
		_expect_true("%s remains a bounded cue" % cue, audio.cue_length_msec(cue) <= (2500 if cue == "victory" else 500))

	audio.set_effects_level(0.45)
	_expect("low level is retained", audio.effects_level(), 0.45)
	_expect("low level has a simple name", audio.level_name(), "LOW")
	_expect_true("known cue can play", audio.play_cue("confirm"))
	_expect_true("unknown cue is refused", not audio.play_cue("private-machine-cue"))
	audio.set_effects_level(0.0)
	_expect("mute has a simple name", audio.level_name(), "OFF")
	_expect_true("mute prevents new playback", not audio.play_cue("confirm"))
	audio.set_effects_level(0.7)
	_expect("arbitrary effects volume is retained", audio.effects_level(), 0.7)
	audio.set_music_level(0.25)
	_expect_true("music can start", audio.play_music())
	_expect("independent music volume", audio.music_level(), 0.25)
	_expect("music does not change effects", audio.effects_level(), 0.7)
	audio.set_music_level(0.0)
	_expect("music can mute separately", audio.music_level(), 0.0)
	_expect_true("music mute preserves effects", audio.play_cue("confirm"))
	audio.set_effects_level(0.0)
	audio.set_music_level(0.5)
	_expect("music remains audible with effects muted", audio.music_level(), 0.5)
	_expect_true("effects remain muted", not audio.play_cue("confirm"))

	_expect(
		"weapon hit maps to attack and impact",
		audio.cues_for_events([{"kind": "unit_damaged"}], "attack"),
		["attack", "impact"]
	)
	_expect(
		"magic restoration maps to ability and restore",
		audio.cues_for_events([{"kind": "unit_restored"}], "ability"),
		["ability", "restore"]
	)
	_expect(
		"simultaneous movement is de-duplicated per presentation batch",
		audio.cues_for_events(
			[{"kind": "unit_moved"}, {"kind": "unit_moved"}], "move"
		),
		["move"]
	)
	_expect(
		"defeat and endurance remain distinct",
		audio.cues_for_events(
			[{"kind": "unit_defeated"}, {"kind": "unit_endured"}], "attack"
		),
		["defeat", "endure"]
	)
	var definitions := {
		"weapons": [{"id": 11, "minimum_reach": 1}, {"id": 12, "minimum_reach": 2}],
		"abilities": [{"id": 21, "kind": "damage", "damage_type": "magical"},
			{"id": 22, "kind": "damage", "damage_type": "physical"}]
	}
	var hit := [{"kind": "unit_damaged", "unit_id": 2, "related_unit_id": 1}]
	_expect("sword selects metal and air", audio.cues_for_events(hit, "attack", 11, definitions), ["blade", "impact"])
	_expect("ranged weapon selects bow", audio.cues_for_events(hit, "attack", 12, definitions), ["bow", "impact"])
	_expect("magic selects arcane effect", audio.cues_for_events(hit, "ability", 21, definitions), ["arcane", "impact"])
	_expect("physical ability is not magic", audio.cues_for_events(hit, "ability", 22, definitions), ["heavy", "impact"])
	_expect("healing has its own sound", audio.cues_for_events([{"kind": "unit_restored"}], "ability", 0, definitions), ["healing", "restore"])
	_expect("counter uses defender weapon rather than original spell", audio.cues_for_events(
		hit + [{"kind": "unit_damaged", "unit_id": 1, "related_unit_id": 2}],
		"ability", 21, definitions, {"units": [{"id": 2, "weapons": [12]}]}),
		["arcane", "bow", "impact"])
	_expect_true("unknown music is refused", not audio.play_music("untrusted-path"))
	for theme: String in audio.MUSIC_THEMES:
		_expect_true("theme loads: " + theme, audio.play_music(theme))

	audio.queue_free()
	await process_frame
	# Let the audio mixer consume pending start/stop commands before shutdown.
	await create_timer(0.15).timeout
	if failures == 0:
		print("PC audio integration smoke passed")
	quit(1 if failures > 0 else 0)


func _expect(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		failures += 1
		push_error("%s: expected %s, got %s" % [label, expected, actual])


func _expect_true(label: String, value: bool) -> void:
	if not value:
		failures += 1
		push_error(label)
