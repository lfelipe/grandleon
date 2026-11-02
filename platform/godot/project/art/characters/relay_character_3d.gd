# SPDX-License-Identifier: MIT
extends Node3D

@export_enum("commander", "drone") var character_kind := "commander"

var _white: StandardMaterial3D
var _navy: StandardMaterial3D
var _amber: StandardMaterial3D
var _violet: StandardMaterial3D
var _metal: StandardMaterial3D
var _cyan: StandardMaterial3D


func _ready() -> void:
	if get_node_or_null("Rig") != null:
		return
	_build_materials()
	if character_kind == "drone":
		_build_drone()
	else:
		_build_commander()


func _build_materials() -> void:
	_white = _material(Color("dce7ea"), 0.58)
	_navy = _material(Color("172a4d"), 0.72)
	_amber = _material(Color("e49a32"), 0.42, 0.08)
	_violet = _material(Color("7451aa"), 0.62)
	_metal = _material(Color("5e7380"), 0.34, 0.72)
	_cyan = _material(Color("4bd8ed"), 0.22, 0.18, true)


func _material(
	colour: Color, roughness: float, metallic := 0.0, glowing := false
) -> StandardMaterial3D:
	var result := StandardMaterial3D.new()
	result.albedo_color = colour
	result.roughness = roughness
	result.metallic = metallic
	if glowing:
		result.emission_enabled = true
		result.emission = colour
		result.emission_energy_multiplier = 1.35
	return result


func _node(parent: Node, name_value: String, at: Vector3) -> Node3D:
	var result := Node3D.new()
	result.name = name_value
	result.position = at
	parent.add_child(result)
	return result


func _box(
	parent: Node, name_value: String, at: Vector3, size_value: Vector3,
	material: Material
) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size_value
	mesh.material = material
	var result := MeshInstance3D.new()
	result.name = name_value
	result.position = at
	result.mesh = mesh
	parent.add_child(result)
	return result


func _cylinder(
	parent: Node, name_value: String, at: Vector3, radius: float,
	height: float, material: Material, segments := 8
) -> MeshInstance3D:
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = segments
	mesh.rings = 1
	mesh.material = material
	var result := MeshInstance3D.new()
	result.name = name_value
	result.position = at
	result.mesh = mesh
	parent.add_child(result)
	return result


func _sphere(
	parent: Node, name_value: String, at: Vector3, radius: float,
	material: Material
) -> MeshInstance3D:
	var mesh := SphereMesh.new()
	mesh.radius = radius
	mesh.height = radius * 2.0
	mesh.radial_segments = 12
	mesh.rings = 6
	mesh.material = material
	var result := MeshInstance3D.new()
	result.name = name_value
	result.position = at
	result.mesh = mesh
	parent.add_child(result)
	return result


func _build_commander() -> void:
	var rig := _node(self, "Rig", Vector3.ZERO)
	var hips := _node(rig, "Hips", Vector3(0, 0.38, 0))
	_box(hips, "Pelvis", Vector3.ZERO, Vector3(0.30, 0.18, 0.22), _navy)

	var left_leg := _node(hips, "LeftLeg", Vector3(-0.105, -0.07, 0))
	_cylinder(left_leg, "Shin", Vector3(0, -0.19, 0), 0.07, 0.34, _white)
	_box(left_leg, "Boot", Vector3(0, -0.37, -0.035), Vector3(0.15, 0.10, 0.22), _navy)
	var right_leg := _node(hips, "RightLeg", Vector3(0.105, -0.07, 0))
	_cylinder(right_leg, "Shin", Vector3(0, -0.19, 0), 0.07, 0.34, _white)
	_box(right_leg, "Boot", Vector3(0, -0.37, -0.035), Vector3(0.15, 0.10, 0.22), _navy)

	var torso := _node(hips, "Torso", Vector3(0, 0.27, 0))
	_box(torso, "Chest", Vector3.ZERO, Vector3(0.38, 0.34, 0.24), _white)
	_box(torso, "ChestPlate", Vector3(0, 0.01, -0.135), Vector3(0.23, 0.22, 0.05), _navy)
	_box(torso, "Mantle", Vector3(0, 0.09, 0.145), Vector3(0.42, 0.20, 0.07), _navy)
	var core := _node(torso, "RelayCore", Vector3(0, 0.02, -0.18))
	_sphere(core, "Glow", Vector3.ZERO, 0.055, _cyan)

	var head := _node(torso, "Head", Vector3(0, 0.29, 0))
	_sphere(head, "Helmet", Vector3.ZERO, 0.145, _white)
	_box(head, "Cowl", Vector3(0, 0.04, 0.09), Vector3(0.24, 0.15, 0.12), _navy)
	_box(head, "Visor", Vector3(0, 0, -0.14), Vector3(0.20, 0.075, 0.035), _amber)

	var left_arm := _node(torso, "LeftArm", Vector3(-0.26, 0.10, 0))
	_sphere(left_arm, "Shoulder", Vector3.ZERO, 0.10, _navy)
	_cylinder(left_arm, "Arm", Vector3(0, -0.17, 0), 0.055, 0.30, _white)
	_sphere(left_arm, "Hand", Vector3(0, -0.34, 0), 0.065, _navy)
	var right_arm := _node(torso, "RightArm", Vector3(0.26, 0.10, 0))
	_sphere(right_arm, "Shoulder", Vector3.ZERO, 0.10, _navy)
	_cylinder(right_arm, "Arm", Vector3(0, -0.17, 0), 0.055, 0.30, _white)
	_sphere(right_arm, "Hand", Vector3(0, -0.34, 0), 0.065, _navy)
	_cylinder(right_arm, "SwordHilt", Vector3(0, -0.41, 0), 0.035, 0.12, _metal, 6)
	_box(right_arm, "Sword", Vector3(0, -0.63, 0), Vector3(0.065, 0.34, 0.055), _cyan)

	_add_commander_animations()


func _build_drone() -> void:
	var rig := _node(self, "Rig", Vector3.ZERO)
	var body := _node(rig, "Body", Vector3(0, 0.38, 0))
	_box(body, "Hull", Vector3.ZERO, Vector3(0.48, 0.25, 0.40), _violet)
	_box(body, "Armour", Vector3(0, 0.12, 0.02), Vector3(0.34, 0.10, 0.30), _metal)
	var head := _node(body, "Head", Vector3(0, 0.02, -0.27))
	_box(head, "Mask", Vector3.ZERO, Vector3(0.30, 0.19, 0.20), _metal)
	_box(head, "Visor", Vector3(0, 0.01, -0.115), Vector3(0.20, 0.06, 0.035), _cyan)
	var core := _node(body, "Core", Vector3(0, 0.03, 0.22))
	_sphere(core, "Glow", Vector3.ZERO, 0.07, _cyan)

	for leg_data: Array in [
		["FrontLeft", Vector3(-0.18, -0.08, -0.15)],
		["FrontRight", Vector3(0.18, -0.08, -0.15)],
		["RearLeft", Vector3(-0.18, -0.08, 0.15)],
		["RearRight", Vector3(0.18, -0.08, 0.15)],
	]:
		var leg := _node(body, String(leg_data[0]), leg_data[1])
		_cylinder(leg, "Upper", Vector3(0, -0.14, 0), 0.05, 0.25, _metal, 6)
		_box(leg, "Foot", Vector3(0, -0.29, -0.025), Vector3(0.12, 0.07, 0.18), _violet)

	var left_rotor := _node(body, "LeftRotor", Vector3(-0.31, 0.08, 0.02))
	_cylinder(left_rotor, "Hub", Vector3.ZERO, 0.08, 0.06, _metal, 8)
	_box(left_rotor, "Blade", Vector3.ZERO, Vector3(0.06, 0.025, 0.38), _cyan)
	var right_rotor := _node(body, "RightRotor", Vector3(0.31, 0.08, 0.02))
	_cylinder(right_rotor, "Hub", Vector3.ZERO, 0.08, 0.06, _metal, 8)
	_box(right_rotor, "Blade", Vector3.ZERO, Vector3(0.06, 0.025, 0.38), _cyan)

	_add_drone_animations()


func _animation(length_value: float, looped := false) -> Animation:
	var result := Animation.new()
	result.length = length_value
	result.loop_mode = Animation.LOOP_LINEAR if looped else Animation.LOOP_NONE
	return result


func _track(
	animation: Animation, path: String, times: PackedFloat32Array, values: Array
) -> void:
	var index := animation.add_track(Animation.TYPE_VALUE)
	animation.track_set_path(index, NodePath(path))
	animation.track_set_interpolation_type(index, Animation.INTERPOLATION_LINEAR)
	for key_index in mini(times.size(), values.size()):
		animation.track_insert_key(index, times[key_index], values[key_index])


func _install_animations(values: Dictionary) -> void:
	var library := AnimationLibrary.new()
	for name_value: Variant in values.keys():
		var animation := values[name_value] as Animation
		if animation != null:
			library.add_animation(StringName(name_value), animation)
	var player := AnimationPlayer.new()
	player.name = "AnimationPlayer"
	player.root_node = NodePath("..")
	player.playback_default_blend_time = 0.10
	player.add_animation_library(StringName(), library)
	add_child(player)


func _add_commander_animations() -> void:
	var idle := _animation(1.4, true)
	_track(idle, "Rig:position", PackedFloat32Array([0.0, 0.7, 1.4]), [Vector3.ZERO, Vector3(0, 0.025, 0), Vector3.ZERO])
	_track(idle, "Rig:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_track(idle, "Rig/Hips/LeftLeg:rotation", PackedFloat32Array([0.0, 0.7, 1.4]), [Vector3(0.04, 0, 0), Vector3(-0.04, 0, 0), Vector3(0.04, 0, 0)])
	_track(idle, "Rig/Hips/RightLeg:rotation", PackedFloat32Array([0.0, 0.7, 1.4]), [Vector3(-0.04, 0, 0), Vector3(0.04, 0, 0), Vector3(-0.04, 0, 0)])
	_track(idle, "Rig/Hips/Torso/LeftArm:rotation", PackedFloat32Array([0.0, 0.7, 1.4]), [Vector3(0, 0, -0.05), Vector3(0, 0, -0.10), Vector3(0, 0, -0.05)])
	_track(idle, "Rig/Hips/Torso/RightArm:rotation", PackedFloat32Array([0.0, 0.7, 1.4]), [Vector3(0, 0, 0.05), Vector3(0, 0, 0.10), Vector3(0, 0, 0.05)])
	_track(idle, "Rig/Hips/Torso/RelayCore:scale", PackedFloat32Array([0.0, 0.7, 1.4]), [Vector3.ONE, Vector3.ONE * 1.22, Vector3.ONE])

	var run := _animation(0.56, true)
	_track(run, "Rig:position", PackedFloat32Array([0.0, 0.14, 0.28, 0.42, 0.56]), [Vector3.ZERO, Vector3(0, 0.05, 0), Vector3.ZERO, Vector3(0, 0.05, 0), Vector3.ZERO])
	_track(run, "Rig:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_track(run, "Rig/Hips/LeftLeg:rotation", PackedFloat32Array([0.0, 0.28, 0.56]), [Vector3(0.65, 0, 0), Vector3(-0.65, 0, 0), Vector3(0.65, 0, 0)])
	_track(run, "Rig/Hips/RightLeg:rotation", PackedFloat32Array([0.0, 0.28, 0.56]), [Vector3(-0.65, 0, 0), Vector3(0.65, 0, 0), Vector3(-0.65, 0, 0)])
	_track(run, "Rig/Hips/Torso/LeftArm:rotation", PackedFloat32Array([0.0, 0.28, 0.56]), [Vector3(-0.48, 0, -0.05), Vector3(0.48, 0, -0.05), Vector3(-0.48, 0, -0.05)])
	_track(run, "Rig/Hips/Torso/RightArm:rotation", PackedFloat32Array([0.0, 0.28, 0.56]), [Vector3(0.48, 0, 0.05), Vector3(-0.48, 0, 0.05), Vector3(0.48, 0, 0.05)])
	_track(run, "Rig/Hips/Torso/RelayCore:scale", PackedFloat32Array([0.0]), [Vector3.ONE])

	var attack := _animation(0.52)
	_track(attack, "Rig:position", PackedFloat32Array([0.0, 0.24, 0.52]), [Vector3.ZERO, Vector3(0, 0, -0.09), Vector3.ZERO])
	_track(attack, "Rig:rotation", PackedFloat32Array([0.0, 0.24, 0.52]), [Vector3.ZERO, Vector3(0, -0.24, 0), Vector3.ZERO])
	_track(attack, "Rig/Hips/LeftLeg:rotation", PackedFloat32Array([0.0]), [Vector3(-0.18, 0, 0)])
	_track(attack, "Rig/Hips/RightLeg:rotation", PackedFloat32Array([0.0]), [Vector3(0.18, 0, 0)])
	_track(attack, "Rig/Hips/Torso/LeftArm:rotation", PackedFloat32Array([0.0]), [Vector3(0.12, 0, -0.18)])
	_track(attack, "Rig/Hips/Torso/RightArm:rotation", PackedFloat32Array([0.0, 0.18, 0.32, 0.52]), [Vector3(-0.8, 0, 0.3), Vector3(-1.65, 0, 0.55), Vector3(0.75, 0, -0.65), Vector3(0, 0, 0.05)])
	_track(attack, "Rig/Hips/Torso/RelayCore:scale", PackedFloat32Array([0.0]), [Vector3.ONE])

	var ability := _animation(0.74)
	_track(ability, "Rig:position", PackedFloat32Array([0.0, 0.36, 0.74]), [Vector3.ZERO, Vector3(0, 0.04, 0), Vector3.ZERO])
	_track(ability, "Rig:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_track(ability, "Rig/Hips/LeftLeg:rotation", PackedFloat32Array([0.0]), [Vector3(-0.10, 0, 0)])
	_track(ability, "Rig/Hips/RightLeg:rotation", PackedFloat32Array([0.0]), [Vector3(0.10, 0, 0)])
	_track(ability, "Rig/Hips/Torso/LeftArm:rotation", PackedFloat32Array([0.0, 0.28, 0.74]), [Vector3.ZERO, Vector3(-0.35, 0, -1.15), Vector3.ZERO])
	_track(ability, "Rig/Hips/Torso/RightArm:rotation", PackedFloat32Array([0.0, 0.28, 0.74]), [Vector3.ZERO, Vector3(-0.35, 0, 1.15), Vector3.ZERO])
	_track(ability, "Rig/Hips/Torso/RelayCore:scale", PackedFloat32Array([0.0, 0.32, 0.56, 0.74]), [Vector3.ONE, Vector3.ONE * 1.9, Vector3.ONE * 1.45, Vector3.ONE])

	var hurt := _animation(0.38)
	_track(hurt, "Rig:position", PackedFloat32Array([0.0, 0.16, 0.38]), [Vector3.ZERO, Vector3(0, 0, 0.10), Vector3.ZERO])
	_track(hurt, "Rig:rotation", PackedFloat32Array([0.0, 0.16, 0.38]), [Vector3.ZERO, Vector3(0.10, 0, -0.18), Vector3.ZERO])
	_track(hurt, "Rig/Hips/LeftLeg:rotation", PackedFloat32Array([0.0]), [Vector3(0.16, 0, 0)])
	_track(hurt, "Rig/Hips/RightLeg:rotation", PackedFloat32Array([0.0]), [Vector3(-0.16, 0, 0)])
	_track(hurt, "Rig/Hips/Torso/LeftArm:rotation", PackedFloat32Array([0.0]), [Vector3(-0.2, 0, -0.2)])
	_track(hurt, "Rig/Hips/Torso/RightArm:rotation", PackedFloat32Array([0.0]), [Vector3(-0.2, 0, 0.2)])
	_track(hurt, "Rig/Hips/Torso/RelayCore:scale", PackedFloat32Array([0.0]), [Vector3.ONE])

	var defeat := _animation(0.82)
	_track(defeat, "Rig:position", PackedFloat32Array([0.0, 0.82]), [Vector3.ZERO, Vector3(0, -0.20, 0.06)])
	_track(defeat, "Rig:rotation", PackedFloat32Array([0.0, 0.82]), [Vector3.ZERO, Vector3(0.10, 0, -1.35)])
	_track(defeat, "Rig/Hips/LeftLeg:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_track(defeat, "Rig/Hips/RightLeg:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_track(defeat, "Rig/Hips/Torso/LeftArm:rotation", PackedFloat32Array([0.0, 0.82]), [Vector3.ZERO, Vector3(0, 0, -0.65)])
	_track(defeat, "Rig/Hips/Torso/RightArm:rotation", PackedFloat32Array([0.0, 0.82]), [Vector3.ZERO, Vector3(0, 0, 0.65)])
	_track(defeat, "Rig/Hips/Torso/RelayCore:scale", PackedFloat32Array([0.0, 0.82]), [Vector3.ONE, Vector3.ONE * 0.4])

	_install_animations({"idle": idle, "run": run, "attack": attack, "ability": ability, "hurt": hurt, "defeat": defeat})


func _add_drone_animations() -> void:
	var idle := _animation(1.2, true)
	_drone_base(idle, PackedFloat32Array([0.0, 0.6, 1.2]), [Vector3.ZERO, Vector3(0, 0.035, 0), Vector3.ZERO])
	_track(idle, "Rig/Body/LeftRotor:rotation", PackedFloat32Array([0.0, 0.6, 1.2]), [Vector3.ZERO, Vector3(0, PI, 0), Vector3(0, TAU, 0)])
	_track(idle, "Rig/Body/RightRotor:rotation", PackedFloat32Array([0.0, 0.6, 1.2]), [Vector3.ZERO, Vector3(0, -PI, 0), Vector3(0, -TAU, 0)])

	var run := _animation(0.48, true)
	_track(run, "Rig:position", PackedFloat32Array([0.0, 0.12, 0.24, 0.36, 0.48]), [Vector3.ZERO, Vector3(0, 0.045, 0), Vector3.ZERO, Vector3(0, 0.045, 0), Vector3.ZERO])
	_track(run, "Rig:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_drone_legs(run, 0.48, 0.48)
	_track(run, "Rig/Body/Core:scale", PackedFloat32Array([0.0]), [Vector3.ONE])
	_track(run, "Rig/Body/LeftRotor:rotation", PackedFloat32Array([0.0, 0.48]), [Vector3.ZERO, Vector3(0, TAU * 2.0, 0)])
	_track(run, "Rig/Body/RightRotor:rotation", PackedFloat32Array([0.0, 0.48]), [Vector3.ZERO, Vector3(0, -TAU * 2.0, 0)])

	var attack := _animation(0.46)
	_track(attack, "Rig:position", PackedFloat32Array([0.0, 0.22, 0.46]), [Vector3.ZERO, Vector3(0, 0.02, -0.18), Vector3.ZERO])
	_track(attack, "Rig:rotation", PackedFloat32Array([0.0, 0.22, 0.46]), [Vector3.ZERO, Vector3(-0.16, 0, 0), Vector3.ZERO])
	_drone_legs(attack, 0.46, 0.22)
	_track(attack, "Rig/Body/Core:scale", PackedFloat32Array([0.0, 0.22, 0.46]), [Vector3.ONE, Vector3.ONE * 1.65, Vector3.ONE])
	_track(attack, "Rig/Body/LeftRotor:rotation", PackedFloat32Array([0.0, 0.46]), [Vector3.ZERO, Vector3(0, TAU, 0)])
	_track(attack, "Rig/Body/RightRotor:rotation", PackedFloat32Array([0.0, 0.46]), [Vector3.ZERO, Vector3(0, -TAU, 0)])

	var ability := _animation(0.68)
	_drone_base(ability, PackedFloat32Array([0.0, 0.34, 0.68]), [Vector3.ZERO, Vector3(0, 0.08, 0), Vector3.ZERO], false)
	_track(ability, "Rig/Body/Core:scale", PackedFloat32Array([0.0, 0.28, 0.52, 0.68]), [Vector3.ONE, Vector3.ONE * 2.1, Vector3.ONE * 1.5, Vector3.ONE])
	_track(ability, "Rig/Body/LeftRotor:rotation", PackedFloat32Array([0.0, 0.68]), [Vector3.ZERO, Vector3(0, TAU * 3.0, 0)])
	_track(ability, "Rig/Body/RightRotor:rotation", PackedFloat32Array([0.0, 0.68]), [Vector3.ZERO, Vector3(0, -TAU * 3.0, 0)])

	var hurt := _animation(0.34)
	_drone_base(hurt, PackedFloat32Array([0.0, 0.15, 0.34]), [Vector3.ZERO, Vector3(0.10, 0.01, 0.08), Vector3.ZERO], true, false)
	_track(hurt, "Rig:rotation", PackedFloat32Array([0.0, 0.15, 0.34]), [Vector3.ZERO, Vector3(0, 0, 0.28), Vector3.ZERO])

	var defeat := _animation(0.76)
	_drone_base(defeat, PackedFloat32Array([0.0, 0.76]), [Vector3.ZERO, Vector3(0, -0.22, 0.05)], false, false)
	_track(defeat, "Rig:rotation", PackedFloat32Array([0.0, 0.76]), [Vector3.ZERO, Vector3(0.25, 0, 1.4)])
	_track(defeat, "Rig/Body/Core:scale", PackedFloat32Array([0.0, 0.76]), [Vector3.ONE, Vector3.ONE * 0.25])

	_install_animations({"idle": idle, "run": run, "attack": attack, "ability": ability, "hurt": hurt, "defeat": defeat})


func _drone_base(
	animation: Animation,
	times: PackedFloat32Array,
	positions: Array,
	include_core := true,
	include_rotation := true
) -> void:
	_track(animation, "Rig:position", times, positions)
	if include_rotation:
		_track(animation, "Rig:rotation", PackedFloat32Array([0.0]), [Vector3.ZERO])
	_drone_legs(animation, animation.length, 0.10)
	if include_core:
		_track(animation, "Rig/Body/Core:scale", PackedFloat32Array([0.0]), [Vector3.ONE])


func _drone_legs(animation: Animation, length_value: float, amount: float) -> void:
	for leg_name: String in ["FrontLeft", "RearRight"]:
		_track(animation, "Rig/Body/%s:rotation" % leg_name, PackedFloat32Array([0.0, length_value * 0.5, length_value]), [Vector3(amount, 0, 0), Vector3(-amount, 0, 0), Vector3(amount, 0, 0)])
	for leg_name: String in ["FrontRight", "RearLeft"]:
		_track(animation, "Rig/Body/%s:rotation" % leg_name, PackedFloat32Array([0.0, length_value * 0.5, length_value]), [Vector3(-amount, 0, 0), Vector3(amount, 0, 0), Vector3(-amount, 0, 0)])
