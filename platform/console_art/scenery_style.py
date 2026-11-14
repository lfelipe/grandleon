"""The same house artwork, placement choices and ground materials as Godot."""
import hashlib
import json
from pathlib import Path
from functools import lru_cache
from PIL import Image


@lru_cache(maxsize=8)
def document(repo: Path) -> dict:
    return json.loads((repo / "platform/godot/project/content/scenery_style.json").read_text())


def house_at(repo: Path, x: int, y: int) -> dict:
    choices = document(repo)["houses"]["placements"]
    seed = x * 17 + y * 31
    return choices[(seed ^ (seed >> 3)) % len(choices)]


def house_image(repo: Path, drawing: int, side: int) -> Image.Image:
    relative = document(repo)["houses"]["drawings"][drawing]
    with Image.open(repo / "platform/godot/project" / relative) as source:
        return source.convert("RGBA").resize((side, side), Image.Resampling.LANCZOS)


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


def write_n64_header(repo: Path, output: Path, theme: int = 0) -> None:
    houses = document(repo)["houses"]
    palette = materials(repo)["palettes"][theme]
    lines = ["// Generated from Godot scenery_style.json and rendered ground materials.",
             "#pragma once", "#include <array>", "#include <cstdint>",
             "namespace grandleon::nintendo64::pyrite::scenery_style {",
             "struct House { std::uint8_t drawing; bool mirror; float x,z; };",
             f"inline constexpr float house_side = {houses['portableCanvasSide']}F;",
             f"inline constexpr float house_centre = {houses['centreAboveGround']}F;",
             f"inline constexpr std::array<House,{len(houses['placements'])}> houses = {{{{"]
    for h in houses["placements"]:
        lines.append(f"{{{h['drawing']},{str(h['mirror']).lower()},{h['offset'][0]}F,{h['offset'][1]}F}},")
    lines += ["}};", "inline House house_at(int x,int y) { const auto seed=x*17+y*31; return houses[(seed^(seed>>3))%houses.size()]; }",
              "inline constexpr std::uint8_t ground[13][3] = {"]
    # The luminance atlas is centred at 238/255; compensate that modulation.
    lines += ["{" + ",".join(str(min(255,round(v*255/238))) for v in colour) + "}," for colour in palette]
    lines += ["};", "}", ""]
    output.write_text("\n".join(lines))


def save_ci4_house(repo: Path, drawing: int, output: Path) -> None:
    image = house_image(repo, drawing, 64)
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
