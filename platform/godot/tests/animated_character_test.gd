# SPDX-License-Identifier: MIT
extends SceneTree

const SEMANTIC_CLIPS := [&"idle", &"run", &"attack", &"ability", &"hurt", &"defeat"]

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var commander := _instantiate("res://art/characters/relay_commander_3d.tscn")
	var drone := _instantiate("res://art/characters/relay_drone_3d.tscn")
	await process_frame

	_test_common("commander", commander)
	_test_common("drone", drone)
	_test_profile_bindings()
	_test_commander_motion(commander)
	_test_drone_motion(drone)

	if is_instance_valid(commander):
		commander.queue_free()
	if is_instance_valid(drone):
		drone.queue_free()
	if failures == 0:
		print("animated character smoke passed")
	quit(1 if failures > 0 else 0)


func _instantiate(path: String) -> Node:
	var scene := load(path) as PackedScene
	if scene == null:
		_fail("could not load %s" % path)
		return null
	var instance := scene.instantiate()
	root.add_child(instance)
	return instance


func _test_common(label: String, character: Node) -> void:
	if character == null:
		return
	var rig := character.get_node_or_null("Rig") as Node3D
	_expect_true("%s rig exists" % label, rig != null)
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	_expect_true("%s animation player exists" % label, player != null)
	if player == null:
		return
	for clip: StringName in SEMANTIC_CLIPS:
		_expect_true("%s has %s" % [label, clip], player.has_animation(clip))
		if player.has_animation(clip):
			_test_unique_tracks(label, clip, player.get_animation(clip))


func _test_unique_tracks(label: String, clip: StringName, animation: Animation) -> void:
	var paths: Dictionary = {}
	for track_index in animation.get_track_count():
		var path := String(animation.track_get_path(track_index))
		_expect_true("%s %s has one %s track" % [label, clip, path], not paths.has(path))
		paths[path] = true


func _test_profile_bindings() -> void:
	var profile_text := FileAccess.get_file_as_string("res://content/graphics_profiles.json")
	var parsed: Variant = JSON.parse_string(profile_text)
	if not parsed is Dictionary:
		_fail("graphics profile parses")
		return
	var profile: Dictionary = parsed
	var common: Dictionary = profile.get("common", {})
	var clips: Dictionary = common.get("animation", {}).get("clips", {})
	_expect_true("move semantic maps to run", String(clips.get("move", "")) == "run")
	var voxel: Dictionary = profile.get("profiles", {}).get("voxel", {})
	var scenes: Dictionary = voxel.get("assets", {}).get("characterScenes", {})
	for archetype: String in ["commander", "beast"]:
		var binding: Dictionary = scenes.get(archetype, {})
		var path := String(binding.get("scene", ""))
		_expect_true("%s scene is bound" % archetype, not path.is_empty())
		_expect_true("%s scene exists" % archetype, ResourceLoader.exists(path))


func _test_commander_motion(character: Node) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	var leg := character.get_node_or_null("Rig/Hips/LeftLeg") as Node3D
	var arm := character.get_node_or_null("Rig/Hips/Torso/RightArm") as Node3D
	var core := character.get_node_or_null("Rig/Hips/Torso/RelayCore") as Node3D
	if player == null or leg == null or arm == null or core == null:
		_fail("commander animation nodes exist")
		return

	_sample(player, &"run", 0.07)
	_expect_true("commander run moves a leg", absf(leg.rotation.x) > 0.1)
	_sample(player, &"attack", 0.18)
	_expect_true("commander attack swings its weapon arm", absf(arm.rotation.x) > 1.0)
	_sample(player, &"ability", 0.32)
	_expect_true("commander ability pulses its core", core.scale.x > 1.5)


func _test_drone_motion(character: Node) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	var leg := character.get_node_or_null("Rig/Body/FrontLeft") as Node3D
	var rotor := character.get_node_or_null("Rig/Body/LeftRotor") as Node3D
	var core := character.get_node_or_null("Rig/Body/Core") as Node3D
	var rig := character.get_node_or_null("Rig") as Node3D
	if player == null or leg == null or rotor == null or core == null or rig == null:
		_fail("drone animation nodes exist")
		return

	_sample(player, &"run", 0.06)
	_expect_true("drone run articulates a leg", absf(leg.rotation.x) > 0.1)
	_expect_true("drone run spins a rotor", absf(rotor.rotation.y) > 1.0)
	_sample(player, &"ability", 0.28)
	_expect_true("drone ability pulses its core", core.scale.x > 1.8)
	_sample(player, &"defeat", 0.5)
	_expect_true("drone defeat tips the rig", absf(rig.rotation.z) > 0.5)


func _sample(player: AnimationPlayer, clip: StringName, position: float) -> void:
	player.stop()
	player.play(clip)
	player.seek(position, true)


func _expect_true(label: String, value: bool) -> void:
	if not value:
		_fail(label)


func _fail(label: String) -> void:
	failures += 1
	push_error(label)
