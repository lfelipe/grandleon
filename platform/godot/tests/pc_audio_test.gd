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
		"ability", "attack", "cancel", "confirm", "defeat", "dialogue",
		"endure", "focus", "impact", "item", "miss", "move", "refuse",
		"restore", "talk",
	]
	_expect("complete default cue set", audio.available_cues(), expected)
	for cue: String in expected:
		_expect_true("%s has audible duration" % cue, audio.cue_length_msec(cue) >= 40)
		_expect_true("%s remains a short cue" % cue, audio.cue_length_msec(cue) <= 500)

	audio.set_effects_level(0.45)
	_expect("low level is retained", audio.effects_level(), 0.45)
	_expect("low level has a simple name", audio.level_name(), "LOW")
	_expect_true("known cue can play", audio.play_cue("confirm"))
	_expect_true("unknown cue is refused", not audio.play_cue("private-machine-cue"))
	audio.set_effects_level(0.0)
	_expect("mute has a simple name", audio.level_name(), "OFF")
	_expect_true("mute prevents new playback", not audio.play_cue("confirm"))
	audio.set_effects_level(0.7)
	_expect("arbitrary level snaps safely", audio.effects_level(), 0.45)

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

	audio.queue_free()
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
