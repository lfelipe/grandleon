#!/usr/bin/env python3
"""Generate deterministic team-colour masks for the mythical HD roster.

Each output is a two-channel grayscale-plus-alpha PNG. The grayscale channel
preserves source luminance (and therefore painted shading); alpha stores soft
team-region membership multiplied by the source antialiasing.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


SHEET_SIZE = (1772, 886)
CELL_SIZE = 443
ROWS = 2
COLS = 4
SCRIPT_DIR = Path(__file__).resolve().parent
CHARACTER_DIR = SCRIPT_DIR.parent / "project" / "art" / "characters"
CONTACT_SHEET_DIR = SCRIPT_DIR.parents[2] / "docs" / "screenshots" / "art-roster" / "team-colours"
IDLE_CONTACT_SHEET = CONTACT_SHEET_DIR / "mythical-team-masks-idle.png"
ACTION_CONTACT_SHEET = CONTACT_SHEET_DIR / "mythical-team-masks-actions.png"


@dataclass(frozen=True)
class MaskConfig:
    seeds: tuple[str, ...]
    delta_e: float
    feather: float
    hue_center: float | None = None
    hue_radius: float = 180.0
    saturation_min: float = 0.0
    saturation_max: float = 1.0
    value_min: float = 0.0
    value_max: float = 1.0
    minimum_component_area: int = 18
    minimum_core_overlap: int = 2
    minimum_core_fraction: float = 0.0
    minimum_coverage: float = 0.003
    maximum_coverage: float = 0.48
    # Pose-local regions known to contain an effect or projectile whose source
    # hue deliberately matches the costume. Coordinates are cell-local.
    exclusion_boxes: tuple[tuple[int, int, int, int, int], ...] = ()


# Seed colours describe the intended source team-colour material, not the
# destination faction colour. Multiple seeds cover the painted shadows and
# highlights without flattening the source shading.
MASK_CONFIGS: dict[str, MaskConfig] = {
    "mythical_archer_hd.png": MaskConfig(
        seeds=("#43264f", "#633978", "#82519a", "#a875bb"),
        delta_e=25,
        feather=9,
        hue_center=306,
        hue_radius=42,
        saturation_min=0.14,
        minimum_core_fraction=0.055,
        exclusion_boxes=((6, 260, 120, 443, 245),),
    ),
    "mythical_archer_male_hd.png": MaskConfig(
        seeds=("#3a2130", "#5b315b", "#7d4566", "#965c86"),
        delta_e=24,
        feather=9,
        hue_center=315,
        hue_radius=38,
        saturation_min=0.13,
        minimum_core_fraction=0.055,
    ),
    "mythical_beast_female_hd.png": MaskConfig(
        seeds=("#d7ae86", "#e7c5a1", "#f5d8b8"),
        delta_e=13,
        feather=6,
        saturation_min=0.08,
        saturation_max=0.28,
        value_min=0.62,
        minimum_component_area=28,
    ),
    "mythical_beast_male_hd.png": MaskConfig(
        seeds=("#d6b08e", "#e8c9a9", "#f1d9bd"),
        delta_e=13,
        feather=6,
        saturation_min=0.06,
        saturation_max=0.27,
        value_min=0.62,
        minimum_component_area=28,
    ),
    "mythical_commander_female_hd.png": MaskConfig(
        seeds=("#343653", "#514e74", "#59618e", "#737caf"),
        delta_e=22,
        feather=8,
        hue_center=236,
        hue_radius=30,
        saturation_min=0.12,
    ),
    "mythical_commander_male_hd.png": MaskConfig(
        seeds=("#323755", "#3b3f64", "#4f5280", "#6570a7"),
        delta_e=22,
        feather=8,
        hue_center=234,
        hue_radius=30,
        saturation_min=0.13,
    ),
    "mythical_healer_female_hd.png": MaskConfig(
        seeds=("#b86a2d", "#d9863d", "#e59d52"),
        delta_e=14,
        feather=6,
        hue_center=33,
        hue_radius=10,
        saturation_min=0.44,
        value_min=0.43,
        minimum_core_fraction=0.05,
    ),
    "mythical_healer_male_hd.png": MaskConfig(
        seeds=("#b96c27", "#d77d2d", "#e18733"),
        delta_e=14,
        feather=6,
        hue_center=31,
        hue_radius=10,
        saturation_min=0.45,
        value_min=0.42,
        minimum_core_fraction=0.05,
    ),
    "mythical_knight_female_hd.png": MaskConfig(
        seeds=("#394887", "#43499e", "#4d63aa", "#5668b6"),
        delta_e=17,
        feather=7,
        hue_center=225,
        hue_radius=13,
        saturation_min=0.27,
    ),
    "mythical_knight_hd.png": MaskConfig(
        seeds=("#354676", "#475582", "#486197", "#5876ad"),
        delta_e=17,
        feather=7,
        hue_center=224,
        hue_radius=13,
        saturation_min=0.25,
    ),
    "mythical_mage_female_hd.png": MaskConfig(
        seeds=("#203229", "#2a3b30", "#466145", "#58734f"),
        delta_e=21,
        feather=8,
        hue_center=132,
        hue_radius=34,
        saturation_min=0.10,
        value_max=0.62,
    ),
    "mythical_mage_male_hd.png": MaskConfig(
        seeds=("#1c2a23", "#28372c", "#395d40", "#4f6745"),
        delta_e=21,
        feather=8,
        hue_center=133,
        hue_radius=34,
        saturation_min=0.11,
        value_max=0.62,
    ),
    "mythical_rogue_female_hd.png": MaskConfig(
        seeds=("#4b2850", "#633061", "#854478", "#a06095"),
        delta_e=22,
        feather=8,
        hue_center=309,
        hue_radius=39,
        saturation_min=0.18,
    ),
    "mythical_rogue_male_hd.png": MaskConfig(
        seeds=("#45254e", "#5b2e63", "#7d3d87", "#935292"),
        delta_e=22,
        feather=8,
        hue_center=297,
        hue_radius=38,
        saturation_min=0.19,
    ),
    "mythical_stormcaller_female_hd.png": MaskConfig(
        seeds=("#5a2526", "#8b2b36", "#b63d42", "#c94d50"),
        delta_e=17,
        feather=7,
        hue_center=355,
        hue_radius=13,
        saturation_min=0.31,
        value_min=0.18,
        value_max=0.77,
    ),
    "mythical_stormcaller_male_hd.png": MaskConfig(
        seeds=("#512b28", "#81403b", "#a63f3e", "#bb4443"),
        delta_e=17,
        feather=7,
        hue_center=2,
        hue_radius=12,
        # The gloves, boots, beard, and staff use warm brown-violet shadows.
        # A stronger chroma gate keeps those materials out without requiring
        # pose-specific painted exclusions.
        saturation_min=0.40,
        value_min=0.17,
        value_max=0.77,
    ),
}


def parse_hex(value: str) -> tuple[int, int, int]:
    value = value.removeprefix("#")
    if len(value) != 6:
        raise ValueError(f"invalid RGB seed {value!r}")
    return tuple(int(value[index : index + 2], 16) for index in (0, 2, 4))


def srgb_to_lab(rgb: np.ndarray) -> np.ndarray:
    values = rgb.astype(np.float32) / 255.0
    linear = np.where(
        values <= 0.04045,
        values / 12.92,
        ((values + 0.055) / 1.055) ** 2.4,
    )
    xyz = linear @ np.array(
        [
            [0.4124564, 0.3575761, 0.1804375],
            [0.2126729, 0.7151522, 0.0721750],
            [0.0193339, 0.1191920, 0.9503041],
        ],
        dtype=np.float32,
    ).T
    xyz /= np.array([0.95047, 1.0, 1.08883], dtype=np.float32)
    delta = 6.0 / 29.0
    transformed = np.where(
        xyz > delta**3,
        np.cbrt(xyz),
        xyz / (3.0 * delta**2) + 4.0 / 29.0,
    )
    return np.stack(
        (
            116.0 * transformed[..., 1] - 16.0,
            500.0 * (transformed[..., 0] - transformed[..., 1]),
            200.0 * (transformed[..., 1] - transformed[..., 2]),
        ),
        axis=-1,
    )


def rgb_to_hsv(rgb: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    values = rgb.astype(np.float32) / 255.0
    maximum = values.max(axis=-1)
    minimum = values.min(axis=-1)
    chroma = maximum - minimum
    saturation = np.divide(
        chroma,
        maximum,
        out=np.zeros_like(chroma),
        where=maximum > 0,
    )
    hue = np.zeros_like(maximum)
    nonzero = chroma > 1e-6
    red = nonzero & (maximum == values[..., 0])
    green = nonzero & (maximum == values[..., 1])
    blue = nonzero & (maximum == values[..., 2])
    hue[red] = ((values[..., 1][red] - values[..., 2][red]) / chroma[red]) % 6.0
    hue[green] = (values[..., 2][green] - values[..., 0][green]) / chroma[green] + 2.0
    hue[blue] = (values[..., 0][blue] - values[..., 1][blue]) / chroma[blue] + 4.0
    return hue * 60.0, saturation, maximum


def label_components(binary: np.ndarray) -> list[list[tuple[int, int]]]:
    height, width = binary.shape
    visited = np.zeros_like(binary, dtype=np.bool_)
    components: list[list[tuple[int, int]]] = []
    for start_y, start_x in zip(*np.nonzero(binary)):
        if visited[start_y, start_x]:
            continue
        component: list[tuple[int, int]] = []
        queue = deque([(int(start_y), int(start_x))])
        visited[start_y, start_x] = True
        while queue:
            y, x = queue.pop()
            component.append((y, x))
            for next_y in range(max(0, y - 1), min(height, y + 2)):
                for next_x in range(max(0, x - 1), min(width, x + 2)):
                    if binary[next_y, next_x] and not visited[next_y, next_x]:
                        visited[next_y, next_x] = True
                        queue.append((next_y, next_x))
        components.append(component)
    return components


def main_body_core(alpha: np.ndarray) -> np.ndarray:
    opaque = Image.fromarray(np.where(alpha >= 0.35, 255, 0).astype(np.uint8), mode="L")
    eroded = np.asarray(opaque.filter(ImageFilter.MinFilter(11))) >= 128
    components = label_components(eroded)
    if not components:
        return np.asarray(opaque) >= 128
    body = np.zeros_like(eroded)
    largest = max(components, key=len)
    for y, x in largest:
        body[y, x] = True
    expanded = Image.fromarray(body.astype(np.uint8) * 255, mode="L").filter(
        ImageFilter.MaxFilter(25)
    )
    return np.asarray(expanded) >= 128


def soft_colour_membership(rgb: np.ndarray, config: MaskConfig) -> np.ndarray:
    lab = srgb_to_lab(rgb)
    seed_array = np.asarray([parse_hex(seed) for seed in config.seeds], dtype=np.uint8)
    seed_lab = srgb_to_lab(seed_array)
    distances = np.sqrt(((lab[..., None, :] - seed_lab) ** 2).sum(axis=-1)).min(axis=-1)
    membership = np.clip(
        (config.delta_e + config.feather - distances) / config.feather,
        0.0,
        1.0,
    )
    hue, saturation, value = rgb_to_hsv(rgb)
    gate = (
        (saturation >= config.saturation_min)
        & (saturation <= config.saturation_max)
        & (value >= config.value_min)
        & (value <= config.value_max)
    )
    if config.hue_center is not None:
        hue_distance = np.abs((hue - config.hue_center + 180.0) % 360.0 - 180.0)
        gate &= hue_distance <= config.hue_radius
    return membership * gate.astype(np.float32)


def filter_cell_components(
    membership: np.ndarray,
    alpha: np.ndarray,
    config: MaskConfig,
) -> np.ndarray:
    candidate = (membership >= 0.08) & (alpha >= 0.05)
    connected = np.asarray(
        Image.fromarray(candidate.astype(np.uint8) * 255, mode="L").filter(
            ImageFilter.MaxFilter(3)
        )
    ) >= 128
    body_core = main_body_core(alpha)
    keep = np.zeros_like(candidate)
    for component in label_components(connected):
        if len(component) < config.minimum_component_area:
            continue
        overlap = sum(1 for y, x in component if body_core[y, x])
        if overlap < config.minimum_core_overlap:
            continue
        if overlap / len(component) < config.minimum_core_fraction:
            continue
        for y, x in component:
            keep[y, x] = True
    keep = np.asarray(
        Image.fromarray(keep.astype(np.uint8) * 255, mode="L").filter(
            ImageFilter.MaxFilter(3)
        )
    ) >= 128
    return membership * keep.astype(np.float32)


def generate_mask(source: Image.Image, config: MaskConfig) -> Image.Image:
    rgba = np.asarray(source.convert("RGBA"), dtype=np.uint8)
    rgb = rgba[..., :3]
    alpha = rgba[..., 3].astype(np.float32) / 255.0
    membership = soft_colour_membership(rgb, config)
    filtered = np.zeros_like(membership)
    for row in range(ROWS):
        for column in range(COLS):
            y0 = row * CELL_SIZE
            x0 = column * CELL_SIZE
            selection = np.s_[y0 : y0 + CELL_SIZE, x0 : x0 + CELL_SIZE]
            filtered[selection] = filter_cell_components(
                membership[selection],
                alpha[selection],
                config,
            )
    for cell_index, local_x0, local_y0, local_x1, local_y1 in config.exclusion_boxes:
        row, column = divmod(cell_index, COLS)
        x0 = column * CELL_SIZE + local_x0
        y0 = row * CELL_SIZE + local_y0
        x1 = column * CELL_SIZE + local_x1
        y1 = row * CELL_SIZE + local_y1
        filtered[y0:y1, x0:x1] = 0.0
    luminance = np.clip(
        0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2],
        0,
        255,
    ).astype(np.uint8)
    mask_alpha = np.clip(alpha * filtered * 255.0, 0, 255).astype(np.uint8)
    return Image.fromarray(np.dstack((luminance, mask_alpha)), mode="LA")


def mask_path(source_path: Path) -> Path:
    return source_path.with_name(f"{source_path.stem}_team_mask.png")


def coverage_by_cell(source: Image.Image, mask: Image.Image) -> list[float]:
    source_alpha = np.asarray(source.convert("RGBA"), dtype=np.uint8)[..., 3]
    mask_alpha = np.asarray(mask.convert("LA"), dtype=np.uint8)[..., 1]
    coverage: list[float] = []
    for row in range(ROWS):
        for column in range(COLS):
            y0 = row * CELL_SIZE
            x0 = column * CELL_SIZE
            selection = np.s_[y0 : y0 + CELL_SIZE, x0 : x0 + CELL_SIZE]
            opaque = source_alpha[selection] >= 16
            selected = mask_alpha[selection] >= 16
            denominator = max(1, int(opaque.sum()))
            coverage.append(float(selected.sum()) / denominator)
    return coverage


def validate_mask(
    source_path: Path,
    source: Image.Image,
    mask: Image.Image,
    config: MaskConfig,
) -> list[float]:
    failures: list[str] = []
    if source.size != SHEET_SIZE:
        failures.append(f"source is {source.size}, expected {SHEET_SIZE}")
    if mask.size != SHEET_SIZE:
        failures.append(f"mask is {mask.size}, expected {SHEET_SIZE}")
    if mask.mode != "LA":
        failures.append(f"mask mode is {mask.mode}, expected LA")
    source_alpha = np.asarray(source.convert("RGBA"), dtype=np.uint8)[..., 3]
    mask_alpha = np.asarray(mask.convert("LA"), dtype=np.uint8)[..., 1]
    if np.any((mask_alpha > 0) & (source_alpha == 0)):
        failures.append("mask selects fully transparent source pixels")
    coverage = coverage_by_cell(source, mask)
    for index, value in enumerate(coverage):
        if value < config.minimum_coverage:
            failures.append(
                f"cell {index} coverage {value:.3%} is below {config.minimum_coverage:.3%}"
            )
        if value > config.maximum_coverage:
            failures.append(
                f"cell {index} coverage {value:.3%} is above {config.maximum_coverage:.3%}"
            )
    if failures:
        joined = "; ".join(failures)
        raise ValueError(f"{source_path.name}: {joined}")
    return coverage


def composite_on_dark(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    rgba = image.convert("RGBA").resize(size, Image.Resampling.LANCZOS)
    background = Image.new("RGBA", size, (31, 36, 43, 255))
    background.alpha_composite(rgba)
    return background.convert("RGB")


def mask_preview(mask: Image.Image, size: tuple[int, int]) -> Image.Image:
    grayscale, alpha = mask.convert("LA").split()
    rgba = Image.merge("RGBA", (grayscale, grayscale, grayscale, alpha)).resize(
        size, Image.Resampling.LANCZOS
    )
    return composite_on_dark(rgba, size)


def overlay_preview(source: Image.Image, mask: Image.Image, size: tuple[int, int]) -> Image.Image:
    source_rgba = np.asarray(source.convert("RGBA"), dtype=np.float32)
    mask_alpha = np.asarray(mask.convert("LA"), dtype=np.float32)[..., 1:2] / 255.0
    tint_strength = mask_alpha * 0.58
    tint = np.array([255.0, 32.0, 220.0], dtype=np.float32)
    source_rgba[..., :3] = source_rgba[..., :3] * (1.0 - tint_strength) + tint * tint_strength
    overlay = Image.fromarray(np.clip(source_rgba, 0, 255).astype(np.uint8), mode="RGBA")
    return composite_on_dark(overlay, size)


def load_font(size: int) -> ImageFont.ImageFont:
    try:
        return ImageFont.truetype("DejaVuSans.ttf", size=size)
    except OSError:
        return ImageFont.load_default()


def generate_idle_contact_sheet(items: list[tuple[Path, Image.Image, Image.Image]]) -> None:
    CONTACT_SHEET_DIR.mkdir(parents=True, exist_ok=True)
    panel_size = (150, 150)
    tile_size = (470, 190)
    sheet = Image.new("RGB", (tile_size[0] * 4, tile_size[1] * 4), (31, 36, 43))
    draw = ImageDraw.Draw(sheet)
    font = load_font(15)
    for index, (source_path, source, mask) in enumerate(items):
        column = index % 4
        row = index // 4
        origin_x = column * tile_size[0]
        origin_y = row * tile_size[1]
        idle = source.crop((0, 0, CELL_SIZE, CELL_SIZE))
        idle_mask = mask.crop((0, 0, CELL_SIZE, CELL_SIZE))
        panels = (
            composite_on_dark(idle, panel_size),
            mask_preview(idle_mask, panel_size),
            overlay_preview(idle, idle_mask, panel_size),
        )
        for panel_index, panel in enumerate(panels):
            sheet.paste(panel, (origin_x + panel_index * 155, origin_y + 28))
        draw.text((origin_x + 6, origin_y + 6), source_path.stem, fill="white", font=font)
    sheet.save(IDLE_CONTACT_SHEET, optimize=True)


def generate_action_contact_sheet(items: list[tuple[Path, Image.Image, Image.Image]]) -> None:
    CONTACT_SHEET_DIR.mkdir(parents=True, exist_ok=True)
    panel_size = (430, 215)
    tile_size = (440, 470)
    sheet = Image.new("RGB", (tile_size[0] * 4, tile_size[1] * 4), (31, 36, 43))
    draw = ImageDraw.Draw(sheet)
    font = load_font(15)
    for index, (source_path, source, mask) in enumerate(items):
        column = index % 4
        row = index // 4
        origin_x = column * tile_size[0]
        origin_y = row * tile_size[1]
        sheet.paste(composite_on_dark(source, panel_size), (origin_x + 5, origin_y + 28))
        sheet.paste(overlay_preview(source, mask, panel_size), (origin_x + 5, origin_y + 248))
        draw.text((origin_x + 6, origin_y + 6), source_path.stem, fill="white", font=font)
    sheet.save(ACTION_CONTACT_SHEET, optimize=True)


def source_paths() -> list[Path]:
    return [CHARACTER_DIR / name for name in sorted(MASK_CONFIGS)]


def run(validate_only: bool, contact_sheets: bool) -> None:
    items: list[tuple[Path, Image.Image, Image.Image]] = []
    for source_path in source_paths():
        if not source_path.exists():
            raise FileNotFoundError(source_path)
        source = Image.open(source_path).convert("RGBA")
        destination = mask_path(source_path)
        if validate_only:
            if not destination.exists():
                raise FileNotFoundError(destination)
            mask = Image.open(destination)
        else:
            mask = generate_mask(source, MASK_CONFIGS[source_path.name])
            mask.save(destination, optimize=True)
        coverage = validate_mask(
            source_path,
            source,
            mask,
            MASK_CONFIGS[source_path.name],
        )
        print(
            f"{source_path.name}: "
            + " ".join(f"{value:.1%}" for value in coverage)
        )
        items.append((source_path, source, mask))
    if contact_sheets:
        generate_idle_contact_sheet(items)
        generate_action_contact_sheet(items)
        print(f"wrote {IDLE_CONTACT_SHEET}")
        print(f"wrote {ACTION_CONTACT_SHEET}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="validate existing masks without rewriting them",
    )
    parser.add_argument(
        "--no-contact-sheets",
        action="store_true",
        help="skip contact-sheet generation",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        run(args.validate_only, not args.no_contact_sheets)
    except (FileNotFoundError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
