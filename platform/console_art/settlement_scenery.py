"""Godot-derived settlement models for both console engines."""
import hashlib
import json
from pathlib import Path
from stone_keep import face_normal, lit_colour
from scenery_style import HOUSE_FAMILIES

SETTLEMENT_KINDS = ("village_house", "abandoned_cart", "village_well", "watchtower", "walled_garden", "town_wall", "town_wall_join")

CASTLE_KINDS = ("castle_wall_horizontal", "castle_wall_vertical", "castle_wall_join", "castle_door", "castle_dais", "castle_roof", "visual_cover")

KINDS = SETTLEMENT_KINDS + ("orchard_bell",) + tuple(f"signal_beacon_{v}" for v in range(6)) + tuple(f"watch_ruin_{edge}_{v}" for edge in range(4) for v in range(2)) + tuple(f"orchard_tree_{v}" for v in range(4)) + CASTLE_KINDS


def load_models(repo: Path) -> dict:
    data = json.loads((repo / "platform/console_art/settlement_scenery.json").read_text())
    source = repo / "platform/godot/project/scripts/settlement_scenery.gd"
    if data.get("schema") != "grandleon.settlement.v1" or data.get("source_sha256") != hashlib.sha256(source.read_text(encoding="utf-8").encode()).hexdigest():
        raise ValueError("settlement models are stale; regenerate with export_settlement_scenery.gd")
    board = repo / "platform/godot/project/scripts/board_view_3d.gd"
    if data.get("board_sha256") != hashlib.sha256(board.read_text(encoding="utf-8").encode()).hexdigest():
        raise ValueError("campaign landmarks are stale; regenerate with export_settlement_scenery.gd")
    nature = repo / "platform/godot/project/scripts/natural_scenery.gd"
    if data.get("nature_sha256") != hashlib.sha256(nature.read_text(encoding="utf-8").encode()).hexdigest():
        raise ValueError("orchard models are stale; regenerate with export_settlement_scenery.gd")
    return data["models"]


def placements(identities: list[int], width: int, height: int, content_id) -> list[tuple]:
    """Recognise complete footprints; never substitute mountain shapes."""
    by_id = {content_id("mountain_" + kind): kind for kind in SETTLEMENT_KINDS}
    for family in HOUSE_FAMILIES[1:]:
        by_id[content_id(f"mountain_{family}_house")] = f"{family}_house"
    by_id[content_id("mountain_ancient_grove")] = "ancient_grove"
    consumed = set()
    result = []
    for y in range(height):
        for x in range(width):
            index = y * width + x
            identity = int(identities[index])
            if index in consumed:
                continue
            orchard_kind = next((k for k, prefix in ((10, "farmland"), (2, "forest")) if identity == content_id(prefix + "_orchard_tree")), None)
            if orchard_kind is not None:
                variation = abs(x * 17 + y * 31 + orchard_kind * 13) % 4
                result.append((x, y, f"orchard_tree_{variation}", 1))
                continue
            if identity == content_id("paved_orchard_bell"):
                if x + 1 >= width or int(identities[index + 1]) != identity:
                    raise ValueError(f"incomplete orchard bell at {x},{y}")
                consumed.update((index, index + 1))
                # The model includes its half-cell offset from the left anchor.
                result.append((x, y, "orchard_bell", 1))
                continue
            beacon_kind = next((k for k, prefix in ((12, "paved"), (1, "road")) if identity == content_id(prefix + "_signal_beacon")), None)
            if beacon_kind is not None:
                variation = abs(x * 17 + y * 31 + beacon_kind * 13) % 6
                result.append((x, y, f"signal_beacon_{variation}", 1))
                continue
            if identity == content_id("ruins_watch_ruin"):
                edge = 0 if y == 3 else 1 if y == 8 else 2 if x == 5 else 3
                variation = abs(x * 17 + y * 31 + 8 * 13) % 2
                result.append((x, y, f"watch_ruin_{edge}_{variation}", 1))
                continue
            kind = by_id.get(identity)
            if not kind:
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


def modular_castle_placements(identities, width, height, hidden_cells, content_id):
    """Match the larger Godot board's adjacent walls and two-cell doorways."""
    wall, door, floor, dais = [content_id(k) for k in
        ("mountain_castle_wall", "paved_castle_door", "paved_castle_floor", "paved_castle_dais")]
    hidden = set(hidden_cells)
    def wall_at(x, y):
        return 0 <= x < width and 0 <= y < height and int(identities[y * width + x]) in (wall, door)
    result = []
    for index, value in enumerate(identities):
        x, y = index % width, index // width
        value = int(value)
        if value == wall:
            vertical = wall_at(x,y-1) or wall_at(x,y+1)
            horizontal = wall_at(x-1,y) or wall_at(x+1,y)
            kind = "join" if vertical and horizontal else "vertical" if vertical else "horizontal"
            result.append((x,y,"castle_wall_" + kind,1))
        elif value == door and (y == 0 or int(identities[index - width]) != door):
            result.append((x,y,"castle_door",1))
        elif value == dais:
            result.append((x,y,"castle_dais",1))
        if index in hidden:
            result.append((x,y,"castle_roof" if value in (floor,dais) else "visual_cover",1))
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
    lines += ["}};"]
    lines += [f"inline constexpr std::uint8_t model_{kind} = {index}U;" for index, kind in enumerate(KINDS)]
    lines += ["}", ""]
    output.write_text("\n".join(lines))
