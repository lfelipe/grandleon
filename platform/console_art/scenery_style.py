"""The same house artwork, placement choices and ground materials as Godot."""
import hashlib
import json
from pathlib import Path
from functools import lru_cache
from PIL import Image


@lru_cache(maxsize=8)
def document(repo: Path) -> dict:
    return json.loads((repo / "platform/godot/project/content/scenery_style.json").read_text())


def house_at(repo: Path, x: int, y: int, family: str = "medieval") -> dict:
    choices = document(repo)["houses"]["placements"]
    seed = x * 17 + y * 31
    result = dict(choices[(seed ^ (seed >> 3)) % len(choices)])
    drawings = document(repo)["houses"]["families"][family]
    result["drawing"] = drawings[result["drawing"] % len(drawings)]
    return result


def house_image(repo: Path, drawing: int, side: int) -> Image.Image:
    relative = document(repo)["houses"]["drawings"][drawing]
    with Image.open(repo / "platform/godot/project" / relative) as source:
        return source.convert("RGBA").resize((side, side), Image.Resampling.LANCZOS)


def grove_image(repo: Path, side: int, theme: int = 0) -> Image.Image:
    relative = document(repo)["ancientGrove"]["drawing"]
    with Image.open(repo / "platform/godot/project" / relative) as source:
        image = source.convert("RGBA")
        image.thumbnail((side, side), Image.Resampling.LANCZOS)
    if theme:
        pixels = []
        for r, g, b, alpha in image.getdata():
            leaf = max(0.0, min(1.0, ((g - max(r, b)) / 255 - .005) / .065))
            leaf = leaf * leaf * (3 - 2 * leaf)
            light = (r * .299 + g * .587 + b * .114) / 255
            foliage = ([light * 1.65, light * .94, light * .28] if theme == 1 else
                       [light * .60 + .34, light * .65 + .36, light * .66 + .36] if theme == 2 else
                       [light * .60, light * .55, light * .48])
            pixels.append(tuple(round(max(0, min(255, original * (1 - leaf) + shaded * 255 * leaf))) for original, shaded in zip((r, g, b), foliage)) + (alpha,))
        image.putdata(pixels)
    canvas = Image.new("RGBA", (side, side))
    canvas.paste(image, ((side - image.width) // 2, side - image.height))
    return canvas


@lru_cache(maxsize=8)
def materials(repo: Path) -> dict:
    data = json.loads((repo / "platform/console_art/natural_materials/manifest.json").read_text())
    for name, expected in data["sources"].items():
        actual = hashlib.sha256((repo / name).read_text(encoding="utf-8").encode()).hexdigest()
        if actual != expected:
            raise ValueError("Godot ground materials are stale; regenerate export_natural_materials.gd")
    return data


def ground_image(repo: Path, kind: int, theme: int = 0) -> Image.Image:
    info = materials(repo)
    path = repo / "platform/console_art/natural_materials" / f"{theme}_{info['names'][kind]}.png"
    return Image.open(path).convert("RGB")


def n64_ground_palette(repo: Path, theme: int = 0) -> list[list[int]]:
    # The small CI4 luminance atlas cannot retain Godot's coloured grain or
    # local lighting. Restore colour separation before neutral modulation.
    result = []
    for colour in materials(repo)["palettes"][theme]:
        luminance = sum(value * weight for value, weight in zip(colour, (.2126, .7152, .0722)))
        result.append([max(0, min(255, round((luminance + (v - luminance) * 1.4) * 255 / 238))) for v in colour])
    return result


def write_n64_header(repo: Path, output: Path, theme: int = 0) -> None:
    houses = document(repo)["houses"]
    palette = n64_ground_palette(repo, theme)
    lines = ["// Generated from Godot scenery_style.json and rendered ground materials.",
             "#pragma once", "#include <array>", "#include <cstdint>",
             "namespace grandleon::nintendo64::pyrite::scenery_style {",
             "struct House { std::uint8_t drawing; bool mirror; float x,z; };",
             f"inline constexpr unsigned house_drawing_count = {len(houses["drawings"])};",
             f"inline constexpr float house_side = {houses['portableCanvasSide']}F;",
             f"inline constexpr float grove_side = {document(repo)['ancientGrove']['portableCanvasSide']}F;",
             f"inline constexpr std::array<House,{len(houses['placements'])}> houses = {{{{"]
    for h in houses["placements"]:
        lines.append(f"{{{h['drawing']},{str(h['mirror']).lower()},{h['offset'][0]}F,{h['offset'][1]}F}},")
    lines += ["}};", "inline House house_at(int x,int y,unsigned family=0) { const auto seed=x*17+y*31; auto house=houses[(seed^(seed>>3))%houses.size()];"]
    for family, name in enumerate(("medieval", "sengoku", "pirates")):
        drawings = houses["families"][name]
        mapped = [drawings[i % len(drawings)] for i in range(4)]
        lines.append(f"if (family=={family}U) {{ constexpr unsigned drawings[4]={{{','.join(map(str,mapped))}}}; house.drawing=drawings[house.drawing]; }}")
    lines += ["return house; }",
              "inline constexpr std::uint8_t ground[13][3] = {"]
    lines += ["{" + ",".join(map(str, colour)) + "}," for colour in palette]
    lines += ["};", "}", ""]
    output.write_text("\n".join(lines))


def save_ci4_house(repo: Path, drawing: int, output: Path) -> None:
    save_ci4_image(house_image(repo, drawing, 64), output)


def save_ci4_image(image: Image.Image, output: Path) -> None:
    pixels = list(image.getdata())
    opaque = [p[:3] for p in pixels if p[3] >= 128]
    sample = Image.new("RGB", (len(opaque), 1))
    sample.putdata(opaque)
    palette = sample.quantize(colors=15, method=Image.Quantize.MEDIANCUT).getpalette()[:45]
    palette += [0] * (45 - len(palette))
    colours = [tuple(palette[i:i+3]) for i in range(0,45,3)]
    indices = [0 if p[3] < 128 else 1 + min(range(15), key=lambda i:sum((p[c]-colours[i][c])**2 for c in range(3))) for p in pixels]
    result = Image.new("P", image.size)
    result.putpalette([0,0,0]+palette+[0]*(768-48))
    result.putdata(indices)
    result.info["transparency"] = 0
    result.save(output, bits=4)
