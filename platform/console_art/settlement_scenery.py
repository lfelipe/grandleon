"""Godot-derived settlement models for both console engines."""
import hashlib
import json
from pathlib import Path
from stone_keep import face_normal, lit_colour

KINDS = ("village_house", "abandoned_cart", "village_well", "watchtower", "walled_garden", "town_wall", "town_wall_join")


def load_models(repo: Path) -> dict:
    data = json.loads((repo / "platform/console_art/settlement_scenery.json").read_text())
    source = repo / "platform/godot/project/scripts/settlement_scenery.gd"
    if data.get("schema") != "grandleon.settlement.v1" or data.get("source_sha256") != hashlib.sha256(source.read_text(encoding="utf-8").encode()).hexdigest():
        raise ValueError("settlement models are stale; regenerate with export_settlement_scenery.gd")
    return data["models"]


def placements(identities: list[int], width: int, height: int, content_id) -> list[tuple]:
    """Recognise complete footprints; never substitute mountain shapes."""
    by_id = {content_id("mountain_" + kind): kind for kind in KINDS}
    consumed = set()
    result = []
    for y in range(height):
        for x in range(width):
            index = y * width + x
            kind = by_id.get(int(identities[index]))
            if not kind or index in consumed:
                continue
            size = 1 if kind.startswith("town_wall") else 2
            cells = [(y + dy) * width + x + dx for dy in range(size) for dx in range(size)]
            if x + size > width or y + size > height or any(int(identities[i]) != int(identities[index]) or i in consumed for i in cells):
                raise ValueError(f"incomplete {kind} footprint at {x},{y}")
            consumed.update(cells)
            if kind == "town_wall" and x > 0 and int(identities[index - 1]) == content_id("mountain_watchtower"):
                kind = "town_wall_join"
            result.append((x, y, kind, size))
    return result


def write_n64_header(repo: Path, output: Path) -> None:
    models = load_models(repo)
    vertices, indices, batches, descriptors = [], [], [], []
    for kind in KINDS:
        batch_start = len(batches)
        for part in models[kind]:
            points, source_indices = part["vertices"], part["indices"]
            # Keep each RSP load even-aligned and within its 32-vertex cache.
            remap, local_vertices, local_indices = {}, [], []
            def flush():
                if not local_indices:
                    return
                first = len(vertices)
                vertices.extend(local_vertices)
                count = len(local_vertices)
                if count & 1:
                    vertices.append(local_vertices[-1])
                batches.append((first, count + (count & 1), len(indices), len(local_indices), part["masonry"]))
                indices.extend(local_indices)
                remap.clear(); local_vertices.clear(); local_indices.clear()
            for offset in range(0, len(source_indices), 3):
                face = source_indices[offset:offset + 3]
                colour = lit_colour(part["colour"], face_normal(points, face))
                rgba = (colour[0] << 24) | (colour[1] << 16) | (colour[2] << 8) | 255
                keys = [(old, rgba) for old in face]
                if len(remap) + sum(key not in remap for key in keys) > 32:
                    flush()
                for old, key in zip(face, keys):
                    if key not in remap:
                        remap[key] = len(local_vertices)
                        uv = part["uvs"][old]
                        local_vertices.append((*[round(v * 52) for v in points[old]], round(uv[0]*1024), round(uv[1]*1024), rgba))
                    local_indices.append(remap[key])
            flush()
        descriptors.append((batch_start, len(batches) - batch_start))
    lines = ["// Generated Godot settlement geometry. Do not edit.", "#pragma once", "#include <array>", "#include <cstdint>",
             "namespace grandleon::nintendo64::pyrite::settlement_art {",
             "struct Vertex { std::int16_t x,y,z,u,v; std::uint32_t colour; };",
             "struct Batch { std::uint16_t first; std::uint8_t count; std::uint16_t index_first,index_count; bool masonry; };",
             "struct Model { std::uint16_t first,count; };",
             f"inline constexpr std::array<Vertex,{len(vertices)}> vertices = {{{{"]
    lines += ["{" + ",".join(str(v) for v in vertex) + "}," for vertex in vertices]
    lines += ["}};", f"inline constexpr std::array<std::uint8_t,{len(indices)}> indices = {{{{", ",".join(map(str,indices)), "}};",
              f"inline constexpr std::array<Batch,{len(batches)}> batches = {{{{"]
    lines += ["{" + ",".join(str(v).lower() for v in batch) + "}," for batch in batches]
    lines += ["}};", f"inline constexpr std::array<Model,{len(descriptors)}> models = {{{{"]
    lines += ["{" + ",".join(map(str,model)) + "}," for model in descriptors]
    lines += ["}};", "}", ""]
    output.write_text("\n".join(lines))
