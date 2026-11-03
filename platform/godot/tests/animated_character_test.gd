# SPDX-License-Identifier: MIT
extends SceneTree

const COMMANDER_CLIPS := {
	"idle": &"Relay_Idle",
	"move": &"Relay_Run",
	"attack": &"Relay_Strike",
	"ability": &"Relay_Channel",
	"hurt": &"Relay_Hit",
	"defeat": &"Relay_Fall",
}
const DRONE_CLIPS := {
	"idle": &"Drone_Hover",
	"move": &"Drone_Scamper",
	"attack": &"Drone_Bite",
	"ability": &"Drone_Charge",
	"hurt": &"Drone_Stagger",
	"defeat": &"Drone_Collapse",
}
const ROGUE_CLIPS := {
	"idle": &"Kestrel_Idle",
	"move": &"Kestrel_Sprint",
	"attack": &"Kestrel_Crosscut",
	"ability": &"Kestrel_Veil",
	"hurt": &"Kestrel_Recoil",
	"defeat": &"Kestrel_Down",
}
const STORMCALLER_CLIPS := {
	"idle": &"Morrow_Idle",
	"move": &"Morrow_Stride",
	"attack": &"Morrow_Glaive",
	"ability": &"Morrow_Arc",
	"hurt": &"Morrow_Stagger",
	"defeat": &"Morrow_Dissolve",
}

var failures := 0


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var commander := _instantiate("res://art/characters/relay_commander.glb")
	var rogue := _instantiate("res://art/characters/relay_rogue.glb")
	var stormcaller := _instantiate("res://art/characters/relay_stormcaller.glb")
	var drone := _instantiate("res://art/characters/relay_drone.glb")
	await process_frame

	_test_common("commander", commander, COMMANDER_CLIPS.values())
	_test_common("rogue", rogue, ROGUE_CLIPS.values())
	_test_common("stormcaller", stormcaller, STORMCALLER_CLIPS.values())
	_test_common("drone", drone, DRONE_CLIPS.values())
	_test_profile_bindings()
	_test_shared_humanoid_skeleton(commander, rogue, stormcaller)
	_test_commander_motion(commander)
	_test_rogue_motion(rogue)
	_test_stormcaller_motion(stormcaller)
	_test_drone_motion(drone)

	if is_instance_valid(commander):
		commander.queue_free()
	if is_instance_valid(rogue):
		rogue.queue_free()
	if is_instance_valid(stormcaller):
		stormcaller.queue_free()
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


func _test_common(label: String, character: Node, clips: Array) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	_expect_true("%s animation player exists" % label, player != null)
	if player == null:
		return
	for clip: StringName in clips:
		_expect_true("%s has %s" % [label, clip], player.has_animation(clip))
		if player.has_animation(clip):
			_test_unique_tracks(label, clip, player.get_animation(clip))


func _test_unique_tracks(label: String, clip: StringName, animation: Animation) -> void:
	var paths: Dictionary = {}
	for track_index in animation.get_track_count():
		var path := String(animation.track_get_path(track_index))
		var key := "%s:%d" % [path, animation.track_get_type(track_index)]
		_expect_true("%s %s has one %s channel" % [label, clip, key], not paths.has(key))
		paths[key] = true


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
	for archetype: String in ["commander", "rogue", "stormcaller", "beast"]:
		var binding: Dictionary = scenes.get(archetype, {})
		var path := String(binding.get("scene", ""))
		_expect_true("%s scene is bound" % archetype, not path.is_empty())
		_expect_true("%s scene exists" % archetype, ResourceLoader.exists(path))
		var expected_scale := 0.68 if archetype == "beast" else 0.72
		_expect_true(
			"%s stays within its calibrated tactical-cell scale" % archetype,
			is_equal_approx(float(binding.get("scale", 0.0)), expected_scale)
		)
	var commander: Dictionary = scenes.get("commander", {})
	_expect_true("commander uses an imported GLB", String(commander.get("scene", "")).ends_with(".glb"))
	var commander_clips: Dictionary = commander.get("clips", {})
	for semantic: String in COMMANDER_CLIPS:
		_expect_true(
			"commander maps %s explicitly" % semantic,
			String(commander_clips.get(semantic, "")) == String(COMMANDER_CLIPS[semantic])
		)
	var rogue: Dictionary = scenes.get("rogue", {})
	_expect_true("rogue uses an imported GLB", String(rogue.get("scene", "")).ends_with(".glb"))
	var rogue_clips: Dictionary = rogue.get("clips", {})
	for semantic: String in ROGUE_CLIPS:
		_expect_true(
			"rogue maps %s explicitly" % semantic,
			String(rogue_clips.get(semantic, "")) == String(ROGUE_CLIPS[semantic])
		)
	var stormcaller: Dictionary = scenes.get("stormcaller", {})
	_expect_true(
		"stormcaller uses an imported GLB",
		String(stormcaller.get("scene", "")).ends_with(".glb")
	)
	var stormcaller_clips: Dictionary = stormcaller.get("clips", {})
	for semantic: String in STORMCALLER_CLIPS:
		_expect_true(
			"stormcaller maps %s explicitly" % semantic,
			String(stormcaller_clips.get(semantic, "")) == String(STORMCALLER_CLIPS[semantic])
		)
	var drone: Dictionary = scenes.get("beast", {})
	_expect_true("drone uses an imported GLB", String(drone.get("scene", "")).ends_with(".glb"))
	var drone_clips: Dictionary = drone.get("clips", {})
	for semantic: String in DRONE_CLIPS:
		_expect_true(
			"drone maps %s explicitly" % semantic,
			String(drone_clips.get(semantic, "")) == String(DRONE_CLIPS[semantic])
		)


func _test_shared_humanoid_skeleton(commander: Node, rogue: Node, stormcaller: Node) -> void:
	if commander == null or rogue == null or stormcaller == null:
		return
	var commander_skeleton := commander.get_node_or_null("RelayCommanderRig/Skeleton3D") as Skeleton3D
	var rogue_skeleton := rogue.get_node_or_null("RelayRogueRig/Skeleton3D") as Skeleton3D
	var stormcaller_skeleton := stormcaller.get_node_or_null(
		"RelayStormcallerRig/Skeleton3D"
	) as Skeleton3D
	if commander_skeleton == null or rogue_skeleton == null or stormcaller_skeleton == null:
		_fail("shared humanoid skeleton nodes exist")
		return
	_expect_true("humanoid skeleton has sixteen bones", commander_skeleton.get_bone_count() == 16)
	_test_matching_skeleton("rogue", commander_skeleton, rogue_skeleton)
	_test_matching_skeleton("stormcaller", commander_skeleton, stormcaller_skeleton)


func _test_matching_skeleton(label: String, reference: Skeleton3D, candidate: Skeleton3D) -> void:
	_expect_true(
		"%s humanoid bone count is reused" % label,
		candidate.get_bone_count() == reference.get_bone_count()
	)
	for index in reference.get_bone_count():
		_expect_true(
			"%s humanoid bone %d name is reused" % [label, index],
			candidate.get_bone_name(index) == reference.get_bone_name(index)
		)
		_expect_true(
			"%s humanoid bone %d parent is reused" % [label, index],
			candidate.get_bone_parent(index) == reference.get_bone_parent(index)
		)
		_expect_true(
			"%s humanoid bone %d rest transform is reused" % [label, index],
			candidate.get_bone_rest(index).is_equal_approx(reference.get_bone_rest(index))
		)


func _test_commander_motion(character: Node) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	var skeleton := character.get_node_or_null("RelayCommanderRig/Skeleton3D") as Skeleton3D
	if player == null or skeleton == null:
		_fail("commander animation nodes exist")
		return

	_sample(player, &"Relay_Run", 0.10)
	_expect_true(
		"commander run moves a leg",
		_bone_rotation_amount(skeleton, "thigh.L") > 0.1
	)
	_sample(player, &"Relay_Strike", 0.23)
	_expect_true(
		"commander attack swings its weapon arm",
		_bone_rotation_amount(skeleton, "upper_arm.R") > 0.7
	)
	_sample(player, &"Relay_Channel", 0.30)
	var core := skeleton.find_bone("core")
	_expect_true(
		"commander ability pulses its core",
		core >= 0 and skeleton.get_bone_pose_scale(core).x > 1.5
	)
	_sample(player, &"Relay_Fall", 0.70)
	_expect_true(
		"commander defeat tips the rig",
		_bone_rotation_amount(skeleton, "root") > 0.5
	)


func _test_rogue_motion(character: Node) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	var skeleton := character.get_node_or_null("RelayRogueRig/Skeleton3D") as Skeleton3D
	if player == null or skeleton == null:
		_fail("rogue animation nodes exist")
		return

	_sample(player, &"Kestrel_Sprint", 0.10)
	_expect_true(
		"rogue sprint moves a leg",
		_bone_rotation_amount(skeleton, "thigh.L") > 0.1
	)
	_sample(player, &"Kestrel_Crosscut", 0.20)
	_expect_true(
		"rogue attack crosses both weapons",
		_bone_rotation_amount(skeleton, "upper_arm.L") > 0.7
		and _bone_rotation_amount(skeleton, "upper_arm.R") > 0.5
	)
	_sample(player, &"Kestrel_Veil", 0.27)
	var core := skeleton.find_bone("core")
	_expect_true(
		"rogue ability pulses its core",
		core >= 0 and skeleton.get_bone_pose_scale(core).x > 1.8
	)
	_sample(player, &"Kestrel_Down", 0.70)
	_expect_true(
		"rogue defeat tips the shared rig",
		_bone_rotation_amount(skeleton, "root") > 0.5
	)


func _bone_rotation_amount(skeleton: Skeleton3D, name: String) -> float:
	var index := skeleton.find_bone(name)
	if index < 0:
		return 0.0
	return skeleton.get_bone_pose_rotation(index).get_angle()


func _test_stormcaller_motion(character: Node) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	var skeleton := character.get_node_or_null("RelayStormcallerRig/Skeleton3D") as Skeleton3D
	if player == null or skeleton == null:
		_fail("stormcaller animation nodes exist")
		return

	_sample(player, &"Morrow_Stride", 0.12)
	_expect_true(
		"stormcaller stride moves a leg",
		_bone_rotation_amount(skeleton, "thigh.L") > 0.1
	)
	_sample(player, &"Morrow_Glaive", 0.23)
	_expect_true(
		"stormcaller attack swings its glaive arm",
		_bone_rotation_amount(skeleton, "upper_arm.R") > 0.7
	)
	_sample(player, &"Morrow_Arc", 0.30)
	var core := skeleton.find_bone("core")
	_expect_true(
		"stormcaller ability pulses its core",
		core >= 0 and skeleton.get_bone_pose_scale(core).x > 2.0
	)
	_sample(player, &"Morrow_Dissolve", 0.80)
	_expect_true(
		"stormcaller defeat tips the shared rig",
		_bone_rotation_amount(skeleton, "root") > 0.5
	)


func _test_drone_motion(character: Node) -> void:
	if character == null:
		return
	var player := character.get_node_or_null("AnimationPlayer") as AnimationPlayer
	var skeleton := character.get_node_or_null("RelayDroneRig/Skeleton3D") as Skeleton3D
	if player == null or skeleton == null:
		_fail("drone animation nodes exist")
		return

	_sample(player, &"Drone_Scamper", 0.12)
	_expect_true(
		"drone run articulates a leg",
		_bone_rotation_amount(skeleton, "upper_front.L") > 0.1
	)
	_expect_true(
		"drone run spins a rotor",
		_bone_rotation_amount(skeleton, "rotor.L") > 1.0
	)
	_sample(player, &"Drone_Charge", 0.30)
	var core := skeleton.find_bone("core")
	_expect_true(
		"drone ability pulses its core",
		core >= 0 and skeleton.get_bone_pose_scale(core).x > 1.8
	)
	_sample(player, &"Drone_Collapse", 0.78)
	_expect_true(
		"drone defeat tips the rig",
		_bone_rotation_amount(skeleton, "root") > 0.5
	)


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
