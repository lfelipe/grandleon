# SPDX-License-Identifier: MIT
"""Name the existing entrance cloth for the N64's small vertex animation rig."""
from pathlib import Path

from stone_keep import load_keep, textured_part_mesh


def write_n64_motion_header(repo: Path, output: Path, enabled: bool = True) -> None:
    # Exact authored part signature from castle_door in board_view_3d.gd.
    # Refuse an ambiguous/stale asset instead of animating a guessed part.
    entrance_pennant = {
        "centre": [-2.84, 2.10, 0.18], "colour": [237, 184, 92],
        "grounded": False, "masonry": False, "shape": "box",
        "size": [0.06, 0.26, 0.32], "upper": False, "yaw": 0.0,
    }
    first, matches, rig = 0, 0, []
    for part in load_keep(repo, "n64") if enabled else []:
        points, _, _ = textured_part_mesh(part, 12)
        if part == entrance_pennant:
            matches += 1
            if len(points) != 8:
                raise ValueError("N64 entrance cloth topology changed")
            free_z = max(point[2] for point in points)
            middle_y = part["centre"][1]
            for index, (_, y, z) in enumerate(points):
                if z == free_z:
                    lower = y < middle_y
                    rig.append((first + index, 3 if lower else 2, 0 if lower else 16))
        first += len(points)
    if enabled and (matches != 1 or len(rig) != 4):
        raise ValueError("N64 entrance pennant rig needs its authored part signature updated")
    lines = ["// Generated N64 motion rig for existing stone-keep cloth. Do not edit.",
             "#pragma once", "#include <array>", "#include <cstdint>",
             "namespace grandleon::nintendo64::pyrite::castle_motion {",
             "struct ClothVertex { std::uint16_t index; std::uint8_t amplitude, phase; };",
             f"inline constexpr std::array<ClothVertex, {len(rig)}> entrance_pennant = {{{{"]
    lines += ["    {" + ",".join(map(str, vertex)) + "}," for vertex in rig]
    lines += ["}};", "}", ""]
    output.write_text("\n".join(lines))
