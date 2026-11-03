# SPDX-License-Identifier: MIT
"""Build Grand Leon's first imported, animated PC character asset.

Run through Blender rather than CPython. The output is a self-contained GLB
with a rigid-weighted humanoid mesh, armature, materials, and six in-place
animation actions. Gameplay data and Godot paths never enter the asset.
"""

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
    white = material("Relay ceramic", (0.62, 0.72, 0.76), 0.42, 0.18)
    navy = material("Deep navy", (0.035, 0.075, 0.15), 0.62, 0.10)
    blue = material("Command blue", (0.08, 0.24, 0.43), 0.46, 0.22)
    amber = material("Signal amber", (0.93, 0.45, 0.07), 0.28, 0.18, (1.0, 0.20, 0.02))
    cyan = material("Relay cyan", (0.03, 0.56, 0.72), 0.22, 0.30, (0.02, 0.85, 1.0))
    metal = material("Dark metal", (0.17, 0.22, 0.26), 0.26, 0.76)
    cloth = material("Mantle cloth", (0.08, 0.12, 0.23), 0.88)

    for side, sign in (("L", -1.0), ("R", 1.0)):
        shin = f"shin.{side}"
        thigh = f"thigh.{side}"
        box(f"Boot.{side}", shin, (0.105 * sign, -0.035, 0.055), (0.17, 0.25, 0.10), navy, bevel=0.025)
        cylinder(f"Greave.{side}", shin, (0.105 * sign, 0.0, 0.17), 0.068, 0.19, white)
        sphere(f"Knee.{side}", shin, (0.105 * sign, -0.035, 0.275), (0.09, 0.075, 0.08), blue)
        cylinder(f"Thigh.{side}", thigh, (0.105 * sign, 0.0, 0.365), 0.078, 0.18, navy)

    box("Pelvis", "hips", (0.0, 0.0, 0.485), (0.31, 0.21, 0.16), navy, bevel=0.04)
    box("Belt", "hips", (0.0, -0.005, 0.55), (0.35, 0.23, 0.055), metal, bevel=0.015)
    box("BeltSignal", "hips", (0.0, -0.13, 0.55), (0.095, 0.035, 0.07), amber, bevel=0.012)
    cylinder("Abdomen", "spine", (0.0, 0.0, 0.61), 0.14, 0.16, blue, vertices=12)
    box("Chest", "spine", (0.0, 0.0, 0.72), (0.43, 0.23, 0.27), white, bevel=0.055)
    box("Breastplate", "spine", (0.0, -0.135, 0.72), (0.29, 0.055, 0.19), navy, bevel=0.025)
    box("ChestChevron", "spine", (0.0, -0.17, 0.66), (0.20, 0.025, 0.045), blue, rotation=(0.0, 0.0, math.radians(45)), bevel=0.008)
    sphere("RelayCore", "core", (0.0, -0.178, 0.745), (0.061, 0.035, 0.061), cyan)
    box("Collar", "spine", (0.0, 0.0, 0.845), (0.27, 0.22, 0.09), navy, bevel=0.025)

    sphere("Helmet", "head", (0.0, 0.0, 0.975), (0.145, 0.135, 0.16), white)
    box("Faceplate", "head", (0.0, -0.112, 0.96), (0.205, 0.075, 0.13), navy, bevel=0.025)
    box("Visor", "head", (0.0, -0.158, 1.005), (0.19, 0.025, 0.047), amber, bevel=0.010)
    cone("HelmetCrest", "head", (0.0, 0.035, 1.14), (0.08, 0.015), 0.22, blue, vertices=6)
    box("CommsFin.L", "head", (-0.14, 0.025, 1.01), (0.035, 0.09, 0.15), navy, rotation=(0.0, math.radians(-10), 0.0), bevel=0.010)
    box("CommsFin.R", "head", (0.14, 0.025, 1.01), (0.035, 0.09, 0.15), navy, rotation=(0.0, math.radians(10), 0.0), bevel=0.010)

    for side, sign in (("L", -1.0), ("R", 1.0)):
        upper = f"upper_arm.{side}"
        forearm = f"forearm.{side}"
        hand = f"hand.{side}"
        sphere(f"Pauldron.{side}", upper, (0.285 * sign, 0.005, 0.76), (0.13, 0.105, 0.095), blue)
        cylinder(f"UpperArm.{side}", upper, (0.275 * sign, 0.0, 0.665), 0.06, 0.19, white)
        cylinder(f"Forearm.{side}", forearm, (0.295 * sign, 0.0, 0.505), 0.065, 0.17, navy)
        sphere(f"Hand.{side}", hand, (0.305 * sign, 0.0, 0.405), (0.073, 0.065, 0.073), metal)

    cylinder("Shield", "hand.L", (-0.385, -0.035, 0.45), 0.19, 0.045, blue, rotation=(math.pi / 2.0, 0.0, 0.0), vertices=10, smooth=False)
    cylinder("ShieldRim", "hand.L", (-0.385, -0.061, 0.45), 0.15, 0.052, white, rotation=(math.pi / 2.0, 0.0, 0.0), vertices=10, smooth=False)
    sphere("ShieldCore", "hand.L", (-0.385, -0.096, 0.45), (0.055, 0.025, 0.055), cyan)

    cylinder("SwordGrip", "hand.R", (0.355, 0.0, 0.34), 0.025, 0.16, metal, vertices=8)
    box("SwordGuard", "hand.R", (0.355, 0.0, 0.29), (0.17, 0.055, 0.045), amber, bevel=0.012)
    box("SwordBlade", "hand.R", (0.355, 0.0, 0.11), (0.058, 0.035, 0.38), cyan, bevel=0.012)

    box("CapeTop", "cape", (0.0, 0.145, 0.69), (0.34, 0.045, 0.23), cloth, bevel=0.025)
    cone("CapeLower", "cape", (0.0, 0.145, 0.48), (0.22, 0.14), 0.34, cloth, vertices=6)

    return join_parts(
        armature,
        "RelayCommanderMesh",
        "RelayCommanderGeometry",
        "RelayCommanderSkin",
    )


def build_animations(armature):
    action(
        armature,
        "Relay_Idle-loop",
        [
            (1, {"spine": {"rotation": r(1)}, "core": {"scale": (1.0, 1.0, 1.0)}, "cape": {"rotation": r(-2)}}),
            (22, {"spine": {"rotation": r(-1)}, "core": {"scale": (1.18, 1.18, 1.18)}, "cape": {"rotation": r(3)}}),
            (43, {"spine": {"rotation": r(1)}, "core": {"scale": (1.0, 1.0, 1.0)}, "cape": {"rotation": r(-2)}}),
        ],
    )
    action(
        armature,
        "Relay_Run-loop",
        [
            (1, {"thigh.L": {"rotation": r(38)}, "thigh.R": {"rotation": r(-38)}, "shin.L": {"rotation": r(-18)}, "upper_arm.L": {"rotation": r(-28)}, "upper_arm.R": {"rotation": r(28)}, "cape": {"rotation": r(-12)}}),
            (5, {"thigh.L": {"rotation": r(4)}, "thigh.R": {"rotation": r(-4)}, "shin.L": {"rotation": r(-42)}, "shin.R": {"rotation": r(-8)}, "spine": {"rotation": r(5)}, "cape": {"rotation": r(-18)}}),
            (10, {"thigh.L": {"rotation": r(-38)}, "thigh.R": {"rotation": r(38)}, "shin.R": {"rotation": r(-18)}, "upper_arm.L": {"rotation": r(28)}, "upper_arm.R": {"rotation": r(-28)}, "cape": {"rotation": r(-12)}}),
            (14, {"thigh.L": {"rotation": r(-4)}, "thigh.R": {"rotation": r(4)}, "shin.R": {"rotation": r(-42)}, "shin.L": {"rotation": r(-8)}, "spine": {"rotation": r(5)}, "cape": {"rotation": r(-18)}}),
            (19, {"thigh.L": {"rotation": r(38)}, "thigh.R": {"rotation": r(-38)}, "shin.L": {"rotation": r(-18)}, "upper_arm.L": {"rotation": r(-28)}, "upper_arm.R": {"rotation": r(28)}, "cape": {"rotation": r(-12)}}),
        ],
    )
    action(
        armature,
        "Relay_Strike",
        [
            (1, {"upper_arm.R": {"rotation": r(-25, 0, -25)}, "forearm.R": {"rotation": r(-35)}, "spine": {"rotation": r(0, 0, -8)}}),
            (7, {"upper_arm.R": {"rotation": r(-115, 0, -45)}, "forearm.R": {"rotation": r(-80)}, "spine": {"rotation": r(0, 0, -18)}, "thigh.R": {"rotation": r(-16)}}),
            (11, {"upper_arm.R": {"rotation": r(78, 0, 48)}, "forearm.R": {"rotation": r(25)}, "spine": {"rotation": r(0, 0, 22)}, "thigh.L": {"rotation": r(-18)}}),
            (17, {}),
        ],
    )
    action(
        armature,
        "Relay_Channel",
        [
            (1, {"core": {"scale": (1.0, 1.0, 1.0)}}),
            (9, {"upper_arm.L": {"rotation": r(-35, 0, 70)}, "upper_arm.R": {"rotation": r(-35, 0, -70)}, "forearm.L": {"rotation": r(-40)}, "forearm.R": {"rotation": r(-40)}, "core": {"scale": (2.0, 2.0, 2.0)}, "spine": {"rotation": r(-7)}}),
            (17, {"upper_arm.L": {"rotation": r(-20, 0, 48)}, "upper_arm.R": {"rotation": r(-20, 0, -48)}, "core": {"scale": (1.45, 1.45, 1.45)}}),
            (23, {}),
        ],
    )
    action(
        armature,
        "Relay_Hit",
        [
            (1, {}),
            (5, {"root": {"rotation": r(10, 0, -14)}, "spine": {"rotation": r(16, 0, -10)}, "head": {"rotation": r(-12)}, "upper_arm.L": {"rotation": r(-20, 0, -18)}, "upper_arm.R": {"rotation": r(-20, 0, 18)}}),
            (13, {}),
        ],
    )
    action(
        armature,
        "Relay_Fall",
        [
            (1, {}),
            (10, {"root": {"rotation": r(20, 0, -18)}, "spine": {"rotation": r(22)}, "upper_arm.L": {"rotation": r(-25, 0, -25)}, "upper_arm.R": {"rotation": r(-25, 0, 25)}}),
            (26, {"root": {"rotation": r(86, 0, -22)}, "spine": {"rotation": r(28)}, "head": {"rotation": r(-25)}, "upper_arm.L": {"rotation": r(10, 0, -45)}, "upper_arm.R": {"rotation": r(10, 0, 45)}, "core": {"scale": (0.35, 0.35, 0.35)}, "cape": {"rotation": r(30)}}),
        ],
    )
    armature.animation_data.action = bpy.data.actions["Relay_Idle-loop"]
    reset_pose(armature)


def main():
    target = output_path("export_relay_commander.py")
    clear_scene()
    armature = humanoid_armature("RelayCommanderRig", "commander")
    mesh = build_mesh(armature)
    build_animations(armature)
    export_glb(target, armature, mesh, "relay_commander_v1", 43)
    print(f"exported relay commander GLB: {target}")


if __name__ == "__main__":
    main()
