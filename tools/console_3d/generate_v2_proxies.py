# SPDX-License-Identifier: MIT
"""Build topology-aware console LODs from the actual Godot Blender sources.

V1 replaced the source primitives and consequently preserved the boxiness it
was meant to test.  V2 starts from the exported mesh instead.  Each disconnected
source component is reduced separately, with a per-component floor, so a thin
weapon, hood tail or shoulder plate cannot disappear into a global decimator.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import math
import pathlib
import sys

import bpy
from mathutils import Vector


TARGET_HEIGHT = 128.0
CAMERA_PITCH_DEGREES = 60.0
LODS = (("low", 360), ("high", 540))


def arguments() -> tuple[list[tuple[str, pathlib.Path]], pathlib.Path, pathlib.Path]:
    try:
        separator = sys.argv.index("--")
        values = sys.argv[separator + 1 :]
    except ValueError:
        values = []
    if len(values) != 4:
        raise SystemExit(
            "usage: blender --background --python generate_v2_proxies.py -- "
            "EXPORT_COMMANDER.py EXPORT_ROGUE.py OUTPUT.h PREVIEW_DIR"
        )
    models = [
        ("commander", pathlib.Path(values[0]).resolve()),
        ("rogue", pathlib.Path(values[1]).resolve()),
    ]
    return models, pathlib.Path(values[2]).resolve(), pathlib.Path(values[3]).resolve()


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in (
        bpy.data.meshes,
        bpy.data.armatures,
        bpy.data.materials,
        bpy.data.cameras,
        bpy.data.lights,
    ):
        for value in list(collection):
            collection.remove(value)


def select_only(value) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    value.select_set(True)
    bpy.context.view_layer.objects.active = value


def triangle_count(value) -> int:
    value.data.calc_loop_triangles()
    return len(value.data.loop_triangles)


def source_parts(path: pathlib.Path, model: str) -> list:
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(f"grandleon_v2_{model}", path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"could not load character source: {path}")
    source = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source)
    source.clear_scene()
    helper = sys.modules["blender_character_tools"]

    # The production builder joins the primitive objects only at its final
    # line.  Capture them immediately before that join.  These are the actual
    # source meshes, including their bevels, rotations, tapers and materials;
    # keeping the objects separate is what lets the LOD protect a knife or hood
    # tail independently from a torso.
    captured = []

    def capture_join(armature, object_name, data_name, modifier_name):
        del armature, object_name, data_name, modifier_name
        captured.extend(helper.PARTS)
        return captured

    source.join_parts = capture_join
    armature = source.humanoid_armature(f"{model}_v2_rig", model)
    source.build_mesh(armature)
    if not captured:
        raise SystemExit(f"{path}: source builder produced no mesh parts")
    bpy.data.objects.remove(armature, do_unlink=True)
    return sorted(captured, key=lambda value: value.name)


def component_floor(value, triangles: int) -> int:
    corners = [value.matrix_world @ Vector(corner) for corner in value.bound_box]
    extents = [
        max(corner[axis] for corner in corners) - min(corner[axis] for corner in corners)
        for axis in range(3)
    ]
    largest = max(extents)
    smallest = max(0.0001, min(extents))
    aspect = largest / smallest
    # Six triangles can still be a wedge.  Long thin components and large
    # silhouette carriers get more so blades, tails, shields and shoulder/head
    # outlines do not collapse into tetrahedra.
    floor = 4
    if aspect >= 3.0:
        floor = 6
    if largest >= 0.28:
        floor = max(floor, 8)
    if triangles >= 100:
        floor = max(floor, 10)
    return min(triangles, floor)


def allocations(parts: list, target: int) -> list[int]:
    original = [triangle_count(part) for part in parts]
    floors = [component_floor(part, count) for part, count in zip(parts, original)]
    if sum(floors) > target:
        raise SystemExit(
            f"{len(parts)} topology islands need at least {sum(floors)} triangles, "
            f"above requested {target}"
        )
    result = floors[:]
    remaining = target - sum(result)
    capacities = [count - floor for count, floor in zip(original, floors)]
    while remaining > 0 and any(capacity > 0 for capacity in capacities):
        weight = sum(math.sqrt(capacity) for capacity in capacities if capacity > 0)
        progressed = False
        for index, capacity in enumerate(capacities):
            if capacity <= 0 or remaining <= 0:
                continue
            share = max(1, round(remaining * math.sqrt(capacity) / weight))
            addition = min(capacity, share, remaining)
            result[index] += addition
            capacities[index] -= addition
            remaining -= addition
            progressed = True
        if not progressed:
            break
    return result


def reduce_part(value, requested: int) -> None:
    before = triangle_count(value)
    if requested >= before:
        return
    select_only(value)
    modifier = value.modifiers.new("Topology-aware console LOD", "DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.ratio = max(0.01, requested / before)
    modifier.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def join_parts(parts: list, name: str):
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    value = bpy.context.object
    value.name = name
    value.data.name = f"{name}Geometry"
    return value


def build_lod(path: pathlib.Path, model: str, lod: str, target: int):
    clear_scene()
    parts = source_parts(path, model)
    requested = allocations(parts, target)
    before = sum(triangle_count(part) for part in parts)
    for part, count in zip(parts, requested):
        reduce_part(part, count)
    proxy = join_parts(parts, f"{model}_{lod}_proxy")
    after = triangle_count(proxy)
    return proxy, {
        "source_triangles": before,
        "triangles": after,
        "components": len(parts),
        "requested_triangles": target,
        "component_minimum": min(requested),
        "component_maximum": max(requested),
    }


def mapped(point: Vector, scale: float, floor: float) -> tuple[int, int, int]:
    return (
        round(point.x * scale),
        round((point.z - floor) * scale),
        round(point.y * scale),
    )


def shade_for(a: tuple[int, int, int], b: tuple[int, int, int], c: tuple[int, int, int]) -> int:
    edge_a = Vector((b[0] - a[0], b[1] - a[1], b[2] - a[2]))
    edge_b = Vector((c[0] - a[0], c[1] - a[1], c[2] - a[2]))
    normal = edge_a.cross(edge_b)
    if normal.length_squared == 0.0:
        return 128
    normal.normalize()
    light = Vector((-0.35, 0.72, -0.60)).normalized()
    return max(72, min(255, round((0.38 + 0.62 * max(0.0, normal.dot(light))) * 255.0)))


def colour_bytes(material) -> tuple[int, int, int]:
    # The authored Principled-BSDF values are scene-linear.  A console's flat
    # 8-bit framebuffer expects display/sRGB bytes; copying the floats straight
    # to bytes crushes intentional dark teals and graphite almost to black.
    def linear_to_srgb(value: float) -> float:
        if value <= 0.0031308:
            return value * 12.92
        return 1.055 * (value ** (1.0 / 2.4)) - 0.055

    colour = material.diffuse_color
    return tuple(
        max(0, min(255, round(linear_to_srgb(float(colour[index])) * 255.0)))
        for index in range(3)
    )


def extract(value) -> dict:
    mesh = value.data
    mesh.calc_loop_triangles()
    world_points = [value.matrix_world @ vertex.co for vertex in mesh.vertices]
    floor = min(point.z for point in world_points)
    ceiling = max(point.z for point in world_points)
    scale = TARGET_HEIGHT / (ceiling - floor)
    vertices = [mapped(point, scale, floor) for point in world_points]
    sine = math.sin(math.radians(CAMERA_PITCH_DEGREES))
    cosine = math.cos(math.radians(CAMERA_PITCH_DEGREES))
    triangles = []
    for triangle in mesh.loop_triangles:
        indices = (triangle.vertices[0], triangle.vertices[2], triangle.vertices[1])
        points = [vertices[index] for index in indices]
        triangles.append(
            {
                "indices": indices,
                "material": triangle.material_index,
                "shade": shade_for(*points),
                "depth": sum(-sine * point[1] + cosine * point[2] for point in points),
            }
        )
    triangles.sort(key=lambda triangle: triangle["depth"], reverse=True)
    return {
        "vertices": vertices,
        "triangles": triangles,
        "materials": [colour_bytes(material) for material in mesh.materials],
    }


def aim(value, target: Vector) -> None:
    value.rotation_euler = (target - value.location).to_track_quat("-Z", "Y").to_euler()


def render_preview(value, target: pathlib.Path) -> None:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(target)
    scene.render.film_transparent = False
    scene.world.color = (0.018, 0.025, 0.045)

    camera_data = bpy.data.cameras.new("V2ProxyCamera")
    camera = bpy.data.objects.new("V2ProxyCamera", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = (0.0, -3.4, 2.45)
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 1.58
    aim(camera, Vector((0.0, 0.0, 0.60)))
    scene.camera = camera

    light_data = bpy.data.lights.new("V2ProxyKey", "AREA")
    light_data.energy = 900.0
    light_data.shape = "DISK"
    light_data.size = 4.0
    light = bpy.data.objects.new("V2ProxyKey", light_data)
    bpy.context.collection.objects.link(light)
    light.location = (-2.4, -3.2, 4.2)
    aim(light, Vector((0.0, 0.0, 0.6)))

    floor_material = bpy.data.materials.new("V2 proxy floor")
    floor_material.diffuse_color = (0.07, 0.11, 0.14, 1.0)
    bpy.ops.mesh.primitive_plane_add(size=3.2, location=(0.0, 0.0, -0.006))
    bpy.context.object.data.materials.append(floor_material)
    target.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.render.render(write_still=True)


def emit_proxy(lines: list[str], name: str, data: dict) -> None:
    vertices = data["vertices"]
    triangles = data["triangles"]
    materials = data["materials"]
    lines.extend(
        [
            f"namespace {name} {{",
            f"inline constexpr int vertex_count = {len(vertices)};",
            f"inline constexpr int triangle_count = {len(triangles)};",
            f"inline constexpr int material_count = {len(materials)};",
            "inline constexpr CharacterProxyV2Vertex vertices[] = {",
        ]
    )
    lines.extend(f"    {{{x}, {y}, {z}}}," for x, y, z in vertices)
    lines.extend(["};", "inline constexpr CharacterProxyV2Triangle triangles[] = {"])
    lines.extend(
        "    {%d, %d, %d, %d, %d},"
        % (*triangle["indices"], triangle["material"], triangle["shade"])
        for triangle in triangles
    )
    lines.extend(["};", "inline constexpr std::uint8_t materials[][3] = {"])
    lines.extend(f"    {{{red}, {green}, {blue}}}," for red, green, blue in materials)
    lines.extend(["};", f"}}  // namespace {name}", ""])


def main() -> None:
    models, header_path, preview_dir = arguments()
    generated = {}
    report = {"camera_pitch_degrees": CAMERA_PITCH_DEGREES, "height_world_units": TARGET_HEIGHT, "models": {}}
    for model, path in models:
        report["models"][model] = {
            "source": f"platform/godot/tools/{path.name}",
            "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "blender_helper_sha256": hashlib.sha256(
                (path.parent / "blender_character_tools.py").read_bytes()
            ).hexdigest(),
            "lods": {},
        }
        for lod, target in LODS:
            proxy, measurements = build_lod(path, model, lod, target)
            generated[f"{model}_{lod}"] = extract(proxy)
            preview = preview_dir / f"{model}-{lod}.png"
            render_preview(proxy, preview)
            measurements["vertices"] = len(generated[f"{model}_{lod}"]["vertices"])
            report["models"][model]["lods"][lod] = measurements
            print(f"{model} {lod}: {measurements}")

    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by tools/console_3d/generate_v2_proxies.py from the Godot Blender sources.",
        "#pragma once",
        "",
        "struct CharacterProxyV2Vertex final {",
        "    std::int16_t x;",
        "    std::int16_t y;",
        "    std::int16_t z;",
        "};",
        "struct CharacterProxyV2Triangle final {",
        "    std::uint16_t a;",
        "    std::uint16_t b;",
        "    std::uint16_t c;",
        "    std::uint8_t material;",
        "    std::uint8_t shade;",
        "};",
        "",
    ]
    for name, data in generated.items():
        emit_proxy(lines, name, data)
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text("\n".join(lines), encoding="utf-8")
    header_path.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
