# SPDX-License-Identifier: MIT
"""Small deterministic Blender helpers for Grandleon character prototypes."""

import math
import pathlib
import sys

import bpy
from mathutils import Vector


FPS = 30
PARTS = []


def output_path(script_name):
    try:
        separator = sys.argv.index("--")
        value = pathlib.Path(sys.argv[separator + 1]).resolve()
    except (ValueError, IndexError):
        raise SystemExit(
            f"usage: blender --background --python {script_name} -- OUTPUT.glb"
        )
    value.parent.mkdir(parents=True, exist_ok=True)
    return value


def clear_scene():
    PARTS.clear()
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in (
        bpy.data.meshes,
        bpy.data.armatures,
        bpy.data.materials,
        bpy.data.actions,
    ):
        for value in list(collection):
            collection.remove(value)


def material(name, colour, roughness, metallic=0.0, emission=None):
    value = bpy.data.materials.new(name)
    value.diffuse_color = (*colour, 1.0)
    value.use_nodes = True
    shader = value.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*colour, 1.0)
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = metallic
    if emission is not None:
        emission_input = shader.inputs.get("Emission Color") or shader.inputs.get(
            "Emission"
        )
        strength_input = shader.inputs.get("Emission Strength")
        if emission_input is not None:
            emission_input.default_value = (*emission, 1.0)
        if strength_input is not None:
            strength_input.default_value = 2.4
    return value


def select_only(value):
    bpy.ops.object.select_all(action="DESELECT")
    value.select_set(True)
    bpy.context.view_layer.objects.active = value


def finish_part(value, name, bone, surface, smooth=False, bevel=0.0):
    value.name = name
    select_only(value)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    if bevel > 0.0:
        modifier = value.modifiers.new("Edge softness", "BEVEL")
        modifier.width = bevel
        modifier.segments = 2
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    value.data.materials.append(surface)
    group = value.vertex_groups.new(name=bone)
    group.add(range(len(value.data.vertices)), 1.0, "REPLACE")
    for polygon in value.data.polygons:
        polygon.use_smooth = smooth
    PARTS.append(value)
    return value


def box(
    name,
    bone,
    location,
    dimensions,
    surface,
    rotation=(0.0, 0.0, 0.0),
    bevel=0.0,
):
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=rotation)
    value = bpy.context.object
    value.dimensions = dimensions
    return finish_part(value, name, bone, surface, bevel=bevel)


def cylinder(
    name,
    bone,
    location,
    radius,
    depth,
    surface,
    rotation=(0.0, 0.0, 0.0),
    vertices=10,
    smooth=True,
):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        end_fill_type="NGON",
        location=location,
        rotation=rotation,
    )
    return finish_part(bpy.context.object, name, bone, surface, smooth=smooth)


def cylinder_between(
    name,
    bone,
    start,
    end,
    radius,
    surface,
    vertices=10,
    smooth=True,
):
    start_value = Vector(start)
    end_value = Vector(end)
    direction = end_value - start_value
    midpoint = (start_value + end_value) * 0.5
    rotation = direction.to_track_quat("Z", "Y").to_euler()
    return cylinder(
        name,
        bone,
        midpoint,
        radius,
        direction.length,
        surface,
        rotation=rotation,
        vertices=vertices,
        smooth=smooth,
    )


def sphere(name, bone, location, scale, surface, subdivisions=2):
    bpy.ops.mesh.primitive_ico_sphere_add(
        subdivisions=subdivisions, radius=1.0, location=location
    )
    value = bpy.context.object
    value.scale = scale
    return finish_part(value, name, bone, surface, smooth=True)


def cone(
    name,
    bone,
    location,
    radii,
    depth,
    surface,
    rotation=(0.0, 0.0, 0.0),
    vertices=8,
):
    bpy.ops.mesh.primitive_cone_add(
        vertices=vertices,
        radius1=radii[0],
        radius2=radii[1],
        depth=depth,
        location=location,
        rotation=rotation,
    )
    return finish_part(bpy.context.object, name, bone, surface, smooth=True)


def add_bone(armature, name, head, tail, parent=None):
    value = armature.edit_bones.new(name)
    value.head = Vector(head)
    value.tail = Vector(tail)
    if parent is not None:
        value.parent = armature.edit_bones[parent]
    return value


def humanoid_armature(object_name, archetype):
    """Create the shared 16-bone humanoid topology used by compatible rigs."""
    data = bpy.data.armatures.new("RelayHumanoidSkeleton")
    value = bpy.data.objects.new(object_name, data)
    bpy.context.collection.objects.link(value)
    select_only(value)
    bpy.ops.object.mode_set(mode="EDIT")
    add_bone(data, "root", (0.0, 0.0, 0.0), (0.0, 0.0, 0.12))
    add_bone(data, "hips", (0.0, 0.0, 0.38), (0.0, 0.0, 0.54), "root")
    add_bone(data, "spine", (0.0, 0.0, 0.54), (0.0, 0.0, 0.81), "hips")
    add_bone(data, "head", (0.0, 0.0, 0.81), (0.0, 0.0, 1.08), "spine")
    add_bone(data, "core", (0.0, -0.14, 0.68), (0.0, -0.14, 0.77), "spine")
    add_bone(data, "cape", (0.0, 0.12, 0.75), (0.0, 0.12, 0.43), "spine")
    for side, sign in (("L", -1.0), ("R", 1.0)):
        add_bone(
            data,
            f"upper_arm.{side}",
            (0.24 * sign, 0.0, 0.76),
            (0.28 * sign, 0.0, 0.59),
            "spine",
        )
        add_bone(
            data,
            f"forearm.{side}",
            (0.28 * sign, 0.0, 0.59),
            (0.30 * sign, 0.0, 0.43),
            f"upper_arm.{side}",
        )
        add_bone(
            data,
            f"hand.{side}",
            (0.30 * sign, 0.0, 0.43),
            (0.31 * sign, 0.0, 0.34),
            f"forearm.{side}",
        )
        add_bone(
            data,
            f"thigh.{side}",
            (0.105 * sign, 0.0, 0.43),
            (0.105 * sign, 0.0, 0.24),
            "hips",
        )
        add_bone(
            data,
            f"shin.{side}",
            (0.105 * sign, 0.0, 0.24),
            (0.105 * sign, 0.0, 0.07),
            f"thigh.{side}",
        )
    bpy.ops.object.mode_set(mode="OBJECT")
    value.show_in_front = True
    value["grandleon_asset"] = "character"
    value["grandleon_archetype"] = archetype
    value["grandleon_contract"] = 1
    return value


def join_parts(armature, object_name, data_name, modifier_name):
    if not PARTS:
        raise RuntimeError("character mesh has no parts")
    select_only(PARTS[0])
    for value in PARTS:
        value.select_set(True)
    bpy.context.view_layer.objects.active = PARTS[0]
    bpy.ops.object.join()
    mesh = bpy.context.object
    mesh.name = object_name
    mesh.data.name = data_name
    mesh.parent = armature
    modifier = mesh.modifiers.new(modifier_name, "ARMATURE")
    modifier.object = armature
    return mesh


def reset_pose(armature):
    for bone in armature.pose.bones:
        bone.rotation_mode = "XYZ"
        bone.location = (0.0, 0.0, 0.0)
        bone.rotation_euler = (0.0, 0.0, 0.0)
        bone.scale = (1.0, 1.0, 1.0)


def pose_frame(armature, frame, changes):
    reset_pose(armature)
    for name, values in changes.items():
        bone = armature.pose.bones[name]
        if "location" in values:
            bone.location = values["location"]
        if "rotation" in values:
            bone.rotation_euler = values["rotation"]
        if "scale" in values:
            bone.scale = values["scale"]
    for bone in armature.pose.bones:
        bone.keyframe_insert("location", frame=frame, group=bone.name)
        bone.keyframe_insert("rotation_euler", frame=frame, group=bone.name)
        bone.keyframe_insert("scale", frame=frame, group=bone.name)


def action(armature, name, frames):
    reset_pose(armature)
    value = bpy.data.actions.new(name)
    value.use_fake_user = True
    armature.animation_data_create()
    armature.animation_data.action = value
    for frame, changes in frames:
        pose_frame(armature, frame, changes)
    return value


def r(x=0.0, y=0.0, z=0.0):
    return (math.radians(x), math.radians(y), math.radians(z))


def export_glb(path, armature, mesh, generator_name, frame_end):
    bpy.context.scene.render.fps = FPS
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = frame_end
    bpy.context.scene["grandleon_generator"] = generator_name
    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = armature
    result = bpy.ops.export_scene.gltf(
        filepath=str(path),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_animation_mode="ACTIONS",
        export_skins=True,
        export_morph=False,
        export_texcoords=False,
        export_cameras=False,
        export_lights=False,
        export_extras=True,
        export_yup=True,
        export_force_sampling=True,
        export_def_bones=True,
        export_leaf_bone=False,
        export_optimize_animation_size=True,
        check_existing=False,
    )
    if "FINISHED" not in result:
        raise SystemExit("Blender did not finish the GLB export")
