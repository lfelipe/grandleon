#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the complete procedural low-poly Sevenfold N64 roster.

The curated roster is raster art, so there is no hidden set of 112 source
meshes to reduce. This generator builds an explicit console model vocabulary:
shared body construction, eight role silhouettes, seven style treatments and
two visibly different body proportions. The output is deterministic C++ data
for Tiny3D plus a machine-readable size report.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path


STYLES = ("medieval", "scifi", "mythical", "nature", "sengoku", "undead", "pirates")
FIGURES = ("first", "second")
ARCHETYPES = ("knight", "archer", "mage", "stormcaller", "healer", "commander", "rogue", "beast")
PALETTES = {
    "medieval": ((255, 255, 255), (45, 48, 55), (112, 126, 139), (205, 156, 67), (178, 187, 193), (205, 166, 130)),
    "scifi": ((255, 255, 255), (17, 29, 49), (58, 116, 153), (52, 222, 225), (135, 157, 171), (211, 172, 139)),
    "mythical": ((255, 255, 255), (42, 30, 74), (90, 79, 157), (236, 184, 71), (185, 176, 221), (220, 176, 140)),
    "nature": ((255, 255, 255), (48, 55, 33), (74, 122, 64), (190, 145, 57), (123, 105, 70), (196, 158, 119)),
    "sengoku": ((255, 255, 255), (44, 27, 27), (139, 48, 47), (225, 172, 61), (93, 99, 105), (211, 170, 132)),
    "undead": ((255, 255, 255), (31, 28, 39), (92, 78, 111), (128, 223, 175), (157, 149, 142), (209, 205, 171)),
    "pirates": ((255, 255, 255), (46, 34, 31), (122, 58, 46), (230, 176, 62), (119, 125, 131), (205, 158, 119)),
}


class Mesh:
    def __init__(self) -> None:
        self.vertices: list[tuple[int, int, int]] = []
        self.triangles: list[tuple[int, int, int, int, int]] = []

    def vertex(self, point: tuple[float, float, float]) -> int:
        self.vertices.append(tuple(round(value) for value in point))
        return len(self.vertices) - 1

    def triangle(self, a: int, b: int, c: int, material: int) -> None:
        pa, pb, pc = self.vertices[a], self.vertices[b], self.vertices[c]
        u = tuple(pb[i] - pa[i] for i in range(3))
        v = tuple(pc[i] - pa[i] for i in range(3))
        normal = (
            u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0],
        )
        length = math.sqrt(sum(value * value for value in normal)) or 1.0
        light = (-0.34, 0.75, -0.57)
        facing = sum(normal[i] / length * light[i] for i in range(3))
        shade = max(76, min(255, round((0.45 + 0.55 * max(0.0, facing)) * 255)))
        self.triangles.append((a, b, c, material, shade))

    def quad(self, a: int, b: int, c: int, d: int, material: int) -> None:
        self.triangle(a, b, c, material)
        self.triangle(a, c, d, material)

    def box(self, center, size, material: int) -> None:
        cx, cy, cz = center
        sx, sy, sz = (value / 2.0 for value in size)
        points = [
            (cx - sx, cy - sy, cz - sz), (cx + sx, cy - sy, cz - sz),
            (cx + sx, cy + sy, cz - sz), (cx - sx, cy + sy, cz - sz),
            (cx - sx, cy - sy, cz + sz), (cx + sx, cy - sy, cz + sz),
            (cx + sx, cy + sy, cz + sz), (cx - sx, cy + sy, cz + sz),
        ]
        p = [self.vertex(point) for point in points]
        for face in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                     (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)):
            self.quad(*(p[index] for index in face), material)

    def rod(self, start, end, radius: float, material: int, segments: int) -> None:
        start = tuple(float(v) for v in start)
        end = tuple(float(v) for v in end)
        axis = tuple(end[i] - start[i] for i in range(3))
        length = math.sqrt(sum(v * v for v in axis))
        if length == 0:
            return
        axis = tuple(v / length for v in axis)
        seed = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.85 else (1.0, 0.0, 0.0)
        side = (
            axis[1] * seed[2] - axis[2] * seed[1],
            axis[2] * seed[0] - axis[0] * seed[2],
            axis[0] * seed[1] - axis[1] * seed[0],
        )
        side_len = math.sqrt(sum(v * v for v in side))
        side = tuple(v / side_len for v in side)
        up = (
            axis[1] * side[2] - axis[2] * side[1],
            axis[2] * side[0] - axis[0] * side[2],
            axis[0] * side[1] - axis[1] * side[0],
        )
        rings = []
        for center in (start, end):
            ring = []
            for index in range(segments):
                angle = 2.0 * math.pi * index / segments
                ring.append(self.vertex(tuple(
                    center[i] + radius * (math.cos(angle) * side[i] + math.sin(angle) * up[i])
                    for i in range(3)
                )))
            rings.append(ring)
        for index in range(segments):
            following = (index + 1) % segments
            self.quad(rings[0][index], rings[0][following], rings[1][following], rings[1][index], material)
        start_center = self.vertex(start)
        end_center = self.vertex(end)
        for index in range(segments):
            following = (index + 1) % segments
            self.triangle(start_center, rings[0][following], rings[0][index], material)
            self.triangle(end_center, rings[1][index], rings[1][following], material)

    def frustum(self, center, height: float, lower: float, upper: float, material: int, segments: int) -> None:
        cx, cy, cz = center
        y0, y1 = cy - height / 2.0, cy + height / 2.0
        lower_ring = [self.vertex((cx + math.cos(2 * math.pi * i / segments) * lower, y0, cz + math.sin(2 * math.pi * i / segments) * lower)) for i in range(segments)]
        upper_ring = [self.vertex((cx + math.cos(2 * math.pi * i / segments) * upper, y1, cz + math.sin(2 * math.pi * i / segments) * upper)) for i in range(segments)]
        for index in range(segments):
            following = (index + 1) % segments
            self.quad(lower_ring[index], lower_ring[following], upper_ring[following], upper_ring[index], material)
        lower_center = self.vertex((cx, y0, cz))
        upper_center = self.vertex((cx, y1, cz))
        for index in range(segments):
            following = (index + 1) % segments
            self.triangle(lower_center, lower_ring[following], lower_ring[index], material)
            self.triangle(upper_center, upper_ring[index], upper_ring[following], material)

    def ellipsoid(self, center, radii, material: int, segments: int) -> None:
        cx, cy, cz = center
        rx, ry, rz = radii
        rings = max(2, segments // 2)
        top = self.vertex((cx, cy + ry, cz))
        bottom = self.vertex((cx, cy - ry, cz))
        points = []
        for ring in range(1, rings + 1):
            phi = math.pi * ring / (rings + 1)
            points.append([
                self.vertex((cx + rx * math.sin(phi) * math.cos(2 * math.pi * i / segments),
                             cy + ry * math.cos(phi),
                             cz + rz * math.sin(phi) * math.sin(2 * math.pi * i / segments)))
                for i in range(segments)
            ])
        for index in range(segments):
            following = (index + 1) % segments
            self.triangle(top, points[0][index], points[0][following], material)
            self.triangle(bottom, points[-1][following], points[-1][index], material)
        for ring in range(len(points) - 1):
            for index in range(segments):
                following = (index + 1) % segments
                self.quad(points[ring][index], points[ring + 1][index], points[ring + 1][following], points[ring][following], material)


def humanoid(mesh: Mesh, style: str, archetype: str, figure: str, high: bool) -> None:
    segments = 8 if high else 5
    second = figure == "second"
    shoulder = 21 if second else 25
    hip = 10 if second else 12
    height_scale = 0.96 if second else 1.0
    hand_y = 47 * height_scale
    shoulder_y = 78 * height_scale
    for sign in (-1, 1):
        mesh.rod((sign * hip, 5, 0), (sign * hip, 35 * height_scale, 0), 5.3 if second else 6.0, 1, segments)
        mesh.box((sign * hip, 4, -3), (14, 8, 20), 1)
        mesh.rod((sign * shoulder, shoulder_y, 0), (sign * (shoulder + 4), hand_y, -1), 5.0, 2, segments)
        mesh.ellipsoid((sign * (shoulder + 4), hand_y - 4, -1), (6, 6, 6), 5, segments)
    mesh.frustum((0, 56 * height_scale, 0), 40 * height_scale, 18 if second else 20, shoulder, 2, segments)
    mesh.box((0, 63 * height_scale, -18), (22 if second else 27, 9, 5), 0)
    mesh.ellipsoid((0, 99 * height_scale, 0), (13 if second else 14, 15, 13), 5, segments)
    if second:
        # The second body needs to read as a different design at 320x240, not
        # merely as shoulders four units narrower. A bright low sash, split
        # coat tails and an asymmetric head cord survive the board camera while
        # remaining shared vocabulary across all seven styles.
        mesh.box((0, 42, -17), (34, 7, 5), 3)
        mesh.box((-9, 28, -13), (8, 24, 6), 2)
        mesh.box((9, 28, -13), (8, 24, 6), 2)
        mesh.rod((10, 106, -8), (23, 84, -11), 2.8, 1, segments)

    # Role silhouette carriers remain deliberately oversized at board scale.
    if archetype == "knight":
        mesh.box((-36, 50, -3), (7, 38, 30), 2)
        mesh.rod((34, 48, 0), (39, 8, -2), 3.2, 4, segments)
        mesh.box((39, 20, -2), (8, 34, 5), 4)
    elif archetype == "archer":
        for a, b in (((34, 72, 0), (43, 51, -5)), ((43, 51, -5), (35, 29, 0))):
            mesh.rod(a, b, 2.2, 3, segments)
        mesh.rod((34, 72, 0), (35, 29, 0), 1.0, 4, segments)
        mesh.box((-24, 64, 13), (10, 34, 9), 1)
    elif archetype == "mage":
        mesh.rod((34, 46, 0), (38, 112, 0), 2.8, 4, segments)
        mesh.ellipsoid((38, 116, 0), (8, 8, 8), 3, segments)
        mesh.frustum((0, 116, 0), 26, 24, 2, 2, segments)
    elif archetype == "stormcaller":
        mesh.rod((-33, 44, 0), (-42, 108, -4), 2.5, 4, segments)
        mesh.rod((33, 44, 0), (42, 108, -4), 2.5, 4, segments)
        mesh.rod((-42, 108, -4), (-33, 120, -4), 2.0, 3, segments)
        mesh.rod((42, 108, -4), (33, 120, -4), 2.0, 3, segments)
    elif archetype == "healer":
        mesh.rod((34, 42, 0), (38, 112, 0), 3.0, 4, segments)
        mesh.rod((38, 112, 0), (28, 123, 0), 2.7, 3, segments)
        mesh.rod((28, 123, 0), (20, 114, 0), 2.7, 3, segments)
        mesh.box((-24, 48, -12), (16, 22, 7), 3)
    elif archetype == "commander":
        mesh.box((-37, 53, -2), (8, 42, 34), 2)
        mesh.rod((35, 45, 0), (39, 12, -2), 3.0, 4, segments)
        mesh.rod((-25, 73, 10), (-25, 128, 10), 2.5, 4, segments)
        mesh.box((-13, 113, 10), (22, 27, 3), 0)
    elif archetype == "rogue":
        mesh.rod((-34, 44, 0), (-43, 15, -7), 2.4, 4, segments)
        mesh.rod((34, 44, 0), (43, 15, 7), 2.4, 4, segments)
        mesh.box((-43, 23, -7), (5, 25, 4), 3)
        mesh.box((43, 23, 7), (5, 25, 4), 3)
        mesh.frustum((0, 103, 3), 30, 18, 5, 1, segments)

    # Style treatments are shared across roles but change the head and outline.
    if style == "medieval":
        mesh.frustum((0, 108, 0), 24, 17, 11, 4, segments)
        mesh.box((0, 101, -13), (20, 5, 5), 3)
    elif style == "scifi":
        mesh.box((0, 101, -13), (24, 7, 5), 3)
        mesh.box((0, 67, 15), (22, 29, 8), 1)
        mesh.box((-shoulder, shoulder_y, 0), (15, 10, 20), 2)
        mesh.box((shoulder, shoulder_y, 0), (15, 10, 20), 2)
    elif style == "mythical":
        mesh.rod((-8, 111, 0), (-17, 127, 0), 2.3, 3, segments)
        mesh.rod((8, 111, 0), (17, 127, 0), 2.3, 3, segments)
        mesh.ellipsoid((0, 71, -20), (6, 6, 4), 3, segments)
    elif style == "nature":
        for sign in (-1, 1):
            mesh.rod((sign * 7, 111, 0), (sign * 17, 127, 0), 2.0, 4, segments)
            mesh.rod((sign * 14, 122, 0), (sign * 24, 124, 0), 1.7, 3, segments)
        mesh.box((0, 58, 15), (28, 35, 5), 1)
    elif style == "sengoku":
        mesh.rod((-9, 110, 0), (-22, 126, 0), 2.5, 3, segments)
        mesh.rod((9, 110, 0), (22, 126, 0), 2.5, 3, segments)
        mesh.box((-shoulder - 3, shoulder_y, 0), (18, 11, 25), 2)
        mesh.box((shoulder + 3, shoulder_y, 0), (18, 11, 25), 2)
    elif style == "undead":
        mesh.ellipsoid((0, 100, 0), (12, 14, 12), 5, max(4, segments - 1))
        for offset in (-12, -6, 0, 6, 12):
            mesh.box((offset, 63, -18), (3, 25 - abs(offset), 4), 5)
        mesh.rod((-shoulder, shoulder_y, 0), (-shoulder - 8, shoulder_y + 14, 0), 2.2, 5, segments)
        mesh.rod((shoulder, shoulder_y, 0), (shoulder + 8, shoulder_y + 14, 0), 2.2, 5, segments)
    elif style == "pirates":
        mesh.box((0, 112, 0), (34, 5, 23), 1)
        mesh.frustum((0, 118, 0), 10, 19, 11, 2, segments)
        mesh.box((-10, 43, 13), (13, 35, 5), 1)
        mesh.box((10, 43, 13), (13, 35, 5), 1)


def beast(mesh: Mesh, style: str, figure: str, high: bool) -> None:
    segments = 8 if high else 5
    second = figure == "second"
    body_y = 51 if second else 55
    mesh.ellipsoid((0, body_y, 0), (32 if second else 37, 22, 18), 2, segments)
    mesh.ellipsoid((0, body_y, -20), (19, 18, 17), 5, segments)
    for x in (-23, 23):
        for z in (-11, 11):
            mesh.rod((x, body_y - 9, z), (x * 1.08, 5, z), 5.0, 1, segments)
    mesh.rod((0, body_y, 17), (0, body_y + 12, 47), 3.0, 1, segments)
    mesh.box((0, body_y + 3, -18), (23, 7, 5), 0)
    horn_material = 3 if style != "undead" else 5
    if second:
        # A lower, swept horn line distinguishes the second quadruped body
        # from the first body's tall horns at the same distance as the roster.
        mesh.rod((-8, body_y + 12, -25), (-28, body_y + 18, -29), 2.7, horn_material, segments)
        mesh.rod((8, body_y + 12, -25), (28, body_y + 18, -29), 2.7, horn_material, segments)
        mesh.rod((-23, body_y + 17, -28), (-29, body_y + 27, -29), 2.0, horn_material, segments)
        mesh.rod((23, body_y + 17, -28), (29, body_y + 27, -29), 2.0, horn_material, segments)
    else:
        mesh.rod((-9, body_y + 13, -25), (-20, body_y + 29, -28), 2.5, horn_material, segments)
        mesh.rod((9, body_y + 13, -25), (20, body_y + 29, -28), 2.5, horn_material, segments)
    if style == "scifi":
        mesh.box((0, body_y + 20, 0), (34, 8, 25), 3)
    elif style == "nature":
        for sign in (-1, 1):
            mesh.rod((sign * 12, body_y + 15, -22), (sign * 25, body_y + 35, -24), 2.0, 4, segments)
    elif style == "sengoku":
        mesh.box((0, body_y + 18, 0), (47, 8, 29), 2)
    elif style == "pirates":
        mesh.box((0, body_y + 17, -21), (38, 5, 21), 1)
    elif style == "mythical":
        mesh.ellipsoid((0, body_y + 13, 0), (8, 8, 8), 3, segments)
    elif style == "medieval":
        mesh.box((0, body_y + 18, 0), (38, 7, 26), 4)
    elif style == "undead":
        for offset in (-12, 0, 12):
            mesh.rod((offset, body_y + 12, 0), (offset, body_y + 31, 2), 2.0, 5, segments)


def build(style: str, archetype: str, figure: str, high: bool) -> Mesh:
    mesh = Mesh()
    if archetype == "beast":
        beast(mesh, style, figure, high)
    else:
        humanoid(mesh, style, archetype, figure, high)
    if not mesh.vertices or not mesh.triangles:
        raise RuntimeError(f"empty model: {style}/{figure}/{archetype}")
    return mesh


def emit_model(lines: list[str], name: str, mesh: Mesh) -> None:
    lines.extend((
        f"namespace {name} {{",
        f"inline constexpr int vertex_count = {len(mesh.vertices)};",
        f"inline constexpr int triangle_count = {len(mesh.triangles)};",
        "inline constexpr SevenfoldProxyVertex vertices[] = {",
    ))
    lines.extend(f"    {{{x}, {y}, {z}}}," for x, y, z in mesh.vertices)
    lines.append("};")
    lines.append("inline constexpr SevenfoldProxyTriangle triangles[] = {")
    lines.extend(f"    {{{a}, {b}, {c}, {material}, {shade}}}," for a, b, c, material, shade in mesh.triangles)
    lines.extend(("};", "}"))


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    output = root / "tools/console_3d/generated/sevenfold_roster_proxies.h"
    report_path = root / "tools/console_3d/generated/sevenfold_roster_proxies.json"
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by tools/console_3d/generate_sevenfold_roster.py. Do not edit.",
        "#pragma once",
        "#include <cstdint>",
        "struct SevenfoldProxyVertex { std::int16_t x, y, z; };",
        "struct SevenfoldProxyTriangle { std::uint16_t a, b, c; std::uint8_t material, shade; };",
        "struct SevenfoldProxy { const SevenfoldProxyVertex* vertices; const SevenfoldProxyTriangle* triangles; std::uint16_t vertices_count, triangles_count; std::uint8_t style, figure, archetype; };",
    ]
    records = {}
    meshes = {}
    for style_index, style in enumerate(STYLES):
        for figure_index, figure in enumerate(FIGURES):
            for archetype_index, archetype in enumerate(ARCHETYPES):
                key = f"{style}/{figure}/{archetype}"
                records[key] = {}
                for lod in ("low", "high"):
                    mesh = build(style, archetype, figure, lod == "high")
                    name = f"sevenfold_{style}_{figure}_{archetype}_{lod}"
                    emit_model(lines, name, mesh)
                    meshes[(style_index, figure_index, archetype_index, lod)] = (name, mesh)
                    records[key][lod] = {"vertices": len(mesh.vertices), "triangles": len(mesh.triangles)}

    lines.append("inline constexpr const char* sevenfold_style_names[] = {" + ",".join(f'\"{value}\"' for value in STYLES) + "};")
    lines.append("inline constexpr const char* sevenfold_figure_names[] = {\"male\",\"female\"};")
    lines.append("inline constexpr const char* sevenfold_archetype_names[] = {" + ",".join(f'\"{value}\"' for value in ARCHETYPES) + "};")
    low_max = max(
        len(mesh.triangles)
        for (_style, _figure, _archetype, lod), (_name, mesh) in meshes.items()
        if lod == "low"
    )
    high_max = max(
        len(mesh.triangles)
        for (_style, _figure, _archetype, lod), (_name, mesh) in meshes.items()
        if lod == "high"
    )
    lines.append(f"inline constexpr int sevenfold_low_max_triangles = {low_max};")
    lines.append(f"inline constexpr int sevenfold_high_max_triangles = {high_max};")
    lines.append("inline constexpr std::uint8_t sevenfold_style_materials[7][6][3] = {")
    for style in STYLES:
        lines.append("    {" + ",".join("{%d,%d,%d}" % colour for colour in PALETTES[style]) + "},")
    lines.append("};")
    for lod in ("low", "high"):
        lines.append(f"inline constexpr SevenfoldProxy sevenfold_proxies_{lod}[7][2][8] = {{")
        for style_index, _style in enumerate(STYLES):
            lines.append("    {")
            for figure_index, _figure in enumerate(FIGURES):
                lines.append("        {")
                for archetype_index, _archetype in enumerate(ARCHETYPES):
                    name, mesh = meshes[(style_index, figure_index, archetype_index, lod)]
                    lines.append(f"            {{{name}::vertices,{name}::triangles,{len(mesh.vertices)},{len(mesh.triangles)},{style_index},{figure_index},{archetype_index}}},")
                lines.append("        },")
            lines.append("    },")
        lines.append("};")

    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    source_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    report = {
        "generator": "tools/console_3d/generate_sevenfold_roster.py",
        "generator_sha256": source_hash,
        "generated_header_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "styles": list(STYLES),
        "figures": ["male", "female"],
        "archetypes": list(ARCHETYPES),
        "models": records,
        "totals": {
            "combinations": len(records),
            "low_triangles": sum(value["low"]["triangles"] for value in records.values()),
            "high_triangles": sum(value["high"]["triangles"] for value in records.values()),
        },
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output}")
    print(f"wrote {report_path}")
    print(json.dumps(report["totals"], sort_keys=True))


if __name__ == "__main__":
    main()
