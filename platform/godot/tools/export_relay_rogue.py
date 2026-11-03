# SPDX-License-Identifier: MIT
"""Build Kestrel's imported mesh on the shared relay humanoid skeleton."""

import math
import pathlib
import sys

import bpy

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from blender_character_tools import (
    action,
    box,
    clear_scene,
    cone,
    cylinder,
    export_glb,
    humanoid_armature,
    join_parts,
    material,
    output_path,
    r,
    reset_pose,
    sphere,
)


def build_mesh(armature):
    graphite = material("Scout graphite", (0.035, 0.055, 0.075), 0.58, 0.22)
    teal = material("Scout teal", (0.02, 0.31, 0.36), 0.44, 0.20)
    ceramic = material("Scout ceramic", (0.54, 0.68, 0.67), 0.39, 0.16)
    bronze = material("Scout bronze", (0.55, 0.30, 0.11), 0.30, 0.62)
    cyan = material(
        "Scout relay cyan", (0.02, 0.62, 0.76), 0.16, 0.25, (0.01, 0.95, 1.0)
    )
    rose = material(
        "Scout signal rose", (0.74, 0.08, 0.32), 0.20, 0.18, (1.0, 0.02, 0.23)
    )
    cloth = material("Scout veil cloth", (0.025, 0.15, 0.18), 0.90)

    for side, sign in (("L", -1.0), ("R", 1.0)):
        shin = f"shin.{side}"
        thigh = f"thigh.{side}"
        box(
            f"RunnerBoot.{side}",
            shin,
            (0.105 * sign, -0.045, 0.055),
            (0.15, 0.27, 0.10),
            graphite,
            bevel=0.025,
        )
        cylinder(
            f"ShinGuard.{side}",
            shin,
            (0.105 * sign, 0.0, 0.17),
            0.056,
            0.19,
            ceramic if side == "L" else teal,
            vertices=8,
        )
        sphere(
            f"KneeLight.{side}",
            shin,
            (0.105 * sign, -0.052, 0.275),
            (0.073, 0.055, 0.062),
            cyan,
        )
        cylinder(
            f"LegWrap.{side}",
            thigh,
            (0.105 * sign, 0.0, 0.365),
            0.065,
            0.18,
            graphite,
            vertices=8,
        )

    box("ScoutPelvis", "hips", (0.0, 0.0, 0.49), (0.27, 0.19, 0.15), graphite, bevel=0.035)
    box("UtilityBelt", "hips", (0.0, 0.0, 0.55), (0.34, 0.22, 0.055), bronze, bevel=0.012)
    for side, sign in (("L", -1.0), ("R", 1.0)):
        box(
            f"BeltPouch.{side}",
            "hips",
            (0.18 * sign, 0.01, 0.49),
            (0.085, 0.095, 0.12),
            teal,
            bevel=0.018,
        )

    cylinder("ScoutWaist", "spine", (0.0, 0.0, 0.62), 0.115, 0.17, graphite, vertices=10)
    box("ScoutChest", "spine", (0.0, 0.0, 0.72), (0.35, 0.20, 0.25), teal, bevel=0.045)
    box("ChestPlate", "spine", (0.0, -0.125, 0.72), (0.24, 0.05, 0.18), graphite, bevel=0.022)
    box(
        "SignalSash",
        "spine",
        (-0.035, -0.158, 0.72),
        (0.075, 0.028, 0.30),
        ceramic,
        rotation=(0.0, math.radians(-18), math.radians(-24)),
        bevel=0.009,
    )
    sphere("VeilCore", "core", (0.0, -0.165, 0.745), (0.052, 0.030, 0.052), cyan)
    box("RaisedCollar", "spine", (0.0, 0.015, 0.845), (0.24, 0.19, 0.095), cloth, bevel=0.025)

    cone("Hood", "head", (0.0, 0.0, 0.985), (0.17, 0.12), 0.30, cloth, vertices=8)
    sphere("HoodCrown", "head", (0.0, 0.025, 1.04), (0.14, 0.13, 0.14), cloth)
    box("ScoutMask", "head", (0.0, -0.115, 0.96), (0.20, 0.07, 0.13), graphite, bevel=0.025)
    box("SplitVisor", "head", (-0.042, -0.158, 1.005), (0.085, 0.022, 0.044), cyan, bevel=0.009)
    box("SignalEye", "head", (0.067, -0.159, 1.005), (0.055, 0.023, 0.044), rose, bevel=0.009)
    cone(
        "HoodTail",
        "head",
        (0.0, 0.12, 0.93),
        (0.07, 0.015),
        0.28,
        cloth,
        rotation=(math.radians(-38), 0.0, 0.0),
        vertices=6,
    )

    for side, sign in (("L", -1.0), ("R", 1.0)):
        upper = f"upper_arm.{side}"
        forearm = f"forearm.{side}"
        hand = f"hand.{side}"
        sphere(
            f"Shoulder.{side}",
            upper,
            (0.27 * sign, 0.0, 0.76),
            (0.105, 0.085, 0.075),
            ceramic if side == "L" else teal,
        )
        cylinder(
            f"UpperArm.{side}", upper, (0.275 * sign, 0.0, 0.665), 0.048, 0.19, cloth, vertices=8
        )
        cylinder(
            f"ForearmBladeGuard.{side}",
            forearm,
            (0.295 * sign, -0.005, 0.505),
            0.055,
            0.17,
            graphite,
            vertices=8,
        )
        box(
            f"ForearmSignal.{side}",
            forearm,
            (0.295 * sign, -0.062, 0.505),
            (0.045, 0.020, 0.12),
            rose if side == "L" else cyan,
            bevel=0.006,
        )
        sphere(f"Glove.{side}", hand, (0.305 * sign, 0.0, 0.405), (0.060, 0.054, 0.060), bronze)

    for side, sign in (("L", -1.0), ("R", 1.0)):
        hand = f"hand.{side}"
        blade_colour = rose if side == "L" else cyan
        cylinder(
            f"KnifeGrip.{side}", hand, (0.345 * sign, 0.0, 0.34), 0.020, 0.14, bronze, vertices=8
        )
        box(
            f"KnifeGuard.{side}",
            hand,
            (0.345 * sign, 0.0, 0.29),
            (0.105, 0.045, 0.032),
            ceramic,
            bevel=0.008,
        )
        box(
            f"KnifeBlade.{side}",
            hand,
            (0.345 * sign, 0.0, 0.17),
            (0.045, 0.025, 0.23),
            blade_colour,
            bevel=0.009,
        )

    box(
        "VeilShoulder",
        "cape",
        (-0.09, 0.13, 0.72),
        (0.25, 0.035, 0.18),
        cloth,
        rotation=(0.0, math.radians(-8), math.radians(8)),
        bevel=0.018,
    )
    cone(
        "VeilTail.L",
        "cape",
        (-0.08, 0.14, 0.48),
        (0.13, 0.045),
        0.38,
        cloth,
        rotation=(0.0, math.radians(-8), math.radians(-7)),
        vertices=6,
    )
    cone(
        "VeilTail.R",
        "cape",
        (0.09, 0.14, 0.50),
        (0.105, 0.035),
        0.34,
        teal,
        rotation=(0.0, math.radians(8), math.radians(9)),
        vertices=6,
    )

    return join_parts(
        armature,
        "RelayRogueMesh",
        "RelayRogueGeometry",
        "RelayRogueSkin",
    )


def build_animations(armature):
    action(
        armature,
        "Kestrel_Idle-loop",
        [
            (1, {"spine": {"rotation": r(2, 0, -2)}, "head": {"rotation": r(0, 0, 3)}, "core": {"scale": (1.0, 1.0, 1.0)}, "cape": {"rotation": r(-4)}}),
            (20, {"spine": {"rotation": r(-2, 0, 2)}, "head": {"rotation": r(0, 0, -4)}, "core": {"scale": (1.22, 1.22, 1.22)}, "cape": {"rotation": r(5)}}),
            (39, {"spine": {"rotation": r(2, 0, -2)}, "head": {"rotation": r(0, 0, 3)}, "core": {"scale": (1.0, 1.0, 1.0)}, "cape": {"rotation": r(-4)}}),
        ],
    )
    action(
        armature,
        "Kestrel_Sprint-loop",
        [
            (1, {"root": {"rotation": r(8)}, "thigh.L": {"rotation": r(48)}, "thigh.R": {"rotation": r(-42)}, "shin.L": {"rotation": r(-22)}, "upper_arm.L": {"rotation": r(-38)}, "upper_arm.R": {"rotation": r(34)}, "cape": {"rotation": r(-22)}}),
            (5, {"root": {"rotation": r(11)}, "thigh.L": {"rotation": r(5)}, "thigh.R": {"rotation": r(-5)}, "shin.L": {"rotation": r(-48)}, "shin.R": {"rotation": r(-10)}, "spine": {"rotation": r(7)}, "cape": {"rotation": r(-29)}}),
            (9, {"root": {"rotation": r(8)}, "thigh.L": {"rotation": r(-42)}, "thigh.R": {"rotation": r(48)}, "shin.R": {"rotation": r(-22)}, "upper_arm.L": {"rotation": r(34)}, "upper_arm.R": {"rotation": r(-38)}, "cape": {"rotation": r(-22)}}),
            (13, {"root": {"rotation": r(11)}, "thigh.L": {"rotation": r(-5)}, "thigh.R": {"rotation": r(5)}, "shin.R": {"rotation": r(-48)}, "shin.L": {"rotation": r(-10)}, "spine": {"rotation": r(7)}, "cape": {"rotation": r(-29)}}),
            (17, {"root": {"rotation": r(8)}, "thigh.L": {"rotation": r(48)}, "thigh.R": {"rotation": r(-42)}, "shin.L": {"rotation": r(-22)}, "upper_arm.L": {"rotation": r(-38)}, "upper_arm.R": {"rotation": r(34)}, "cape": {"rotation": r(-22)}}),
        ],
    )
    action(
        armature,
        "Kestrel_Crosscut",
        [
            (1, {"upper_arm.L": {"rotation": r(-55, 0, 58)}, "forearm.L": {"rotation": r(-58)}, "upper_arm.R": {"rotation": r(-55, 0, -58)}, "forearm.R": {"rotation": r(-58)}, "spine": {"rotation": r(0, 0, -12)}}),
            (6, {"upper_arm.L": {"rotation": r(-105, 0, 82)}, "forearm.L": {"rotation": r(-75)}, "upper_arm.R": {"rotation": r(35, 0, -38)}, "forearm.R": {"rotation": r(25)}, "spine": {"rotation": r(6, 0, -25)}, "thigh.R": {"rotation": r(-18)}}),
            (10, {"upper_arm.L": {"rotation": r(42, 0, -48)}, "forearm.L": {"rotation": r(25)}, "upper_arm.R": {"rotation": r(-105, 0, -82)}, "forearm.R": {"rotation": r(-75)}, "spine": {"rotation": r(6, 0, 25)}, "thigh.L": {"rotation": r(-18)}}),
            (16, {}),
        ],
    )
    action(
        armature,
        "Kestrel_Veil",
        [
            (1, {"core": {"scale": (1.0, 1.0, 1.0)}}),
            (8, {"upper_arm.L": {"rotation": r(-28, 0, 64)}, "forearm.L": {"rotation": r(-55)}, "upper_arm.R": {"rotation": r(-28, 0, -64)}, "forearm.R": {"rotation": r(-55)}, "head": {"rotation": r(-9)}, "core": {"scale": (2.2, 2.2, 2.2)}, "cape": {"rotation": r(24)}}),
            (16, {"root": {"rotation": r(-7)}, "spine": {"rotation": r(-10)}, "core": {"scale": (1.6, 1.6, 1.6)}, "cape": {"rotation": r(-26)}}),
            (24, {}),
        ],
    )
    action(
        armature,
        "Kestrel_Recoil",
        [
            (1, {}),
            (5, {"root": {"rotation": r(9, 0, 17)}, "spine": {"rotation": r(18, 0, 13)}, "head": {"rotation": r(-15)}, "upper_arm.L": {"rotation": r(-35, 0, -28)}, "upper_arm.R": {"rotation": r(-35, 0, 28)}, "cape": {"rotation": r(17)}}),
            (12, {}),
        ],
    )
    action(
        armature,
        "Kestrel_Down",
        [
            (1, {}),
            (9, {"root": {"rotation": r(24, 0, 22)}, "spine": {"rotation": r(20)}, "upper_arm.L": {"rotation": r(-35, 0, -38)}, "upper_arm.R": {"rotation": r(-35, 0, 38)}, "cape": {"rotation": r(26)}}),
            (24, {"root": {"rotation": r(88, 0, 28)}, "spine": {"rotation": r(26)}, "head": {"rotation": r(-30)}, "upper_arm.L": {"rotation": r(18, 0, -55)}, "upper_arm.R": {"rotation": r(18, 0, 55)}, "core": {"scale": (0.3, 0.3, 0.3)}, "cape": {"rotation": r(42)}}),
        ],
    )
    armature.animation_data.action = bpy.data.actions["Kestrel_Idle-loop"]
    reset_pose(armature)


def main():
    target = output_path("export_relay_rogue.py")
    clear_scene()
    armature = humanoid_armature("RelayRogueRig", "rogue")
    mesh = build_mesh(armature)
    build_animations(armature)
    export_glb(target, armature, mesh, "relay_rogue_v1", 39)
    print(f"exported relay rogue GLB: {target}")


if __name__ == "__main__":
    main()
