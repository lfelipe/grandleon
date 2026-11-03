# SPDX-License-Identifier: MIT
"""Build Morrow's imported mesh on the shared relay humanoid skeleton."""

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
    obsidian = material("Warden obsidian", (0.025, 0.035, 0.065), 0.54, 0.30)
    violet = material("Warden violet", (0.22, 0.08, 0.39), 0.48, 0.18)
    lavender = material("Warden lavender", (0.48, 0.25, 0.68), 0.38, 0.20)
    silver = material("Weathered silver", (0.47, 0.55, 0.57), 0.34, 0.68)
    brass = material("Relay brass", (0.52, 0.33, 0.12), 0.32, 0.62)
    arc = material(
        "Storm arc", (0.08, 0.58, 0.76), 0.14, 0.28, (0.05, 0.88, 1.0)
    )
    storm = material(
        "Storm violet", (0.56, 0.13, 0.78), 0.18, 0.20, (0.78, 0.08, 1.0)
    )
    cloth = material("Warden mantle", (0.075, 0.035, 0.13), 0.92)

    for side, sign in (("L", -1.0), ("R", 1.0)):
        shin = f"shin.{side}"
        thigh = f"thigh.{side}"
        box(
            f"WardenBoot.{side}",
            shin,
            (0.105 * sign, -0.045, 0.055),
            (0.16, 0.27, 0.10),
            obsidian,
            bevel=0.025,
        )
        cylinder(
            f"RibbedGreave.{side}",
            shin,
            (0.105 * sign, 0.0, 0.17),
            0.061,
            0.19,
            silver if side == "L" else violet,
            vertices=8,
        )
        sphere(
            f"StormKnee.{side}",
            shin,
            (0.105 * sign, -0.045, 0.275),
            (0.076, 0.058, 0.065),
            storm if side == "L" else arc,
        )
        cylinder(
            f"WardenLeg.{side}",
            thigh,
            (0.105 * sign, 0.0, 0.365),
            0.066,
            0.18,
            cloth,
            vertices=8,
        )

    box("WardenPelvis", "hips", (0.0, 0.0, 0.49), (0.29, 0.20, 0.15), obsidian, bevel=0.035)
    box("RelayBelt", "hips", (0.0, -0.005, 0.55), (0.34, 0.22, 0.055), brass, bevel=0.012)
    box("BeltSeal", "hips", (0.0, -0.126, 0.55), (0.09, 0.03, 0.07), storm, bevel=0.012)
    cone("FrontRobe", "hips", (0.0, -0.01, 0.34), (0.20, 0.11), 0.43, cloth, vertices=7)

    cylinder("WardenWaist", "spine", (0.0, 0.0, 0.62), 0.12, 0.17, violet, vertices=10)
    box("WardenChest", "spine", (0.0, 0.0, 0.72), (0.38, 0.21, 0.25), obsidian, bevel=0.045)
    box("StormCuirass", "spine", (0.0, -0.126, 0.72), (0.25, 0.05, 0.18), violet, bevel=0.022)
    box(
        "RelayGlyph",
        "spine",
        (0.0, -0.16, 0.72),
        (0.055, 0.025, 0.18),
        silver,
        rotation=(0.0, 0.0, math.radians(45)),
        bevel=0.007,
    )
    sphere("StormCore", "core", (0.0, -0.174, 0.745), (0.058, 0.032, 0.058), storm)
    box("HighCollar", "spine", (0.0, 0.015, 0.845), (0.27, 0.20, 0.10), cloth, bevel=0.025)

    cone("WardenCowl", "head", (0.0, 0.02, 0.985), (0.17, 0.115), 0.31, cloth, vertices=8)
    sphere("MaskShell", "head", (0.0, -0.012, 0.985), (0.125, 0.115, 0.145), obsidian)
    box("WardenMask", "head", (0.0, -0.118, 0.97), (0.19, 0.065, 0.14), silver, bevel=0.022)
    box("StormVisor", "head", (0.0, -0.158, 1.015), (0.16, 0.022, 0.038), storm, bevel=0.009)
    cone(
        "CrownSpire.L",
        "head",
        (-0.065, 0.01, 1.14),
        (0.035, 0.008),
        0.24,
        silver,
        rotation=(0.0, math.radians(-8), math.radians(-8)),
        vertices=6,
    )
    cone(
        "CrownSpire.R",
        "head",
        (0.065, 0.01, 1.14),
        (0.035, 0.008),
        0.24,
        silver,
        rotation=(0.0, math.radians(8), math.radians(8)),
        vertices=6,
    )

    for side, sign in (("L", -1.0), ("R", 1.0)):
        upper = f"upper_arm.{side}"
        forearm = f"forearm.{side}"
        hand = f"hand.{side}"
        cone(
            f"WardenPauldron.{side}",
            upper,
            (0.285 * sign, 0.005, 0.77),
            (0.125, 0.06),
            0.18,
            lavender if side == "L" else violet,
            rotation=(0.0, math.radians(90), 0.0),
            vertices=7,
        )
        cylinder(
            f"UpperArm.{side}", upper, (0.275 * sign, 0.0, 0.665), 0.052, 0.19, cloth, vertices=8
        )
        cylinder(
            f"ArcBracer.{side}",
            forearm,
            (0.295 * sign, 0.0, 0.505),
            0.06,
            0.17,
            silver,
            vertices=8,
        )
        box(
            f"BracerLight.{side}",
            forearm,
            (0.295 * sign, -0.063, 0.505),
            (0.045, 0.018, 0.11),
            arc if side == "L" else storm,
            bevel=0.005,
        )
        sphere(f"WardenHand.{side}", hand, (0.305 * sign, 0.0, 0.405), (0.062, 0.055, 0.062), obsidian)

    cylinder("GlaiveShaft", "hand.R", (0.385, 0.0, 0.48), 0.024, 0.90, silver, vertices=8)
    cylinder("GlaiveGrip", "hand.R", (0.385, 0.0, 0.38), 0.034, 0.22, brass, vertices=8)
    sphere("GlaiveFocus", "hand.R", (0.385, 0.0, 0.91), (0.09, 0.065, 0.09), storm)
    box(
        "GlaiveBlade",
        "hand.R",
        (0.385, 0.0, 1.06),
        (0.06, 0.035, 0.28),
        arc,
        rotation=(0.0, math.radians(-18), 0.0),
        bevel=0.011,
    )
    cone("GlaivePommel", "hand.R", (0.385, 0.0, 0.015), (0.045, 0.01), 0.13, brass, vertices=6)
    sphere("PalmArc", "hand.L", (-0.36, -0.025, 0.40), (0.055, 0.04, 0.055), arc)

    box("MantleTop", "cape", (0.0, 0.14, 0.70), (0.36, 0.045, 0.22), cloth, bevel=0.025)
    cone("MantleLower", "cape", (0.0, 0.145, 0.46), (0.24, 0.13), 0.42, cloth, vertices=7)
    box(
        "MantleStripe",
        "cape",
        (0.0, 0.17, 0.48),
        (0.055, 0.025, 0.31),
        lavender,
        bevel=0.008,
    )

    return join_parts(
        armature,
        "RelayStormcallerMesh",
        "RelayStormcallerGeometry",
        "RelayStormcallerSkin",
    )


def build_animations(armature):
    action(
        armature,
        "Morrow_Idle-loop",
        [
            (1, {"spine": {"rotation": r(2, 0, -2)}, "head": {"rotation": r(0, 0, 4)}, "core": {"scale": (1.0, 1.0, 1.0)}, "cape": {"rotation": r(-3)}}),
            (24, {"spine": {"rotation": r(-2, 0, 2)}, "head": {"rotation": r(0, 0, -5)}, "hand.R": {"rotation": r(0, 0, -4)}, "core": {"scale": (1.3, 1.3, 1.3)}, "cape": {"rotation": r(5)}}),
            (47, {"spine": {"rotation": r(2, 0, -2)}, "head": {"rotation": r(0, 0, 4)}, "core": {"scale": (1.0, 1.0, 1.0)}, "cape": {"rotation": r(-3)}}),
        ],
    )
    action(
        armature,
        "Morrow_Stride-loop",
        [
            (1, {"thigh.L": {"rotation": r(34)}, "thigh.R": {"rotation": r(-34)}, "shin.L": {"rotation": r(-16)}, "upper_arm.L": {"rotation": r(-22)}, "upper_arm.R": {"rotation": r(12)}, "cape": {"rotation": r(-12)}}),
            (6, {"thigh.L": {"rotation": r(3)}, "thigh.R": {"rotation": r(-3)}, "shin.L": {"rotation": r(-38)}, "shin.R": {"rotation": r(-7)}, "spine": {"rotation": r(4)}, "cape": {"rotation": r(-18)}}),
            (11, {"thigh.L": {"rotation": r(-34)}, "thigh.R": {"rotation": r(34)}, "shin.R": {"rotation": r(-16)}, "upper_arm.L": {"rotation": r(22)}, "upper_arm.R": {"rotation": r(-12)}, "cape": {"rotation": r(-12)}}),
            (16, {"thigh.L": {"rotation": r(-3)}, "thigh.R": {"rotation": r(3)}, "shin.R": {"rotation": r(-38)}, "shin.L": {"rotation": r(-7)}, "spine": {"rotation": r(4)}, "cape": {"rotation": r(-18)}}),
            (21, {"thigh.L": {"rotation": r(34)}, "thigh.R": {"rotation": r(-34)}, "shin.L": {"rotation": r(-16)}, "upper_arm.L": {"rotation": r(-22)}, "upper_arm.R": {"rotation": r(12)}, "cape": {"rotation": r(-12)}}),
        ],
    )
    action(
        armature,
        "Morrow_Glaive",
        [
            (1, {"upper_arm.R": {"rotation": r(-28, 0, -24)}, "forearm.R": {"rotation": r(-38)}, "spine": {"rotation": r(0, 0, -8)}}),
            (7, {"upper_arm.R": {"rotation": r(-112, 0, -52)}, "forearm.R": {"rotation": r(-72)}, "upper_arm.L": {"rotation": r(-30, 0, 35)}, "spine": {"rotation": r(4, 0, -22)}}),
            (12, {"upper_arm.R": {"rotation": r(74, 0, 52)}, "forearm.R": {"rotation": r(22)}, "upper_arm.L": {"rotation": r(18, 0, -28)}, "spine": {"rotation": r(5, 0, 24)}, "thigh.L": {"rotation": r(-17)}}),
            (20, {}),
        ],
    )
    action(
        armature,
        "Morrow_Arc",
        [
            (1, {"core": {"scale": (1.0, 1.0, 1.0)}}),
            (10, {"upper_arm.L": {"rotation": r(-45, 0, 62)}, "forearm.L": {"rotation": r(-56)}, "upper_arm.R": {"rotation": r(-72, 0, -44)}, "forearm.R": {"rotation": r(-48)}, "head": {"rotation": r(-10)}, "core": {"scale": (2.35, 2.35, 2.35)}, "cape": {"rotation": r(24)}}),
            (19, {"root": {"rotation": r(-7)}, "spine": {"rotation": r(-11)}, "upper_arm.L": {"rotation": r(-20, 0, 42)}, "core": {"scale": (1.65, 1.65, 1.65)}, "cape": {"rotation": r(-25)}}),
            (28, {}),
        ],
    )
    action(
        armature,
        "Morrow_Stagger",
        [
            (1, {}),
            (6, {"root": {"rotation": r(10, 0, -16)}, "spine": {"rotation": r(18, 0, -12)}, "head": {"rotation": r(-15)}, "upper_arm.L": {"rotation": r(-28, 0, -24)}, "upper_arm.R": {"rotation": r(-20, 0, 28)}, "cape": {"rotation": r(18)}}),
            (14, {}),
        ],
    )
    action(
        armature,
        "Morrow_Dissolve",
        [
            (1, {}),
            (10, {"root": {"rotation": r(20, 0, -20)}, "spine": {"rotation": r(24)}, "upper_arm.L": {"rotation": r(-35, 0, -34)}, "upper_arm.R": {"rotation": r(-28, 0, 38)}, "cape": {"rotation": r(24)}}),
            (27, {"root": {"rotation": r(84, 0, -26)}, "spine": {"rotation": r(30)}, "head": {"rotation": r(-30)}, "upper_arm.L": {"rotation": r(12, 0, -50)}, "upper_arm.R": {"rotation": r(8, 0, 55)}, "core": {"scale": (0.18, 0.18, 0.18)}, "cape": {"rotation": r(40)}}),
        ],
    )
    armature.animation_data.action = bpy.data.actions["Morrow_Idle-loop"]
    reset_pose(armature)


def main():
    target = output_path("export_relay_stormcaller.py")
    clear_scene()
    armature = humanoid_armature("RelayStormcallerRig", "stormcaller")
    mesh = build_mesh(armature)
    build_animations(armature)
    export_glb(target, armature, mesh, "relay_stormcaller_v1", 47)
    print(f"exported relay stormcaller GLB: {target}")


if __name__ == "__main__":
    main()
