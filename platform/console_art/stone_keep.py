"""Portable geometry derived from the real Godot stone keep.

Console exports consume checked-in geometry, so normal game exports do not
require a Godot development executable. Regenerate after editing the source
castle with platform/godot/tests/export_stone_keep.gd.
"""

import hashlib
import json
import math
from pathlib import Path


def load_keep(repo: Path, target: str) -> list[dict]:
    root = repo / "platform/godot/project/scripts"
    data = json.loads((repo / "platform/console_art/stone_keep.json").read_text())
    source = (root / "board_view_3d.gd").read_text()
    marker = "func _build_medieval_castle_cell("
    cells = marker + source.split(marker, 1)[1].split("\n\nfunc ", 1)[0]
    hashes = {"stone_keep_sha256": hashlib.sha256((root / "stone_keep.gd").read_text().encode()).hexdigest(),
              "castle_cells_sha256": hashlib.sha256(cells.encode()).hexdigest()}
    if data.get("schema") != "grandleon.stone_keep.v1" or data.get("source") != hashes:
        raise ValueError("portable castle is stale: regenerate with export_stone_keep.gd before exporting consoles")
    parts = []
    for original in data["parts"]:
        part = dict(original)
        if target == "ps1":
            x, y, z = part["centre"]
            sx, sy, sz = part["size"]
            # Preserve the four towers, terrace and gate. At board scale the
            # second tooth per wall cell and tiny raised ironwork add clutter.
            if part["shape"] == "cylinder" and sy < 0.2:
                continue
            if sx < 0.06 and sz < 0.06:
                continue
            if not part["masonry"] and sy < 0.05:
                continue
            if sx > 4 and sz < 0.2 or sz > 4 and sx < 0.2:
                continue
            if sx < 0.02 and part["masonry"]:
                continue
            if part["upper"] and abs(y - 1.68) < 0.01:
                along = z if sx > sz else x
                if (along + 0.5) % 1.0 < 0.5:
                    continue
            if part["upper"] and abs(y - 2.34) < 0.01:
                dx = abs(x - round(x * 2) / 2)
                dz = abs(z - round(z * 2) / 2)
                if dx > 0.05 and dz > 0.05:
                    continue
                part["centre"] = [x, y - 0.06, z]
        parts.append(part)
    return parts


def part_mesh(part: dict, segments: int = 8) -> tuple[list[tuple], list[tuple]]:
    """Godot-clockwise triangles; negate Z for the PS1 coordinate system."""
    sx, sy, sz = (value * 0.5 for value in part["size"])
    if part["shape"] == "box":
        vertices = [(-sx, -sy, -sz), (sx, -sy, -sz), (sx, -sy, sz), (-sx, -sy, sz),
                    (-sx, sy, -sz), (sx, sy, -sz), (sx, sy, sz), (-sx, sy, sz)]
        faces = [(0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
                 (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
                 (4, 5, 6), (4, 6, 7), (0, 3, 2), (0, 2, 1)]
    else:
        vertices = [(math.cos(i * math.tau / segments) * sx, y,
                     math.sin(i * math.tau / segments) * sz)
                    for y in (-sy, sy) for i in range(segments)]
        faces = []
        for i in range(segments):
            j = (i + 1) % segments
            faces.extend(((i, j, j + segments), (i, j + segments, i + segments)))
        for i in range(1, segments - 1):
            faces.extend(((segments, segments + i, segments + i + 1), (0, i + 1, i)))
    angle = part["yaw"]
    cosine, sine = math.cos(angle), math.sin(angle)
    cx, cy, cz = part["centre"]
    return [(cx + x * cosine + z * sine, cy + y, cz - x * sine + z * cosine)
            for x, y, z in vertices], faces


def face_normal(vertices: list[tuple], face: tuple) -> tuple[float, float, float]:
    a, b, c = (vertices[i] for i in face)
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    normal = (u[2] * v[1] - u[1] * v[2], u[0] * v[2] - u[2] * v[0], u[1] * v[0] - u[0] * v[1])
    length = math.sqrt(sum(value * value for value in normal))
    return tuple(value / length for value in normal)


def textured_part_mesh(part: dict, segments: int = 8) -> tuple[list, list, list]:
    points, faces = part_mesh(part, segments)
    if part["shape"] == "box":
        horizontal = part["size"][1] < 0.3 and min(part["size"][0], part["size"][2]) > 1
        uvs = [((x if horizontal else x + z) * 10 + 64,
                z * 10 + 64 if horizontal else y * 32) for x, y, z in points]
        return points, faces, uvs
    # A separate cap UV and a repeated seam vertex keep masonry courses on the
    # tower sides and real two-dimensional flagstones on top, without streaks.
    ring = segments + 1
    result = []
    uvs = []
    for level in range(2):
        for i in range(ring):
            point = points[level * segments + i % segments]
            result.append(point)
            uvs.append((64 + i * 32 / segments, point[1] * 32))
    cap = len(result)
    for point in points[segments:]:
        result.append(point)
        uvs.append((point[0] * 10 + 64, point[2] * 10 + 64))
    faces = []
    for i in range(segments):
        faces.extend(((i, i + 1, ring + i + 1), (i, ring + i + 1, ring + i)))
    for i in range(1, segments - 1):
        faces.extend(((cap, cap + i, cap + i + 1), (0, i + 1, i)))
    return result, faces, uvs


def lit_colour(colour: list[int], normal: tuple) -> tuple[int, int, int]:
    sunlight = max(0.0, sum(a * b for a, b in zip(normal, (-0.48, 0.80, -0.36))))
    brightness = 0.58 + sunlight * 0.42
    return tuple(round(value * brightness) for value in colour)


def write_n64_header(repo: Path, output: Path, enabled: bool = True) -> None:
    vertices, indices, descriptors = [], [], []
    for part in load_keep(repo, "n64") if enabled else []:
        points, faces, uvs = textured_part_mesh(part, 12)
        normals = [[0.0, 0.0, 0.0] for _ in points]
        for face in faces:
            normal = face_normal(points, face)
            for index in face:
                for axis in range(3):
                    normals[index][axis] += normal[axis]
        first = len(vertices)
        for point, normal, uv in zip(points, normals, uvs):
            length = math.sqrt(sum(value * value for value in normal))
            if length == 0:
                normal, length = (0.0, 1.0, 0.0), 1.0
            colour = lit_colour(part["colour"], tuple(value / length for value in normal))
            rgba = (colour[0] << 24) | (colour[1] << 16) | (colour[2] << 8) | 255
            vertices.append((*[round(value * 52) for value in point],
                             round(uv[0] * 32), round(uv[1] * 32), rgba))
        descriptors.append((first, len(points), len(indices), len(faces) * 3,
                            part["upper"], part["masonry"], part["grounded"]))
        indices.extend(index for face in faces for index in face)
    lines = ["// Generated from the real Godot stone keep. Do not edit.",
             "#pragma once", "#include <array>", "#include <cstdint>",
             "namespace grandleon::nintendo64::pyrite::castle_art {",
             "struct Vertex { std::int16_t x, y, z, u, v; std::uint32_t colour; };",
             "struct Part { std::uint16_t first; std::uint8_t count; std::uint16_t index_first, index_count; bool upper, masonry, grounded; };",
             f"inline constexpr std::array<Vertex, {len(vertices)}> vertices = {{{{"]
    lines += ["    {" + ",".join(str(value) for value in vertex[:-1]) + f",0x{vertex[-1]:08x}U" + "}," for vertex in vertices]
    lines += ["}};", f"inline constexpr std::array<std::uint8_t, {len(indices)}> indices = {{{{"]
    lines += ["    " + ",".join(str(value) for value in indices[start:start + 48]) + "," for start in range(0, len(indices), 48)]
    lines += ["}};", f"inline constexpr std::array<Part, {len(descriptors)}> parts = {{{{"]
    lines += ["    {" + ",".join(str(value).lower() if isinstance(value, bool) else str(value) for value in part) + "}," for part in descriptors]
    lines += ["}};", "}", ""]
    output.write_text("\n".join(lines))
