// SPDX-License-Identifier: MIT
// The art library's PlayStation view, as the renderer wants to ask for it.
//
// `tools/placeholder_art` generates `assets/playstation.h`, holding texel data
// already in the layout a GP0(0xA0) transfer wants and CLUTs already as
// PlayStation colour words. The build writes a sparse header for each project;
// it includes the style headers that project uses and points only at its exact
// drawings. This file is the one place that knows that generated table.
//
// The interesting part is that there is **no bank arithmetic here at all.** A
// machine with a handful of hardware palettes needs machinery to decide which
// of them a board gets. A PlayStation names a CLUT in every textured-primitive
// packet, so every asset keeps the sixteen-colour subset the `n64_ci4` profile
// already chose for it and no two assets ever have to agree. The library's
// per-asset palettes are used exactly as generated.
//
// Nor is any colour converted. `tools/placeholder_art/placeholder_art/
// playstation_header.py` says why, and `verify.check_playstation` asserts it:
// the master palette's 124 entries stay 124 distinct colours at five bits per
// channel, so a PlayStation shows the art the other clients show.

#ifndef GRANDLEON_PLATFORM_PLAYSTATION_PSX_ART_H
#define GRANDLEON_PLATFORM_PLAYSTATION_PSX_ART_H

#include "playstation.h"

// The project-specific sparse roster chosen by CMake for this executable.
#ifndef GRANDLEON_PSX_CHARACTER_HEADER
#error "GRANDLEON_PSX_CHARACTER_HEADER is not defined; the build chooses the project art"
#endif
#include GRANDLEON_PSX_CHARACTER_HEADER

// The same style's roster drawn as *solids*, for a target that has depth.
//
// Optional, and that is the whole design: a mesh is another drawing of the same
// archetype, chosen by the same style and by the same seam, but nothing shipped
// draws one yet, so a build that does not ask for meshes pays nothing for them.
// `platform/playstation/CMakeLists.txt` defines this only for the target that
// wants it. When the 3D board ships, the definition moves beside the character
// one and this `#ifdef` becomes the `#error` above.
#ifdef GRANDLEON_PSX_MESH_HEADER
#include GRANDLEON_PSX_MESH_HEADER
static_assert(grandleon_playstation_mesh_style
                  == grandleon_playstation_project_default_style,
              "the embedded meshes are not the project's default style's");
#endif

namespace grandleon::playstation::art {

// A board cell as the art library draws it, in texels and in VRAM halfwords.
inline constexpr int cell_size = grandleon_playstation_cell_size;
inline constexpr int texels_per_halfword =
    grandleon_playstation_texels_per_halfword;
inline constexpr int halfwords_per_row = grandleon_playstation_halfwords_per_row;
inline constexpr int halfwords_per_cell = grandleon_playstation_halfwords_per_cell;
inline constexpr int clut_size = grandleon_playstation_clut_size;

inline constexpr int terrain_kind_count = grandleon_playstation_terrain_kind_count;
inline constexpr int theme_count = grandleon_playstation_theme_count;
inline constexpr int variant_count = grandleon_playstation_terrain_variant_count;
inline constexpr int style_count = grandleon_playstation_style_count;
inline constexpr int figure_count = grandleon_playstation_figure_count;
inline constexpr int project_default_style =
    grandleon_playstation_project_default_style;
inline constexpr int project_default_figure =
    grandleon_playstation_project_default_figure;
static_assert(project_default_style >= 0 && project_default_style < style_count,
              "the project's default style is not one of the library's");
static_assert(project_default_figure >= 0 &&
                  project_default_figure < figure_count,
              "the project's default figure is not one of the library's");

inline constexpr int archetype_count = grandleon_playstation_archetype_count;
inline constexpr int faction_colour_count = grandleon_playstation_faction_count;

inline constexpr int character_count =
    grandleon_playstation_project_character_count;

// One drawable thing: the halfwords of its cell, and the sixteen-entry CLUT
// its indices mean. Both travel together because on this machine they are
// uploaded together and named together in the same draw packet.
struct Asset final {
    const unsigned short* texels;
    const unsigned short* clut;
};

[[nodiscard]] inline Asset terrain(int theme, int kind, int variant) noexcept {
    return {
        grandleon_playstation_terrain[theme][kind][variant],
        grandleon_playstation_terrain_clut[theme][kind]
    };
}

// The sparse table's slot for an exact drawing, or -1 when the compiled project
// can never ask for it. The configure-time resolver and package compiler use
// the same menus and resolution rules; a miss at run time is therefore a
// checked content/build disagreement, never an invitation to substitute art.
[[nodiscard]] inline int character_index(
    int style, int figure, int archetype, int colour
) noexcept {
    for (int i = 0; i < character_count; ++i) {
        const auto& entry = grandleon_playstation_project_characters[i];
        if (entry.style == style && entry.figure == figure &&
            entry.archetype == archetype && entry.colour == colour) {
            return i;
        }
    }
    return -1;
}

[[nodiscard]] inline Asset character(int index) noexcept {
    if (index < 0 || index >= character_count) return {nullptr, nullptr};
    const auto& entry = grandleon_playstation_project_characters[index];
    return {entry.texels, entry.clut};
}

[[nodiscard]] inline Asset character(
    int style, int figure, int archetype, int colour
) noexcept {
    return character(character_index(style, figure, archetype, colour));
}

// The palette index of one texel of a cell.
//
// Row major, four texels to a halfword, the leftmost in the *low* nibble. That
// last part is the one thing the format reverses against the 4bpp PNGs the
// header is repacked from and against the Nintendo 64's CI4, and it is easy to
// get backwards, which is why `verify.check_playstation` unpacks a cell and
// compares rather than trusting the emitter.
[[nodiscard]] inline int index_at(const Asset& asset, int x, int y) noexcept {
    if (asset.texels == nullptr) return 0;
    const unsigned short word =
        asset.texels[y * halfwords_per_row + x / texels_per_halfword];
    return (word >> (4 * (x % texels_per_halfword))) & 0xF;
}

// The colour a texel resolves to, through the asset's own CLUT.
//
// A CLUT word of zero is what the GPU skips, so a caller that finds one is
// looking at a hole in the art and must look somewhere else rather than probe
// it. Note that this is a property of the *colour*, not of the index: unlike
// every other console this repository targets, index 0 is not reserved here,
// and the forty opaque terrain sheets legitimately use it for ground.
[[nodiscard]] inline unsigned short colour_at(
    const Asset& asset, int x, int y
) noexcept {
    if (asset.clut == nullptr) return 0;
    return asset.clut[index_at(asset, x, y)];
}

#ifdef GRANDLEON_PSX_MESH_HEADER
// One archetype as a solid: a run of convex boxes, eight values each, in the
// figure's own space. `parts` is null for an archetype this style has no mesh
// for, which is not an incomplete style: a mesh is an additional drawing of an
// archetype, not a cell of the animation sequence every style must ship.
struct Mesh final {
    const short* parts;
    int part_count;
};

[[nodiscard]] inline Mesh mesh(int archetype) noexcept {
    return {grandleon_playstation_mesh_parts[archetype],
            grandleon_playstation_mesh_part_count[archetype]};
}

// The sprite silhouette a mesh archetype is held to, in texels of its 32x32
// cell, measured by the generator off the very arrays uploaded above rather
// than declared. The rule is that a mesh matches its own style's sprite on both
// screen axes; the scratch program asserts these against what it measures on
// the machine rather than trusting either side.
struct MeshSilhouette final {
    int width;
    int height;
    int area;
};

[[nodiscard]] inline MeshSilhouette mesh_silhouette(int archetype) noexcept {
    return {grandleon_playstation_mesh_silhouette_width[archetype],
            grandleon_playstation_mesh_silhouette_height[archetype],
            grandleon_playstation_mesh_silhouette_area[archetype]};
}
#endif

}  // namespace grandleon::playstation::art

#endif  // GRANDLEON_PLATFORM_PLAYSTATION_PSX_ART_H
