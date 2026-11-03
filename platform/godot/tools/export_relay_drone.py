# SPDX-License-Identifier: MIT
"""Build the imported relay drone and its six semantic animation clips."""

import math
import pathlib
import sys

import bpy

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from blender_character_tools import (
    action,
    add_bone,
    box,
    clear_scene,
    cone,
    cylinder,
    cylinder_between,
    export_glb,
    join_parts,
    material,
    output_path,
    r,
    reset_pose,
    select_only,
    sphere,
)


LEGS = {
    "front.L": ((-0.22, -0.18, 0.48), (-0.34, -0.29, 0.28), (-0.36, -0.39, 0.08)),
    "front.R": ((0.22, -0.18, 0.48), (0.34, -0.29, 0.28), (0.36, -0.39, 0.08)),
    "rear.L": ((-0.22, 0.18, 0.48), (-0.35, 0.29, 0.27), (-0.37, 0.39, 0.08)),
    "rear.R": ((0.22, 0.18, 0.48), (0.35, 0.29, 0.27), (0.37, 0.39, 0.08)),
}


def build_armature():
    data = bpy.data.armatures.new("RelayDroneSkeleton")
    value = bpy.data.objects.new("RelayDroneRig", data)
    bpy.context.collection.objects.link(value)
    select_only(value)
    bpy.ops.object.mode_set(mode="EDIT")
    add_bone(data, "root", (0.0, 0.0, 0.02), (0.0, 0.0, 0.14))
    add_bone(data, "body", (0.0, 0.0, 0.42), (0.0, 0.0, 0.66), "root")
    add_bone(data, "head", (0.0, -0.18, 0.56), (0.0, -0.43, 0.53), "body")
    add_bone(data, "jaw", (0.0, -0.38, 0.49), (0.0, -0.56, 0.41), "head")
    add_bone(data, "core", (0.0, -0.16, 0.59), (0.0, -0.23, 0.59), "body")
    add_bone(data, "tail", (0.0, 0.24, 0.56), (0.0, 0.62, 0.70), "body")
    for side, sign in (("L", -1.0), ("R", 1.0)):
        add_bone(
            data,
            f"rotor.{side}",
            (0.43 * sign, 0.02, 0.54),
            (0.43 * sign, 0.02, 0.67),
            "body",
        )
    for leg_name, points in LEGS.items():
        shoulder, knee, foot = points
        upper_name = f"upper_{leg_name}"
        lower_name = f"lower_{leg_name}"
        add_bone(data, upper_name, shoulder, knee, "body")
        add_bone(data, lower_name, knee, foot, upper_name)
    bpy.ops.object.mode_set(mode="OBJECT")
    value.show_in_front = True
    value["grandleon_asset"] = "character"
    value["grandleon_archetype"] = "beast"
    value["grandleon_contract"] = 1
    return value


def build_mesh(armature):
    shell = material("Drone violet shell", (0.25, 0.12, 0.43), 0.38, 0.24)
    shell_light = material("Drone lavender armour", (0.55, 0.34, 0.72), 0.42, 0.16)
    metal = material("Drone dark metal", (0.07, 0.09, 0.14), 0.29, 0.76)
    joint = material("Drone joint", (0.20, 0.23, 0.31), 0.34, 0.62)
    cyan = material(
        "Drone relay cyan", (0.02, 0.58, 0.78), 0.18, 0.26, (0.01, 0.92, 1.0)
    )
    magenta = material(
        "Drone warning magenta", (0.73, 0.05, 0.43), 0.22, 0.18, (1.0, 0.01, 0.38)
    )
    tooth = material("Drone ceramic tooth", (0.72, 0.78, 0.76), 0.35, 0.12)

    sphere("Hull", "body", (0.0, 0.02, 0.55), (0.29, 0.40, 0.22), metal)
    box("TopCarapace", "body", (0.0, 0.03, 0.68), (0.39, 0.50, 0.13), shell, bevel=0.055)
    box("SpinePlate", "body", (0.0, 0.20, 0.73), (0.20, 0.24, 0.08), shell_light, bevel=0.025)
    for side, sign in (("L", -1.0), ("R", 1.0)):
        box(
            f"Flank.{side}",
            "body",
            (0.25 * sign, 0.03, 0.58),
            (0.11, 0.46, 0.20),
            shell,
            rotation=(0.0, math.radians(10.0 * sign), 0.0),
            bevel=0.035,
        )
        box(
            f"SignalStripe.{side}",
            "body",
            (0.30 * sign, -0.02, 0.61),
            (0.025, 0.27, 0.055),
            magenta,
            bevel=0.008,
        )

    sphere("HeadShell", "head", (0.0, -0.34, 0.57), (0.25, 0.24, 0.19), shell)
    box("Mask", "head", (0.0, -0.51, 0.56), (0.32, 0.13, 0.18), metal, bevel=0.035)
    box("Visor", "head", (0.0, -0.585, 0.61), (0.25, 0.025, 0.055), cyan, bevel=0.012)
    for side, sign in (("L", -1.0), ("R", 1.0)):
        cone(
            f"EarFin.{side}",
            "head",
            (0.20 * sign, -0.35, 0.76),
            (0.065, 0.012),
            0.23,
            shell_light,
            rotation=(0.0, math.radians(18.0 * sign), 0.0),
            vertices=6,
        )

    box("Jaw", "jaw", (0.0, -0.51, 0.45), (0.30, 0.17, 0.09), joint, bevel=0.025)
    for side, sign in (("L", -1.0), ("R", 1.0)):
        for index in range(2):
            cone(
                f"Fang.{side}.{index}",
                "jaw",
                (0.075 * sign, -0.58 + index * 0.055, 0.40),
                (0.028, 0.004),
                0.09,
                tooth,
                rotation=(math.pi, 0.0, 0.0),
                vertices=6,
            )

    sphere("RelayCore", "core", (0.0, -0.245, 0.59), (0.075, 0.04, 0.075), cyan)
    cylinder(
        "CoreRing",
        "core",
        (0.0, -0.225, 0.59),
        0.11,
        0.025,
        shell_light,
        rotation=(math.pi / 2.0, 0.0, 0.0),
        vertices=12,
        smooth=False,
    )

    cylinder_between("TailBoom", "tail", (0.0, 0.24, 0.57), (0.0, 0.58, 0.69), 0.055, metal)
    box(
        "TailBlade",
        "tail",
        (0.0, 0.62, 0.72),
        (0.07, 0.22, 0.24),
        shell_light,
        rotation=(math.radians(-14.0), 0.0, 0.0),
        bevel=0.025,
    )
    sphere("TailLight", "tail", (0.0, 0.67, 0.82), (0.045, 0.035, 0.045), magenta)

    for side, sign in (("L", -1.0), ("R", 1.0)):
        rotor = f"rotor.{side}"
        location = (0.43 * sign, 0.02, 0.58)
        cylinder(f"RotorPod.{side}", rotor, location, 0.12, 0.12, shell, vertices=12)
        cylinder(
            f"RotorHub.{side}",
            rotor,
            (0.43 * sign, 0.02, 0.66),
            0.045,
            0.075,
            cyan,
            vertices=10,
        )
        box(
            f"RotorBladeA.{side}",
            rotor,
            (0.43 * sign, 0.02, 0.70),
            (0.42, 0.045, 0.025),
            joint,
            bevel=0.012,
        )
        box(
            f"RotorBladeB.{side}",
            rotor,
            (0.43 * sign, 0.02, 0.70),
            (0.045, 0.42, 0.025),
            joint,
            bevel=0.012,
        )

    for leg_name, points in LEGS.items():
        shoulder, knee, foot = points
        upper_name = f"upper_{leg_name}"
        lower_name = f"lower_{leg_name}"
        sphere(f"Shoulder.{leg_name}", upper_name, shoulder, (0.105, 0.105, 0.105), shell_light)
        cylinder_between(
            f"UpperLeg.{leg_name}", upper_name, shoulder, knee, 0.060, shell, vertices=8
        )
        sphere(f"Knee.{leg_name}", lower_name, knee, (0.078, 0.078, 0.078), cyan)
        cylinder_between(
            f"LowerLeg.{leg_name}", lower_name, knee, foot, 0.050, joint, vertices=8
        )
        box(
            f"Foot.{leg_name}",
            lower_name,
            (foot[0], foot[1] - 0.035, foot[2]),
            (0.15, 0.22, 0.07),
            metal,
            bevel=0.022,
        )

    return join_parts(
        armature,
        "RelayDroneMesh",
        "RelayDroneGeometry",
        "RelayDroneSkin",
    )


def build_animations(armature):
    action(
        armature,
        "Drone_Hover-loop",
        [
            (1, {"body": {"location": (0.0, 0.0, 0.0)}, "core": {"scale": (1.0, 1.0, 1.0)}, "rotor.L": {"rotation": r(0, 0, 0)}, "rotor.R": {"rotation": r(0, 0, 0)}}),
            (20, {"body": {"location": (0.0, 0.0, 0.025)}, "head": {"rotation": r(2)}, "tail": {"rotation": r(-3, 0, 3)}, "core": {"scale": (1.22, 1.22, 1.22)}, "rotor.L": {"rotation": r(0, 0, 180)}, "rotor.R": {"rotation": r(0, 0, -180)}}),
            (39, {"body": {"location": (0.0, 0.0, 0.0)}, "core": {"scale": (1.0, 1.0, 1.0)}, "rotor.L": {"rotation": r(0, 0, 359)}, "rotor.R": {"rotation": r(0, 0, -359)}}),
        ],
    )
    action(
        armature,
        "Drone_Scamper-loop",
        [
            (1, {"upper_front.L": {"rotation": r(32)}, "upper_rear.R": {"rotation": r(32)}, "upper_front.R": {"rotation": r(-28)}, "upper_rear.L": {"rotation": r(-28)}, "tail": {"rotation": r(-8)}, "rotor.L": {"rotation": r(0, 0, 0)}, "rotor.R": {"rotation": r(0, 0, 0)}}),
            (6, {"body": {"location": (0.0, 0.0, 0.035)}, "lower_front.L": {"rotation": r(-38)}, "lower_rear.R": {"rotation": r(-38)}, "rotor.L": {"rotation": r(0, 0, 180)}, "rotor.R": {"rotation": r(0, 0, -180)}}),
            (11, {"upper_front.L": {"rotation": r(-28)}, "upper_rear.R": {"rotation": r(-28)}, "upper_front.R": {"rotation": r(32)}, "upper_rear.L": {"rotation": r(32)}, "tail": {"rotation": r(8)}, "rotor.L": {"rotation": r(0, 0, 360)}, "rotor.R": {"rotation": r(0, 0, -360)}}),
            (16, {"body": {"location": (0.0, 0.0, 0.035)}, "lower_front.R": {"rotation": r(-38)}, "lower_rear.L": {"rotation": r(-38)}, "rotor.L": {"rotation": r(0, 0, 540)}, "rotor.R": {"rotation": r(0, 0, -540)}}),
            (21, {"upper_front.L": {"rotation": r(32)}, "upper_rear.R": {"rotation": r(32)}, "upper_front.R": {"rotation": r(-28)}, "upper_rear.L": {"rotation": r(-28)}, "tail": {"rotation": r(-8)}, "rotor.L": {"rotation": r(0, 0, 720)}, "rotor.R": {"rotation": r(0, 0, -720)}}),
        ],
    )
    action(
        armature,
        "Drone_Bite",
        [
            (1, {"head": {"rotation": r(-8)}, "jaw": {"rotation": r(18)}}),
            (7, {"body": {"rotation": r(-9)}, "head": {"rotation": r(24)}, "jaw": {"rotation": r(48)}, "upper_front.L": {"rotation": r(-18)}, "upper_front.R": {"rotation": r(-18)}}),
            (11, {"body": {"rotation": r(12)}, "head": {"rotation": r(-22)}, "jaw": {"rotation": r(-10)}}),
            (18, {}),
        ],
    )
    action(
        armature,
        "Drone_Charge",
        [
            (1, {"core": {"scale": (1.0, 1.0, 1.0)}}),
            (10, {"body": {"location": (0.0, 0.0, 0.045)}, "head": {"rotation": r(-10)}, "core": {"scale": (2.25, 2.25, 2.25)}, "rotor.L": {"rotation": r(0, 0, 240)}, "rotor.R": {"rotation": r(0, 0, -240)}, "tail": {"rotation": r(-12)}}),
            (18, {"core": {"scale": (1.55, 1.55, 1.55)}, "rotor.L": {"rotation": r(0, 0, 480)}, "rotor.R": {"rotation": r(0, 0, -480)}, "tail": {"rotation": r(8)}}),
            (25, {}),
        ],
    )
    action(
        armature,
        "Drone_Stagger",
        [
            (1, {}),
            (5, {"root": {"rotation": r(8, 0, -18)}, "head": {"rotation": r(16, 0, 10)}, "tail": {"rotation": r(-20)}, "upper_front.L": {"rotation": r(-24)}, "lower_rear.R": {"rotation": r(30)}}),
            (13, {}),
        ],
    )
    action(
        armature,
        "Drone_Collapse",
        [
            (1, {}),
            (11, {"root": {"rotation": r(22, 0, -28)}, "head": {"rotation": r(18)}, "jaw": {"rotation": r(24)}, "tail": {"rotation": r(-24)}, "upper_front.L": {"rotation": r(-28)}, "upper_rear.R": {"rotation": r(28)}}),
            (28, {"root": {"rotation": r(82, 0, -35), "location": (0.0, 0.0, -0.10)}, "head": {"rotation": r(30)}, "jaw": {"rotation": r(38)}, "core": {"scale": (0.25, 0.25, 0.25)}, "rotor.L": {"rotation": r(0, 18, 40)}, "rotor.R": {"rotation": r(0, -18, -40)}, "lower_front.L": {"rotation": r(45)}, "lower_rear.R": {"rotation": r(-45)}}),
        ],
    )
    armature.animation_data.action = bpy.data.actions["Drone_Hover-loop"]
    reset_pose(armature)


def main():
    target = output_path("export_relay_drone.py")
    clear_scene()
    armature = build_armature()
    mesh = build_mesh(armature)
    build_animations(armature)
    export_glb(target, armature, mesh, "relay_drone_v1", 39)
    print(f"exported relay drone GLB: {target}")


if __name__ == "__main__":
    main()
