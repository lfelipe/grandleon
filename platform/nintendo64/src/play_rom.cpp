// SPDX-License-Identifier: MIT
// Nintendo 64 play ROM.
//
// The whole vertical on the console: the checked-in Tarnholt source project is
// embedded as bytes, parsed and compiled to a package *on the target*, loaded,
// and played through the same client session the desktop uses. The only code
// unique to this file is the presenter: framebuffer drawing and controller
// input. Campaign flow, rules, and the opposing side come from platform/client
// and the engine, unchanged.
//
// The board is drawn through the RDP: textured-rectangle terrain from the
// embedded mksprite sheets and billboard unit sprites with alpha-compare
// transparency, with cursor, markers, health bars and text as CPU overlays on
// the settled output. Everything rdpq rides on the rspq microcode, so the ROM
// is checked under ares, whose paraLLEl-RDP writes rendered pixels back to
// RDRAM where the probe's own CPU sampling sees them.
//
// The RDP is the only renderer here. An emulator that cannot execute rspq
// microcode sees nothing the RDP draws, and would need a second renderer
// beside it to witness anything at all: CPU writes through libdragon's
// graphics API, flat cell colours and flat side-coloured boxes. That buys a
// worse drawing of the same board, maintained for the benefit of the harness
// rather than the player.
//
// GRANDLEON_N64_PROBE replaces controller input with a scripted playthrough
// and per-frame framebuffer classification, reporting RESULT PASS/FAIL over
// the emulator log channel exactly like the conformance ROM.
//
// GRANDLEON_N64_AUTOPILOT keeps every interactive code path the probe builds
// compile out, and replaces only the controller reads: a deterministic script
// of synthetic input (autopilot.h; the sequence lives beside the campaign
// content) drives the title screen, the cutscenes, the cursor, the action
// menu, and the battle itself, with named checkpoints that sample the
// framebuffer and report over the same channel. It is how the screens a
// person sees get verified without a person.
//
// GRANDLEON_N64_CAMPAIGN is the campaign the player keeps. The same six
// steps every other client walks, driven by `client::run_persistent_campaign`
// instead of `client::run_campaign`, with the cartridge's SRAM behind a slot
// (src/sram_window.h) and the management stage between battles drawn in this
// file's own cursor-and-menu vocabulary. Everything else is the same code the
// other builds run: the board, the action menu, the sheet, the cutscenes, the
// audio. That is the reason it is a variant of this file rather than a second
// ROM source.

#include <libdragon.h>

#include "grandleon/view/board_view.hpp"
#include "grandleon/view/motion.hpp"
#include "grandleon/view/list_view.hpp"

#include <grandleon/client/attack_gesture.hpp>
#include <grandleon/client/presenter.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>

#include "project_source.h"
#include "project_identity.h"
#include "backdrops.h"
#include "styles.h"
#include "themes.h"

// Where a row of text is allowed to land on this frame, and the words a
// refusal is said in. Both are headers rather than blocks in this file so that
// a host test can compile the very arithmetic and the very table the ROM
// draws: libdragon does not clip, and the wording is shared with another
// console.
#include "screen_text.h"
#include "refusal_words.h"

// The embedded sprite bytes: the board's terrain sheets and unit billboards,
// and the dialogue portraits, which are CPU-decoded from the same drawings.
#include "sprite_assets.h"

#include <cstdlib>
#include <cstring>

#ifndef GRANDLEON_N64_PROBE
#include "synth.h"
#endif

#ifdef GRANDLEON_N64_CAMPAIGN
#include <grandleon/client/campaign_session.hpp>

// The slot screen's own state machine: its rows, its caret, its verbs and the
// arming that stands between a held slot and being written over. Shared with
// every client that offers the screen, because the requirement is the same
// screen rather than each machine's own, and the cheapest way to mean that is
// one model and a renderer per machine.
#include "grandleon/view/slot_menu.hpp"

// What the company is at the two moments the persistence check looks at it,
// for a scripted run of the shipped game. Derived on the host by
// `tests/nintendo64/campaign_expectations_test.cpp` from this same content
// through this same session, so one header serves both machines and the
// console's assertions cannot be adjusted without breaking the derivation.
//
// It also names the shipped game's campaign and slot, which the ROM does not
// read from here: `project_identity.h` derives those from whichever project
// was built. The two are not allowed to drift: the autopilot build
// asserts they agree, so the duplicate is a check rather than a second answer.
#include "campaign_expectations.h"
#include "sram_window.h"
#endif

#ifdef GRANDLEON_N64_AUTOPILOT
#include "autopilot.h"
#ifdef GRANDLEON_N64_CAMPAIGN
#include "campaign_autopilot.h"
#else
#include "fordlight_autopilot.h"
#endif
#endif

#ifndef GRANDLEON_N64_PROBE
// The project logo, generated by tools/placeholder_art. Including the
// generated header is what keeps the title screen single-sourced with the
// README's mark: both are outputs of the same module.
#include "logo.h"
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>

// All reporting goes to stderr: libdragon routes stderr to the debug channels
// (ISViewer under both pinned emulators) without needing the video console.
// stdout would need console_init, which owns the display this ROM draws on.
#define report_line(...) std::fprintf(stderr, __VA_ARGS__)
#include <string>
#include <vector>

namespace client = grandleon::client;
namespace core = grandleon::core;
#ifdef GRANDLEON_N64_CAMPAIGN
namespace campaign = grandleon::campaign;
namespace storage = grandleon::storage;
#endif
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace view = grandleon::view;
namespace screen = grandleon::n64screen;

namespace {

// The screen's own arithmetic, in the words the drawing below already used.
using grandleon::n64screen::ascii_only;
using grandleon::n64screen::clip_rows;
using grandleon::n64screen::clipped;
using grandleon::n64screen::frame_height;
using grandleon::n64screen::frame_width;
using grandleon::n64screen::pages_of;
using grandleon::n64screen::wrap_text;
using grandleon::n64words::refusal_text;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    report_line("%s %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition) ++failures;
}

class N64Presenter;

#ifdef GRANDLEON_N64_AUTOPILOT
// The autopilot needs to see what a person would see: the presenter for the
// board, and the most recently shown surface for every other screen.
N64Presenter* autopilot_presenter = nullptr;
surface_t* autopilot_screen = nullptr;
// How long a frame is held after `CHECKPOINT` is printed, in frames of video.
//
// The harness polls the log and photographs the screen when it sees a line it
// has not seen before (`platform/nintendo64/ares/headless.sh`), so a frame that
// is not still up when the poll comes round is a frame the trail misses. Stated
// once and shared by the two places that hold a frame: the script's own
// checkpoint op and the defeat the renderer photographs on its own account. A
// hold that differed between them would be one of the two silently failing to
// be photographed.
inline constexpr int autopilot_checkpoint_hold = 150;
// What the last staged scene named, so the cutscene checkpoint derives the
// colour it expects from the same generated table the screen drew from rather
// than from a number somebody wrote down beside it.
std::uint8_t autopilot_scene_backdrop = 0;

// A shown pixel, read back through the CPU's own view of the surface. Valid
// for anything the CPU drew: every overlay, panel, and full screen the
// checkpoints sample. Only board cell interiors belong to the RDP, and those
// go through the presenter's uncached sampler instead.
std::uint16_t surface_pixel(const surface_t* disp, int x, int y) {
    const auto* base = static_cast<const std::uint8_t*>(disp->buffer);
    const auto* row = base + static_cast<std::size_t>(y) * disp->stride;
    return reinterpret_cast<const std::uint16_t*>(row)[x];
}

std::uint16_t pack16(int r, int g, int b) {
    return static_cast<std::uint16_t>(
        graphics_make_color(r, g, b, 255) & 0xFFFFU
    );
}

// How many pixels in a rectangle are not the backdrop.
//
// The title screen's own name band and every campaign screen are text on a
// flat house colour. Such a screen cannot be asserted by sampling one point: a
// glyph's top-left corner is background in most of this font, so a point
// assertion asserts the font's shape rather than the screen's content.
// Counting the ink across the band a screen reserves for a line asserts that
// the line is there, and is robust to a character moving by a pixel, which is
// what a framebuffer assertion about text should be.
int ink_in_band(int left, int right, int top, int bottom) {
    if (autopilot_screen == nullptr) return 0;
    const std::uint16_t backdrop = pack16(16, 26, 27);
    int lit = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if (surface_pixel(autopilot_screen, x, y) != backdrop) ++lit;
        }
    }
    return lit;
}
#endif

// Presents a frame, remembering it for the autopilot's checkpoints.
void show(surface_t* disp) {
#ifdef GRANDLEON_N64_AUTOPILOT
    autopilot_screen = disp;
#endif
    display_show(disp);
}

#ifndef GRANDLEON_N64_PROBE
// One controller poll. The interactive build reads the joypad; the autopilot
// build substitutes the next scripted event (defined after the presenter,
// whose cursor it steers).
[[nodiscard]] joypad_buttons_t poll_buttons();
#endif

// The terrain vocabulary the renderer draws, generated with the art
// (tools/placeholder_art/assets/themes.h): the registry of kinds, each one's
// elevation, and which palette entries a theme's water spends its shimmer on.
// There is no table of any game's terrain names here. The package carries the
// kind the compiler resolved from each authored name, and this ROM reads it
// rather than re-deriving it.
constexpr std::size_t terrain_kind_count = gc::terrain_kind_count;

static_assert(
    grandleon_terrain_kind_count == static_cast<int>(terrain_kind_count),
    "the art library and the content reader disagree on the terrain registry"
);
static_assert(
    grandleon_theme_count == static_cast<int>(gc::theme_count),
    "the art library and the content reader disagree on the theme menu"
);
static_assert(
    grandleon_character_style_count ==
        static_cast<int>(gc::character_style_count),
    "the art library and the content reader disagree on the character style "
    "menu"
);

// A kind's position in the generated registry, resolved at compile time so a
// probe can name a terrain without a runtime lookup.
constexpr bool same_name(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

constexpr std::uint8_t kind_named(const char* name) {
    for (int index = 0; index < grandleon_terrain_kind_count; ++index) {
        if (same_name(grandleon_terrain_kind_names[index], name)) {
            return static_cast<std::uint8_t>(index);
        }
    }
    return gc::terrain_kind_unknown;
}

// The one terrain kind this repository animates. Water, because light on water
// is the effect palette cycling was invented for and the only one either
// console can afford without a second frame of art: the terrain sheet is
// 128x32 CI4, exactly the usable CI4 TMEM, so a second frame costs an extra
// sheet upload every frame for every animated kind. A terrain whose motion is
// shape rather than light (a windmill, a flag, a banner) cannot be cycled and
// would have to be drawn.
constexpr const char* shimmered_terrain = "water";


// sprite_load_buf parses in place, so it gets a heap copy of the ROM bytes.
sprite_t* load_sprite(const unsigned char* data, std::size_t size) {
    void* buffer = std::malloc(size);
    // Four megabytes, and every sheet in the art grid is copied into them in
    // one loop. A drawing that would not fit is a null sprite, which every
    // caller here already answers with an `expect` that names the sheet,
    // rather than a copy into address zero.
    if (buffer == nullptr) return nullptr;
    std::memcpy(buffer, data, size);
    return sprite_load_buf(buffer, static_cast<int>(size));
}

// One CI4 texel of a loaded sprite, as the RGBA16 palette value it indexes.
std::uint16_t texel16(sprite_t* sprite, int tx, int ty) {
    const surface_t pixels = sprite_get_pixels(sprite);
    const auto* row =
        static_cast<const std::uint8_t*>(pixels.buffer) +
        static_cast<std::size_t>(ty) * pixels.stride;
    const std::uint8_t pair = row[tx / 2];
    const auto index = static_cast<std::size_t>(
        tx % 2 == 0 ? pair >> 4 : pair & 0x0F
    );
    return sprite_get_palette(sprite)[index];
}

// Character art, addressed by archetype and faction colour rather than by any
// one game's unit type identifiers. The generator emits exactly this grid
// (tools/placeholder_art, `<archetype>_<colour>.png`), so a second game's
// units resolve through the same table the first game's do.
//
// The other two dimensions of the key (the character style and the figure)
// are bound at build time, not here. This table is **the project's own**
// combination: the CMake asset walk embeds it whole and gives its drawings
// symbols that spell all four keys, and the two macros below paste the
// project's pair in once. So the table keeps the same shape, the same indices
// and the same size whatever combination the project names, adding a style to
// the menu still costs the ROM nothing, and a character drawn in some *other*
// combination is a separate, sparser table rather than a second dimension
// here, because a ROM should carry the drawings its content draws and no more.
struct SpriteBytes final {
    const unsigned char* data;
    std::size_t size;
};

constexpr std::size_t archetype_count = gc::archetype_count;
constexpr std::size_t faction_count = gc::faction_colour_count;

// The roster is shared by every style and closed at eight, so this table's
// height is a property of the art library rather than of the style chosen.
// The content reader holds the roster itself, in the same order, and the two
// have to be the same list or this table's rows address the wrong sprites.
static_assert(
    grandleon_archetype_count == static_cast<int>(archetype_count),
    "the art library and this ROM disagree on the archetype roster"
);

// A drawing's symbol, out of the four keys the asset walk names it by. Two
// levels, because the prefix arrives as a macro and a token pasted with `##`
// is never expanded first: the outer form takes the prefix as an argument, so
// `GRANDLEON_N64_PROJECT_DRAWING` is expanded on the way in and the inner form
// pastes the result.
#define GRANDLEON_N64_PASTE_DRAWING(prefix, a, c) sprite_##prefix##_##a##_##c
#define GRANDLEON_N64_DRAWING(prefix, a, c) \
    GRANDLEON_N64_PASTE_DRAWING(prefix, a, c)
#define GRANDLEON_N64_PASTE_DRAWING_SIZE(prefix, a, c) \
    sprite_##prefix##_##a##_##c##_size
#define GRANDLEON_N64_DRAWING_SIZE(prefix, a, c) \
    GRANDLEON_N64_PASTE_DRAWING_SIZE(prefix, a, c)

// One cell of the project's own combination, standing.
#define GRANDLEON_N64_CELL(a, c)                                          \
    {GRANDLEON_N64_DRAWING(GRANDLEON_N64_PROJECT_DRAWING, a, c),          \
     GRANDLEON_N64_DRAWING_SIZE(GRANDLEON_N64_PROJECT_DRAWING, a, c)}

// Column order is the faction colour menu, the same order the source schema's
// `faction.color` enumerates. A game's faction picks a column; a faction that
// picks nothing takes the column at its own position in the faction list.
#define GRANDLEON_N64_ARCHETYPE_ROW(a)  \
    {GRANDLEON_N64_CELL(a, blue),       \
     GRANDLEON_N64_CELL(a, red),        \
     GRANDLEON_N64_CELL(a, green),      \
     GRANDLEON_N64_CELL(a, violet),     \
     GRANDLEON_N64_CELL(a, amber),      \
     GRANDLEON_N64_CELL(a, bone)}

// The same column order, in words, for the one place this ROM says out loud
// which drawing it chose. Written beside the macro that fixes that order
// rather than derived, because there is nothing to derive it from: the art
// library publishes no colour-name table, and the order is the source schema's
// `faction.color` enumeration, which the row macro above spells out.
constexpr const char* faction_colour_names[faction_count] = {
    "blue", "red", "green", "violet", "amber", "bone"
};

constexpr SpriteBytes character_art[archetype_count][faction_count] = {
    GRANDLEON_N64_ARCHETYPE_ROW(knight),
    GRANDLEON_N64_ARCHETYPE_ROW(archer),
    GRANDLEON_N64_ARCHETYPE_ROW(mage),
    GRANDLEON_N64_ARCHETYPE_ROW(stormcaller),
    GRANDLEON_N64_ARCHETYPE_ROW(healer),
    GRANDLEON_N64_ARCHETYPE_ROW(commander),
    GRANDLEON_N64_ARCHETYPE_ROW(rogue),
    GRANDLEON_N64_ARCHETYPE_ROW(beast),
};

#undef GRANDLEON_N64_ARCHETYPE_ROW
#undef GRANDLEON_N64_CELL

// The animation cells, on exactly the same key. One strip per standing sprite:
// `view::sequence_cell_count` cells of 32x32 side by side. The ROM therefore
// embeds one sprite object, one palette and one header for a whole sequence,
// and a cell is chosen with a texture-coordinate offset exactly as a terrain
// variant is. The CMake asset walk refuses to build a ROM with one of these
// missing: every unit it can draw standing it must also be able to draw moving.
#define GRANDLEON_N64_PASTE_SEQUENCE(prefix, a, c) \
    sprite_##prefix##_##a##_##c##_frames
#define GRANDLEON_N64_SEQUENCE(prefix, a, c) \
    GRANDLEON_N64_PASTE_SEQUENCE(prefix, a, c)
#define GRANDLEON_N64_PASTE_SEQUENCE_SIZE(prefix, a, c) \
    sprite_##prefix##_##a##_##c##_frames_size
#define GRANDLEON_N64_SEQUENCE_SIZE(prefix, a, c) \
    GRANDLEON_N64_PASTE_SEQUENCE_SIZE(prefix, a, c)

#define GRANDLEON_N64_SEQUENCE_CELL(a, c)                                 \
    {GRANDLEON_N64_SEQUENCE(GRANDLEON_N64_PROJECT_DRAWING, a, c),         \
     GRANDLEON_N64_SEQUENCE_SIZE(GRANDLEON_N64_PROJECT_DRAWING, a, c)}

#define GRANDLEON_N64_FRAME_ROW(a)               \
    {GRANDLEON_N64_SEQUENCE_CELL(a, blue),       \
     GRANDLEON_N64_SEQUENCE_CELL(a, red),        \
     GRANDLEON_N64_SEQUENCE_CELL(a, green),      \
     GRANDLEON_N64_SEQUENCE_CELL(a, violet),     \
     GRANDLEON_N64_SEQUENCE_CELL(a, amber),      \
     GRANDLEON_N64_SEQUENCE_CELL(a, bone)}

constexpr SpriteBytes character_frames[archetype_count][faction_count] = {
    GRANDLEON_N64_FRAME_ROW(knight),
    GRANDLEON_N64_FRAME_ROW(archer),
    GRANDLEON_N64_FRAME_ROW(mage),
    GRANDLEON_N64_FRAME_ROW(stormcaller),
    GRANDLEON_N64_FRAME_ROW(healer),
    GRANDLEON_N64_FRAME_ROW(commander),
    GRANDLEON_N64_FRAME_ROW(rogue),
    GRANDLEON_N64_FRAME_ROW(beast),
};

#undef GRANDLEON_N64_FRAME_ROW
#undef GRANDLEON_N64_SEQUENCE_CELL

#ifdef GRANDLEON_N64_EXTRA_DRAWINGS

// The drawings this ROM carries beyond its project's own combination, and
// nothing else. A medieval company with two nature archers pays for two
// nature archers, not for a nature roster. The build resolves the list from
// the content and publishes it as `GRANDLEON_N64_EXTRA_DRAWING_LIST`; this is
// the only place the ROM learns what is in it.
//
// The whole of this section is compiled only when the build embedded at least
// one, which is what makes "a project drawing one combination costs what it
// always cost" a property of the source rather than of the optimiser. A ROM
// for such a project holds no table, no lookup and no assertion here.
//
// Flat and searched rather than indexed, deliberately. Indexing would mean a
// table over the art library's *menus*: every style by every figure by every
// archetype by every colour. That would make adding a style to the library
// cost every ROM bytes, and break the guarantee this console is held to: a
// style a project does not draw costs the ROM zero.
// The list is as long as the content is wide, which is two here and a handful
// at worst.
struct ExtraDrawing final {
    std::uint8_t style;
    std::uint8_t figure;
    std::uint8_t archetype;
    std::uint8_t colour;
    // What the build called it, so a booted ROM says which drawings it is
    // carrying rather than leaving that to be inferred from its size.
    const char* name;
    SpriteBytes standing;
    SpriteBytes sequence;
};

#define GRANDLEON_N64_EXTRA_ROW(si, fi, ai, ci, s, f, a, c)               \
    {si, fi, ai, ci, #s "/" #f "/" #a "/" #c,                             \
     {sprite_##s##_##f##_##a##_##c, sprite_##s##_##f##_##a##_##c##_size}, \
     {sprite_##s##_##f##_##a##_##c##_frames,                              \
      sprite_##s##_##f##_##a##_##c##_frames_size}},

constexpr ExtraDrawing extra_drawings[] = {
    GRANDLEON_N64_EXTRA_DRAWING_LIST(GRANDLEON_N64_EXTRA_ROW)
};

#undef GRANDLEON_N64_EXTRA_ROW

constexpr std::size_t extra_drawing_count =
    sizeof(extra_drawings) / sizeof(extra_drawings[0]);

static_assert(
    extra_drawing_count == GRANDLEON_N64_EXTRA_DRAWINGS,
    "the build published a different number of extra drawings than it emitted"
);

// Each index against the name the same generated line spells, in the art
// library's own published menus. The build computed those indices by walking
// the art manifest and the package carries indices into the *compiler's*
// menus; these assertions are what say the two are the same menus. They cost
// nothing: a menu renamed or reordered under this ROM stops it compiling
// rather than drawing somebody else's character.
#define GRANDLEON_N64_EXTRA_NAMES(si, fi, ai, ci, s, f, a, c)               \
    static_assert(                                                          \
        same_name(grandleon_character_style_names[si], #s),                 \
        "an embedded drawing's style index does not name its style"         \
    );                                                                      \
    static_assert(                                                          \
        same_name(grandleon_character_figure_names[fi], #f),                \
        "an embedded drawing's figure index does not name its figure"       \
    );                                                                      \
    static_assert(                                                          \
        same_name(grandleon_archetype_names[ai], #a),                       \
        "an embedded drawing's archetype index does not name its archetype" \
    );                                                                      \
    static_assert(                                                          \
        (ci) < static_cast<int>(faction_count),                             \
        "an embedded drawing's colour index is off the faction menu"        \
    );

GRANDLEON_N64_EXTRA_DRAWING_LIST(GRANDLEON_N64_EXTRA_NAMES)

#undef GRANDLEON_N64_EXTRA_NAMES

// Where a drawing sits in the table above, or `extra_drawing_count` for one
// this ROM does not carry. The four keys are exactly what the package says:
// the style and the figure out of the two drawing joins, the archetype out of
// the archetype join, and the colour out of the faction join.
[[nodiscard]] constexpr std::size_t extra_drawing_at(
    std::uint8_t style,
    std::uint8_t figure,
    std::uint8_t archetype,
    std::uint8_t colour
) {
    for (std::size_t index = 0; index < extra_drawing_count; ++index) {
        const ExtraDrawing& drawing = extra_drawings[index];
        if (drawing.style == style && drawing.figure == figure &&
            drawing.archetype == archetype && drawing.colour == colour) {
            return index;
        }
    }
    return extra_drawing_count;
}

// The combination this ROM's own project is drawn in, as the two menu indices
// the package speaks in. The build emits the indices and the names beside the
// symbol prefix the roster table pastes, and all three are pinned to each
// other here: the names against the art library's published menus, and the
// pair of names against the prefix itself. So the table below and the roster
// table above cannot come to name different combinations.
#define GRANDLEON_N64_PASTE_NAME(prefix) #prefix
#define GRANDLEON_N64_DRAWING_NAME(prefix) GRANDLEON_N64_PASTE_NAME(prefix)

static_assert(
    same_name(
        grandleon_character_style_names[GRANDLEON_N64_PROJECT_STYLE],
        GRANDLEON_N64_PROJECT_STYLE_NAME
    ),
    "this ROM's style index does not name its style"
);
static_assert(
    same_name(
        grandleon_character_figure_names[GRANDLEON_N64_PROJECT_FIGURE],
        GRANDLEON_N64_PROJECT_FIGURE_NAME
    ),
    "this ROM's figure index does not name its figure"
);
static_assert(
    same_name(
        GRANDLEON_N64_DRAWING_NAME(GRANDLEON_N64_PROJECT_DRAWING),
        GRANDLEON_N64_PROJECT_STYLE_NAME "_" GRANDLEON_N64_PROJECT_FIGURE_NAME
    ),
    "this ROM's roster table is pasted from a different combination than the "
    "one its drawing lookup compares against"
);

#undef GRANDLEON_N64_DRAWING_NAME
#undef GRANDLEON_N64_PASTE_NAME

#endif



// The embedded mksprite output, keyed by the same stable content identities.
// A terrain sheet is 128x32 CI4: the four interior variants side by side,
// 2,048 texel bytes, exactly the usable CI4 TMEM, so a whole sheet uploads in
// one piece. A character is a single 32x32 billboard whose palette index 0 is
// transparent.
struct TerrainSheet final {
    const unsigned char* data;
    std::size_t size;
};

// Row per theme, column per terrain kind, in the generated registry's own
// orders. A theme is a recolour of the same sheets, so the whole table is the
// same art in four climates and the ROM embeds all of it; only the sheets of
// the theme a game chose are ever loaded into RDRAM.
#define GRANDLEON_N64_SHEET(symbol) {symbol, symbol##_size}
#define GRANDLEON_N64_THEME_ROW(theme)                             \
    {GRANDLEON_N64_SHEET(sprite_water_base##theme),                \
     GRANDLEON_N64_SHEET(sprite_road_base##theme),                 \
     GRANDLEON_N64_SHEET(sprite_forest_base##theme),               \
     GRANDLEON_N64_SHEET(sprite_mountain_base##theme),             \
     GRANDLEON_N64_SHEET(sprite_sand_base##theme),                 \
     GRANDLEON_N64_SHEET(sprite_snow_base##theme),                 \
     GRANDLEON_N64_SHEET(sprite_swamp_base##theme),                \
     GRANDLEON_N64_SHEET(sprite_hills_base##theme),                \
     GRANDLEON_N64_SHEET(sprite_ruins_base##theme),                \
     GRANDLEON_N64_SHEET(sprite_grass_base##theme),                \
     GRANDLEON_N64_SHEET(sprite_farmland_base##theme),             \
     GRANDLEON_N64_SHEET(sprite_bamboo_base##theme),               \
     GRANDLEON_N64_SHEET(sprite_paved_base##theme)}

constexpr TerrainSheet terrain_sheets[gc::theme_count][terrain_kind_count] = {
    GRANDLEON_N64_THEME_ROW(),
    GRANDLEON_N64_THEME_ROW(_autumn),
    GRANDLEON_N64_THEME_ROW(_winter),
    GRANDLEON_N64_THEME_ROW(_ashland),
};

#undef GRANDLEON_N64_THEME_ROW
#undef GRANDLEON_N64_SHEET


// Deterministic interior-variant choice, shared by the renderer and by the
// probe's expected values.
//
// This is the one place the console deliberately does not use the shared
// model's autotile lookup: the ROM embeds the four-variant base sheets, whose
// whole point is that all four variants sit in TMEM at once, and the 47-blob
// sheet the mask table indexes is 24 KB and can never be resident against
// 2,048 bytes of usable CI4 TMEM. The desktop and the editor, which do
// hold the blob sheets, both go through `view::autotile_variant`.
constexpr int terrain_variant(int x, int y) {
    return (x + y * 3) % 4;
}

// The frame's draw list lives here rather than inside the presenter so it
// costs stack nothing: there is one presenter for the life of the ROM, and a
// few hundred items is more than the largest board the layout can produce
// (300/14 by 200/14 cells) plus a shadow and a billboard for every unit on it.
constexpr int draw_capacity = 512;
view::DrawItem draw_storage[draw_capacity];

// The board a slide's route may be planned over, and the longest route that
// may be drawn. Both are bounds rather than sizes: a board with more cells
// than the first, or a move longer than the second, falls back to the straight
// line every client drew before the route existed. 512 cells is the same board
// the draw list is sized for; 32 tiles is four times the longest movement
// allowance the content authors.
constexpr int route_cells = 512;
constexpr int route_capacity = 32;

#ifndef GRANDLEON_N64_PROBE

#ifdef GRANDLEON_N64_CAMPAIGN
// The house palette, for the screens that stand outside the presenter. The
// presenter has a static of the same name for the same reason; both are one
// call to libdragon and neither holds a table.
std::uint32_t colour(int r, int g, int b) {
    return graphics_make_color(r, g, b, 255);
}
#endif

std::uint32_t logo_colour(unsigned char index) {
    return graphics_make_color(
        grandleon_logo_palette[index][0],
        grandleon_logo_palette[index][1],
        grandleon_logo_palette[index][2],
        255
    );
}

// Blits the logo at an integer scale, centred horizontally.
void draw_logo(surface_t* disp, int top, int scale) {
    const int left = (320 - grandleon_logo_width * scale) / 2;
    for (int y = 0; y < grandleon_logo_height; ++y) {
        for (int x = 0; x < grandleon_logo_width; ++x) {
            const unsigned char index =
                grandleon_logo_indices[y * grandleon_logo_width + x];
            if (index == 0) continue;
            graphics_draw_box(
                disp, left + x * scale, top + y * scale, scale, scale,
                logo_colour(index)
            );
        }
    }
}

// Where the game's own name sits on the title screen, and what happens to a
// name too long for it.
//
// The logo is 65x60 drawn at scale two from y=28, so it ends at y=148; the
// prompt's erase box starts at y=188. That leaves 40 rows, which is three
// lines of this font at the dialogue screen's own 11-pixel step. The band is
// stated here rather than spelled into the drawing, because the autopilot
// samples exactly it.
constexpr int title_band_top = 152;
constexpr int title_band_step = 11;
constexpr int title_band_rows = 3;
constexpr int title_band_bottom =
    title_band_top + title_band_rows * title_band_step;
// The same 34 columns the dialogue screen wraps at, so one number governs both
// and the safe area is the safe area everywhere.
constexpr std::size_t title_columns = screen::safe_columns;

// A `displayName` may be 160 characters (schemas/source/v1/common.schema.json),
// which is five of these lines. Three is what the screen has, so the third is
// clipped and says so with an ellipsis rather than running off the edge or
// pushing the prompt down. A title that fits is untouched.
std::vector<std::string> title_rows(const std::string& title) {
    std::vector<std::string> rows = wrap_text(ascii_only(title), title_columns);
    clip_rows(rows, title_band_rows, title_columns);
    return rows;
}

// The title screen: the engine's mark, the name of the game on the cartridge,
// and PRESS START blinking at the cadence of the battery-box attract screens
// this console grew up with.
//
// The logo stays where it was and the name goes underneath it, which is the
// way round these two belong: the mark says what built this, and the name says
// what the player thinks they are holding. The name is drawn in the cream the
// screen's other words are drawn in and centred on each of its own rows, so a
// short title reads as a title rather than as a caption pinned to the left.
//
// The static content is painted once per display buffer; every later frame is
// paced and touches only the prompt, so nothing races the video interface.
void title_screen(const std::string& title) {
    grandleon::n64audio::play(grandleon::n64audio::Sfx::title);
    surface_t* painted[3] = {};
    std::size_t painted_count = 0;
    int frame = 0;
    while (true) {
        grandleon::n64audio::pump();
        const joypad_buttons_t pressed = poll_buttons();
        if (pressed.start || pressed.a) return;

        surface_t* disp = display_get();
        bool fresh = true;
        for (std::size_t i = 0; i < painted_count; ++i) {
            if (painted[i] == disp) fresh = false;
        }
        if (fresh) {
            graphics_fill_screen(disp, graphics_make_color(16, 26, 27, 255));
            draw_logo(disp, 28, 2);
            graphics_set_color(graphics_make_color(245, 234, 210, 255), 0);
            int y = title_band_top;
            for (const std::string& row : title_rows(title)) {
                graphics_draw_text(
                    disp,
                    (320 - static_cast<int>(row.size()) * 8) / 2,
                    y,
                    row.c_str()
                );
                y += title_band_step;
            }
            if (painted_count < 3) painted[painted_count++] = disp;
        }
        graphics_draw_box(
            disp, 114, 188, 92, 11, graphics_make_color(16, 26, 27, 255)
        );
        if ((frame / 30) % 2 == 0) {
            graphics_set_color(
                graphics_make_color(245, 234, 210, 255), 0
            );
            graphics_draw_text(disp, 116, 190, "PRESS START");
        }
        show(disp);
        ++frame;
        wait_ms(16);
    }
}

std::string lowered(const std::string& text) {
    std::string out = text;
    for (char& character : out) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return out;
}

// One drawing out of this ROM's own combination, loaded once and cached. The
// same `character_art` grid the board's roster table is built from, so a
// portrait and a board sprite of one character are one drawing rather than
// two that agree.
sprite_t* portrait_from_roster(std::size_t archetype, std::size_t faction) {
    static sprite_t* cache[archetype_count][faction_count] = {};
    if (cache[archetype][faction] == nullptr) {
        cache[archetype][faction] = load_sprite(
            character_art[archetype][faction].data,
            character_art[archetype][faction].size
        );
    }
    return cache[archetype][faction];
}

#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
// And one out of the sparse table, for a character drawn by another hand or at
// another build. Cached on the same terms, in the same shape.
sprite_t* portrait_from_extras(std::size_t slot) {
    static sprite_t* cache[extra_drawing_count] = {};
    if (cache[slot] == nullptr) {
        cache[slot] = load_sprite(
            extra_drawings[slot].standing.data,
            extra_drawings[slot].standing.size
        );
    }
    return cache[slot];
}
#endif

// The portrait for a speaker no scene cast anybody for.
//
// This is the whole of the keyword fallback, kept because omitting the cast has
// to mean exactly what a scene meant before the cast field existed: the first
// archetype word appearing in the words on screen, and the first colour. It is
// a guess, and it is only ever reached by a scene that declined to say.
//
// What it deliberately does *not* do is alias `captain`, `warden` and `runner`
// to archetypes, or pick a colour by looking for `ashen`, `coil` or `kesh` in
// the speaker's name. Those are one campaign's proper nouns, and a renderer
// every campaign uses must not carry them compiled in. A scene that wants a
// particular character says so.
sprite_t* portrait_by_keyword(const std::string& speaker) {
    const std::uint8_t named = gc::archetype_index(lowered(speaker));
    const std::size_t archetype =
        named == gc::archetype_unnamed ? gc::archetype_default : named;
    return portrait_from_roster(archetype, 0);
}

// What a scene is drawn against, when it names one.
//
// A backdrop is a run of flat horizontal bands rather than a picture, and that
// is exactly why this machine can afford it:
// a 320x240 CI4 image is 38,400 bytes against the 2,048 a CI4 texture may hold
// in TMEM, so it would have to be cut into twenty strips and uploaded twenty
// times. Bands cost no texture, no TMEM and no embedded art at all. They are
// the same `graphics_draw_box` this screen already draws its portrait frame
// with, and the table they come from is the generated header above.
//
// `backdrop` is the menu index plus one, as the package carries it; zero, and
// anything past the end of the menu the library published, is a scene that
// names none and gets the flat house colour the screen always had.
//
// The frame this ROM opens (`display_init(RESOLUTION_320x240, ...)`) is named
// in `screen_text.h`, beside the bands measured against it, rather than
// repeated in the arithmetic below.

// A backdrop row scaled to this frame's height. One function, so the pixel the
// screen paints and the pixel an expectation looks at are the same arithmetic
// rather than two copies of it.
int backdrop_row_top(int row) {
    return (row * frame_height + grandleon_backdrop_rows / 2) /
           grandleon_backdrop_rows;
}

// Whether a scene names a backdrop this library can draw. Zero, and anything
// past the end of the menu the library published, is a scene that names none
// and gets the flat house colour the screen always had.
bool scene_has_backdrop(std::uint8_t backdrop) {
    return backdrop != 0 &&
           backdrop <= static_cast<std::uint8_t>(grandleon_backdrop_count);
}

void draw_scene_backdrop(surface_t* disp, std::uint8_t backdrop) {
#ifdef GRANDLEON_N64_AUTOPILOT
    autopilot_scene_backdrop = backdrop;
#endif
    if (!scene_has_backdrop(backdrop)) {
        graphics_fill_screen(
            disp, graphics_make_color(16, 26, 27, 255)
        );
        return;
    }
    const int index = backdrop - 1;
    for (int band = 0; band < grandleon_backdrop_band_count[index]; ++band) {
        const auto& entry = grandleon_backdrop_bands[index][band];
        const int top = backdrop_row_top(entry[0]);
        const int bottom = backdrop_row_top(entry[0] + entry[1]);
        graphics_draw_box(
            disp, 0, top, frame_width, bottom - top,
            graphics_make_color(entry[2], entry[3], entry[4], 255)
        );
    }
}

// CPU-decoded CI4 blit at an integer scale, shared by both renderers so the
// dialogue screen never depends on which one drew the battle.
void draw_portrait(surface_t* disp, sprite_t* portrait, int left, int top) {
    if (portrait == nullptr) return;
    constexpr int scale = 2;
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 32; ++tx) {
            const std::uint16_t value = texel16(portrait, tx, ty);
            if ((value & 1U) == 0U) continue;
            graphics_draw_box(
                disp, left + tx * scale, top + ty * scale, scale, scale,
                (static_cast<std::uint32_t>(value) << 16) | value
            );
        }
    }
}

// Display names for weapons, abilities and items live in `grandleon::sheet`, in
// one table every client reads. One table rather than a copy per renderer,
// because a copy per renderer is exactly how a menu row and a stat sheet come
// to call one weapon two things.
//
// A unit type is not among them any more, and that is the point of
// `sheet::character_name`: nothing on this console asks what kind of thing a
// token is without also asking who it is, and one function answers both in the
// right order.
using grandleon::sheet::ability_name;
using grandleon::sheet::item_name;
using grandleon::sheet::weapon_name;

// One row the unit action menu can offer: a walk, a strike with a named
// weapon, a cast, waiting, the character's own sheet, or the way out. A zero
// `weapon` on a strike row means the weapon in hand, which is the command the
// engine has always been given. A future row that spends a carried item is
// another kind here and nothing else about the menu moves.
struct MenuChoice final {
    const char* text;
    // Walks the character. Like a strike and unlike an item, the row names no
    // tile yet: it hands the player back the cursor over the tiles the engine
    // already lights, and whichever of them they land on is the destination.
    //
    // It is the first row because it is the first thing a turn does, and it is
    // a row at all because a board whose only way to walk was a bare press on
    // open ground never told anybody that walking was one of the character's
    // choices. The press still works (see `controller_intent`) and is the
    // shortcut for a row the menu names rather than the only way to find it.
    bool move;
    bool attack;
    bool wait;
    sim::ContentId ability;
    sim::ContentId weapon;
    // The carried item this row spends. Unlike a strike or a cast this row
    // commits on the press: an item reaches the hand that holds it, so there
    // is nothing for the player to aim afterwards.
    sim::ContentId item;
    // Talks to somebody standing next to the character. Unlike an item and
    // like a strike, this row names nobody yet: a talk reaches a neighbour
    // rather than the hand that holds it, so the row hands the player back the
    // cursor and the engine judges whoever it lands on.
    bool talk;
    // Opens the character's full information sheet and comes back to the menu.
    // The only row that commits nothing at all, which is why it sits below
    // WAIT and above CANCEL rather than among the rows that spend a turn.
    bool info;
};

// Room for the item rows' labels, which are the only menu rows whose text is
// not a constant: a row says how many are left, and that number changes as
// they are spent. Four rows because that is what the sheet lists, and
// twenty-four characters because the longest shipped name plus " x99" fits.
inline constexpr int menu_item_labels = 4;
inline constexpr int menu_item_label_size = 24;

// What the player took out of the menu and has not yet pointed at anything.
//
// The menu answers "what should this character do"; a walk, a strike, a cast
// and a talk all still need a tile, and the presenter holds the first answer
// while the player gives the second. Waiting needs no tile and so never reaches
// here, and neither does spending an item, which reaches only the hand that
// holds it.
enum class Aim : std::uint8_t { none = 0, walk, strike, cast, talk };

// Whether anybody standing beside `actor` would answer a talk from it.
//
// The whole question is put to the engine, one candidate at a time. Adjacency,
// an authored record, still standing, not already gone: `forecast_talk` decides
// every one of them, on exactly the terms `apply` would, so the row on screen
// and the command it sends cannot disagree. A menu that spelled the same test
// out here would be a second copy of the rule, and the first copy would go on
// changing without it.
bool any_talkable_neighbour(
    const sim::EncounterSnapshot& snapshot,
    const sim::UnitSnapshot& actor
) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == actor.id) continue;
        if (sim::forecast_talk(snapshot, actor.id, unit.id)) return true;
    }
    return false;
}

// One screen of controls. The campaign is playable without ever having read
// them, but nobody should have to discover a cursor by accident.
void controls_screen() {
    surface_t* disp = display_get();
    graphics_fill_screen(disp, graphics_make_color(16, 26, 27, 255));
    graphics_set_color(graphics_make_color(242, 193, 78, 255), 0);
    graphics_draw_text(
        disp,
        screen::centred_x(std::string_view{screen::controls_title}),
        screen::controls_title_y,
        screen::controls_title
    );
    graphics_set_color(graphics_make_color(245, 234, 210, 255), 0);
    // The rows, the margin and the band are all `screen_text.h`'s, so a line
    // that would run off the right edge or down over the prompt is a build
    // that does not compile rather than a screen somebody has to notice.
    for (std::size_t index = 0; index < screen::controls_line_count; ++index) {
        graphics_draw_text(
            disp,
            screen::controls_left,
            screen::controls_band.y_of(static_cast<int>(index)),
            screen::controls_lines[index]
        );
    }
    graphics_draw_text(
        disp,
        screen::centred_x(std::string_view{screen::controls_prompt}),
        screen::controls_prompt_y,
        screen::controls_prompt
    );
    show(disp);
    while (true) {
        grandleon::n64audio::pump();
        const joypad_buttons_t pressed = poll_buttons();
        if (pressed.a || pressed.start) return;
        wait_ms(16);
    }
}

#ifdef GRANDLEON_N64_CAMPAIGN

// The slot screen: which of the cartridge's saves this run reads and writes.
//
// One row per slot the directory reserves, each saying what is in it. CONTINUE
// is offered for a slot the cartridge answers for and not for one it does not.
// That is the rule "a CONTINUE that does nothing is worse than no CONTINUE at
// all", applied per row rather than per cartridge. It is the whole of how this
// ROM decides whether it is founding or resuming: the cartridge answers, and no
// press can make it say otherwise.
//
// It is deliberately the *only* place the found-or-resume decision is made. The
// session takes it as an option before it begins, so a screen that asked later
// would be asking about a campaign that had already been founded.
//
// What the rows say, what the footer says and what the buttons do is
// `view::SlotMenu`, which every client that offers this screen renders out of
// the same file, because the requirement is the same screen rather than each
// client's own, and the cheapest way to mean that is one model and a renderer
// per machine. This function is this console's half:
// pixels, and the button letter the model is opened with.
//
// The caret opens on slot one, the row every scripted run takes and the row a
// player who has only ever used one save wants. The two-process persistence
// check therefore reaches the campaign on exactly the presses it always used.
// ---------------------------------------------------------------------------
// The windows a company screen is made of
//
// Three lists and three fixed windows, and the numbers are the screen's rather
// than the content's. A roster is capped at five hundred and twelve by
// `schemas/source/v1/campaign.schema.json` and a store is capped by nothing at
// all, so a screen whose height followed its content would be a screen that
// could run off the bottom of a television.
//
// Seven roster rows is what this screen already drew between its heading at
// y=44 and its store at y=142, at fourteen pixels a row; four store rows is
// what fits between y=158 and the refusal line at y=194 at thirteen. Eight menu
// rows is what a box eighty-eight pixels tall holds below y=40. The three
// numbers belong to the screen rather than to this renderer, so a client that
// draws the same three lists declares the same three.
//
// A list shorter than its window is drawn exactly as it was before there was a
// window, which is why Tarnholt's four-member company is unmoved by any of it.
// ---------------------------------------------------------------------------
inline constexpr int company_roster_rows = 7;
inline constexpr int company_store_rows = 4;
inline constexpr int member_menu_rows = 8;

// How far the caret is kept from a window's edge where the list allows it. One
// row of context: the board's camera asks for two out of eleven, and a
// seven-row window cannot honour two on both sides at once.
inline constexpr int company_scroll_margin = 1;

struct SlotDecision final {
    char slot[view::slot_menu_name_size]{};
    bool resume{false};
};

// The game's own name, in the voice the menu chrome speaks in.
//
// This screen is set entirely in capitals: `CHOOSE A SAVE` under the heading,
// `CONTINUE` and `NEW COMPANY` in the rows, the button footer below them. A
// title dropped in as authored would read as the one thing on the screen that
// had been pasted in rather than typed. It is also the only form the other
// consoles can draw at all: the shared console font in
// `grandleon/view/glyphs.hpp` holds ASCII 0x20 to 0x5F, sixty-four glyphs with
// no lower case in them, and the same title crosses the same shared client to
// the machines that draw with it.
//
// One line, so a title longer than the band is cut with an ellipsis the way the
// title screen's third row is, at the same 34 columns.
std::string slot_screen_heading(const std::string& title) {
    std::string out = ascii_only(title);
    for (char& character : out) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - ('a' - 'A'));
        }
    }
    if (out.size() > title_columns) {
        out.resize(title_columns - 3);
        out += "...";
    }
    return out;
}

[[nodiscard]] SlotDecision campaign_slot_screen(
    const char* base, const bool* holds, const std::string& title
) {
    const std::string heading = slot_screen_heading(title);
    view::SlotMenu menu;
    // `Z` is this machine's name for the button that opens a menu, and the
    // model writes that letter into the sentence rather than a renderer
    // substituting it into a finished one. A substitution hits every occurrence
    // of the letter it replaces, including the `C` of `COMPANY`, which is how a
    // television comes to read `A START A NEW ZOMPANY`.
    menu.open(base, holds, view::slot_menu_rows, 'Z');
    using Answer = view::SlotMenu::Answer;
    Answer answer = Answer::none;
    bool dirty = true;
    while (answer == Answer::none) {
        if (dirty) {
            surface_t* disp = display_get();
            graphics_fill_screen(disp, colour(16, 26, 27));
            draw_logo(disp, 20, 1);
            graphics_set_color(colour(242, 193, 78), 0);
            graphics_draw_text(disp, 20, 96, heading.c_str());
            graphics_set_color(colour(125, 138, 133), 0);
            graphics_draw_text(disp, 20, 112, "CHOOSE A SAVE");
            for (int index = 0; index < menu.rows(); ++index) {
                const bool on = index == menu.caret();
                graphics_set_color(
                    on ? colour(242, 193, 78) : colour(245, 234, 210), 0
                );
                graphics_draw_text(disp, 24, 132 + index * 16, on ? ">" : " ");
                graphics_draw_text(
                    disp, 44, 132 + index * 16, menu.row_label(index)
                );
            }
            // The footer says what the buttons do *here*, because what they do
            // depends on what is under the caret. It is drawn as the model
            // composed it, `Z` and all.
            graphics_set_color(colour(125, 138, 133), 0);
            graphics_draw_text(disp, 20, 206, menu.footer());
            graphics_draw_text(disp, 20, 220, "THE GAME SAVES ITSELF");
            show(disp);
            dirty = false;
        }
        grandleon::n64audio::pump();
        const joypad_buttons_t pressed = poll_buttons();
        if (pressed.d_up) {
            menu.move(-1);
            dirty = true;
        }
        if (pressed.d_down) {
            menu.move(1);
            dirty = true;
        }
        if (pressed.b) {
            menu.cancel();
            dirty = true;
        }
        // Z is the button this machine's action menu already opens with, so a
        // player who has met one of this campaign's screens has met this
        // screen's second verb. On a held row it arms; on an armed row it
        // founds; on an empty row there is nothing to protect and it founds.
        if (pressed.z) {
            answer = menu.over();
            dirty = true;
        }
        if (answer == Answer::none && (pressed.a || pressed.start)) {
            answer = menu.choose();
            dirty = true;
        }
        if (answer != Answer::none) {
            grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
        }
        wait_ms(16);
    }
    SlotDecision decision;
    const char* name = menu.slot_name(menu.caret());
    int at = 0;
    while (name[at] != '\0' &&
           at + 1 < static_cast<int>(sizeof decision.slot)) {
        decision.slot[at] = name[at];
        ++at;
    }
    decision.slot[at] = '\0';
    decision.resume = answer == Answer::resume;
    return decision;
}

// The stack, measured rather than budgeted.
//
// libdragon puts the stack at the top of RDRAM growing down and newlib's heap
// grows up from the end of `.bss`, so there is no fixed allowance to overrun
// into a neighbour. What there is instead is four megabytes and no natural
// bound at all, and this ROM runs `CampaignSession` on top of
// `play_encounter`, which is the deepest call chain it has.
//
// So it paints a window immediately below `main`'s own frame, before anything
// is allocated, and counts back from the far end to the first byte that still
// holds the pattern. Exact to a byte and monotonic. Its one blind spot, a
// frame allocated and never written, is an underestimate rather than a false
// pass. No stack switching and no assembly: there is nothing here to switch to.
inline constexpr std::size_t stack_probe_bytes = 48U * 1024U;
unsigned char* stack_paint_base = nullptr;

void paint_the_stack() {
    unsigned char* here =
        static_cast<unsigned char*>(__builtin_frame_address(0));
    stack_paint_base = here - stack_probe_bytes;
    std::memset(stack_paint_base, 0xA5, stack_probe_bytes);
}

[[nodiscard]] std::size_t stack_high_water() {
    if (stack_paint_base == nullptr) return 0;
    std::size_t untouched = 0;
    while (untouched < stack_probe_bytes &&
           stack_paint_base[untouched] == 0xA5U) {
        ++untouched;
    }
    return stack_probe_bytes - untouched;
}

#endif

#endif

// A campaign build's presenter is also the campaign's narrator. Two interfaces
// rather than one because `grandleon_client` must not learn what a save is, as
// `campaign_session.hpp` says, and one class because the thing holding the
// controller and the thing holding the display are the same thing.
#ifdef GRANDLEON_N64_CAMPAIGN
using PresenterBase = client::CampaignFrontEnd;
#else
using PresenterBase = client::Presenter;
#endif

class N64Presenter final : public PresenterBase {
public:
    // The package's presentation section is what says which archetype and
    // which faction colour each unit type wears, which season its ground is
    // drawn in, and which terrain kind each of its cells draws as, so the
    // renderer holds no game's content identities of its own, and re-derives
    // none of the rules that resolved them either.
    // `package` is borrowed and outlives this presenter. It is held for one
    // question: what did the author call this unit type? That cannot be
    // answered from the presentation section, because presentation carries the
    // joins a renderer needs to *draw* content and deliberately carries no
    // words. See `character_called`.
    N64Presenter(const pr::Presentation& shown, const pf::LoadedPackage& package)
        : theme_(shown.theme < gc::theme_count ? shown.theme
                                               : gc::default_theme),
          shown_(shown),
          package_(&package) {
        bool terrain_ok = true;
        // Only the chosen theme's sheets are loaded: the others are the same
        // art in another climate and no cell can ask for them.
        for (std::size_t i = 0; i < terrain_kind_count; ++i) {
            terrain_sprites_[i] = load_sprite(
                terrain_sheets[theme_][i].data, terrain_sheets[theme_][i].size
            );
            if (terrain_sprites_[i] == nullptr ||
                terrain_sprites_[i]->width != 128 ||
                terrain_sprites_[i]->height != 32) {
                terrain_ok = false;
            }
        }
        expect(
            terrain_ok,
            "the chosen theme's CI4 terrain sheets load (128x32)"
        );
        bool units_ok = true;
        bool frames_ok = true;
        for (std::size_t archetype = 0; archetype < archetype_count;
             ++archetype) {
            for (std::size_t faction = 0; faction < faction_count; ++faction) {
                sprite_t* loaded = load_sprite(
                    character_art[archetype][faction].data,
                    character_art[archetype][faction].size
                );
                unit_sprites_[archetype][faction] = loaded;
                remember_spent_tlut(loaded);
                if (loaded == nullptr || loaded->width != 32 ||
                    loaded->height != 32) {
                    units_ok = false;
                }
                sprite_t* sequence = load_sprite(
                    character_frames[archetype][faction].data,
                    character_frames[archetype][faction].size
                );
                unit_frames_[archetype][faction] = sequence;
                // A strip of exactly the cells the shared model indexes. A
                // sheet of any other width would silently draw a neighbour's
                // pose, so it is asserted rather than trusted.
                if (sequence == nullptr ||
                    sequence->width != 32 * view::sequence_cell_count ||
                    sequence->height != 32) {
                    frames_ok = false;
                }
            }
        }
        expect(
            units_ok,
            "every archetype loads in every faction's colours (32x32 CI4)"
        );
        expect(
            frames_ok,
            "and every one of them carries its animation cells "
            "(128x32 CI4: contact, pass, lunge, cast)"
        );
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
        // The drawings beyond this ROM's own combination, loaded the same way
        // and asserted to the same sizes. Compiled only into a ROM that has
        // some.
        bool extras_ok = true;
        for (std::size_t index = 0; index < extra_drawing_count; ++index) {
            sprite_t* standing = load_sprite(
                extra_drawings[index].standing.data,
                extra_drawings[index].standing.size
            );
            extra_sprites_[index] = standing;
            remember_spent_tlut(standing);
            sprite_t* sequence = load_sprite(
                extra_drawings[index].sequence.data,
                extra_drawings[index].sequence.size
            );
            extra_frames_[index] = sequence;
            if (standing == nullptr || standing->width != 32 ||
                standing->height != 32 || sequence == nullptr ||
                sequence->width != 32 * view::sequence_cell_count ||
                sequence->height != 32) {
                extras_ok = false;
            }
            report_line(
                "DRAWING %s standing=%u sequence=%u\n",
                extra_drawings[index].name,
                static_cast<unsigned>(extra_drawings[index].standing.size),
                static_cast<unsigned>(extra_drawings[index].sequence.size)
            );
        }
        expect(
            extras_ok,
            "every drawing beyond this ROM's own combination loads, standing "
            "and moving"
        );

        // And every one of them is a different picture from the drawing this
        // ROM's own combination holds for the same archetype and colour.
        //
        // Over the whole 32x32, which is where this claim can be made
        // honestly: two figures of one role share most of their texels, so a
        // single sampled pixel cannot carry it. Without this a build that
        // embedded the wrong file, the roster's own sprite under another
        // combination's name, would satisfy every other check here, and the
        // ROM would spend bytes on a drawing nobody could see.
        bool extras_differ = true;
        for (std::size_t index = 0; index < extra_drawing_count; ++index) {
            sprite_t* chosen = extra_sprites_[index];
            sprite_t* own =
                unit_sprites_[extra_drawings[index].archetype]
                             [extra_drawings[index].colour];
            if (chosen == nullptr || own == nullptr) {
                extras_differ = false;
                continue;
            }
            bool differs = false;
            for (int ty = 0; ty < 32 && !differs; ++ty) {
                for (int tx = 0; tx < 32; ++tx) {
                    if (texel16(chosen, tx, ty) != texel16(own, tx, ty)) {
                        differs = true;
                        break;
                    }
                }
            }
            if (!differs) extras_differ = false;
        }
        expect(
            extras_differ,
            "and draws a different picture from the one this ROM's own "
            "combination holds for that archetype and colour"
        );

        // And every drawing the package asks for is one this ROM carries.
        //
        // This is the check that holds the build's resolution to the
        // compiler's. Three of the four keys the build resolved by reading
        // what the compiler wrote; the archetype it had to restate, because
        // the compiler that owns that rule runs on this console rather than in
        // the configure. So a disagreement between the two is a checked
        // failure naming what is missing, rather than a character quietly
        // drawn in the project's own style, which is a failure to refuse
        // rather than hide.
        bool every_drawing_embedded = true;
        for (const pr::UnitTypeCharacterStyle& styled : shown.character_styles) {
            if (!embedded(styled.unit_type_id)) {
                every_drawing_embedded = false;
            }
        }
        for (const pr::UnitTypeCharacterFigure& built :
             shown.character_figures) {
            if (!embedded(built.unit_type_id)) {
                every_drawing_embedded = false;
            }
        }
        expect(
            every_drawing_embedded,
            "every drawing the package asks for is one this ROM embedded"
        );
#endif
        // One shadow for the whole roster: the silhouettes differ above the
        // ankles, not below them, so the generator emits a single sprite and
        // every unit stands on it.
        shadow_sprite_ = load_sprite(sprite_shadow, sprite_shadow_size);
        expect(
            shadow_sprite_ != nullptr && shadow_sprite_->width == 32 &&
                shadow_sprite_->height == 32,
            "the drop shadow loads (32x32 CI4)"
        );
    }

#ifndef GRANDLEON_N64_PROBE
    // The portrait for one line: the character the scene cast for its speaker,
    // drawn exactly as the board draws that character.
    //
    // The four questions are the four `unit_sprite` asks, of the same
    // presentation records, about the same unit type identity: the archetype,
    // the faction colour, the style and the figure. Nothing here re-derives
    // any of them and nothing here reads the speaker's display name. A scene
    // that cast nobody for this line falls back to the keyword convention,
    // which is what a portrait was before a scene could name anybody.
    [[nodiscard]] sprite_t* portrait_for(
        const pr::Dialogue& dialogue,
        const pr::DialogueLine& line
    ) const {
        const std::uint64_t* unit_type = dialogue.speaker_unit_type(line);
        if (unit_type == nullptr) return portrait_by_keyword(line.speaker);

        const std::uint8_t named = shown_.archetype_of_unit_type(*unit_type);
        const std::size_t archetype = named < archetype_count ? named : 0;
        const std::uint8_t colour = shown_.colour_of_unit_type(*unit_type);
        // A character belonging to no faction has no colour in the package on
        // purpose. On a board the client draws it in the column of whichever
        // side it turned up on; a speaker stands on no board, so it takes the
        // first column, which is what an uncast speaker takes too.
        const std::size_t faction = colour < faction_count ? colour : 0U;
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
        const std::size_t slot = extra_slot(*unit_type, archetype, faction);
        if (slot < extra_drawing_count) return portrait_from_extras(slot);
#endif
        return portrait_from_roster(archetype, faction);
    }

    // The same resolution, said out loud. It repeats the four lookups rather
    // than being folded into `portrait_for`, because a claim derived from the
    // sprite that was chosen would be a claim about a pointer; this is a claim
    // about the four answers the package gave, which is what a reader outside
    // the machine wants to check.
    void report_portrait(
        const pr::Dialogue& dialogue,
        const pr::DialogueLine& line
    ) const {
        const std::uint64_t* unit_type = dialogue.speaker_unit_type(line);
        if (unit_type == nullptr) {
            const std::uint8_t named = gc::archetype_index(lowered(line.speaker));
            report_line(
                "face %s uncast %s/%s/%s/%s\n",
                line.speaker.c_str(),
                GRANDLEON_N64_PROJECT_STYLE_NAME,
                GRANDLEON_N64_PROJECT_FIGURE_NAME,
                grandleon_archetype_names[
                    named == gc::archetype_unnamed ? gc::archetype_default
                                                   : named
                ],
                faction_colour_names[0]
            );
            return;
        }
        const std::uint8_t named = shown_.archetype_of_unit_type(*unit_type);
        const std::size_t archetype = named < archetype_count ? named : 0;
        const std::uint8_t colour = shown_.colour_of_unit_type(*unit_type);
        const std::size_t faction = colour < faction_count ? colour : 0U;
        const std::uint8_t named_style =
            shown_.character_style_of_unit_type(*unit_type);
        const std::uint8_t named_figure =
            shown_.character_figure_of_unit_type(*unit_type);
        report_line(
            "face %s cast %s/%s/%s/%s\n",
            line.speaker.c_str(),
            named_style == pr::character_style_unresolved
                ? GRANDLEON_N64_PROJECT_STYLE_NAME
                : grandleon_character_style_names[named_style],
            named_figure == pr::character_figure_unresolved
                ? GRANDLEON_N64_PROJECT_FIGURE_NAME
                : grandleon_character_figure_names[named_figure],
            grandleon_archetype_names[archetype],
            faction_colour_names[faction]
        );
    }
#endif

    // One line per screen, Fire Emblem style: the speaker's portrait and
    // name, the line wrapped inside the safe area, and A to advance.
    //
    // A speech longer than the band is paged rather than truncated, and never
    // drawn past the bottom of it. `dialogue.schema.json` allows a line of
    // 4,096 characters; the band holds eight rows of thirty-four columns, and
    // `tests/nintendo64/screen_text_test.cpp` measures 351 characters as the
    // point at which a row would have been drawn below the last scanline of
    // the framebuffer rather than not drawn at all. That is one ordinary
    // paragraph, where Tarnholt's longest line is 108. The number of pages
    // comes from the band, so the screen cannot be given a row it has no
    // scanline for.
    //
    // The prompt says which of the two the press does. Every line the shipped
    // game authors fits on one page and reads A: NEXT exactly as it did.
    void present_dialogue(
        const grandleon::package_runtime::Dialogue& dialogue
    ) override {
        for (const auto& line : dialogue.lines) {
            report_line("say  %s: %s\n", line.speaker.c_str(),
                        line.text.c_str());
#ifndef GRANDLEON_N64_PROBE
            // What the machine says it is about to draw beside those words.
            //
            // The four keys, in the art library's own spellings, resolved
            // through the package by the same call that picks the sprite, so
            // a portrait resolving to the wrong character says so on the
            // channel rather than only on the screen. A speaker the scene cast
            // nobody for reports the keyword fallback it fell to, which is the
            // state a project written before a cast could exist is in.
            //
            // Once per authored line, beside the `say` above it, rather than
            // once per page: it is a fact about who is speaking, and a line
            // long enough to be paged does not change speaker part-way down.
            report_portrait(dialogue, line);
            const std::vector<std::string> rows =
                wrap_text(ascii_only(line.text), screen::safe_columns);
            const int per_page = screen::dialogue_band.rows();
            const int pages =
                pages_of(static_cast<int>(rows.size()), per_page);
            for (int page = 0; page < pages; ++page) {
                surface_t* disp = display_get();
                draw_scene_backdrop(disp, dialogue.backdrop);
                graphics_draw_box(disp, 22, 42, 68, 68, colour(242, 193, 78));
                graphics_draw_box(disp, 24, 44, 64, 64, colour(31, 42, 46));
                draw_portrait(disp, portrait_for(dialogue, line), 24, 44);
                graphics_set_color(colour(242, 193, 78), 0);
                // Cut to what fits beside the portrait: a `displayName` may be
                // 160 characters, which at eight pixels each is four times the
                // width of this display and would run down the scanlines under
                // it.
                graphics_draw_text(
                    disp, 100, 56,
                    clipped(
                        ascii_only(line.speaker),
                        static_cast<std::size_t>(screen::columns_from(100))
                    ).c_str()
                );
                graphics_set_color(colour(245, 234, 210), 0);
                const screen::Page shown =
                    screen::page_of(rows.size(), per_page, page);
                for (std::size_t index = shown.first; index < shown.last;
                     ++index) {
                    graphics_draw_text(
                        disp, screen::safe_left,
                        screen::dialogue_band.y_of(
                            static_cast<int>(index - shown.first)
                        ),
                        rows[index].c_str()
                    );
                }
                graphics_set_color(colour(125, 138, 133), 0);
                graphics_draw_text(
                    disp, 240, screen::dialogue_prompt_y,
                    page + 1 < pages ? "A: MORE" : "A: NEXT"
                );
                show(disp);
                wait_for_a();
            }
#endif
        }
    }

#ifndef GRANDLEON_N64_PROBE
    // A saying drawn over the board, beside whoever is speaking.
    //
    // The cutscene page is the right surface for a story node, which happens
    // between battles and has the screen to itself. It is the wrong one for a
    // word said during a fight: the board is what the words are about, and
    // replacing it with a portrait hides the thing being talked about at the
    // moment somebody is talking about it.
    //
    // So the board keeps its screen and the words sit on it. Who is speaking is
    // said by *where* the bubble is rather than only by a name: it is drawn
    // against the speaker's own cell, and the camera is brought to them first,
    // which on a board with edges is the difference between a bubble about
    // somebody and a bubble about nobody visible.
    void say_over_board(
        const sim::EncounterSnapshot& snapshot,
        const grandleon::package_runtime::Dialogue& dialogue,
        const grandleon::package_runtime::DialogueLine& line
    ) {

        // Whoever on this board is the character the scene cast for this
        // saying. A scene that cast nobody, or cast somebody who is not
        // standing here, still gets its words: the bubble simply has no cell to
        // sit against and takes the foot of the screen.
        sim::UnitId who = 0;
        sim::Position stands{};
        const std::uint64_t* const cast = dialogue.speaker_unit_type(line);
        if (cast != nullptr) {
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (!sim::on_board(unit)) continue;
                if (unit.unit_type_id != *cast) continue;
                who = unit.id;
                stands = unit.position;
                break;
            }
        }
        if (who != 0) bring_into_view(stands);

        const std::vector<std::string> rows =
            wrap_text(ascii_only(line.text), screen::bubble_columns);
        const int per_page = screen::bubble_rows;
        const int pages = pages_of(static_cast<int>(rows.size()), per_page);
        for (int page = 0; page < pages; ++page) {
            const auto shown = screen::page_of(rows.size(), per_page, page);
            const int lines = static_cast<int>(shown.last - shown.first);
            const int height = 8 * (lines + 1) + 10;
            const int width = screen::bubble_columns * 8 + 10;

            // Against the speaker, and never off the screen. Above them when
            // they are low on the board and below when they are high, so the
            // bubble does not cover the character it is about.
            int left = 8;
            int top = 200 - height;
            if (who != 0) {
                const int cell_x = px(stands.x);
                const int cell_y = py(stands.x, stands.y);
                left = cell_x + tile_ / 2 - width / 2;
                top = cell_y > 120 ? cell_y - height - 4 : cell_y + tile_ + 4;
            }
            if (left < 4) left = 4;
            if (left + width > 316) left = 316 - width;
            if (top < 16) top = 16;
            if (top + height > 232) top = 232 - height;

            // Kept so a checkpoint can assert what was drawn and where: that
            // the bubble is on the screen at all, and that it is not sitting on
            // top of the character it is about.
            said_left_ = left;
            said_top_ = top;
            said_width_ = width;
            said_height_ = height;
            said_about_ = who;
            said_at_x_ = who != 0 ? px(stands.x) + tile_ / 2 : -1;
            said_at_y_ = who != 0 ? py(stands.x, stands.y) + tile_ / 2 : -1;

            surface_t* disp = display_get();
            render(disp, snapshot, 0);
            draw_highlights(disp, snapshot);
            draw_cursor(disp, snapshot);
            graphics_draw_box(disp, left, top, width, height, colour(242, 193, 78));
            graphics_draw_box(
                disp, left + 2, top + 2, width - 4, height - 4, colour(16, 26, 27)
            );
            graphics_set_color(colour(242, 193, 78), 0);
            graphics_draw_text(
                disp, left + 5, top + 5, ascii_only(line.speaker).c_str()
            );
            graphics_set_color(colour(245, 234, 210), 0);
            for (std::size_t index = shown.first; index < shown.last; ++index) {
                graphics_draw_text(
                    disp, left + 5,
                    top + 5 + 8 * static_cast<int>(index - shown.first + 1),
                    rows[index].c_str()
                );
            }
            grandleon::n64audio::pump();
            show(disp);
            wait_for_a();
        }
    }

#endif

    void battle_begins(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        sim::Side player_side,
        const std::vector<std::uint64_t>& terrain
    ) override {
        player_side_ = player_side;
        terrain_ = terrain;
        roster_ = roster;
        weapons_.clear();
        abilities_.clear();
        has_previous_ = false;
        cursor_x_ = 0;
        cursor_y_ = 0;
        selected_ = 0;
        // Nothing carries over a board: a turn being drained and a menu waiting
        // to reopen both belong to the battle that asked for them.
        finishing_ = false;
        drain_last_ = 0;
        reopen_menu_ = 0;
        // Fit the board to a 320x240 frame, leaving room for the status line.
        // A board that fits at a readable size renders whole. A larger board
        // keeps a readable tile and scrolls instead: the camera follows the
        // cursor and clamps at the map edge.
        //
        // The four numbers are this machine's; the rule that reads them is
        // `view::fit_board`, which the PlayStation asks with its own four.
        // Three hundred by two hundred is what the status line leaves, a cell
        // is never larger than 26 nor smaller than 14, and a board too large
        // for that draws at 18.
        constexpr view::FitRule frame{300, 200, 26, 14, 18};
        const view::BoardFit fit =
            view::fit_board(frame, snapshot.width, snapshot.height);
        tile_ = fit.tile;
        camera_ = {0, 0, fit.view_w, fit.view_h,
                   snapshot.width, snapshot.height};
        board_width_ = snapshot.width;
        // High ground is drawn above the row it stands in, so the board needs
        // headroom for its tallest terrain or the first row's peaks would
        // climb into the status line. A board with no raised terrain reserves
        // nothing and lands on exactly the origin the flat renderer used.
        //
        // The reservation is clamped to the slack the frame has, so on a board
        // that already fills it a peak can be drawn above the reservation. It
        // cannot leave the frame: the model bounds a lift at three eighths of
        // a cell, at most nine pixels at the largest cell chosen here, and the
        // board never starts above row 14, so the worst case intrudes into
        // the status line, which is drawn after the board and covers it.
        const int step = view::elevation_step_for(tile_);
        int slack = 216 - tile_ * camera_.view_h;
        if (slack < 0) slack = 0;
        int reserved = view::headroom(highest_elevation(snapshot), step, tile_);
        if (reserved > slack) reserved = slack;
        origin_x_ = (320 - tile_ * camera_.view_w) / 2;
        origin_y_ =
            14 + reserved + (216 - reserved - tile_ * camera_.view_h) / 2;
        projection_ = view::Projection{origin_x_, origin_y_, tile_, step};
    }

    // Kept so the danger zone counts every band a unit could use, not only the
    // weapon in its hand. A snapshot names identities; these resolve them.
    // What this board says while it is being fought, and the join that turns
    // the unit an event names into the placement a moment is about. A
    // placement's identity *is* the unit's identity on the board, so the table
    // is a membership test rather than a translation.
    void battle_moments(
        const std::vector<grandleon::package_runtime::EncounterMoment>& moments,
        const std::vector<grandleon::package_runtime::PlacementIdentity>&
            placements
    ) override {
        moments_ = moments;
        moment_placements_ = placements;
        opening_moments_played_ = false;
    }

    // Plays every moment of one occasion, in the order they were authored.
    // `about` is zero for the board's own.
    void play_moments(
        const sim::EncounterSnapshot& snapshot,
        grandleon::package_runtime::MomentTrigger when, std::uint64_t about
    ) {
#ifdef GRANDLEON_N64_PROBE
        // A probe says nothing over a board, so the board it would have said it
        // over is unread here.
        static_cast<void>(snapshot);
#endif
        if (moments_.empty() || package_ == nullptr) return;
        for (const auto& moment : moments_) {
            if (moment.trigger != when) continue;
            if (when !=
                    grandleon::package_runtime::MomentTrigger::stage_opens &&
                moment.placement_id != about) {
                continue;
            }
            const auto scene = grandleon::package_runtime::load_dialogue(
                *package_, moment.dialogue_id
            );
            // Skipped rather than stopped: the loader already refuses a moment
            // about nobody on the board, and a ROM that halted over a missing
            // line would turn a silent scene into an unplayable board.
            if (!scene) continue;
            // Over the board rather than over a page. A moment is a word said
            // during a fight, and the fight is what it is about; a story node
            // between battles still takes the whole screen, through
            // `present_dialogue` above.
            //
            // The channel still reports the scene the way every other one is
            // reported, so a headless run records that it played and the
            // expectation this run is compared against does not move.
            report_line(
                "say  scene %d lines\n",
                static_cast<int>(scene.dialogue.lines.size())
            );
            for (const auto& line : scene.dialogue.lines) {
                report_line(
                    "say  %s: %s\n", line.speaker.c_str(), line.text.c_str()
                );
#ifndef GRANDLEON_N64_PROBE
                // A probe draws no scene: it has no player to press on, and
                // every surface it photographs is a board rather than a word
                // said over one. The channel still records what was said.
                say_over_board(snapshot, scene.dialogue, line);
#endif
            }
        }
    }

    [[nodiscard]] std::uint64_t placement_of(sim::UnitId unit) const {
        for (const auto& identity : moment_placements_) {
            if (identity.unit_id == unit) return identity.unit_id;
        }
        return 0;
    }

    void battle_definitions(
        const std::vector<sim::WeaponDefinition>& weapons,
        const std::vector<sim::AbilityDefinition>& abilities,
        const std::vector<sim::ItemDefinition>& items,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override {
        weapons_ = weapons;
        abilities_ = abilities;
        items_ = items;
        // How many rounds this board is won by surviving, and zero where
        // nothing on it is. That is every board this ROM has been handed, and
        // is why the status line says exactly what it always said.
        rounds_to_survive_ = grandleon::sheet::rounds_to_survive(objectives);
    }

    // The numbered roster is unnamed here because nothing this console draws
    // needs one. It is the terminal client's addressing scheme (`move 3 5 2`),
    // and a cartridge steers a cursor onto a character instead, so the only
    // thing on this machine that still asks for a number is `report`, writing
    // the transcript the harness reads.
    void draw(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster&
    ) override {
#ifndef GRANDLEON_N64_PROBE
        // The hand-off is announced, not implied: a banner interstitial when
        // the active side changes, and one at battle start.
        if (!has_previous_ || previous_.active_side != snapshot.active_side) {
            phase_banner(snapshot);
        }
#endif
#ifndef GRANDLEON_N64_PROBE
        // A character who is no longer standing on the board is not the
        // player's to hold, whether they fell or were talked away, and neither
        // is one the engine has finished with: its turn closed behind whatever
        // it just did, and so did the other side stepping in. Asked of the
        // engine's own predicate rather than tracked here, so this client can
        // never disagree with the board about who may still be given orders.
        // It is also what lets a walk keep the selection, because a walk does
        // not finish anybody.
        if (selected_ != 0) {
            const sim::UnitSnapshot* still = nullptr;
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (unit.id == selected_) still = &unit;
            }
            if (still == nullptr || !sim::on_board(*still) ||
                still->has_acted || still->side != snapshot.active_side) {
                selected_ = 0;
            }
        }
        // A pick belongs to the selection that made it. This is also the
        // repaint the input loop asks for between presses, so it must not
        // clear a pick that is still being aimed, only one whose character
        // is gone.
        if (selected_ == 0) clear_aim();
        // And the drain the board menu asked for is over the moment the turn
        // it was ending has passed to the other side. Read off the engine's own
        // `active_side` rather than counted here, for the same reason the line
        // above reads the engine's own `has_acted`: a client keeping its own
        // tally of whose turn it is would be a client that could disagree with
        // the board about it.
        if (finishing_ && snapshot.active_side != player_side_) {
            finishing_ = false;
            drain_last_ = 0;
        }
#endif
#ifndef GRANDLEON_N64_PROBE
        // Every board the player is shown starts with the cursor at rest, so
        // the pulse is a property of how long they have been looking at one
        // board rather than of how long the machine has been on.
        pulse_frame_ = 0;
#endif
#ifndef GRANDLEON_N64_PROBE
        // A board too wide for this screen is shown across once, before it is
        // played on, so that a player is told how much board there is rather
        // than finding out by walking into it. It travels from the column that
        // shows the right edge to the column play begins at, which on this
        // machine is the left edge: the cursor opens at the corner and the camera
        // has not been asked to follow it yet.
        //
        // `!has_previous_` is the first drawn frame of a board and no other, the
        // same test the banner above uses, so a board reopened by a jump is
        // revealed again and a board repainted mid-turn is not. A board that fits
        // asks for no frames and this loop does not run.
        //
        // The ground only: no cursor, no highlights, no panel. The cursor is at a
        // corner the camera has not reached, and a reveal is about the board.
        if (!has_previous_) {
            const int settles_at = camera_.x;
            const int opens_at = camera_.rightmost_x();
            const int frames = view::sweep_frames_total(opens_at, settles_at);
            // From zero, and the arrival is the settled frame below rather than
            // the last frame of this loop. Where the camera stands on each of
            // them is `view::sweep_at`, the same rule the shared client's own
            // sweep runs.
            for (int frame = 0; frame < frames; ++frame) {
                camera_.x = view::sweep_at(opens_at, settles_at, frame);
                camera_.clamp();
                surface_t* sweeping = display_get();
                render(sweeping, snapshot, 0);
                grandleon::n64audio::pump();
                show(sweeping);
            }
            camera_.x = settles_at;
            camera_.clamp();
        }
#endif
#ifndef GRANDLEON_N64_PROBE
        // And what this board opens with, after the reveal and before it is
        // played on: a scene before the sweep would be words about ground
        // nobody had been shown.
        if (!has_previous_ && !opening_moments_played_) {
            opening_moments_played_ = true;
            play_moments(
                snapshot,
                grandleon::package_runtime::MomentTrigger::stage_opens, 0
            );
        }
#endif
#ifndef GRANDLEON_N64_PROBE
        // A settled board is always framed on the cursor, whatever the camera
        // was doing while something was being animated. The pan that follows
        // the action leaves the camera wherever the action was, and without
        // this that framing would persist into the next settled frame - and a
        // checkpoint that counts what the window holds would count a different
        // board depending on where somebody had just walked.
        //
        // The shared client gets this from `refresh_queries`, which re-follows
        // before every paint. This is the same guarantee, said once here rather
        // than after each animation.
        camera_.follow(cursor_x_, cursor_y_, 2);
#endif
        surface_t* disp = display_get();
        render(disp, snapshot, 0);
#ifndef GRANDLEON_N64_PROBE
        draw_highlights(disp, snapshot);
        draw_cursor(disp, snapshot);
        draw_info_panel(disp, snapshot);
        grandleon::n64audio::pump();
#endif
        classify(disp, snapshot);
        show(disp);
        previous_ = snapshot;
        has_previous_ = true;
    }

#ifndef GRANDLEON_N64_PROBE
    // The board again, exactly as `draw` paints it, without the parts that are
    // about a *change*: no banner, no aim clearing, no classification, no new
    // pre-event snapshot. This is what a pulse frame costs, and it is why the
    // pulse repaints twice a period rather than sixty times a second.
    void repaint_board(const sim::EncounterSnapshot& snapshot) {
        surface_t* disp = display_get();
        render(disp, snapshot, 0);
        draw_highlights(disp, snapshot);
        draw_cursor(disp, snapshot);
        draw_info_panel(disp, snapshot);
        grandleon::n64audio::pump();
        show(disp);
    }

    // One frame of the board's own periodic presentation, counted rather than
    // timed. The cursor's emphasis and the water's shimmer both read this
    // counter, and the board is repainted only on the frames one of them
    // changes: twice a period for the pulse and four times for the shimmer,
    // six of thirty-two rather than sixty times a second.
    //
    // Sharing one counter is not an economy, it is the settle rule. The
    // checkpoint puts this counter back to zero, and both effects are at rest
    // at zero, so one pin settles the whole board.
    void advance_pulse(const sim::EncounterSnapshot& snapshot) {
        const bool was = view::cursor_emphasised(pulse_frame_);
        const bool shimmered = view::water_cycle_changes(pulse_frame_);
        ++pulse_frame_;
        if (view::cursor_emphasised(pulse_frame_) == was && !shimmered) return;
        repaint_board(snapshot);
    }
#endif

    // What to call a board unit, in the campaign's own words wherever the
    // campaign has any.
    //
    // The join a battle was prepared with is the only thing that knows a
    // numbered unit is a character somebody has been steering for six battles,
    // and it is handed to this front end once, at `board_prepared`. A unit no
    // member stands in (a bandit, a summon, the opposing side) falls through
    // to its class, which is the only thing anybody knows about it; and a unit
    // the pre-event board no longer holds falls through to a word, because the
    // alternative is printing a 64-bit identity at a player, and this ROM's
    // report channel is read on a machine with no 64-bit arithmetic anyway.
    //
    // A build with no campaign has no roster of characters at all and always
    // takes the class, which is right: nothing in a one-off skirmish has a name
    // to lose.
    //
    // The name comes back as the shared resolver folds it: the author's own
    // word out of the package, upper-cased and ASCII-folded, because that is
    // the only character set the font every console here shares can draw. A
    // roster member keeps their campaign name, which is a person's and comes
    // from somewhere else entirely.
    // The campaign's own name for whoever stands in a board unit, or null when
    // nobody does, which is every unit on a board played outside a campaign,
    // and the whole of the opposing side on one played inside it.
    //
    // The join the session published, which is the one piece of naming only a
    // client can answer. Everything done with the answer, and everything done
    // when there is none, belongs to `sheet::character_name`, shared with the
    // other consoles and the terminal so that three machines cannot name one
    // character three things.
    //
    // Borrowed from the roster entry, which outlives every frame drawn against
    // it: the board is prepared once and handed to this presenter for the
    // battle.
    [[nodiscard]] const char* campaign_name_of(sim::UnitId unit) const {
#ifdef GRANDLEON_N64_CAMPAIGN
        if (board_ != nullptr) {
            return client::member_name_on_board(
                board_->binding, board_->roster, unit
            );
        }
#else
        static_cast<void>(unit);
#endif
        return nullptr;
    }

    [[nodiscard]] std::string character_called(sim::UnitId unit) const {
        return grandleon::sheet::character_name(
                   package_, previous_, unit, campaign_name_of(unit)
        ).c_str();
    }

    // What this board's campaign calls a character going down. A death under the
    // permanent rule and a fall under the recoverable one: the same event, two
    // vocabularies, and which one is in force is a fact about the campaign and
    // not a taste of this console's. A build with no campaign has nobody to
    // return anybody to, so it says the plain thing.
    [[nodiscard]] const char* fall_word(sim::UnitId unit) const noexcept {
#ifdef GRANDLEON_N64_CAMPAIGN
        // The softer word belongs to the company and to nobody else. A campaign
        // that carries its own people off the field does not carry the bandit
        // off with them, and a screen that said the bandit fell would be
        // promising a player they were going to meet him again.
        if (board_ != nullptr &&
            board_->character_loss == pr::CharacterLoss::recoverable &&
            board_->binding
                    .persistent_of(campaign::BattleEntityId{unit})
                    .value != 0U) {
            return "FELL";
        }
#endif
        static_cast<void>(unit);
        return "DEFEATED";
    }

    void report(
        const sim::CommandResult& result,
        const client::Roster& roster
    ) override {
        for (const sim::Event& event : result.events) {
            if (event.type == sim::EventType::unit_damaged) {
                report_line(
                    "hit  %s for %d\n",
                    roster.label(event.unit_id).c_str(),
                    static_cast<int>(event.amount)
                );
            }
            if (event.type == sim::EventType::attack_missed) {
                report_line(
                    "miss %s\n", roster.label(event.unit_id).c_str()
                );
            }
            if (event.type == sim::EventType::unit_defeated) {
                // Named by the campaign rather than by the board, which is the
                // one line here that wants a different name from the others: a
                // hit and a miss are things that happened to a token, and a
                // death is something that happened to a character. A headless
                // run (the probe, the autopilot, anything reading this channel
                // out of an emulator log) records who it was for the same
                // reason the screen says it.
                report_line(
                    "fall %s\n", character_called(event.unit_id).c_str()
                );
                play_moments(
                    previous_,
                    grandleon::package_runtime::MomentTrigger::character_falls,
                    placement_of(event.unit_id)
                );
            }
            if (event.type == sim::EventType::unit_talked) {
                // Somebody walked off the board alive, and whatever this board
                // has to say about it. Deliberately not spelled like a defeat:
                // leaving and falling are two facts the engine reports with two
                // events, and a channel that called them one thing would undo
                // the distinction every rule underneath it keeps.
                report_line(
                    "left %s\n", character_called(event.unit_id).c_str()
                );
                play_moments(
                    previous_,
                    grandleon::package_runtime::MomentTrigger::character_talked,
                    placement_of(event.unit_id)
                );
            }
            if (event.type == sim::EventType::unit_endured) {
                // And the blow that did not do it. On the channel for the same
                // reason it is on the screen: a headless run reading this log
                // would otherwise see a character struck for more than they had
                // and still standing on the next line, with nothing between the
                // two saying why.
                report_line(
                    "hold %s\n", character_called(event.unit_id).c_str()
                );
            }
            if (event.type == sim::EventType::item_dropped) {
                // Named by the fallen rather than by the thing: a drop enters
                // nobody's pack, so there is no satchel on this console to look
                // an item name up in. What fell is the campaign's to record.
                report_line(
                    "drop %s\n", roster.label(event.unit_id).c_str()
                );
            }
#ifndef GRANDLEON_N64_PROBE
            animate(event);
#endif
        }
    }

    // On a console the debug channel does not exist; a refusal the player
    // cannot see is a button that appears to do nothing. Name the reason on
    // screen for a moment, then restore the board.
    void refused(sim::CommandError error) override {
        report_line("refused %s\n", sim::error_name(error).data());
#ifndef GRANDLEON_N64_PROBE
        if (!has_previous_) return;
        // The engine's word, and only the engine's word.
        // `target_out_of_range` covers both too close and too far, and telling
        // them apart here would mean measuring the distance and comparing it
        // against the actor's reach, which is the engine's range rule
        // restated on a console where nothing could notice it drifting. The
        // day the distinction is worth drawing, the engine is where it should
        // be drawn, and every client gets it at once.
        const char* text = refusal_text(error);
        // Two of these are facts about the character the player picked rather
        // than about the battle, and under a side block picking is the whole of
        // what a player does. So they say who: a banner reading
        // "MIREA HAS ALREADY ACTED" is a sentence about somebody, where
        // "THEY HAVE ALREADY ACTED" leaves a player holding a cursor over a
        // line of characters wondering which one it meant.
        std::string named;
        if (commanded_ != 0 &&
            (error == sim::CommandError::already_acted ||
             error == sim::CommandError::already_moved)) {
            named = ascii_only(character_called(commanded_));
            named += error == sim::CommandError::already_acted
                         ? " HAS ALREADY ACTED"
                         : " HAS ALREADY MOVED";
            // The banner is centred on a 320-pixel display and its box is
            // sixteen pixels wider than the text, so a name long enough to
            // push the sentence past thirty-seven characters would draw its
            // frame off the left edge. Trimmed rather than allowed to: an
            // author's roster names are theirs to choose, and a refusal that
            // wrecks the screen is worse than one that reads short.
            if (named.size() > 37U) named.resize(37U);
            text = named.c_str();
        }
        if (text[0] == '\0') return;
        const int width = static_cast<int>(std::strlen(text)) * 8 + 16;
        const int left = (320 - width) / 2;
        surface_t* disp = display_get();
        render(disp, previous_, 0);
        graphics_draw_box(disp, left - 2, 102, width + 4, 24,
                          colour(179, 72, 63));
        graphics_draw_box(disp, left, 104, width, 20, colour(16, 26, 27));
        graphics_set_color(colour(245, 234, 210), 0);
        graphics_draw_text(disp, left + 8, 110, text);
        show(disp);
        int hold = 75;
#ifdef GRANDLEON_N64_AUTOPILOT
        // Every refusal is a checkpoint: the banner is asserted on screen
        // and held for the harness's photograph.
        report_line(
            "CHECKPOINT refusal-%s\n", sim::error_name(error).data()
        );
        expect(
            surface_pixel(disp, 160, 103) == pack16(179, 72, 63),
            "the refusal banner frames its message"
        );
        hold = 150;
#endif
        for (int i = 0; i < hold; ++i) {
            grandleon::n64audio::pump();
            wait_ms(16);
        }
        surface_t* settled = display_get();
        render(settled, previous_, 0);
        draw_cursor(settled, previous_);
        draw_info_panel(settled, previous_);
        show(settled);
#endif
    }

    void show_state(
        const sim::EncounterSnapshot&,
        std::uint64_t canonical_hash,
        const std::vector<sim::ObjectiveDefinition>&
    ) override {
        report_line(
            "state hash=%016llx\n",
            static_cast<unsigned long long>(canonical_hash)
        );
    }

    void battle_ended(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash
    ) override {
        report_line(
            "battle %s hash=%016llx\n",
            snapshot.outcome == sim::Outcome::first_side_won ? "blue" : "red",
            static_cast<unsigned long long>(canonical_hash)
        );
#ifndef GRANDLEON_N64_PROBE
        // Won by the player, not won by the first side. The fanfare and the
        // words both hang on this: comparing the outcome against
        // `first_side_won` alone plays victory at a player who just lost, on
        // any board where they hold the second side.
        const bool player_won =
            (snapshot.outcome == sim::Outcome::first_side_won) ==
            (player_side_ == sim::Side::first);
        grandleon::n64audio::play(
            player_won ? grandleon::n64audio::Sfx::victory
                       : grandleon::n64audio::Sfx::defeat_battle
        );
        surface_t* disp = display_get();
        graphics_fill_screen(disp, colour(16, 26, 27));
        graphics_set_color(colour(245, 234, 210), 0);
        graphics_draw_text(
            disp, 100, 110, player_won ? "YOUR SIDE WINS" : "THE ENEMY WINS"
        );
        graphics_draw_text(disp, 100, 130, "PRESS A");
        show(disp);
        wait_for_a();
#endif
    }

    void campaign_ended() override {
        report_line("campaign complete\n");
#ifndef GRANDLEON_N64_PROBE
        grandleon::n64audio::play(grandleon::n64audio::Sfx::victory);
        surface_t* disp = display_get();
        graphics_fill_screen(disp, colour(16, 26, 27));
        draw_logo(disp, 22, 2);
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 132, 172, "THE END");
        graphics_set_color(colour(245, 234, 210), 0);
        graphics_draw_text(disp, 92, 196, "THANKS FOR PLAYING");
        graphics_set_color(colour(125, 138, 133), 0);
        graphics_draw_text(disp, 52, 216, "github.com/lfelipe/grandleon");
        show(disp);
        wait_for_a();
#endif
    }

    client::Intent next_intent(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override {
#ifdef GRANDLEON_N64_PROBE
        static_cast<void>(roster);
        const client::Intent intent = probe_intent(snapshot);
#else
        const client::Intent intent = controller_intent(snapshot, roster);
#endif
        // Whose command this is, kept so a refusal can name them. The engine
        // answers with an error and nothing else, and two of its answers are
        // facts about a character rather than about the battle.
        commanded_ = intent.unit_id;
        return intent;
    }

#ifdef GRANDLEON_N64_CAMPAIGN
    // -----------------------------------------------------------------------
    // The campaign the player keeps.
    //
    // Everything below is `client::CampaignNarrator`. Not one number on any of
    // these screens is worked out here: the roster, the kits, the store, the
    // levels and every consequence of a battle arrive from the session exactly
    // as `campaign_runtime` derived them, and this file arranges them into
    // pixels. That is the same division the board already keeps, and it is why
    // the terminal, the browser and this console can never disagree about what
    // a campaign holds.
    // -----------------------------------------------------------------------

    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override {
        // The line the persistence check keys on. Which of the two words this
        // is comes from what the cartridge held and from nothing else, which is
        // exactly the property under test: a cartridge that forgot would say
        // FOUNDED twice and the check would fail on the word.
        report_line(
            "CAMPAIGN %s slot=%.*s roster=%d store=%d\n",
            resumed ? "RESUMED" : "FOUNDED",
            static_cast<int>(slot.size()), slot.data(),
            static_cast<int>(roster.size()), static_cast<int>(store.size())
        );
        for (const client::RosterEntry& entry : roster) {
            report_line(
                "  member %llu %s L%u av=%d kit=%d\n",
                static_cast<unsigned long long>(entry.member.value),
                entry.name.c_str(),
                static_cast<unsigned>(entry.progression.level),
                static_cast<int>(entry.availability),
                static_cast<int>(entry.carried.size())
            );
        }
        for (const campaign::InventoryStack& stack : store) {
            report_line(
                "  store %s x%u\n",
                grandleon::sheet::item_name(stack.item.stable_id),
                static_cast<unsigned>(stack.quantity)
            );
        }
#ifdef GRANDLEON_N64_AUTOPILOT
        check_the_company(roster, store, resumed);
#endif
        draw_company_sheet(roster, store, resumed);
        wait_for_a();
    }

    void slot_refused(const client::SlotFailure& failure) override {
        report_line(
            "slot refused storage=%d migration=%d save=%d state=%d wrong=%d\n",
            static_cast<int>(failure.storage),
            static_cast<int>(failure.migration),
            static_cast<int>(failure.save),
            static_cast<int>(failure.state),
            failure.wrong_campaign ? 1 : 0
        );
        // A refusal is somebody else's vocabulary and is shown as such: the
        // layer that said no, and its own word for it. Nothing is invented here
        // and nothing is swallowed: the freshly founded campaign is what the
        // session goes on to play, and the player is told why.
        surface_t* disp = display_get();
        graphics_fill_screen(disp, colour(16, 26, 27));
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 76, 60, "THE SAVED CAMPAIGN");
        graphics_draw_text(disp, 92, 76, "WAS NOT TAKEN");
        graphics_set_color(colour(245, 234, 210), 0);
        char line[40];
        std::snprintf(
            line, sizeof line, "%s",
            failure.wrong_campaign ? "IT BELONGS TO ANOTHER STORY"
                                   : refusal_reason(failure)
        );
        graphics_draw_text(disp, 24, 112, line);
        graphics_draw_text(disp, 24, 132, "A NEW COMPANY TAKES THE FIELD");
        graphics_set_color(colour(125, 138, 133), 0);
        graphics_draw_text(disp, 240, 210, "A: NEXT");
        show(disp);
        wait_for_a();
    }

    void board_prepared(const client::CampaignBoard& board) override {
        // Borrowed for exactly the battle it describes, so that a character who
        // dies during that battle can be named at the moment it happens. The
        // board holds both halves of the answer: the roster, and the join
        // saying which numbered unit each member is standing in. This is the
        // only moment the front end is handed them together.
        //
        // A raw pointer is safe here and only here: `run_persistent_campaign`
        // holds the prepared board across the fight it prepared it for, so the
        // window this is readable in is the window it is valid in. It is
        // dropped again in `battle_aftermath`, which is the far end of that
        // window, rather than left to be overwritten by the next battle's.
        board_ = &board;
        report_line(
            "board node=%llu encounter=%llu roster=%d excluded=%d\n",
            static_cast<unsigned long long>(board.node.stable_id),
            static_cast<unsigned long long>(board.encounter_id),
            static_cast<int>(board.roster.size()),
            static_cast<int>(board.excluded.size())
        );
    }

    // What the battle did to the company, as the campaign committed it. Every
    // line is read out of `campaign_runtime::BattleProgression` and out of the
    // roster the commit left behind; nothing here adds anything up.
    void battle_aftermath(const client::BattleAftermath& aftermath) override {
        // The battle the borrowed board described is over, so the borrow ends
        // here rather than at the next `board_prepared`. Everything below reads
        // the aftermath's own roster, which is the company as the commit left
        // it and is the right list to read after a battle anyway.
        board_ = nullptr;
        report_line(
            "aftermath outcome=%d fallen=%d joined=%d levels=%d moved=%d\n",
            static_cast<int>(aftermath.outcome),
            static_cast<int>(aftermath.fallen.size()),
            static_cast<int>(aftermath.recruited.size()),
            static_cast<int>(aftermath.progression.level_ups.size()),
            aftermath.completion.advanced ? 1 : 0
        );
        surface_t* disp = display_get();
        graphics_fill_screen(disp, colour(16, 26, 27));
        graphics_draw_box(disp, 12, 12, 320 - 24, 240 - 24, colour(242, 193, 78));
        graphics_draw_box(disp, 14, 14, 320 - 28, 240 - 28, colour(16, 26, 27));
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 24, 26, "AFTER THE BATTLE");
        graphics_set_color(colour(245, 234, 210), 0);
        int y = 48;
        char line[40];
        // Who is gone, by name, before anything else on the screen. The list
        // has been on `BattleAftermath::fallen` since there was an aftermath
        // and this screen printed its `.size()` and nothing else, which left a
        // player to learn from the digit `1` that somebody they had been
        // steering for six battles was dead.
        //
        // Before the roster rather than after them because this screen is
        // height-limited: the roster stops at scanline 170 and the store at
        // 208, so whatever is last is what a full company pushes off the
        // bottom. The names are the one thing on here that must not be the
        // thing that goes, and the roster listing, which the management screen
        // shows again in full between every pair of battles, is.
        //
        // Drawn in the same red the dead roster rows below are drawn in, so the
        // two halves of the same fact look like one fact.
        //
        // And in the rule's own words. Under the permanent rule these people
        // are dead and the company buries them; under the recoverable rule they
        // were carried off and they are back, which is a very different screen
        // to read and must not be worded like the first one.
        const bool buried =
            aftermath.character_loss == pr::CharacterLoss::permanent;
        if (!aftermath.fallen.empty()) {
            graphics_draw_text(
                disp, 24, y,
                buried ? "THE COMPANY BURIES" : "CARRIED OFF, AND BACK"
            );
            y += 13;
            graphics_set_color(colour(196, 84, 74), 0);
            // The band a name may stand in. A company larger than the seven
            // names that fit could in principle lose more of itself in one
            // battle than this screen has room for, and a name silently
            // dropped off the bottom is the exact failure this block exists to
            // fix, so the overflow is counted out loud on the line the eighth
            // name would have taken. The company screen names its own overflow
            // for the same reason.
            std::size_t named = 0;
            for (const campaign::PersistentEntityId member : aftermath.fallen) {
                const std::size_t unnamed = aftermath.fallen.size() - named;
                if (!screen::aftermath_fallen_band.holds(y) && unnamed > 1U) {
                    std::snprintf(
                        line, sizeof line, "  AND %u MORE",
                        static_cast<unsigned>(unnamed % 100U)
                    );
                    graphics_draw_text(disp, 24, y, line);
                    y += 13;
                    break;
                }
                std::snprintf(
                    line, sizeof line, "  %.28s",
                    member_name(aftermath.roster, member).c_str()
                );
                graphics_draw_text(disp, 24, y, line);
                y += 13;
                ++named;
            }
            // A gap, so the buried and the living are not read as one list.
            y += 6;
            graphics_set_color(colour(245, 234, 210), 0);
        }
        for (const client::RosterEntry& entry : aftermath.roster) {
            if (!screen::aftermath_roster_band.holds(y)) break;
            std::snprintf(
                line, sizeof line, "%-14.14s L%-2u XP%-5u %s",
                grandleon::sheet::person_name(entry.name.c_str()).c_str(),
                static_cast<unsigned>(entry.progression.level),
                static_cast<unsigned>(entry.progression.experience),
                availability_word(entry.availability)
            );
            graphics_set_color(
                entry.availability == campaign::Availability::dead
                    ? colour(196, 84, 74)
                    : colour(245, 234, 210),
                0
            );
            graphics_draw_text(disp, 24, y, line);
            y += 13;
        }
        graphics_set_color(colour(242, 193, 78), 0);
        // LOST rather than DEFEATED, and the choice is worth stating because
        // the battle line says DEFEATED for the very same event.
        //
        // This is the *company's* summary and it sits between LEVELS and
        // JOINED, two numbers about our own people. `DEFEATED 2` in that row
        // reads as two enemies beaten, which is the opposite of what it counts.
        // `LOST 2` cannot be read that way, and it is the word the roster rows
        // above use for exactly these members. Four letters, so the row is the
        // width it was.
        //
        // Under the recoverable rule it stays FELL, which is the same fact said
        // softly: down, and coming back.
        std::snprintf(
            line, sizeof line,
            buried ? "LEVELS %u   LOST %u   JOINED %u"
                   : "LEVELS %u   FELL %u   JOINED %u",
            static_cast<unsigned>(aftermath.progression.level_ups.size() % 100U),
            static_cast<unsigned>(aftermath.fallen.size() % 100U),
            static_cast<unsigned>(aftermath.recruited.size() % 100U)
        );
        graphics_draw_text(disp, 24, 182, line);
        graphics_set_color(colour(245, 234, 210), 0);
        y = screen::aftermath_store_band.top;
        for (const campaign::InventoryStack& stack : aftermath.store) {
            if (!screen::aftermath_store_band.holds(y)) break;
            std::snprintf(
                line, sizeof line, "STORE %-16.16s x%u",
                grandleon::sheet::item_name(stack.item.stable_id),
                static_cast<unsigned>(stack.quantity)
            );
            graphics_draw_text(disp, 24, y, line);
            y += 12;
        }
        graphics_set_color(colour(125, 138, 133), 0);
        graphics_draw_text(disp, 240, 216, "A: NEXT");
        show(disp);
        wait_for_a();
    }

    // Who joined, by name.
    //
    // Bounded at both ends, the way every other list on this console is.
    // `campaign.schema.json` caps a node's `recruits` at 64 and a
    // `displayName` at 160 characters, and neither number is one this screen
    // has room for: the twelfth name would start below the last scanline of
    // the framebuffer, and a full-length name at eight pixels a character
    // would run four screens wide. libdragon draws both of those, into
    // whatever memory the arithmetic lands in.
    //
    // So the band says how many rows there are and the overflow is counted out
    // loud on the row the last name would have taken, exactly as the aftermath
    // names its own. Tarnholt's largest intake is two, so the shipped game
    // draws the screen it always drew.
    void members_joined(
        const std::vector<client::RosterEntry>& joined
    ) override {
        if (joined.empty()) return;
        report_line("joined %d\n", static_cast<int>(joined.size()));
        surface_t* disp = display_get();
        graphics_fill_screen(disp, colour(16, 26, 27));
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 92, 60, "JOINS THE COMPANY");
        graphics_set_color(colour(245, 234, 210), 0);
        char line[64];
        const int arrivals = static_cast<int>(joined.size());
        const int named =
            screen::named_of(arrivals, screen::joined_band.rows());
        for (int index = 0; index < named; ++index) {
            graphics_draw_text(
                disp, screen::joined_left, screen::joined_band.y_of(index),
                clipped(
                    grandleon::sheet::person_name(
                        joined[static_cast<std::size_t>(index)].name.c_str()
                    ).c_str(),
                    screen::joined_columns
                ).c_str()
            );
        }
        if (named < arrivals) {
            std::snprintf(
                line, sizeof line, "AND %u MORE",
                static_cast<unsigned>((arrivals - named) % 100)
            );
            graphics_draw_text(
                disp, screen::joined_left, screen::joined_band.y_of(named),
                line
            );
        }
        graphics_set_color(colour(125, 138, 133), 0);
        graphics_draw_text(disp, 240, screen::joined_prompt_y, "A: NEXT");
        show(disp);
        wait_for_a();
    }

    void campaign_saved(
        std::string_view slot,
        storage::StorageError error
    ) override {
        report_line(
            "saved slot=%.*s error=%.*s\n",
            static_cast<int>(slot.size()), slot.data(),
            static_cast<int>(storage::storage_error_name(error).size()),
            storage::storage_error_name(error).data()
        );
        saved_ = error == storage::StorageError::none;
        // A toast on the next company redraw rather than a screen of its own.
        // Saving happens between every gesture, and a screen per save would put
        // a press between a player and every single thing they do.
        save_toast_ = 1;
#ifdef GRANDLEON_N64_AUTOPILOT
        expect(saved_, "the cartridge took the save");
#endif
    }

    void management_opened(const client::CompanyManagement& company) override {
        report_line(
            "manage node=%llu roster=%d store=%d placeable=%d refused=%d\n",
            static_cast<unsigned long long>(company.node.stable_id),
            static_cast<int>(company.roster.size()),
            static_cast<int>(company.store.size()),
            static_cast<int>(company.placeable.size()),
            static_cast<int>(company.refused)
        );
        manage_row_ = 0;
        roster_top_ = 0;
#ifdef GRANDLEON_N64_AUTOPILOT
        expect(
            static_cast<int>(company.placeable.size()) ==
                grandleon::tarnholt::fordlight_placeable,
            "the board the company stands before places every member"
        );
#endif
    }

    void management_committed(const client::ManagementCommit& result) override {
        report_line(
            "gesture error=%d outcome=%d saved=%d\n",
            static_cast<int>(result.error),
            static_cast<int>(result.application.error),
            result.saved ? 1 : 0
        );
#ifdef GRANDLEON_N64_AUTOPILOT
        expect(
            static_cast<bool>(result),
            "the management gesture the script pressed committed"
        );
        expect(result.saved, "and wrote the slot as it committed");
#endif
        if (!result) {
            // The campaign's own name for the refusal, on screen for a beat, in
            // the same box a refused command says its reason in. The enum stays
            // authoritative; this is presentation only.
            campaign_refusal_banner(
                campaign::outcome_error_name(result.application.error)
            );
        }
    }

    // A jump, taken or refused.
    //
    // Two things happen here and both are obligations rather than reporting.
    // The borrowed board is dropped, because a jump ends the board it came out
    // of without an aftermath and this is the far end of that window: leaving
    // the pointer set would leave it dangling across every screen between here
    // and the next board. And a refusal gets the banner a refused gesture gets,
    // in the campaign's own word, because nothing else happens after a refused
    // jump — the battle is gone and the campaign stands where it stood, and
    // with nothing said that reads as the cartridge having lost the game.
    void stage_jumped(const client::StageJump& jump) override {
        board_ = nullptr;
        report_line(
            "jumped node=%llu moved=%d error=%d saved=%d\n",
            static_cast<unsigned long long>(jump.target.stable_id),
            jump.completion.advanced ? 1 : 0,
            static_cast<int>(jump.completion.error),
            jump.saved ? 1 : 0
        );
        if (static_cast<bool>(jump)) return;
        // Whichever layer refused it, in that layer's own word. The campaign
        // has one for every way a move through a graph can fail; the session
        // has its own for the one refusal that is not the campaign's, which is
        // a Stage this game does not offer.
        campaign_refusal_banner(
            jump.completion.error != campaign::ProgressionError::none
                ? campaign::progression_error_name(jump.completion.error)
                : client::campaign_session_error_name(jump.error)
        );
    }

    // The company, between battles. One screen, one caret, and a menu per
    // member holding every verb the stage has. That is the same shape the unit
    // action menu already taught: nothing is aimed at, the rows say what taking
    // them costs, and B backs out one step.
    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        const int rows = static_cast<int>(company.roster.size());
        if (rows == 0) return {client::ManagementVerb::quit, {}, {}};
        if (manage_row_ >= rows) manage_row_ = rows - 1;
        if (manage_row_ < 0) manage_row_ = 0;

        // The window follows the caret, so a member past the seventh row is on
        // the screen at the moment the caret stands on them. `follow` clamps to
        // the list at both ends, and the same call puts a window that outlived
        // a shrinking roster back inside it.
        const auto follow_the_caret = [&]() {
            view::ListWindow members;
            members.rows = company_roster_rows;
            members.total = rows;
            members.top = roster_top_;
            members.follow(manage_row_, company_scroll_margin);
            roster_top_ = members.top;
        };
        follow_the_caret();

        bool dirty = true;
        while (true) {
            if (dirty) {
                surface_t* disp = display_get();
                draw_company(disp, company, manage_row_, -1);
                show(disp);
                dirty = false;
            }
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            if (pressed.d_up && manage_row_ > 0) {
                --manage_row_;
                follow_the_caret();
                dirty = true;
            }
            if (pressed.d_down && manage_row_ + 1 < rows) {
                ++manage_row_;
                follow_the_caret();
                dirty = true;
            }
            if (pressed.start) {
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                return {client::ManagementVerb::proceed, {}, {}};
            }
            if (pressed.b) {
                // Leaving loses nothing and the footer says so: every gesture
                // committed and saved when it was made, so there is no state
                // here for a confirmation screen to protect.
                return {client::ManagementVerb::quit, {}, {}};
            }
            // Z is this machine's name for the button that opens a menu, which
            // is what the slot screen already taught and what the unit action
            // menu uses. The footer names it whenever it does anything.
            //
            // **The picker has to be reachable from here, and that is not a
            // convenience.** A jump recruits nobody on behalf of the Stages it
            // passed over, so a board whose objective names a late-joining
            // character refuses to open — and a refused board is exactly what
            // sends a player to this screen. There is nothing they could do here
            // to recruit anybody, and the jump has already written the
            // cartridge. Without this the aid could leave a save standing at a
            // Stage nothing can open.
            if (pressed.z && !company.stages.empty()) {
                const std::uint64_t going = run_stage_picker(company.stages);
                if (going != 0U) {
                    client::ManagementIntent jump;
                    jump.verb = client::ManagementVerb::jump;
                    jump.stage = going;
                    return jump;
                }
                dirty = true;
            }
            if (pressed.a) {
                client::ManagementIntent chosen;
                if (run_member_menu(company, manage_row_, chosen)) {
                    return chosen;
                }
                dirty = true;
            }
            wait_ms(16);
        }
    }
#endif

    // The board opens arranged rather than fought, where the content authors a
    // region. Kept so the tiles under the marker can be lit without reading
    // them out of every snapshot, exactly as the terrain is.
    void deployment_begins(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        const std::vector<sim::Position>& zone
    ) override {
        static_cast<void>(roster);
        static_cast<void>(zone);
        selected_ = 0;
#ifdef GRANDLEON_N64_PROBE
        static_cast<void>(snapshot);
#endif
#ifndef GRANDLEON_N64_PROBE
        // Said out loud, because this phase is not the battle and does not
        // behave like it: A picks somebody up and puts them down, the lit tiles
        // are where they may stand rather than where they may walk, and there
        // is no action menu because there are no actions yet. A player meeting
        // that unannounced reads it as a board that will not respond.
        phase_words(
            snapshot, "ARRANGE YOUR COMPANY", "A MOVE THEM   START BEGIN"
        );
#endif
    }

    // The same thumb, a different phase: A picks a character up and puts it
    // down, B puts it back down where it stands, Start opens the battle. Start
    // is the button that ends an activation once one has begun, and it ends the
    // arranging for the same reason. Z opens nothing, because there is no
    // action menu before there are actions.
    //
    // A separate hook rather than a mode flag on `next_intent`, so a menu that
    // cannot be taken is never offered.
    client::Intent next_deployment_intent(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) override {
#ifdef GRANDLEON_N64_PROBE
        static_cast<void>(snapshot);
        static_cast<void>(roster);
        client::Intent intent;
        intent.kind = client::IntentKind::begin_battle;
        return intent;
#else
        while (true) {
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            bool moved = false;
            if (pressed.d_up && cursor_y_ > 0) { --cursor_y_; moved = true; }
            if (pressed.d_down && cursor_y_ + 1 < snapshot.height) {
                ++cursor_y_;
                moved = true;
            }
            if (pressed.d_left && cursor_x_ > 0) { --cursor_x_; moved = true; }
            if (pressed.d_right && cursor_x_ + 1 < snapshot.width) {
                ++cursor_x_;
                moved = true;
            }
            if (pressed.b && selected_ != 0) {
                selected_ = 0;
                moved = true;
            }
            if (pressed.start) {
                selected_ = 0;
                client::Intent intent;
                intent.kind = client::IntentKind::begin_battle;
                return intent;
            }
            if (pressed.a) {
                const sim::UnitSnapshot* occupant = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (sim::on_board(unit) &&
                        unit.position.x == cursor_x_ &&
                        unit.position.y == cursor_y_) {
                        occupant = &unit;
                    }
                }
                if (occupant != nullptr &&
                    sim::is_deployable(snapshot, *occupant) &&
                    occupant->side == player_side_) {
                    grandleon::n64audio::play(
                        grandleon::n64audio::Sfx::select
                    );
                    selected_ = occupant->id;
                    moved = true;
                } else if (selected_ != 0 && occupant == nullptr) {
                    client::Intent intent;
                    intent.kind = client::IntentKind::deploy_to;
                    intent.unit_id = selected_;
                    intent.destination = {cursor_x_, cursor_y_};
                    return intent;
                }
            }
            if (moved) {
                camera_.follow(cursor_x_, cursor_y_, 2);
                draw(snapshot, roster);
            } else {
                advance_pulse(snapshot);
            }
            wait_ms(16);
        }
#endif
    }

    // Framebuffer classification, filled in by the most recent draw().
    struct Classified final {
        int grass{};
        int water{};
        int road{};
        int forest{};
        int other_terrain{};
        int blue{};
        int red{};
        int unknown{};
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
        // Cells whose sampled pixel matched a drawing beyond this ROM's own
        // combination; of those, the ones whose chosen drawing and roster
        // drawing can be told apart *at the texels this census samples*; and
        // of those, the ones that showed the chosen drawing's texel and not
        // the roster's.
        //
        // The middle number is why there are three. A check that only asked
        // "did the pixel match the sprite we chose" would pass just as well on
        // a ROM that ignored the choice, because two figures of one role are
        // the same character in the same colours: `mage_blue` and
        // `mage_blue_second` differ in 77 of their 1,024 texels and share
        // three of the four the sampler looks at. So the census counts what it
        // is entitled to speak about and asserts all of that. The claim that
        // the drawing itself is a different picture is made where it can be
        // made honestly, over the whole sprite, in the constructor.
        int extra{};
        int extra_tellable{};
        int extra_distinct{};
#endif
    };
    Classified classified;

    [[nodiscard]] bool cell_is_blue(
        const sim::EncounterSnapshot& snapshot,
        int x,
        int y
    ) const {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) && unit.side == sim::Side::first &&
                unit.position.x == x && unit.position.y == y) {
                return sample_matches(
                    sample_at(x, y), unit_sprite(unit), 0, 0, unit.has_acted
                );
            }
        }
        return false;
    }

#ifdef GRANDLEON_N64_AUTOPILOT
    // What the autopilot needs to see, read-only: the cursor it steers and
    // the frame it asserts against.
    [[nodiscard]] std::int16_t cursor_x() const { return cursor_x_; }
    [[nodiscard]] std::int16_t cursor_y() const { return cursor_y_; }

    // The settle rule for the one free-running thing on this board. Called
    // immediately before a checkpoint asserts: the pulse goes back to its
    // resting phase and, if the frame on screen was drawn emphasised, the
    // board is repainted at rest. The pulse itself is never suspended: it runs
    // for the whole autopilot run exactly as it runs for a player. So what is
    // asserted is the picture a player sees, sampled at a documented phase
    // rather than at whichever one the script happened to land on.
    //
    // Repainting only when the board is what is on screen matters: a menu, an
    // information sheet or a refusal banner is drawn over the board and never
    // advances the pulse, so `cursor_emphasised` is false there and this does
    // nothing at all.
    void settle_pulse() {
        const bool emphasised = view::cursor_emphasised(pulse_frame_);
        // The shimmer is settled by the same pin, and needs it more: a walk or
        // a strike is bounded and puts itself back, but water never stops
        // moving, so the only thing that makes its phase predictable at a
        // checkpoint is this. Phase zero is the palette the sheet carries, so
        // what the checkpoint photographs is what it always photographed.
        const bool shimmered = view::water_cycle_phase(pulse_frame_) != 0;
        pulse_frame_ = 0;
        if (!(emphasised || shimmered) || !has_previous_) return;
        // **Whatever is on screen has to still be on screen afterwards.**
        // Repainting the board unconditionally is the tempting shape, on the
        // argument that a menu or a sheet is drawn over the board and never
        // advances the pulse, so the counter would be at rest wherever one was
        // standing. The first half is true and the second does not follow: the
        // counter is not advanced *while* a menu is open, but it keeps whatever
        // value it had when the menu opened, which after a checkpoint's own
        // hold is a hundred and fifty frames of pulse. An unconditional
        // repaint therefore paints the bare board over the menu the checkpoint
        // is about to assert, and every frame assertion over an overlay passes
        // or fails on where the pulse happens to be. The board menu's
        // checkpoint is where that shows first, but it was never a fact about
        // the board menu.
        //
        // The sheet is a whole screen and the leaving question is another, so
        // both are simply left alone: nothing under them is photographed, and
        // the pulse behind them is already back at rest for the next board.
        if (sheet_open_ || leaving_open_) return;
        if (menu_open_ && menu_choices_ != nullptr) {
            // The board at rest, and the menu put back on top of it. That is
            // the frame the player was looking at, drawn at the documented
            // phase rather than at whichever one the script landed on.
            surface_t* disp = display_get();
            render(disp, previous_, 0);
            draw_highlights(disp, previous_);
            draw_cursor(disp, previous_);
            draw_info_panel(disp, previous_);
            draw_menu(disp, menu_choices_, menu_count_, menu_chosen_);
            show(disp);
            return;
        }
        repaint_board(previous_);
    }

    // The board checkpoints, run when the script names them. Everything
    // asserted here is sampled from the shown frame or recomputed from the
    // engine's own queries over the snapshot that frame drew.
    void autopilot_check(grandleon::n64autopilot::Check check) {
        namespace ap = grandleon::n64autopilot;
        const sim::EncounterSnapshot& snapshot = previous_;
        const sim::UnitSnapshot* hovered = nullptr;
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) && unit.position.x == cursor_x_ &&
                unit.position.y == cursor_y_) {
                hovered = &unit;
            }
        }
        switch (check) {
            case ap::Check::survive_status:
                // The two numbers the status line carries on a survive board,
                // checked against the state and the content they come from
                // rather than against a constant a script could have written.
                expect(
                    rounds_to_survive_ != 0U,
                    "the board is won by outlasting rounds"
                );
                expect(
                    has_previous_,
                    "and its status line has been drawn at least once"
                );
                expect(
                    snapshot.round < rounds_to_survive_,
                    "with the round in progress inside the count to outlast"
                );
                return;
            case ap::Check::battle_open:
                expect(has_previous_, "the board has been drawn");
                // The window this machine opens the Fordlight at, which is
                // sixteen columns of a thirty-two column board. The crossing and
                // both lines are inside it, so this still asks the question it
                // always asked: the water and the road are the same counts they
                // were when the whole board fitted, and only the open terrain
                // either side of them grew.
                expect(
                    classified.grass == 64 && classified.forest == 28 &&
                        classified.water == 12 && classified.road == 16,
                    "the authored Fordlight terrain is on screen"
                );
                expect(
                    classified.blue == 4 && classified.red == 4,
                    "both sides' units are on screen"
                );
                expect(classified.unknown == 0, "no unclassifiable cell");
                return;
            case ap::Check::unit_panel:
                expect(hovered != nullptr, "a unit sits under the cursor");
                expect(
                    surface_pixel(autopilot_screen, 305, 20) ==
                        pack16(242, 193, 78),
                    "the information panel frames the hovered unit"
                );
                return;
            case ap::Check::move_range: {
                expect(selected_ != 0, "a unit is selected");
                const sim::UnitSnapshot* actor = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (unit.id == selected_) actor = &unit;
                }
                expect(
                    actor != nullptr && actor->ability_ids.size() == 1 &&
                        actor->ability_ids[0] ==
                            core::stable_content_id_v1("power_strike"),
                    "the selected knight knows Power Strike"
                );
                const auto moves = sim::reachable_tiles(snapshot, selected_);
                const auto danger = sim::danger_tiles(
                    snapshot, sim::Side::second, weapons_, abilities_
                );
                expect(
                    !moves.empty() && !danger.empty(),
                    "movement and danger queries both light tiles"
                );
                const auto contains = [](
                    const std::vector<sim::Position>& tiles,
                    sim::Position tile
                ) {
                    for (const sim::Position& candidate : tiles) {
                        if (candidate == tile) return true;
                    }
                    return false;
                };
                // A lit tile is a place this character may go: red where
                // something threatens it, amber where nothing does, and dark
                // where it cannot go at all, whether or not somebody could
                // strike there. The union of the two lit sets is exactly the
                // reachable set, and this walks the whole board to say so.
                //
                // Amber rather than blue because this checkpoint is reached
                // with the WALK row taken: the script opens the menu, takes
                // the first row and steps the cursor. So the board is
                // answering a *pick* rather than a selection. Which is the
                // stronger claim of the two: it says the aiming query and the
                // movement query agree tile for tile, and that a walk aimed at
                // the board lights exactly where that walk could land.
                expect(
                    aim_ == Aim::walk,
                    "the walk taken out of the menu is still in hand"
                );
                const auto aimed = sim::aimable_tiles(
                    snapshot, selected_, aimed_gesture(), weapons_, abilities_
                );
                expect(
                    aimed == moves,
                    "an aimed walk lands exactly where the character can reach"
                );
                int wrong = 0;
                for (std::int16_t y = 0;
                     y < static_cast<std::int16_t>(snapshot.height); ++y) {
                    for (std::int16_t x = 0;
                         x < static_cast<std::int16_t>(snapshot.width);
                         ++x) {
                        const sim::Position tile{x, y};
                        const std::uint16_t value = stipple_pixel(x, y);
                        if (!contains(moves, tile)) {
                            if (value == packed(216, 64, 52) ||
                                value == packed(248, 200, 56)) {
                                ++wrong;
                            }
                        } else if (contains(danger, tile)) {
                            if (value != packed(216, 64, 52)) ++wrong;
                        } else if (value != packed(248, 200, 56)) {
                            ++wrong;
                        }
                    }
                }
                expect(
                    wrong == 0,
                    "the lit board matches the engine's queries tile for tile"
                );
                // And that the two answers really do differ on this board, so
                // the walk above is proving an intersection rather than
                // agreeing with itself.
                int threatened_beyond_reach = 0;
                for (const sim::Position& tile : danger) {
                    if (!contains(moves, tile)) ++threatened_beyond_reach;
                }
                expect(
                    threatened_beyond_reach > 0,
                    "and the board holds threatened tiles this one cannot reach"
                );
                return;
            }
            case ap::Check::after_move:
                // The guard's block is still open behind the knight, so
                // nobody has answered and nobody has fallen. The knight
                // itself is spent and drawn in grey, and the census has to
                // find it among its own four rather than lose it to the
                // terrain. That is the frame this game spends most of its
                // time on and the one no alternating board ever draws.
                expect(
                    classified.blue == 4 && classified.red == 4,
                    "both sides stand, and the spent knight among them"
                );
                expect(
                    cell_is_blue(snapshot, 1, 3),
                    "the knight holds the bridge road"
                );
                return;
            case ap::Check::action_menu: {
                // The shape of the menu, not merely the fact of it. A menu
                // that opened with the wrong rows, or lost the two every menu
                // has to end with, would draw the same box.
                expect(menu_open_, "the unit action menu is open");
                expect(
                    !board_menu_open_,
                    "and it is the character's menu rather than the board's"
                );
                expect(
                    surface_pixel(autopilot_screen, 19, 139) ==
                        pack16(242, 193, 78),
                    "the unit action menu is framed on screen"
                );
                expect(
                    menu_choices_ != nullptr && menu_count_ >= 5,
                    "the menu offers a walk, a strike, an end of turn, a "
                    "sheet and a way out"
                );
                if (menu_choices_ == nullptr || menu_count_ < 5) return;
                // Whether the walk row is there at all is the engine's to say,
                // and this asserts the menu against the query rather than
                // against what the row is expected to be. A client that had
                // gone back to tracking the walk itself would pass every
                // assertion below and fail this one the first time the two
                // disagreed.
                expect(
                    sim::gesture_available(
                        snapshot, selected_, {sim::Gesture::walk, 0, 0},
                        weapons_, abilities_
                    ) == menu_choices_[0].move,
                    "the walk row is offered exactly when the engine would "
                    "take a walk"
                );
                // The head of the menu, which is the order a turn is spent in:
                // where the character goes, and then what it does there.
                expect(
                    menu_choices_[0].move &&
                        std::strcmp(menu_choices_[0].text, "WALK") == 0,
                    "WALK is the first row"
                );
                expect(
                    !menu_choices_[0].attack && !menu_choices_[0].wait &&
                        menu_choices_[0].ability == 0 &&
                        menu_choices_[0].item == 0,
                    "and it is a walk and nothing else"
                );
                expect(
                    menu_choices_[1].attack && !menu_choices_[1].move,
                    "the second row strikes with what the character carries"
                );
                // The tail, in the order of how much each row commits: the
                // last row that spends the character's turn, then the two that
                // spend nothing. A row that spends a carried item still sits
                // above all three, between the spells and WAIT.
                expect(
                    menu_choices_[menu_count_ - 3].wait &&
                        std::strcmp(menu_choices_[menu_count_ - 3].text,
                                    "END CHARACTER TURN") == 0,
                    "END CHARACTER TURN is the third-to-last row, and says "
                    "whose turn it ends"
                );
                expect(
                    menu_choices_[menu_count_ - 2].info &&
                        std::strcmp(menu_choices_[menu_count_ - 2].text,
                                    "INFO") == 0,
                    "INFO is the second-to-last row"
                );
                expect(
                    !menu_choices_[menu_count_ - 2].wait &&
                        !menu_choices_[menu_count_ - 2].attack &&
                        menu_choices_[menu_count_ - 2].ability == 0,
                    "the INFO row commits the character to nothing"
                );
                expect(
                    std::strcmp(menu_choices_[menu_count_ - 1].text,
                                "CANCEL") == 0,
                    "CANCEL is the last row"
                );
                return;
            }
            case ap::Check::board_menu_in_hand: {
                // Everything the plain board-menu checkpoint asserts, plus the
                // one fact it cannot: a character was in hand when START was
                // pressed, and is still in hand behind the menu. START means
                // the same thing in both states rather than "wait" while
                // somebody is selected, and putting a character down would be
                // this menu answering a question nobody asked.
                expect(menu_open_ && board_menu_open_, "the board menu is open");
                expect(
                    selected_ != 0,
                    "and a character was in hand when it was opened"
                );
                expect(
                    menu_choices_ != nullptr && menu_count_ == 3,
                    "with the same three rows it opens with on a bare board"
                );
                return;
            }
            case ap::Check::character_turn_row: {
                // The end-of-turn row under the caret, reached by walking up
                // off the top of the menu, which is the gesture a player has
                // to discover. A row that exists and cannot be got to is a row
                // that is not offered.
                expect(menu_open_, "the character's menu is open");
                expect(
                    !board_menu_open_,
                    "and it is the character's rather than the board's"
                );
                expect(
                    menu_choices_ != nullptr && menu_count_ >= 4,
                    "the menu has a tail to walk into"
                );
                if (menu_choices_ == nullptr || menu_count_ < 4) return;
                expect(
                    menu_chosen_ == menu_count_ - 3,
                    "the caret is on the third-to-last row"
                );
                expect(
                    menu_choices_[menu_chosen_].wait &&
                        std::strcmp(menu_choices_[menu_chosen_].text,
                                    "END CHARACTER TURN") == 0,
                    "which is the row that ends this character's turn"
                );
                return;
            }
            case ap::Check::board_menu: {
                // The menu on the button a player presses looking for it. Its
                // rows are asserted by name because the whole point of it is
                // what it says: a board menu that opened with the right box and
                // the wrong words would be the same defect wearing a frame.
                expect(menu_open_, "a menu is open");
                expect(
                    board_menu_open_,
                    "and it is the board's menu rather than a character's"
                );
                expect(
                    surface_pixel(autopilot_screen, 19, 139) ==
                        pack16(242, 193, 78),
                    "the board menu is framed on screen"
                );
                expect(
                    menu_choices_ != nullptr && menu_count_ == 3,
                    "the board menu is three rows"
                );
                if (menu_choices_ == nullptr || menu_count_ != 3) return;
                expect(
                    std::strcmp(menu_choices_[0].text, "BACK TO BATTLE") == 0,
                    "the way back is the first row"
                );
                // The side by the name its own banner announced, which is what
                // keeps this row from reading as the character menu's.
                expect(
                    std::strcmp(menu_choices_[1].text, "END YOUR TURN") == 0,
                    "the end of the side's turn names the side"
                );
                expect(
                    std::strcmp(menu_choices_[2].text, "LEAVE BATTLE") == 0,
                    "and the way out is the last row"
                );
                // Nothing a character does is on it. The two menus are told
                // apart by what they hold and not only by which flag is set.
                for (int row = 0; row < menu_count_; ++row) {
                    expect(
                        !menu_choices_[row].move &&
                            !menu_choices_[row].attack &&
                            !menu_choices_[row].wait &&
                            !menu_choices_[row].talk &&
                            !menu_choices_[row].info &&
                            menu_choices_[row].ability == 0 &&
                            menu_choices_[row].item == 0,
                        "no row of the board menu commands a character"
                    );
                }
                return;
            }
            case ap::Check::side_turn_passed: {
                // What the row promised, checked against the engine rather
                // than against a count kept here. The drain is over, the turn
                // has been round to the opposing side and back, and no menu or
                // pick survived the gesture that started it.
                expect(
                    !finishing_,
                    "the drain the board menu started has finished"
                );
                expect(
                    snapshot.active_side == player_side_,
                    "and the turn has come back round to the player"
                );
                expect(
                    !menu_open_ && !board_menu_open_ && !sheet_open_,
                    "with no menu and no sheet left standing"
                );
                expect(
                    selected_ == 0 && aim_ == Aim::none,
                    "and nothing held over from before the turn ended"
                );
                // Both sides are still on the board they were, which is what
                // says the drain spent activations rather than characters.
                // The round it ends is the one the script spends entirely on
                // the board menu, so nobody has struck anybody yet.
                expect(
                    classified.blue == 4 && classified.red == 4,
                    "the drain spent activations and not characters"
                );
                return;
            }
            case ap::Check::info_sheet: {
                // The whole of what a character is, on a screen of its own.
                // Every line is asserted against `grandleon::sheet` built here
                // from the same snapshot, so a ROM that drew a stale sheet, or
                // dropped the stats the richer stat line added, fails here
                // rather than in a screenshot nobody reads.
                expect(sheet_open_, "the information sheet is on screen");
                expect(!menu_open_, "the menu stood down behind the sheet");
                expect(
                    surface_pixel(autopilot_screen, 13, 13) ==
                        pack16(242, 193, 78),
                    "the sheet wears the action menu's own frame"
                );
                const sim::UnitSnapshot* subject = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (unit.id == selected_) subject = &unit;
                }
                expect(
                    subject != nullptr,
                    "the sheet is the selected character's own"
                );
                if (subject == nullptr) return;
                const grandleon::sheet::UnitSheet wanted =
                    grandleon::sheet::build(
                        snapshot, *subject, campaign_name_of(subject->id),
                        weapons_, abilities_, items_, nullptr, package_
                    );
                expect(
                    sheet_.count == wanted.count && wanted.count == 11,
                    "the sheet is the eleven lines the shipped vocabulary fills"
                );
                bool same = sheet_.count == wanted.count;
                for (int i = 0; i < wanted.count && same; ++i) {
                    same = std::strcmp(sheet_.line(i), wanted.line(i)) == 0;
                }
                expect(same, "every line is the one the rules compose");
                // And the numbers themselves, spelled out, so the assertion is
                // about the knight rather than about two copies of one bug.
                expect(
                    std::strcmp(sheet_.line(0), "DAWN KNIGHT 1") == 0,
                    // Who, and not a number. The Fordlight fields two of this
                    // author's knights, so the derived name carries an ordinal
                    // to tell them apart, in ascending identity, which is the
                    // same order on every client. This is the northern
                    // one. Nobody authored either a placement name or, on a
                    // cartridge with no campaign in it, a roster name, so the
                    // derivation is the whole of what is known about them.
                    "the sheet names the character"
                );
                expect(
                    std::strcmp(sheet_.line(1), "KNIGHT") == 0,
                    // The class under the name: the author's own word for it,
                    // out of the package this cartridge carries. Not the unit
                    // type: `DAWN KNIGHT` and `ASHEN KNIGHT` are two of them
                    // and one class, and the class is what says what either
                    // can do.
                    "and the class it belongs to on the row beneath"
                );
                expect(
                    std::strcmp(sheet_.line(2), "HP 12/12  AP 2  MOV 3  SPD 1")
                        == 0,
                    "the knight's health, points, movement and speed"
                );
                expect(
                    std::strcmp(sheet_.line(3), "STR 5  DEF 3  RES 0  MAG 0")
                        == 0,
                    "what the knight deals and what it takes"
                );
                expect(
                    std::strcmp(sheet_.line(4), "SKL 0  LCK 0  EVA 0") == 0,
                    "the stats that decide whether a blow lands"
                );
                expect(
                    std::strcmp(
                        sheet_.line(6), "  GUARD SWORD  RNG 1  HIT 100%"
                    ) == 0,
                    "the carried weapon with its band and its accuracy"
                );
                expect(
                    std::strcmp(sheet_.line(8), "  POWER STRIKE  RNG 1") == 0,
                    "the ability the knight knows"
                );
                return;
            }
            case ap::Check::spell_choice: {
                // The point of the whole shape of this menu: it asks a
                // question. Every unit type in every shipped game carries
                // exactly one ability apart from the mage, who knows two, so
                // this is the only console menu that opens on an actual choice.
                expect(menu_open_, "the unit action menu is open");
                expect(
                    menu_choices_ != nullptr && menu_count_ == 8,
                    "the mage's menu is eight rows"
                );
                if (menu_choices_ == nullptr || menu_count_ != 8) return;
                expect(
                    menu_choices_[2].ability ==
                            core::stable_content_id_v1("ember_bolt") &&
                        menu_choices_[3].ability ==
                            core::stable_content_id_v1("cinder_arc"),
                    "both of the mage's spells are on offer, in carried order"
                );
                expect(
                    menu_chosen_ == 3,
                    "the caret rests on the second spell"
                );
                // The two are a real decision rather than two spellings of
                // one, and the engine is what says so: from where the mage
                // stands both are legal, and they do different things.
                const sim::AbilityDefinition* bolt = nullptr;
                const sim::AbilityDefinition* arc = nullptr;
                for (const sim::AbilityDefinition& ability : abilities_) {
                    if (ability.id == menu_choices_[2].ability) bolt = &ability;
                    if (ability.id == menu_choices_[3].ability) arc = &ability;
                }
                expect(
                    bolt != nullptr && arc != nullptr,
                    "the encounter defines both spells"
                );
                if (bolt == nullptr || arc == nullptr) return;
                expect(
                    bolt->power > arc->power &&
                        arc->maximum_reach > bolt->maximum_reach &&
                        arc->minimum_reach > bolt->minimum_reach,
                    "one spell hits harder and the other reaches further"
                );
                expect(
                    bolt->area == sim::AreaShape::single &&
                        arc->area != sim::AreaShape::single,
                    "one spell strikes a character and the other a shape"
                );
                return;
            }
            case ap::Check::item_row: {
                // The row the menu keeps a place open for, filled in. It sits
                // after the last spell and before the row that ends the
                // character's turn, says how many are left, and commits nothing
                // about a tile.
                expect(menu_open_, "the mage's menu is open again");
                expect(
                    menu_choices_ != nullptr && menu_count_ == 8,
                    "the mage's menu is still eight rows"
                );
                if (menu_choices_ == nullptr || menu_count_ != 8) return;
                expect(
                    menu_choices_[4].item ==
                        core::stable_content_id_v1("field_tonic"),
                    "the fifth row spends the draught the mage carries"
                );
                expect(
                    menu_choices_[3].ability != 0 && menu_choices_[5].wait,
                    "the item row sits between the last spell and the end of "
                    "the character's turn"
                );
                expect(
                    std::strcmp(menu_choices_[4].text, "FIELD TONIC x1") == 0,
                    "the row says what it is and how many are left"
                );
                expect(menu_chosen_ == 4, "the caret rests on the draught");
                return;
            }
            case ap::Check::after_item: {
                // Deterministic, and therefore exactly assertable: the tonic
                // restores four, the mage was on two out of seven, and no
                // stream was drawn from on the way. The menu is gone, the
                // character is put down, and the draught is spent.
                expect(!menu_open_, "the menu closed behind the draught");
                expect(aim_ == Aim::none, "nothing is left aimed");
                const sim::UnitSnapshot* mage = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (unit.unit_type_id ==
                        core::stable_content_id_v1("dawn_mage")) {
                        mage = &unit;
                    }
                }
                expect(mage != nullptr, "the mage is still standing");
                if (mage == nullptr) return;
                // The mage drinks unhurt, and that is the claim: a heal does
                // not carry a character past its own maximum, and the draught
                // is spent all the same. Nothing here is a die roll: spending
                // an item draws from no stream. So both halves are exact.
                expect(
                    mage->health == mage->maximum_health,
                    "a draught does not carry a character past its maximum"
                );
                expect(
                    mage->item_ids.size() == 1 && mage->item_counts.size() == 1,
                    "the row stays after the draught is gone"
                );
                if (mage->item_counts.empty()) return;
                expect(
                    mage->item_counts[0] == 0,
                    "the draught it drank is the draught it no longer has"
                );
                return;
            }
            case ap::Check::aim_with_a_target:
            case ap::Check::aim_with_no_target: {
                // The question a player asks of a cartridge: does choosing
                // ATTACK show the tiles the strike can actually reach. Both
                // halves of the answer are checked here, on the same board and
                // with the same character, because the useful half is the one
                // that lights nothing. Without it, a player with nobody in
                // range has to sweep the cursor over the whole board to find
                // that out, and the panel can only ever answer one tile at a
                // time.
                expect(!menu_open_, "the menu closed behind the strike");
                expect(
                    aim_ == Aim::strike && selected_ != 0,
                    "a strike is in hand and its character is still selected"
                );
                const auto aimed = sim::aimable_tiles(
                    snapshot, selected_, aimed_gesture(), weapons_, abilities_
                );
                const bool wanted = check == ap::Check::aim_with_a_target;
                expect(
                    aimed.empty() != wanted,
                    wanted ? "the strike has somebody to aim at"
                           : "the strike has nobody to aim at"
                );
                int lit = 0;
                int wrong = 0;
                for (std::int16_t y = 0;
                     y < static_cast<std::int16_t>(snapshot.height); ++y) {
                    for (std::int16_t x = 0;
                         x < static_cast<std::int16_t>(snapshot.width); ++x) {
                        if (!camera_.visible(x, y)) continue;
                        if (sample_under_panel(x, y)) continue;
                        bool named = false;
                        for (const sim::Position& tile : aimed) {
                            if (tile.x == x && tile.y == y) named = true;
                        }
                        const bool amber =
                            stipple_pixel(x, y) == packed(248, 200, 56);
                        if (amber) ++lit;
                        if (amber != named) ++wrong;
                    }
                }
                expect(
                    wrong == 0,
                    "the board lights exactly the tiles the strike can reach"
                );
                expect(
                    (lit > 0) == wanted,
                    wanted ? "and there is something lit to see"
                           : "and the board is dark, which is the whole answer"
                );
                return;
            }
            case ap::Check::aiming: {
                expect(!menu_open_, "the menu closed behind the pick");
                expect(
                    aim_ == Aim::cast &&
                        aim_ability_ == core::stable_content_id_v1("cinder_arc"),
                    "the pick survived the menu closing"
                );
                expect(
                    std::strcmp(aim_text_, "AIM CINDER ARC") == 0,
                    "the screen says what the next press will do"
                );
                expect(
                    selected_ != 0,
                    "the character is still selected while its pick is aimed"
                );
                // And the half a player needs off the board itself: it says
                // where this spell can land, in the engine's own words, before
                // a single step of the cursor.
                //
                // Cinder Arc reaches two to three tiles, so its lit set is a
                // ring with a hole in the middle. That makes this a stronger
                // walk than the movement one, because a renderer that lit "the
                // tiles within three" rather than "the tiles the engine
                // accepts" would pass a maximum-only check and fail this one.
                const auto aimed = sim::aimable_tiles(
                    snapshot, selected_, aimed_gesture(), weapons_, abilities_
                );
                const auto holds = [](
                    const std::vector<sim::Position>& tiles, sim::Position tile
                ) {
                    for (const sim::Position& candidate : tiles) {
                        if (candidate == tile) return true;
                    }
                    return false;
                };
                expect(!aimed.empty(), "the spell has somewhere to land");
                const sim::UnitSnapshot* caster = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (unit.id == selected_) caster = &unit;
                }
                expect(caster != nullptr, "the caster is on the board");
                if (caster == nullptr) return;
                int wrong = 0;
                int hole = 0;
                int offscreen = 0;
                int covered = 0;
                for (std::int16_t y = 0;
                     y < static_cast<std::int16_t>(snapshot.height); ++y) {
                    for (std::int16_t x = 0;
                         x < static_cast<std::int16_t>(snapshot.width);
                         ++x) {
                        const sim::Position tile{x, y};
                        const bool lit = holds(aimed, tile);
                        // Only what the camera is showing. A spell whose band
                        // reaches past the edge of the view is answered
                        // truthfully by the engine and drawn by nobody, and a
                        // claim about a pixel that is not on the screen would
                        // be a claim about the camera rather than about the
                        // query. `stipple` makes the same test before it draws.
                        if (!camera_.visible(x, y)) {
                            if (lit) ++offscreen;
                            continue;
                        }
                        // And not what the information panel is sitting on:
                        // it is drawn over the highlights, so a cell beneath it
                        // is a cell whose colour nothing may be claimed for.
                        if (sample_under_panel(x, y)) {
                            if (lit) ++covered;
                            continue;
                        }
                        const std::uint16_t value = stipple_pixel(x, y);
                        if (lit != (value == packed(248, 200, 56))) ++wrong;
                        // A tile inside the caster's minimum reach: near
                        // enough to be within the ring's outer edge and
                        // refused all the same.
                        const int dx = x - caster->position.x;
                        const int dy = y - caster->position.y;
                        const int away =
                            (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                        if (!lit && away > 0 && away < 2) ++hole;
                    }
                }
                expect(
                    wrong == 0,
                    "the amber board matches the engine's aiming query tile "
                    "for tile"
                );
                expect(
                    hole > 0,
                    "and the spell's minimum reach leaves an unlit hole under "
                    "the caster"
                );
                // Reported rather than asserted either way: how much of the
                // spell's band the view is cutting off is a fact about this
                // board and this camera, and pinning it here would make a
                // checkpoint about aiming fail the day the camera changed.
                report_line(
                    "note aim offscreen %d panelled %d\n", offscreen, covered
                );
                // The splash, drawn as the cursor being the size the cast is.
                // Cinder Arc is a cross, so it covers five tiles rather than
                // one, and the query that says which five is the one apply
                // walks the units against.
                const auto splash = sim::area_tiles(
                    snapshot, aim_ability_, {cursor_x_, cursor_y_}, abilities_
                );
                expect(
                    splash.size() == 5,
                    "a cross under the cursor covers five tiles"
                );
                expect(
                    holds(splash, {cursor_x_, cursor_y_}),
                    "and the tile the cursor is on is one of them"
                );
                return;
            }
            case ap::Check::forecast:
            case ap::Check::forecast_lethal: {
                expect(
                    hovered != nullptr && selected_ != 0,
                    "a forecast pairing is on screen"
                );
                if (hovered == nullptr) return;
                const auto forecast =
                    sim::forecast_attack(snapshot, selected_, hovered->id);
                expect(
                    static_cast<bool>(forecast),
                    "the forecast prices the strike"
                );
                if (check == ap::Check::forecast) {
                    expect(
                        forecast.damage == 3 &&
                            forecast.target_health_after == 9 &&
                            !forecast.lethal,
                        "the bow's forecast promises three off a knight's "
                        "twelve"
                    );
                    // And promises it nineteen times in twenty, which is the
                    // whole of what the panel says. The Long Bow is
                    // authored at 90 and the Archer carries five points of
                    // skill, and the chance a forecast states is the folded
                    // one: 90 + 5 against a stormcaller that dodges nothing.
                    // Re-derived on the host, never eyeballed:
                    // `grandleon_playstation_expect` writes `95% HIT 5 LEFT 3`
                    // for the same board off the same rules.
                    expect(
                        forecast.hit_chance == 95,
                        "the bow's forecast states the chance it will be rolled"
                    );
                } else {
                    expect(
                        hovered->health == 3,
                        "the line has worn this one down to three"
                    );
                    expect(
                        forecast.damage == 5 && forecast.lethal,
                        "and the sword's forecast says so before it is thrown"
                    );
                }
                // Beside the target, wherever that put it. The panel is placed
                // so as not to cover the character it prices, so the corner it
                // is drawn at is a function of where on the board the cursor
                // rests. The rectangle recorded by the draw is what the frame
                // is looked for in, rather than a pixel written down here that
                // only one target's placement would satisfy.
                expect(
                    panel_width_ > 0 &&
                        surface_pixel(
                            autopilot_screen, panel_left_ + 1, panel_top_ + 1
                        ) == pack16(242, 193, 78),
                    "the panel shows beside the target"
                );
                return;
            }
            case ap::Check::saying:
                // The two claims this surface exists to make, and neither is made
                // by any other checkpoint.
                //
                // First: there is a bubble. Its border is the panel's own amber,
                // sampled at the corner the ROM recorded when it drew it, so a
                // bubble that moved is checked where it moved to rather than where
                // it used to be.
                expect(said_width_ > 0, "a saying was drawn over the board");
                expect(
                    surface_pixel(autopilot_screen, said_left_, said_top_) ==
                        pack16(242, 193, 78),
                    "the bubble is framed on the screen"
                );
                // Second, and the whole point of drawing this here rather than
                // on a page: the speaker is still on the screen, in their own
                // cell, with the board around them. A page would have replaced
                // every pixel of it, so the tile the speaker stands on showing
                // anything but the bubble's own paper is the claim - and it is
                // read off the framebuffer rather than off `classified`, which
                // is filled by the settled frame this checkpoint precedes.
                //
                // It doubles as the other thing a bubble placed against a
                // character can get wrong: sitting on top of them. If it did,
                // this samples paper and says so.
                expect(
                    said_at_x_ >= 0,
                    "the saying names somebody standing on this board"
                );
                if (said_at_x_ >= 0) {
                    expect(
                        !(said_at_x_ >= said_left_ &&
                          said_at_x_ < said_left_ + said_width_ &&
                          said_at_y_ >= said_top_ &&
                          said_at_y_ < said_top_ + said_height_),
                        "and never covers the character it is about"
                    );
                    expect(
                        surface_pixel(autopilot_screen, said_at_x_, said_at_y_) !=
                            pack16(16, 26, 27),
                        "and the speaker is still drawn on the board under the "
                        "words"
                    );
                }
                return;
            case ap::Check::after_ability: {
                // Three of ours and four of theirs. Cinder Arc took the knight
                // the archer had already worn to one, and the Coil took the
                // southern knight in the block before this one. See the
                // script's header for the round-by-round derivation.
                //
                // Four rather than three because a fifth of theirs is on this
                // board: the levy that opens far out on the east road, pursuing
                // at three tiles an activation. By this checkpoint it has
                // walked the twenty-odd columns of open ground into the window
                // this counts, which is what a pursuing character does and the
                // reason it was placed out there rather than left as scenery.
                expect(
                    classified.blue == 3 && classified.red == 4,
                    "the arc took the wounded knight, the guard is three, and "
                    "the levy has come down the road"
                );
                // Alive, and deliberately not `sim::on_board`. The claim is
                // that the cast *killed* whoever stood here, and `on_board` is
                // the weaker test: it would pass on a survivor who merely
                // walked away or was talked off the tile, which is not what
                // this checkpoint is asserting.
                bool struck_tile_clear = true;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (unit.health > 0 && unit.position.x == 3 &&
                        unit.position.y == 3) {
                        struck_tile_clear = false;
                    }
                }
                expect(
                    struck_tile_clear,
                    "no living unit remains on the struck tile"
                );
                return;
            }
            default:
                expect(false, "checkpoint belongs to a screen, not the board");
                return;
        }
    }
#endif

private:
#ifdef GRANDLEON_N64_CAMPAIGN
    // -----------------------------------------------------------------------
    // The campaign screens' own drawing. Nothing below reads campaign state
    // that was not handed to it, and nothing below decides anything: which
    // verbs a member offers is what the campaign holds for them, and what a
    // gesture does is what the session commits.
    // -----------------------------------------------------------------------

    // The word this console uses for an availability, and the word every other
    // client uses for it: `platform/client/src/turn_client.cpp` carries this
    // same table, because a player who has seen one console's company should
    // recognise the other's.
    //
    // `dead` says DEAD and not FALLEN. The two words are two different facts:
    // somebody who *fell* is down and out of the battle and is coming back, and
    // somebody who is *dead* is not.
    // `Availability::dead` is reachable only through `record_permanent_death`,
    // which only a campaign playing under the permanent rule ever commits, so
    // this row is never the softer of the two and must never be worded as
    // though it might be.
    static const char* availability_word(campaign::Availability where) {
        switch (where) {
            case campaign::Availability::unrecruited: return "AWAY";
            case campaign::Availability::available: return "FIELD";
            case campaign::Availability::retired: return "BENCH";
            case campaign::Availability::dead: return "LOST";
        }
        return "?";
    }

    // What the campaign calls a member, out of a roster it has already been
    // handed.
    //
    // The roster a commit leaves behind still holds everybody the campaign
    // knows, the buried included, so the name of somebody who died is read out
    // of the same list as the name of somebody who did not. A member no roster
    // holds is named by a word rather than by a number: the identity is 64 bits
    // wide and neither this screen's formatter nor the report channel this ROM
    // is read through has any business printing one at a player.
    static std::string member_name(
        const std::vector<client::RosterEntry>& roster,
        campaign::PersistentEntityId member
    ) {
        for (const client::RosterEntry& entry : roster) {
            if (entry.member.value == member.value) {
                return grandleon::sheet::person_name(entry.name.c_str())
                    .c_str();
            }
        }
        return "SOMEBODY";
    }

    // Which layer refused the slot, in that layer's own word for it. Four
    // channels rather than one string is `SlotFailure`'s design and this reads
    // whichever is set rather than flattening them into a house sentence.
    static const char* refusal_reason(const client::SlotFailure& failure) {
        if (failure.storage != storage::StorageError::none) {
            return "THE CARTRIDGE COULD NOT BE READ";
        }
        if (failure.save != campaign::SaveError::none) {
            return "THE SAVE IS NOT ONE THIS GAME READS";
        }
        if (failure.migration != campaign::MigrationError::none) {
            return "IT IS FROM AN OLDER VERSION";
        }
        if (failure.state != campaign::StateError::none) {
            return "THE CAMPAIGN IN IT DOES NOT ADD UP";
        }
        return "IT COULD NOT BE READ";
    }

    void campaign_refusal_banner(std::string_view reason) {
        char text[40];
        std::snprintf(
            text, sizeof text, "%.*s", static_cast<int>(reason.size()),
            reason.data()
        );
        for (char& character : text) {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - ('a' - 'A'));
            }
            if (character == '_') character = ' ';
        }
        const int width = static_cast<int>(std::strlen(text)) * 8 + 16;
        const int left = (320 - width) / 2;
        surface_t* disp = display_get();
        graphics_draw_box(
            disp, left - 2, 102, width + 4, 24, colour(179, 72, 63)
        );
        graphics_draw_box(disp, left, 104, width, 20, colour(16, 26, 27));
        graphics_set_color(colour(245, 234, 210), 0);
        graphics_draw_text(disp, left + 8, 110, text);
        show(disp);
        // Held rather than dismissed, and no input is read while it is up: on
        // an autopilot build a `poll_buttons` here would eat a scripted press
        // and desynchronise the run. The battle's refusal banner holds on the
        // same terms and for the same reason.
        for (int held = 0; held < 60; ++held) {
            grandleon::n64audio::pump();
            wait_ms(16);
        }
    }

    // One roster row: who they are, what they have grown into, whether they
    // take the next board, and how much they are carrying.
    void company_row(
        const client::RosterEntry& entry,
        char* into,
        std::size_t size
    ) const {
        std::uint32_t carried = 0;
        for (const campaign::InventoryStack& stack : entry.carried) {
            carried += stack.quantity;
        }
        std::snprintf(
            into, size, "%-14.14s L%-2u %-6s KIT %u",
            grandleon::sheet::person_name(entry.name.c_str()).c_str(),
            static_cast<unsigned>(entry.progression.level),
            availability_word(entry.availability),
            static_cast<unsigned>(carried)
        );
    }

    // The company, drawn once and read the same way whether it is the screen
    // the campaign opened on or the stage between two battles.
    //
    // Two lists and two windows. The roster's follows the caret, so a company
    // larger than the seven rows this screen has is reachable to its last
    // member rather than silently cut at the seventh with nothing on screen to
    // say so.
    //
    // The store's window follows nothing and never moves: the caret is on the
    // roster, and a second thing sliding under a thumb steering the first is a
    // screen a player cannot read. The roster's window being a fixed seven rows
    // is what makes "never moves" a place: the store's heading lands on the
    // same scanline whether the company is four or four hundred. Its own
    // overflow is named by its legend rather than dropped, and the items
    // themselves are reached where they have always been reached, on a member's
    // menu, which scrolls under its own caret for the same reason.
    void draw_company_body(
        surface_t* disp,
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        int caret
    ) const {
        char line[48];
        char legend[view::scroll_legend_size];

        view::ListWindow members;
        members.rows = company_roster_rows;
        members.total = static_cast<int>(roster.size());
        members.top = caret >= 0 ? roster_top_ : 0;
        members.clamp();
        int y = 44;
        for (int index = members.top; index < members.end(); ++index) {
            const bool on = caret == index;
            company_row(roster[static_cast<std::size_t>(index)], line, sizeof line);
            graphics_set_color(
                on ? colour(242, 193, 78) : colour(245, 234, 210), 0
            );
            graphics_draw_text(disp, 20, y, on ? ">" : " ");
            graphics_draw_text(disp, 36, y, line);
            y += 14;
        }
        // A window that is not scrolling writes nothing, so a company that fits
        // gets the screen it always had.
        if (view::scroll_legend(members, legend, sizeof legend) > 0) {
            graphics_set_color(colour(125, 168, 133), 0);
            graphics_draw_text(disp, 232, 32, legend);
        }

        view::ListWindow shelf;
        shelf.rows = company_store_rows;
        shelf.total = static_cast<int>(store.size());
        shelf.top = 0;
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 20, 142, "THE COMPANY'S STORE");
        if (view::scroll_legend(shelf, legend, sizeof legend) > 0) {
            graphics_set_color(colour(125, 168, 133), 0);
            graphics_draw_text(disp, 200, 142, legend);
        }
        graphics_set_color(colour(245, 234, 210), 0);
        y = 158;
        if (store.empty()) {
            graphics_set_color(colour(125, 138, 133), 0);
            graphics_draw_text(disp, 36, y, "NOTHING BUT WHAT THEY CARRY");
        }
        for (int index = shelf.top; index < shelf.end(); ++index) {
            const campaign::InventoryStack& stack =
                store[static_cast<std::size_t>(index)];
            std::snprintf(
                line, sizeof line, "%-18.18s x%u",
                grandleon::sheet::item_name(stack.item.stable_id),
                static_cast<unsigned>(stack.quantity)
            );
            graphics_draw_text(disp, 36, y, line);
            y += 13;
        }
    }

    // The screen the campaign opens on, founded or resumed. It says which,
    // because a player who switched the console off in the middle of a
    // campaign wants to be told it came back.
    void draw_company_sheet(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        bool resumed
    ) {
        surface_t* disp = display_get();
        graphics_fill_screen(disp, colour(16, 26, 27));
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(
            disp, 20, 22, resumed ? "THE COMPANY STANDS AS YOU LEFT IT"
                                  : "THE COMPANY MUSTERS"
        );
        draw_company_body(disp, roster, store, -1);
        graphics_set_color(colour(125, 138, 133), 0);
        graphics_draw_text(disp, 240, 214, "A: NEXT");
        show(disp);
    }

    // Whether one more member would carry this board past its authored
    // capacity. An early copy of `join_campaign_roster`'s own gate and never a
    // substitute for it: the engine refuses an over-cap company however this
    // screen counted, and says so in the REFUSED line below.
    [[nodiscard]] static bool field_is_full(
        const client::CompanyManagement& company
    ) {
        return company.capacity != 0U &&
               company.fielded.size() >=
                   static_cast<std::size_t>(company.capacity);
    }

    // The management stage: the same body, with a caret on it and the two
    // things a controller can do from the top level named at the foot.
    void draw_company(
        surface_t* disp,
        const client::CompanyManagement& company,
        int caret,
        int
    ) {
        graphics_fill_screen(disp, colour(16, 26, 27));
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 20, 22, "BEFORE THE BATTLE");
        if (save_toast_ > 0) {
            graphics_set_color(
                saved_ ? colour(125, 168, 133) : colour(196, 84, 74), 0
            );
            graphics_draw_text(disp, 252, 22, saved_ ? "SAVED" : "NOT SAVED");
            --save_toast_;
        }
        // How many of the company this board's author lets out, against how
        // many would go as it stands. A line only a capped board has: a board
        // that caps nothing is not counting anything, so its screen is the
        // screen it always was rather than one that says NO LIMIT.
        if (company.capacity != 0U) {
            graphics_set_color(
                field_is_full(company) ? colour(196, 84, 74)
                                       : colour(125, 168, 133),
                0
            );
            char counted[32];
            std::snprintf(
                counted, sizeof counted, "FIELDED %u OF %u",
                static_cast<unsigned>(company.fielded.size()),
                static_cast<unsigned>(company.capacity)
            );
            graphics_draw_text(disp, 20, 32, counted);
        }
        draw_company_body(disp, company.roster, company.store, caret);
        // A board the roster refused is the company's own business and is said
        // in the roster's word for it, on the screen that can change it.
        if (company.refused != grandleon::campaign_runtime::RosterError::none) {
            graphics_set_color(colour(196, 84, 74), 0);
            char line[48];
            const std::string_view name =
                grandleon::campaign_runtime::roster_error_name(company.refused);
            std::snprintf(
                line, sizeof line, "REFUSED: %.*s",
                static_cast<int>(name.size()), name.data()
            );
            graphics_draw_text(disp, 20, 194, line);
        }
        graphics_set_color(colour(125, 138, 133), 0);
        graphics_draw_text(disp, 20, 212, "A CHOOSE   START TO BATTLE");
        // Z is named only when it does something, which is only on a campaign
        // whose author asked for the Stage picker. A footer that listed a
        // button doing nothing would be a footer a player learns to distrust.
        graphics_draw_text(
            disp, 20, 224,
            company.stages.empty() ? "B SAVE AND LEAVE"
                                   : "B SAVE AND LEAVE   Z GO TO A STAGE"
        );
    }

    // One member's menu: every verb the stage has, aimed at nothing, in the
    // order that taking a row costs something. The availability row first
    // because it is about the next board; the moves after it because they are
    // about a satchel; CANCEL last, spending nothing.
    //
    // A row for an item the store cannot pay is not offered, because unlike a
    // strike at empty ground there is nothing to refuse: the store either holds
    // one or does not, and the screen showing what it holds is the same screen.
    [[nodiscard]] bool run_member_menu(
        const client::CompanyManagement& company,
        int row,
        client::ManagementIntent& chosen
    ) {
        struct Row final {
            char text[30];
            client::ManagementVerb verb;
            campaign::DefinitionRef item;
        };
        // Twenty rather than twelve because a menu capped at twelve drops rows,
        // which is the other half of the truncation this screen's window exists
        // to end: the rows past the twelfth are the store's and the member's
        // own kit, which is to say items nothing on screen can reach. Twenty
        // covers the availability row, nine store stacks, nine carried stacks
        // and CANCEL; the window shows eight of them at a time. This is a
        // stack frame in a function `main` calls, on a machine with four
        // megabytes and a measured watermark, so the cost is reported rather
        // than budgeted.
        constexpr int capacity = 20;
        Row rows[capacity];
        int count = 0;
        const client::RosterEntry& entry = company.roster[static_cast<std::size_t>(row)];

        const bool fielded =
            entry.availability == campaign::Availability::available;
        // A member the campaign has buried is on no board and takes no gift;
        // the engine refuses both by name, and offering the rows would be
        // offering a press whose only outcome is a refusal banner.
        const bool actionable =
            entry.availability != campaign::Availability::dead &&
            entry.availability != campaign::Availability::unrecruited;
        if (actionable) {
            // The row stays offered when the board is full: taking it is how
            // a player is told why, and a row that vanished would leave the
            // screen saying nothing about the one member it concerns.
            const bool full = !fielded && field_is_full(company);
            std::snprintf(
                rows[count].text, sizeof rows[count].text, "%s",
                fielded ? "SIT THIS ONE OUT"
                : full  ? "TAKE THE FIELD (FULL)"
                        : "TAKE THE FIELD"
            );
            rows[count].verb = fielded ? client::ManagementVerb::bench
                                       : client::ManagementVerb::field;
            rows[count].item = {};
            ++count;
            for (const campaign::InventoryStack& stack : company.store) {
                if (count + 1 >= capacity) break;
                std::snprintf(
                    rows[count].text, sizeof rows[count].text, "GIVE %s x%u",
                    grandleon::sheet::item_name(stack.item.stable_id),
                    static_cast<unsigned>(stack.quantity)
                );
                rows[count].verb = client::ManagementVerb::give;
                rows[count].item = stack.item;
                ++count;
            }
            for (const campaign::InventoryStack& stack : entry.carried) {
                if (count + 1 >= capacity) break;
                std::snprintf(
                    rows[count].text, sizeof rows[count].text, "TAKE %s x%u",
                    grandleon::sheet::item_name(stack.item.stable_id),
                    static_cast<unsigned>(stack.quantity)
                );
                rows[count].verb = client::ManagementVerb::take;
                rows[count].item = stack.item;
                ++count;
            }
        }
        std::snprintf(rows[count].text, sizeof rows[count].text, "CANCEL");
        rows[count].verb = client::ManagementVerb::none;
        rows[count].item = {};
        ++count;

        int caret = 0;
        // The menu's own window, on the same terms as the roster's: a member
        // carrying nine things out of a store holding nine has twenty rows and
        // eight of them on the screen, and the last of them is reachable.
        view::ListWindow visible;
        visible.rows = member_menu_rows;
        visible.total = count;
        visible.top = 0;
        bool dirty = true;
        while (true) {
            if (dirty) {
                surface_t* disp = display_get();
                draw_company(disp, company, row, -1);
                // The box is sized to what is on the screen, not to what the
                // menu holds, so a long menu is a scrolling box rather than one
                // that runs off the bottom of the display.
                int width_chars = 0;
                for (int index = visible.top; index < visible.end(); ++index) {
                    const int length =
                        static_cast<int>(std::strlen(rows[index].text));
                    if (length > width_chars) width_chars = length;
                }
                char legend[view::scroll_legend_size];
                const bool scrolling =
                    view::scroll_legend(visible, legend, sizeof legend) > 0;
                if (scrolling) {
                    const int length = static_cast<int>(std::strlen(legend));
                    if (length > width_chars) width_chars = length;
                }
                const int width = (width_chars + 2) * 8 + 12;
                const int height = (visible.shown() + (scrolling ? 1 : 0)) * 11 + 8;
                const int left = 300 - width < 12 ? 12 : 300 - width;
                const int top = 40;
                graphics_draw_box(
                    disp, left - 2, top - 2, width + 4, height + 4,
                    colour(242, 193, 78)
                );
                graphics_draw_box(
                    disp, left, top, width, height, colour(16, 26, 27)
                );
                int drawn = 0;
                for (int index = visible.top; index < visible.end(); ++index) {
                    graphics_set_color(
                        index == caret ? colour(242, 193, 78)
                                       : colour(245, 234, 210),
                        0
                    );
                    char text[34];
                    std::snprintf(
                        text, sizeof text, "%s%s",
                        index == caret ? "> " : "  ", rows[index].text
                    );
                    graphics_draw_text(
                        disp, left + 6, top + 4 + drawn * 11, text
                    );
                    ++drawn;
                }
                if (scrolling) {
                    graphics_set_color(colour(125, 168, 133), 0);
                    graphics_draw_text(
                        disp, left + 6, top + 4 + drawn * 11, legend
                    );
                }
                show(disp);
                dirty = false;
            }
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            if (pressed.d_up && caret > 0) {
                --caret;
                visible.follow(caret, company_scroll_margin);
                dirty = true;
            }
            if (pressed.d_down && caret + 1 < count) {
                ++caret;
                visible.follow(caret, company_scroll_margin);
                dirty = true;
            }
            if (pressed.b) return false;
            if (pressed.a) {
                if (rows[caret].verb == client::ManagementVerb::none) {
                    return false;
                }
                // A board its author capped fields no more than it says, so the
                // press is refused here rather than committed and undone. The
                // sentence is the roster's own name for it, never one written
                // on this console, because it is the very refusal
                // `join_campaign_roster` would have given a gesture later.
                if (rows[caret].verb == client::ManagementVerb::field &&
                    field_is_full(company)) {
                    campaign_refusal_banner(
                        grandleon::campaign_runtime::roster_error_name(
                            grandleon::campaign_runtime::RosterError::
                                over_deployment_capacity
                        )
                    );
                    dirty = true;
                    continue;
                }
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                chosen.verb = rows[caret].verb;
                chosen.member = entry.member;
                chosen.item = rows[caret].item;
                return true;
            }
            wait_ms(16);
        }
    }

    // The campaign's Stages, and the one the player picks out of them.
    //
    // A screen of its own rather than a box over whatever was underneath,
    // because it is opened from two places that look nothing alike — the board
    // menu in the middle of a battle, and the company screen between battles —
    // and a box drawn over both would be a box a player met in two shapes.
    //
    // Two words carry the whole of what to know before choosing. HERE is where
    // the campaign stands; SEEN is somewhere this playthrough has actually
    // stood. A Stage with neither has never been reached, and nothing that would
    // have happened on the way there has happened, so its board may name a
    // character the company has not got and refuse to open at all. The line
    // under the heading says that once rather than leaving a player to find it
    // out by being stuck.
    //
    // Returns the campaign node the player chose, or zero when they backed out.
    [[nodiscard]] std::uint64_t run_stage_picker(
        const std::vector<client::CampaignStage>& stages
    ) {
        if (stages.empty()) return 0U;
        const int count = static_cast<int>(stages.size());
        int caret = 0;
        // As many rows as the band has room for, on the same terms the
        // company's seven are: the window follows the caret, so a campaign with
        // more Stages than fit is reachable to its last one rather than cut
        // with nothing on screen to say so. The count comes from
        // `screen::stage_band` rather than from a number written here, because
        // libdragon clips nothing and a row past the last scanline is written
        // into the heap the campaign session is allocating out of.
        view::ListWindow visible;
        visible.rows = screen::stage_band.rows();
        visible.total = count;
        visible.top = 0;
        bool dirty = true;
        while (true) {
            if (dirty) {
                surface_t* disp = display_get();
                graphics_fill_screen(disp, colour(16, 26, 27));
                graphics_set_color(colour(242, 193, 78), 0);
                graphics_draw_text(disp, 20, 22, "GO TO ANOTHER STAGE");
                char legend[view::scroll_legend_size];
                if (view::scroll_legend(visible, legend, sizeof legend) > 0) {
                    graphics_set_color(colour(125, 168, 133), 0);
                    graphics_draw_text(disp, 232, 22, legend);
                }
                graphics_set_color(colour(196, 84, 74), 0);
                graphics_draw_text(
                    disp, 20, 36, "A STAGE YOU HAVE NOT SEEN MAY NOT OPEN"
                );
                int y = screen::stage_band.top;
                for (int index = visible.top; index < visible.end(); ++index) {
                    if (!screen::stage_band.holds(y)) break;
                    const client::CampaignStage& stage =
                        stages[static_cast<std::size_t>(index)];
                    const bool on = index == caret;
                    graphics_set_color(
                        on ? colour(242, 193, 78) : colour(245, 234, 210), 0
                    );
                    graphics_draw_text(disp, 20, y, on ? ">" : " ");
                    char line[48];
                    // The author's own name for the board, and its place in the
                    // flow in front of it. A package with no name for a board
                    // leaves the number, which is not much and is never nothing.
                    std::snprintf(
                        line, sizeof line, "%-2d %-24.24s %s", index + 1,
                        stage.name.empty() ? "STAGE" : stage.name.c_str(),
                        stage.standing ? "HERE" : (stage.reached ? "SEEN" : "")
                    );
                    graphics_draw_text(disp, 36, y, line);
                    y += screen::stage_band.step;
                }
                graphics_set_color(colour(125, 138, 133), 0);
                graphics_draw_text(
                    disp, 20, screen::stage_prompt_y, "A GO   B BACK"
                );
                show(disp);
                dirty = false;
            }
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            if (pressed.d_up && caret > 0) {
                --caret;
                visible.follow(caret, company_scroll_margin);
                dirty = true;
            }
            if (pressed.d_down && caret + 1 < count) {
                ++caret;
                visible.follow(caret, company_scroll_margin);
                dirty = true;
            }
            if (pressed.b) return 0U;
            if (pressed.a) {
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                return stages[static_cast<std::size_t>(caret)].node_id;
            }
            wait_ms(16);
        }
    }

#ifdef GRANDLEON_N64_AUTOPILOT
    // The company, against what the host derived. Every constant compared here
    // lives in `games/tarnholt/autopilot/campaign_expectations.h` and is pinned
    // by `tests/nintendo64/campaign_expectations_test.cpp`, which drives the
    // same session through the same gestures on a host, so a console assertion
    // cannot be adjusted to make a run go green without breaking the host test
    // that derives it.
    void check_the_company(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        bool resumed
    ) {
        namespace want = grandleon::tarnholt;
        expect(
            static_cast<int>(roster.size()) == want::founding_roster_size,
            "the company is the size the host derived"
        );
        for (int index = 0; index < want::founding_roster_size; ++index) {
            const want::FoundingMember& member = want::founding_roster[index];
            const client::RosterEntry* entry = nullptr;
            for (const client::RosterEntry& held : roster) {
                if (held.member.value == member.member) entry = &held;
            }
            expect(entry != nullptr, "the campaign holds the derived member");
            if (entry == nullptr) continue;
            expect(
                entry->name == member.name,
                "and calls them what the content calls them"
            );
            expect(
                entry->progression.level == want::founding_level,
                "and holds the level the host derived"
            );
            if (!resumed) {
                expect(
                    static_cast<std::uint32_t>(entry->carried.size()) ==
                        member.carried_stacks,
                    "and the starting kit the content authored"
                );
                expect(
                    entry->availability == campaign::Availability::available,
                    "and everybody is deployable at founding"
                );
            }
        }
        if (!resumed) {
            expect(
                static_cast<int>(store.size()) == want::founding_store_stacks,
                "the store is empty at founding"
            );
            return;
        }

        // The whole claim of this ROM, on the second run: what the cartridge
        // gave back is what the first run put in it.
        expect(
            static_cast<int>(store.size()) == want::resumed_store_stacks,
            "the resumed store holds what the first run took into it"
        );
        std::uint32_t tonics = 0;
        for (const campaign::InventoryStack& stack : store) {
            tonics += stack.quantity;
        }
        expect(
            tonics == want::resumed_store_tonics,
            "and exactly as many of it"
        );
        for (const client::RosterEntry& entry : roster) {
            if (entry.member.value == want::mage_member) {
                expect(
                    static_cast<std::uint32_t>(entry.carried.size()) ==
                        want::resumed_mage_carried_stacks,
                    "the mage is still carrying nothing"
                );
            }
            if (entry.member.value == want::benched_member) {
                expect(
                    static_cast<std::uint8_t>(entry.availability) ==
                        want::resumed_benched_availability,
                    "the knight benched before the power went off is benched"
                );
            } else {
                expect(
                    static_cast<std::uint8_t>(entry.availability) ==
                        want::resumed_available_availability,
                    "and nobody else was benched by the round trip"
                );
            }
        }
    }
#endif

    int manage_row_{0};
    // Where the roster's window is standing. `draw_company_body` reads it and
    // never moves it, being a drawing routine. The caret's own loop is what
    // moves it, through `ListWindow::follow`, which clamps.
    int roster_top_{0};
    // Counts down over the company redraws that follow a save, so the word
    // appears where the player is looking rather than on a screen of its own.
    // Saving happens between every gesture, and a screen per save would put a
    // press between a player and every single thing they do.
    int save_toast_{0};
    bool saved_{false};
    // The board the battle now being played was prepared from, or null between
    // battles. Set by `board_prepared` and dropped by `battle_aftermath`; its
    // lifetime is exactly the battle it describes, and it is read for one thing
    // only, which is putting a character's own name on their death.
    const client::CampaignBoard* board_{nullptr};
#endif

    // Everything the frame draws, in the order the shared model puts it in.
    // Both renderers walk this one list: the rdpq one blits art from it, the
    // framebuffer one paints boxes from it, and neither decides for itself
    // where a cell goes or which token covers which.
    void build_draw_list(
        const sim::EncounterSnapshot& snapshot,
        sim::UnitId skip_unit,
        sim::UnitId skip_other = 0
    ) {
        draw_list_.clear();
        const int last_x = camera_.x + camera_.view_w;
        const int last_y = camera_.y + camera_.view_h;
        for (int y = camera_.y; y < last_y && y < snapshot.height; ++y) {
            for (int x = camera_.x; x < last_x && x < snapshot.width; ++x) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * snapshot.width +
                    static_cast<std::size_t>(x);
                const std::uint8_t kind =
                    index < terrain_.size() ? kind_of(terrain_[index])
                                            : gc::terrain_kind_unknown;
                // The kind is both the sheet and the batch: cells sharing an
                // elevation cannot overlap, so grouping them by sheet costs
                // the picture nothing and saves the rdpq renderer a TMEM
                // upload per run. Terrain the art library does not name takes
                // a slot too, so classify() still sees a cell there.
                draw_list_.add(
                    view::terrain_item(
                        camera_, projection_, x, y, elevation_at(x, y), kind,
                        terrain_variant(x, y), kind
                    ),
                    kind
                );
            }
        }
        for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
            const sim::UnitSnapshot& unit = snapshot.units[i];
            // Only what is standing there is drawn. `classify` below takes its
            // census with the same predicate, and the two have to agree or the
            // probe asserts about a cell nobody painted.
            if (!sim::on_board(unit) || unit.id == skip_unit) continue;
            // A second unit is lifted out for exactly one gesture: a landed
            // hit draws both the striker and the struck itself, because both
            // of them are posed for it.
            if (skip_other != 0 && unit.id == skip_other) continue;
            if (!camera_.visible(unit.position.x, unit.position.y)) continue;
            const int elevation =
                elevation_at(unit.position.x, unit.position.y);
            draw_list_.add(view::billboard_item(
                camera_, projection_, view::Layer::shadow, unit.position.x,
                unit.position.y, elevation, 0,
                static_cast<std::uint32_t>(i)
            ));
            draw_list_.add(view::billboard_item(
                camera_, projection_, view::Layer::unit, unit.position.x,
                unit.position.y, elevation, 0,
                static_cast<std::uint32_t>(i)
            ));
        }
        draw_list_.sort();
    }

    // Renders one full frame into `disp`. `skip_unit` is left undrawn, which
    // is what lets an animation draw that unit somewhere else.
    void render(
        surface_t* disp,
        const sim::EncounterSnapshot& snapshot,
        sim::UnitId skip_unit,
        sim::UnitId skip_other = 0
    ) {
        build_draw_list(snapshot, skip_unit, skip_other);
        render_board_rdpq(disp, snapshot);
        // The markers and the health bars are CPU overlays under both
        // renderers, drawn over the settled board in the same depth order the
        // tokens themselves were.
        for (int i = 0; i < draw_list_.size(); ++i) {
            const view::DrawItem& item = draw_list_[i];
            if (item.layer != view::Layer::unit) continue;
            draw_unit(disp, snapshot, snapshot.units[item.subject]);
        }
        graphics_set_color(colour(245, 234, 210), 0);
        char status[64];
        // What the character in hand has left, asked of the engine rather than
        // read off the side.
        //
        // `EncounterSnapshot::remaining_action_points` is the side's budget and
        // is the whole answer only under the orders that commit the side to one
        // character at a time. Under `side_blocks` nobody holds an activation,
        // that field is zero for the entire block, and a status line reading it
        // would print `AP 0` over a company with every point still to spend.
        // `action_points_left` is the rule itself: the side-wide budget for
        // whoever is holding one, and otherwise the character's own budget less
        // what its own turn has spent. With empty hands there is no character to
        // answer for and the line says zero, which is what an unheld board has.
        const unsigned points = static_cast<unsigned>(
            sim::action_points_left(snapshot, selected_)
        );
        // Whose turn it is, asked as the player's own question. The side that
        // holds the board is compared against the side the player holds, not
        // against `Side::first`. The two coincide only while the player is the
        // first side, and a line reading YOUR TURN over the opponent's
        // activation is worse than no line at all.
        const char* whose =
            snapshot.active_side == player_side_ ? "YOUR" : "ENEMY";
        // The round the player is in, plus how many there are to outlast where
        // the content says the battle is won by outlasting a number of them. A
        // board that authors no such objective prints the shorter line.
        if (rounds_to_survive_ != 0U) {
            std::snprintf(
                status, sizeof status, "%s TURN  ROUND %lu/%lu  AP %u", whose,
                static_cast<unsigned long>(snapshot.round + 1),
                static_cast<unsigned long>(rounds_to_survive_), points
            );
        } else {
            std::snprintf(
                status, sizeof status, "%s TURN  ROUND %lu  AP %u", whose,
                static_cast<unsigned long>(snapshot.round + 1), points
            );
        }
        graphics_draw_text(disp, 16, 4, status);
        // What the player took out of the menu and has not yet aimed. A player
        // who cannot see what the next press will do is a player who has lost
        // the thread.
        //
        // **On its own line under the board rather than at the far end of the
        // status line**, where it does not fit: right-aligned at the top,
        // `PICK A TARGET` begins at column 200 and `YOUR TURN  ROUND 1  AP 1`
        // ends at 208, so the prompt eats the number of action points and the
        // screen reads `AP BICK A TARGET`. The room at the top was measured
        // against the shortest of these prompts and there are longer ones.
        // Under the board there is a whole line nothing else uses while a pick
        // is held: the action menu is closed by definition, since taking a row
        // is what sets this text, and the menu is the only other thing that
        // draws there.
#ifndef GRANDLEON_N64_PROBE
        if (aim_text_[0] != '\0') {
            graphics_set_color(colour(242, 193, 78), 0);
            graphics_draw_text(disp, 16, 228, aim_text_);
        }
#endif
    }

#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
    // A unit type drawn in this ROM's own combination, which is the roster
    // table rather than any row of the extras. Deliberately not the same
    // answer as "a drawing this ROM does not carry", which is
    // `extra_drawing_count` and is a fault the constructor already refused.
    static constexpr std::size_t project_drawing =
        static_cast<std::size_t>(-1);

    // Which drawing beyond this ROM's own combination a unit type wants,
    // `project_drawing` for one drawn in the project's own, or
    // `extra_drawing_count` for one this ROM does not carry.
    //
    // Read, not derived. The package's presentation section says which style
    // and which figure each unit type is drawn in, the compiler having already
    // applied "the character's own, else the game's"; this asks it, exactly as
    // its callers ask it which archetype and which colour. There is no second
    // lookup and no rule restated here.
    //
    // Outside the renderer's own block, because the board is drawn by one
    // renderer and the dialogue portrait by both: a portrait is CPU-decoded
    // whichever renderer draws the board, and it resolves a character through
    // this same function rather than through a second one beside it.
    [[nodiscard]] std::size_t extra_slot(
        std::uint64_t unit_type_id,
        std::size_t archetype,
        std::size_t faction
    ) const {
        const std::uint8_t named_style =
            shown_.character_style_of_unit_type(unit_type_id);
        const std::uint8_t named_figure =
            shown_.character_figure_of_unit_type(unit_type_id);
        // A package that carries no drawing record says every character
        // follows the game's, which is this ROM's own combination.
        const std::uint8_t style =
            named_style == pr::character_style_unresolved
                ? static_cast<std::uint8_t>(GRANDLEON_N64_PROJECT_STYLE)
                : named_style;
        const std::uint8_t figure =
            named_figure == pr::character_figure_unresolved
                ? static_cast<std::uint8_t>(GRANDLEON_N64_PROJECT_FIGURE)
                : named_figure;
        if (style == GRANDLEON_N64_PROJECT_STYLE &&
            figure == GRANDLEON_N64_PROJECT_FIGURE) {
            return project_drawing;
        }
        return extra_drawing_at(
            style,
            figure,
            static_cast<std::uint8_t>(archetype),
            static_cast<std::uint8_t>(faction)
        );
    }

#endif

    [[nodiscard]] sprite_t* terrain_sprite(std::uint64_t id) const {
        const std::uint8_t kind = kind_of(id);
        return kind < terrain_kind_count ? terrain_sprites_[kind] : nullptr;
    }

    // Archetype and faction, never a unit type identity: content the package
    // says nothing about still draws as a knight in its side's colours.
    [[nodiscard]] sprite_t* unit_sprite(const sim::UnitSnapshot& unit) const {
        const std::uint8_t named =
            shown_.archetype_of_unit_type(unit.unit_type_id);
        const std::uint8_t colour =
            shown_.colour_of_unit_type(unit.unit_type_id);
        const std::size_t archetype = named < archetype_count ? named : 0;
        const std::size_t faction =
            colour < faction_count
                ? colour
                : (unit.side == sim::Side::first ? 0U : 1U);
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
        const std::size_t slot =
            extra_slot(unit.unit_type_id, archetype, faction);
        if (slot < extra_drawing_count) {
            return extra_sprites_[slot];
        }
#endif
        return unit_sprites_[archetype][faction];
    }

    // The same unit's sequence sheet, on the same key. Nothing about the key
    // changes: a sequence is addressed by the archetype and faction colour its
    // standing sprite is addressed by, which is what lets the CMake walk assert
    // the two tables have the same shape.
    [[nodiscard]] sprite_t* unit_frame_sheet(
        const sim::UnitSnapshot& unit
    ) const {
        const std::uint8_t named =
            shown_.archetype_of_unit_type(unit.unit_type_id);
        const std::uint8_t colour =
            shown_.colour_of_unit_type(unit.unit_type_id);
        const std::size_t archetype = named < archetype_count ? named : 0;
        const std::size_t faction =
            colour < faction_count
                ? colour
                : (unit.side == sim::Side::first ? 0U : 1U);
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
        const std::size_t slot =
            extra_slot(unit.unit_type_id, archetype, faction);
        if (slot < extra_drawing_count) {
            return extra_frames_[slot];
        }
#endif
        return unit_frames_[archetype][faction];
    }

#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
    // The drawing this ROM's own combination holds for that unit, whatever
    // combination the package asked for. Only a check wants this: it is what
    // the unit *would* have been drawn as before it could ask for anything
    // else, and the framebuffer census compares against it to say that the
    // picture on screen is the one the package chose rather than that one.
    //
    // The two lines of key resolution are repeated from `unit_sprite` rather
    // than factored out of it, deliberately: factoring would rewrite a
    // function that every ROM compiles, and this whole section exists only in
    // a ROM that carries a second combination.
    // Whether the census could tell those two drawings apart at all: none of
    // the texels it would accept for one may be a texel it would accept for
    // the other. Where that fails, a matching pixel says nothing about which
    // of the two was drawn, and the census says so rather than claiming it.
    [[nodiscard]] bool tellable_apart(sprite_t* chosen, sprite_t* own) const {
        const int base = sampled_texel();
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                int tx = base + dx;
                int ty = base + dy;
                if (tx > 31) tx = 31;
                if (ty > 31) ty = 31;
                if (sample_matches(texel16(chosen, tx, ty), own, 0, 0)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] sprite_t* roster_sprite(const sim::UnitSnapshot& unit) const {
        const std::uint8_t named =
            shown_.archetype_of_unit_type(unit.unit_type_id);
        const std::uint8_t colour =
            shown_.colour_of_unit_type(unit.unit_type_id);
        const std::size_t archetype = named < archetype_count ? named : 0;
        const std::size_t faction =
            colour < faction_count
                ? colour
                : (unit.side == sim::Side::first ? 0U : 1U);
        return unit_sprites_[archetype][faction];
    }

    // Whether this ROM carries every drawing that unit type could be drawn as.
    //
    // "Could be" rather than "is", because a unit type naming no faction has
    // no colour in the package on purpose: the client draws it in the column
    // of whichever side it turns up on, so both of those columns have to be
    // here. That is the same widening the build applied when it chose what to
    // embed, asked from the other end.
    [[nodiscard]] bool embedded(std::uint64_t unit_type_id) const {
        const std::uint8_t named =
            shown_.archetype_of_unit_type(unit_type_id);
        const std::size_t archetype = named < archetype_count ? named : 0;
        const std::uint8_t colour =
            shown_.colour_of_unit_type(unit_type_id);
        if (colour < faction_count) {
            return extra_slot(unit_type_id, archetype, colour) !=
                   extra_drawing_count;
        }
        return extra_slot(unit_type_id, archetype, 0) != extra_drawing_count &&
               extra_slot(unit_type_id, archetype, 1) != extra_drawing_count;
    }
#endif

    // The frame the board's periodic presentation is drawn at. It is the
    // cursor pulse's own counter: it starts at zero every time a board is
    // drawn, advances only while a player is holding that board, and is put
    // back to zero by the autopilot's checkpoint. A probe ROM draws one board
    // and holds it, so it is always at rest.
    [[nodiscard]] std::uint32_t animation_frame() const {
#ifdef GRANDLEON_N64_PROBE
        return 0U;
#else
        return pulse_frame_;
#endif
    }

    // Animated water, presentation-side. Four entries of the water sheet's own
    // sixteen-entry lookup table are rotated; the sheet's texels are untouched
    // and the generator's palette is unpermuted. Which four is published by the
    // art library per theme (`grandleon_water_cycle_tlut`), measured off the
    // sheet's own palette rather than assumed, so a theme whose water stopped
    // spending the whole ramp would fail the art build rather than shimmer the
    // wrong colours here.
    //
    // At phase zero this writes nothing at all, which is the whole reason every
    // pixel expectation in the checks stood without being loosened: a
    // checkpoint settles the counter first, so what it photographs is the
    // palette `rdpq_sprite_upload` just wrote.
    void rotate_water_tlut() {
        const std::uint32_t frame = animation_frame();
        if (view::water_cycle_phase(frame) == 0) return;
        sprite_t* sheet = terrain_sprites_[water_kind_];
        if (sheet == nullptr) return;
        const std::uint16_t* source = sprite_get_palette(sheet);
        if (source == nullptr) return;
        for (int entry = 0; entry < water_tlut_entries; ++entry) {
            water_tlut_[entry] = source[entry];
        }
        for (int slot = 0; slot < view::water_cycle_entries; ++slot) {
            const int into = grandleon_water_cycle_tlut[theme_][slot];
            const int from = grandleon_water_cycle_tlut[theme_]
                [view::water_cycle_source(slot, frame)];
            if (into < 0 || into >= water_tlut_entries) continue;
            if (from < 0 || from >= water_tlut_entries) continue;
            water_tlut_[into] = source[from];
        }
        // Out of the data cache and into RDRAM before the RDP is pointed at
        // it, for the reason `remember_spent_tlut` spells out: `LOAD_TLUT`
        // carries a physical address and the RDP never sees the processor's
        // cache. One buffer is enough here where it is not enough for a spent
        // character, because the phase is the frame, so every rotation inside
        // a frame writes the same four entries, and the frame is finished with
        // `rdpq_detach_wait` before the next one writes different ones.
        data_cache_hit_writeback(water_tlut_, sizeof water_tlut_);
        rdpq_tex_upload_tlut(water_tlut_, 0, water_tlut_entries);
    }

    // The board through the RDP, straight down the draw list: scaled textured
    // rectangles for the cells, then one shadow under every unit, then the
    // alpha-compared billboards. Cells whose terrain no sheet claims stay
    // background, which classify() would count as unknown. Markers and health
    // bars are drawn by draw_unit as CPU overlays on the settled RDP output.
    //
    // The list is grouped by sheet inside each elevation band, so a flat board
    // still costs exactly one TMEM upload per terrain sheet per frame, the
    // arrangement the scratch ROM measured at 1.7-2.0 ms for a full board. A
    // board with high ground costs one more per sheet per band it uses.
    void render_board_rdpq(
        surface_t* disp,
        const sim::EncounterSnapshot& snapshot
    ) {
        rdpq_attach(disp, nullptr);
        rdpq_clear(color_t{16, 26, 27, 255});
        rdpq_set_mode_standard();
        rdpq_mode_tlut(TLUT_RGBA16);
        // Dithering stays off on both layers. The TLUT is RGBA16 and the
        // framebuffer is RGBA5551, so an unblended texel round-trips
        // bit-exactly. There is nothing for a dither pattern to recover, and
        // switching one on would only put noise into the low bit of every
        // channel and take every exact pixel assertion with it.
        rdpq_mode_dithering(DITHER_NONE_NONE);
        // The ground is filtered and the figures are not, and the two are
        // separate decisions taken off emulator screenshots.
        //
        // Terrain is a 32-texel cell drawn into a 25-pixel one, so seven of
        // every thirty-two texel columns are dropped by point sampling. That
        // is exactly what breaks a one-pixel-wide wave crest into dashes.
        // Bilinear carries every texel into the picture at some weight, and
        // water is where that shows: the crests read as continuous swells
        // rather than as a dotted line, and the horizontal seam between two
        // water cells disappears.
        rdpq_mode_filter(FILTER_BILINEAR);
        int uploaded_sheet = -1;
        bool alpha_compared = false;
        for (int i = 0; i < draw_list_.size(); ++i) {
            const view::DrawItem& item = draw_list_[i];
            if (item.layer == view::Layer::terrain) {
                if (item.sheet < 0 ||
                    static_cast<std::size_t>(item.sheet) >=
                        terrain_kind_count) {
                    continue;
                }
                if (uploaded_sheet != item.sheet) {
                    rdpq_sprite_upload(
                        TILE0, terrain_sprites_[item.sheet], nullptr
                    );
                    uploaded_sheet = item.sheet;
                    // The one place animated terrain costs anything: the water
                    // sheet's own lookup table is overwritten with a rotated
                    // copy, immediately after the upload that wrote the
                    // unrotated one and before a single rectangle of it is
                    // drawn. Per texture, so nothing but water moves; at phase
                    // zero it does not fire at all, so a settled board is drawn
                    // from exactly the palette the sprite carries.
                    if (item.sheet == water_kind_) rotate_water_tlut();
                }
                const int s0 = item.variant * 32;
                rdpq_texture_rectangle_scaled(
                    TILE0, item.x, item.y, item.x + item.w, item.y + item.h,
                    s0, 0, s0 + 32, 32
                );
                continue;
            }
            if (!alpha_compared) {
                rdpq_mode_alphacompare(1);
                // And point from here down. A billboard is a figure with a
                // one-texel ink outline and a hard alpha edge: filtering it
                // blends the outline into the fill and blends the transparent
                // index into the rim, which reads as a soft dark halo and
                // loses the face, the blade and the shield rim outright. The
                // screenshots that settled it are in the change's design note.
                rdpq_mode_filter(FILTER_POINT);
                alpha_compared = true;
            }
            if (item.layer == view::Layer::shadow) {
                // Every shadow is one sprite, and the layer stack puts them
                // all together, so the whole board's shadows cost one upload.
                blit_sprite(shadow_sprite_, item.x, item.y);
            } else {
                const sim::UnitSnapshot& unit = snapshot.units[item.subject];
                if (unit.has_acted) {
                    blit_sprite_spent(unit_sprite(unit), item.x, item.y);
                    // The greyed table was written over whatever palette slot
                    // zero was holding, so the next terrain sheet has to be
                    // uploaded again even where it is the sheet already there.
                    // Only this path invalidates it, because only this path
                    // writes a table the sheet did not.
                    uploaded_sheet = -1;
                } else {
                    blit_sprite(unit_sprite(unit), item.x, item.y);
                }
            }
        }
        rdpq_detach_wait();
    }

    // One entry of a drawing's palette, as a character that has already taken
    // its turn is drawn in it. `view::spent_grey` is the whole rule and lives
    // in the shared view module, where the host suite pins it and where a
    // second console can read the same definition; this is only the RGBA5551
    // packing around it.
    //
    // The alpha bit is carried through untouched. A transparent texel has to
    // stay transparent or a greyed billboard would be a grey square, and the
    // alpha compare that cuts the figure out of its 32x32 sheet reads exactly
    // that bit.
    [[nodiscard]] static std::uint16_t spent_entry(std::uint16_t entry) {
        const int grey = view::spent_grey(
            expand5(entry, 11), expand5(entry, 6), expand5(entry, 1)
        );
        const auto channel = static_cast<std::uint16_t>(grey >> 3);
        return static_cast<std::uint16_t>(
            (channel << 11) | (channel << 6) | (channel << 1) | (entry & 1U)
        );
    }

    // The greyed lookup table one drawing is spent in, or null for a drawing
    // this presenter never loaded.
    //
    // Greying is a property of a drawing rather than of a frame, so the tables
    // are built once beside the sprites and this is a lookup rather than a
    // computation. The list is one entry per drawing the ROM carries, a dozen
    // at the outside, and it is walked rather than indexed because the caller
    // has a sprite in hand and the archetype and colour that chose it are two
    // frames further up.
    [[nodiscard]] std::uint16_t* spent_tlut_of(sprite_t* drawing) {
        for (std::size_t i = 0; i < spent_tlut_count_; ++i) {
            if (spent_tluts_[i].drawing == drawing) return spent_tluts_[i].tlut;
        }
        return nullptr;
    }

    // Assumes an attached surface with TLUT and alpha-compare configured.
    // One 32x32 sprite scaled into one cell, drawn through a greyed copy of its
    // own palette rather than through the palette it carries.
    //
    // Spelled out rather than handed to `rdpq_sprite_blit`, for the one reason
    // the terrain path is spelled out too: the lookup table has to be written
    // *between* the texture upload and the rectangle, and a call that does both
    // leaves no seam to write it in. It is the same two-step
    // `rotate_water_tlut` already uses, over a character's palette instead of
    // the water's.
    //
    // Nothing about the sprite is copied or rewritten: the texels in cartridge
    // ROM are the texels the board has always drawn, and what changes is the
    // sixteen entries they index. So a spent character is its own drawing,
    // which is what lets the probe recompute the pixel rather than be told it.
    //
    // The table handed over is the drawing's own and is never written again.
    // That is a hardware requirement rather than tidiness: `LOAD_TLUT` is an
    // RDP command carrying a *physical address*, and the RDP reads it whenever
    // it gets to it, which is behind the processor by however far the command
    // queue is long. A table computed into one shared buffer immediately
    // before each rectangle would be overwritten by the next character while
    // the first was still waiting to be drawn, and every spent figure in the
    // frame would come out in the last one's palette.
    void blit_sprite_spent(sprite_t* sprite, int x, int y) {
        if (sprite == nullptr) return;
        std::uint16_t* greyed = spent_tlut_of(sprite);
        if (greyed == nullptr) {
            blit_sprite(sprite, x, y);
            return;
        }
        rdpq_sprite_upload(TILE0, sprite, nullptr);
        rdpq_tex_upload_tlut(greyed, 0, spent_tlut_entries);
        rdpq_texture_rectangle_scaled(
            TILE0, x, y, x + tile_, y + tile_, 0, 0, 32, 32
        );
    }

    // The greyed table for one drawing, built and put where the RDP can read
    // it. Called once per drawing, as the drawings are loaded.
    //
    // The write-back is the whole reason this is a function rather than a loop
    // in the constructor. Everything the processor writes lands in the data
    // cache and stays there until the line is evicted; the RDP reads RDRAM
    // over the bus and cannot see the cache at all. A table left dirty is a
    // table the RDP reads as whatever RDRAM held before it: all-zero entries
    // read as alpha zero, and the alpha compare that cuts a billboard out of
    // its sheet then cuts away the whole figure, so the character is *absent*
    // rather than mis-coloured, and stays absent until something else evicts
    // the line.
    void remember_spent_tlut(sprite_t* drawing) {
        if (drawing == nullptr) return;
        if (spent_tlut_count_ >= spent_tlut_capacity) return;
        if (spent_tlut_of(drawing) != nullptr) return;
        const std::uint16_t* source = sprite_get_palette(drawing);
        if (source == nullptr) return;
        SpentTlut& built = spent_tluts_[spent_tlut_count_++];
        built.drawing = drawing;
        for (int entry = 0; entry < spent_tlut_entries; ++entry) {
            built.tlut[entry] = spent_entry(source[entry]);
        }
        data_cache_hit_writeback(built.tlut, sizeof built.tlut);
    }

    // Assumes an attached surface with TLUT and alpha-compare configured.
    // One 32x32 sprite scaled into one cell, which is every billboard this
    // renderer draws: a unit, or the shadow it stands on.
    void blit_sprite(sprite_t* sprite, int x, int y) {
        if (sprite == nullptr) return;
        rdpq_blitparms_t parms{};
        parms.scale_x = static_cast<float>(tile_) / 32.0f;
        parms.scale_y = parms.scale_x;
        rdpq_sprite_blit(
            sprite, static_cast<float>(x), static_cast<float>(y), &parms
        );
    }

    // One cell of a sequence sheet, into one cell of the board. The strip is a
    // wide texture and the cell is a window into it, which is the same trick
    // the terrain sheets use to pick a variant, so an animation cell costs a
    // texture-coordinate offset rather than a second sprite object, a second
    // palette, or a second TMEM upload.
    void blit_sprite_cell(sprite_t* sheet, int cell, int x, int y) {
        if (sheet == nullptr) return;
        rdpq_blitparms_t parms{};
        parms.scale_x = static_cast<float>(tile_) / 32.0f;
        parms.scale_y = parms.scale_x;
        parms.s0 = cell * 32;
        parms.width = 32;
        parms.height = 32;
        rdpq_sprite_blit(
            sheet, static_cast<float>(x), static_cast<float>(y), &parms
        );
    }

    // Where the pixel every probe samples lands inside the texture, in the
    // texture unit's own arithmetic.
    //
    // libdragon writes a TEXRECT whose step is
    // `((s1 - s0) << 7) / (x1 - x0)` with the source in 10.5 and the screen in
    // 10.2 (`rdpq_rect.h`), which is the step in 5.10 per screen pixel; the
    // texture unit accumulates it and keeps five fractional bits. For a 32-
    // texel cell drawn into a `tile_`-pixel one that is
    //
    //     step  = (32 * 32 << 7) / (tile_ * 4)
    //     coord = (tile_ / 2) * step        // the sampled pixel is the centre
    //
    // and on the console's 25-pixel cell it comes out at texel 15 and 11/32 of
    // the way to texel 16, in both axes. Derived rather than measured, and the
    // checks are what prove the derivation: a probe that had it wrong would
    // fail on every cell rather than drift.
    //
    // Two properties fall out of the same arithmetic and are worth stating,
    // because they are why filtering the ground costs nothing anywhere else.
    // The last pixel of a cell, at `tile_ - 1`, reaches texel 30 and 31, so a
    // filtered cell **never samples past its own 32 columns**, and a variant
    // cannot bleed into the variant beside it on the sheet. And the sampled
    // pixel is the cell's centre, which every overlay in this ROM is drawn
    // clear of.
    struct FilterTap {
        int texel;  ///< The lower texel of the filtered pair, in both axes.
        int frac;   ///< How far towards the upper one, in thirty-seconds.
    };

    [[nodiscard]] FilterTap centre_tap() const {
        if (tile_ <= 0) return {0, 0};
        const int step = ((32 * 32) << 7) / (tile_ * 4);
        const int coord = (tile_ / 2) * step;
        return {coord >> 10, (coord >> 5) & 31};
    }

    // The same coordinate, rounded to the nearest thirty-second instead of
    // truncated, which is what the *filtered* comparison needs.
    //
    // `coord` carries five bits below the fraction a tap holds. Point sampling
    // wants them thrown away: the texture unit takes the texel a coordinate
    // falls in, and that is the one `centre_tap` names. The three-point filter
    // does not work that way. At a coordinate all but arrived at the next
    // texel it writes that texel, and a tap that truncated asks instead for a
    // blend most of the way back to the one below.
    //
    // On the 25-pixel cell a board that fits draws at, the discarded bits hold
    // 8/32 and the two agree, which is why one tap served both for as long as
    // every board fitted. On the 18-pixel cell a scrolling board draws at they
    // hold 28/32: the sample stands at texel 15 and 31.875 thirty-seconds, the
    // console writes the texel at 16 exactly, and the truncated tap predicted a
    // blend that differed from it in red.
    //
    // Measured, not reasoned: the probe was made to report what the framebuffer
    // held against both candidates, and the framebuffer held the texel at 16.
    // Water is where it shows, its neighbouring texels being the furthest apart
    // of any terrain drawn here; on grass, road and forest the two answers agree
    // and a wrong tap is invisible.
    [[nodiscard]] FilterTap filter_tap() const {
        if (tile_ <= 0) return {0, 0};
        const int step = ((32 * 32) << 7) / (tile_ * 4);
        const int coord = (tile_ / 2) * step + 16;
        return {coord >> 10, (coord >> 5) & 31};
    }

    // The lower texel of the block the sampled pixel can land in, in both
    // axes, for a point-sampled rectangle.
    //
    // The same arithmetic as the filtered case and deliberately the same
    // function: the texture unit accumulates one step whatever the filter is,
    // and what point sampling changes is only which of the surrounding texels
    // reaches the pixel, not where the coordinate lands. Dividing the half-
    // tile offset in screen pixels instead (`((tile_ / 2) * 32) / tile_`)
    // agrees on a 25-pixel cell and is one texel high on a 22-pixel one,
    // because it rounds the offset before the step's own fixed point does.
    // A board large enough to shrink the cell would then be compared against
    // the wrong column of every character on it.
    [[nodiscard]] int sampled_texel() const { return centre_tap().texel; }

    // What the RDP's three-point filter writes at that pixel, exactly.
    //
    // The texture unit expands each 5-bit channel of the TLUT entry to 8 bits
    // by replication, takes three of the four texels around the sample (the
    // triangle the sample falls in), and the blender truncates the result back
    // to RGBA5551 on the way into the framebuffer. Every step is integer, so
    // this returns the one value the framebuffer may hold and the probe
    // compares for equality. It is a *stronger* assertion than the point-
    // sampled one below, which had to accept any of a 2x2 block.
    [[nodiscard]] std::uint16_t filtered_sample(
        sprite_t* sprite,
        int s_origin,
        int t_origin
    ) const {
        const FilterTap tap = filter_tap();
        const int s = tap.texel > 30 ? 30 : tap.texel;
        const int t = tap.texel > 30 ? 30 : tap.texel;
        const std::uint16_t t00 = texel16(sprite, s_origin + s, t_origin + t);
        const std::uint16_t t10 =
            texel16(sprite, s_origin + s + 1, t_origin + t);
        const std::uint16_t t01 =
            texel16(sprite, s_origin + s, t_origin + t + 1);
        const std::uint16_t t11 =
            texel16(sprite, s_origin + s + 1, t_origin + t + 1);
        std::uint16_t out = 1;  // RGBA5551: the alpha bit an opaque texel has.
        for (int shift = 11; shift >= 1; shift -= 5) {
            const int a = expand5(t00, shift);
            const int b = expand5(t10, shift);
            const int c = expand5(t01, shift);
            const int d = expand5(t11, shift);
            const int f = tap.frac;
            const int blended =
                f + f < 32
                    ? (a * 32 + f * (b - a) + f * (c - a)) / 32
                    : (d * 32 + (32 - f) * (c - d) + (32 - f) * (b - d)) / 32;
            int channel = blended >> 3;
            if (channel > 31) channel = 31;
            if (channel < 0) channel = 0;
            out = static_cast<std::uint16_t>(out | (channel << shift));
        }
        return out;
    }

    // One RGBA5551 channel as the texture unit sees it: five bits carried up
    // to eight by replication, so 31 is 255 and 0 is 0.
    [[nodiscard]] static int expand5(std::uint16_t value, int shift) {
        const int channel = (value >> shift) & 31;
        return (channel << 3) | (channel >> 2);
    }

    // A point-sampled scaled rectangle maps the sampled pixel to one texel of
    // a known 2x2 block: the fixed-point step can land the half-tile offset on
    // either neighbour. Equality against the block's palette values is still
    // an exact check. This is the billboards' check: they are drawn
    // point-sampled and deliberately.
    // `spent` asks for the greyed reading of the same four texels, which is
    // what a character that has already taken its turn is drawn in. It is a
    // parameter rather than a second function because there is one block of
    // texels either way and the only difference is what the palette entry is
    // put through, and because a caller that got the flag wrong should fail
    // here rather than quietly compare against the other picture.
    [[nodiscard]] bool sample_matches(
        std::uint16_t value,
        sprite_t* sprite,
        int s_origin,
        int t_origin,
        bool spent = false
    ) const {
        const int base = sampled_texel();
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                int tx = base + dx;
                int ty = base + dy;
                if (tx > 31) tx = 31;
                if (ty > 31) ty = 31;
                const std::uint16_t texel =
                    texel16(sprite, s_origin + tx, t_origin + ty);
                if (value == (spent ? spent_entry(texel) : texel)) return true;
            }
        }
        return false;
    }

#ifndef GRANDLEON_N64_PROBE
    // One token, drawn somewhere other than the cell it stands in. Every
    // gesture animates through here, drawing the same shadow and billboard the
    // board draws, so a slide and a flinch cannot end up meaning something
    // other than what the board means.
    void draw_token_at(
        surface_t* disp, const sim::UnitSnapshot& unit, int x, int y,
        int cell = view::sequence_cell_stand
    ) {
        rdpq_attach(disp, nullptr);
        rdpq_set_mode_standard();
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_dithering(DITHER_NONE_NONE);
        rdpq_mode_alphacompare(1);
        // The shadow travels with the unit, so the token stays grounded while
        // it moves and a step onto high ground reads as a climb rather than a
        // jump. The shadow never poses: the silhouettes differ above the
        // ankles, and a shadow that walked with the feet would be a second
        // sequence for no legibility at all.
        blit_sprite(shadow_sprite_, x, y);
        if (cell >= 0 && cell < view::sequence_cell_count) {
            blit_sprite_cell(unit_frame_sheet(unit), cell, x, y);
        } else {
            blit_sprite(unit_sprite(unit), x, y);
        }
        rdpq_detach_wait();
    }

    // One leg of a slide: `frames` drawn frames carrying the token from one
    // pixel position to the next, ending exactly on it. The board underneath
    // is the pre-event snapshot with this unit left out, which is what lets the
    // token be somewhere between two cells at all.
    // `elapsed` and `total` are the whole move's frame count, not this leg's,
    // because the walk cycle belongs to the move: the cell alternates once a
    // tile, so a four-tile route takes four steps rather than restarting on
    // each. Both are zero for a caller that has no route to count, which draws
    // the sequence from its own start.
    void slide_token(
        const sim::UnitSnapshot& unit,
        int from_x,
        int from_y,
        int to_x,
        int to_y,
        int frames,
        int elapsed = 0,
        int total = 0
    ) {
        const int whole = total > 0 ? total : frames;
        for (int frame = 1; frame <= frames; ++frame) {
            surface_t* disp = display_get();
            render(disp, previous_, unit.id);
            draw_token_at(
                disp, unit,
                view::slide_between(from_x, to_x, frame, frames),
                view::slide_between(from_y, to_y, frame, frames),
                view::walk_cell(elapsed + frame, whole)
            );
            grandleon::n64audio::pump();
            show(disp);
        }
    }

    // The route a slide is drawn along, derived from the simulation's own
    // reachability answer for the state the unit moved out of, plus the tiles
    // the mover's own side holds. A walk goes through those and never stops on
    // them, so the query does not list them and a route filing past a fellow
    // would otherwise find a hole where it needed to go. Nothing here knows
    // what terrain is or how far a unit may walk; it only knows which tiles
    // came back from the query and who was standing where.
    //
    // The scratch is fixed and lives on the presenter, so an animation never
    // allocates. A board with more cells than the scratch holds leaves the
    // route empty, and the caller draws the straight line.
    void plan_move_route(
        const sim::UnitSnapshot& actor,
        sim::Position destination,
        view::Route& route
    ) {
        route.clear();
        const int width = static_cast<int>(previous_.width);
        const int height = static_cast<int>(previous_.height);
        if (width <= 0 || height <= 0) return;
        if (width * height > route_cells) return;
        const std::vector<sim::Position> reach =
            sim::reachable_tiles(previous_, actor.id);
        if (reach.empty()) return;
        for (int i = 0; i < width * height; ++i) route_reachable_[i] = 0;
        const auto mark = [this, width, height](sim::Position tile) {
            if (tile.x < 0 || tile.y < 0 || tile.x >= width ||
                tile.y >= height) {
                return;
            }
            route_reachable_[tile.y * width + tile.x] = 1;
        };
        for (const sim::Position tile : reach) mark(tile);
        for (const sim::UnitSnapshot& other : previous_.units) {
            if (other.id == actor.id || other.side != actor.side) continue;
            if (!sim::on_board(other)) continue;
            mark(other.position);
        }
        view::plan_route(
            actor.position.x, actor.position.y, destination.x, destination.y,
            width, height,
            [this, width](int x, int y) {
                return route_reachable_[y * width + x] != 0;
            },
            route_distance_, route
        );
    }

    // Small, honest animations: a moved token walks its route, a hit lands with
    // a flinch, a defeat blinks out. They draw from the pre-event snapshot,
    // which is why draw() keeps a copy; the session's own draw supplies the
    // settled frame after.
    // The three gestures, as one routine. A swing opens with nothing, a shot
    // opens with a bolt crossing the board, a cast opens with the caster
    // holding a pose; all three resolve the same way, and all three end with
    // the board exactly at rest.
    //
    // Which of the three is *derived*, not reported. `client::gesture_for` asks
    // the one question a presenter can answer from what it already holds:
    // could a damaging magical ability this striker knows have crossed this
    // separation? It asks it over the very ability records `battle_definitions`
    // handed this ROM before the first frame. The shared client the other two
    // consoles compile calls the same fold, so the same blow is the same
    // picture on every machine, and the host derivation that produces this
    // ROM's expected frames calls it too.
    //
    // The pose comes out of the sequence sheet; the marks are
    // `graphics_draw_box` overlays on the settled RDP output, which is why a
    // travelling projectile and an opening flare cost no sheet at all.
    void play_attack(const sim::Event& event, bool landed) {
        if (!camera_.visible(event.position.x, event.position.y)) return;
        const sim::UnitSnapshot* struck = nullptr;
        const sim::UnitSnapshot* striker = nullptr;
        for (const sim::UnitSnapshot& unit : previous_.units) {
            if (unit.id == event.unit_id) struck = &unit;
            if (event.related_unit_id != 0 &&
                unit.id == event.related_unit_id) {
                striker = &unit;
            }
        }
        int toward_x = 0;
        int toward_y = 0;
        if (struck != nullptr && striker != nullptr) {
            toward_x = struck->position.x - striker->position.x;
            toward_y = struck->position.y - striker->position.y;
        }
        const int separation =
            struck != nullptr && striker != nullptr
                ? client::separation_between(
                      struck->position, striker->position)
                : 0;
        const view::AttackGesture gesture =
            client::gesture_for(striker, abilities_, separation);

        const int cell_x = px(event.position.x);
        const int cell_y = py(event.position.x, event.position.y);
        // The thrower is lifted out of the board with the struck token so it
        // can be drawn posed: the two of them are the two ends of one gesture.
        const bool posed =
            striker != nullptr &&
            camera_.visible(striker->position.x, striker->position.y);
        const int thrower_x = posed ? px(striker->position.x) : 0;
        const int thrower_y =
            posed ? py(striker->position.x, striker->position.y) : 0;

        // ---- the opening -------------------------------------------------
        const int lead = view::gesture_lead_frames(gesture, separation);
        for (int frame = 0; frame < lead; ++frame) {
            surface_t* disp = display_get();
            render(
                disp, previous_,
                struck != nullptr ? struck->id : 0, posed ? striker->id : 0
            );
            if (posed) {
                // A shot holds the lunge for the whole flight, a bow's release
                // being a sword's coil at a longer distance, and a cast holds
                // the cast pose for the whole hold.
                draw_token_at(
                    disp, *striker, thrower_x, thrower_y,
                    gesture == view::AttackGesture::cast
                        ? view::cast_cell(frame)
                        : view::sequence_cell_lunge
                );
            }
            if (struck != nullptr) {
                draw_token_at(disp, *struck, cell_x, cell_y);
            }
            if (gesture == view::AttackGesture::shot) {
                // The bolt. `slide_between` on both axes is the arithmetic a
                // token walks on, and `rise_and_fall` lifts it off the board by
                // one drawn tier of height at the top of the flight and by
                // exactly nothing at either end.
                const int peak = view::projectile_arc_peak(tile_);
                const int bolt = peak > 3 ? peak : 3;
                graphics_draw_box(
                    disp,
                    view::slide_between(thrower_x, cell_x, frame, lead) +
                        (tile_ - bolt) / 2,
                    view::slide_between(thrower_y, cell_y, frame, lead) +
                        (tile_ - bolt) / 2 -
                        view::rise_and_fall(frame, lead, peak),
                    bolt, bolt, colour(245, 234, 210)
                );
            } else if (gesture == view::AttackGesture::cast) {
                // The flare, opening on the target tile: the spell is already
                // there, and what is held is the caster. It swells from nothing
                // and returns to nothing over the hold, so the frame the hold
                // ends draws no flare at all.
                const int width = view::rise_and_fall(
                    frame, lead, view::effect_bloom_peak(tile_)
                );
                if (width > 0) {
                    graphics_draw_box(
                        disp, cell_x + (tile_ - width) / 2,
                        cell_y + (tile_ - width) / 2, width, width,
                        colour(245, 234, 210)
                    );
                }
            }
            grandleon::n64audio::pump();
            show(disp);
        }

        // ---- the resolution ----------------------------------------------
        //
        // A landed blow gets the bone-white cell flash and a miss gets amber,
        // which is how this machine says "that happened and took nothing" in a
        // colour as well as in a duration. A miss lasts `miss_frames`, the
        // shared model's number rather than one of this ROM's own, and the
        // striker throws it either way.
        const int resolve = landed ? view::flinch_frames : view::miss_frames;
        for (int frame = 0; frame < resolve; ++frame) {
            surface_t* disp = display_get();
            render(
                disp, previous_,
                struck != nullptr ? struck->id : 0, posed ? striker->id : 0
            );
            if (frame % 2 == 0) {
                graphics_draw_box(
                    disp, cell_x, cell_y, tile_ - 1, tile_ - 1,
                    landed ? colour(245, 234, 210) : colour(242, 193, 78)
                );
            }
            if (posed) {
                // A swing throws its blow here, over the same three frames the
                // struck token is knocked away for; a shot and a cast have
                // already thrown theirs and stand back up.
                draw_token_at(
                    disp, *striker, thrower_x, thrower_y,
                    gesture == view::AttackGesture::swing
                        ? view::strike_cell(frame)
                        : view::sequence_cell_stand
                );
            }
            if (struck != nullptr) {
                draw_token_at(
                    disp, *struck,
                    cell_x + (landed ? view::flinch_offset(
                                           frame, toward_x, tile_)
                                     : 0),
                    cell_y + (landed ? view::flinch_offset(
                                           frame, toward_y, tile_)
                                     : 0)
                );
            }
            grandleon::n64audio::pump();
            show(disp);
        }

#ifdef GRANDLEON_N64_AUTOPILOT
        // A landed blow, photographed on the renderer's own account.
        //
        // It is tempting to leave this out, on the reasoning that a hit already
        // has a picture either side of it: the script's forecast checkpoint
        // naming the damage, and its struck checkpoint showing the board with
        // the damage taken. **That reasoning is read off one campaign's press
        // table and is false in general.** A press table is authored per
        // campaign and decides for itself what it names: one of the two shipped
        // campaigns names `-forecast` and `-struck` around every blow, and the
        // other names only `-moved` and `-holds`, which are cursor positions
        // and information panels. On that second campaign the entire first
        // battle photographs as somebody browsing a board, ten frames of it,
        // with no blow and no death anywhere in the film.
        //
        // So the renderer cannot rely on the script having pointed a camera at
        // the fight. What is drawn here is the *contact* frame the resolve loop
        // above opens with: the strike thrown, the struck token knocked away,
        // and the cell flashed bone-white. It is repainted steady rather than
        // composed, being frame zero of a loop that has just run rather than a
        // picture invented for the camera.
        //
        // Landed blows only. A miss is already a bounded animation nobody is
        // owed a still of, and the pair that matters for somebody reading stills
        // is a blow connecting and, when it fells, the fall right after it.
        if (landed) {
            surface_t* disp = display_get();
            render(
                disp, previous_,
                struck != nullptr ? struck->id : 0, posed ? striker->id : 0
            );
            graphics_draw_box(
                disp, cell_x, cell_y, tile_ - 1, tile_ - 1,
                colour(245, 234, 210)
            );
            if (posed) {
                draw_token_at(
                    disp, *striker, thrower_x, thrower_y,
                    gesture == view::AttackGesture::swing
                        ? view::strike_cell(0)
                        : view::sequence_cell_stand
                );
            }
            if (struck != nullptr) {
                draw_token_at(
                    disp, *struck,
                    cell_x + view::flinch_offset(0, toward_x, tile_),
                    cell_y + view::flinch_offset(0, toward_y, tile_)
                );
            }
            show(disp);
            report_line("CHECKPOINT struck\n");
            for (int held = 0; held < autopilot_checkpoint_hold; ++held) {
                grandleon::n64audio::pump();
                wait_ms(16);
            }
        }
#endif
    }

    void animate(const sim::Event& event) {
        if (!has_previous_) return;
        const sim::UnitSnapshot* actor = nullptr;
        for (const sim::UnitSnapshot& unit : previous_.units) {
            if (unit.id == event.unit_id) actor = &unit;
        }
        switch (event.type) {
            case sim::EventType::unit_moved: {
                if (actor == nullptr) return;
                grandleon::n64audio::play(grandleon::n64audio::Sfx::move);
                // The camera goes to whoever is walking, rather than this
                // returning and drawing nothing. A side the player is not
                // steering walks wherever the rules send it, and on a board
                // with edges that is regularly out of view: what this used to
                // do was skip the animation entirely, so a whole turn happened
                // somewhere the player could not see and the board simply
                // changed under them.
                bring_into_view(actor->position);
                if (!camera_.visible(actor->position.x, actor->position.y) ||
                    !camera_.visible(event.position.x, event.position.y)) {
                    // Still not both on the screen: a walk whose ends are
                    // further apart than the window is one no single camera
                    // position can hold, and a token drawn half off the edge is
                    // worse than the board changing.
                    return;
                }
                // The token walks the route rather than crossing the board in
                // a straight line, because a straight line walks a footsoldier
                // over the river. The route is not this client's invention: it
                // is a walk over the tiles `sim::reachable_tiles` returned for
                // this unit in the state it moved from, so every tile the
                // token stands on is a tile the engine said it could stand on.
                // When there is no such route (content this presenter has
                // never seen, a board wider than the scratch) the straight
                // line is drawn instead, at the same speed per tile.
                view::RouteTile route_storage[route_capacity];
                view::Route route(route_storage, route_capacity);
                plan_move_route(*actor, event.position, route);

                int from_x = px(actor->position.x);
                int from_y = py(actor->position.x, actor->position.y);
                if (route.size() == 0) {
                    const int dx = event.position.x - actor->position.x;
                    const int dy = event.position.y - actor->position.y;
                    const int tiles =
                        (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    slide_token(
                        *actor, from_x, from_y, px(event.position.x),
                        py(event.position.x, event.position.y),
                        view::slide_frames_for(tiles > 0 ? tiles : 1)
                    );
                    return;
                }
                const int whole = view::slide_frames_for(route.size());
                for (int leg = 0; leg < route.size(); ++leg) {
                    const int to_x = px(route[leg].x);
                    const int to_y = py(route[leg].x, route[leg].y);
                    slide_token(
                        *actor, from_x, from_y, to_x, to_y,
                        view::slide_frames_per_tile,
                        leg * view::slide_frames_per_tile, whole
                    );
                    from_x = to_x;
                    from_y = to_y;
                }
                return;
            }
            case sim::EventType::unit_damaged: {
                grandleon::n64audio::play(grandleon::n64audio::Sfx::hit);
                play_attack(event, true);
                return;
            }
            case sim::EventType::attack_missed: {
                // A miss has to look like something, and the cheap answer is
                // the *only* thing this event seems to say on its own: an amber
                // box, struck once, with nobody visibly swinging at anything.
                // The engine names whoever swung on this very event, so a
                // missed blow is drawn as a blow: thrown, travelled if it had
                // distance to travel, and landing on nothing.
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                play_attack(event, false);
                return;
            }
            case sim::EventType::unit_defeated: {
                // Somebody died, said at the moment it happened and with their
                // name on it. The blink on its own is mute: a token flashes out,
                // and the only other place a name and a loss appear together is
                // the company screen, which on a campaign can be a battle or
                // two later. A player can lose a character and not know which
                // one until then.
                //
                // Which word depends on what the campaign has decided a fall
                // costs, and it is the sentence every other client says at this
                // same instant. Under the permanent rule it is DIED, because it
                // is a death and a player is owed the word. Under the
                // recoverable rule they are down and out of this battle and
                // they are coming back, so FELL. Telling that player they died
                // would be a lie the aftermath screen contradicts.
                //
                // The sentence is the board's own message box: the frame, the
                // backdrop and the cream text a refused command is named in.
                // It is put at the foot of the screen rather than across the
                // middle, because the middle is where the blink it is
                // captioning is most likely to be. The name is cut at twenty
                // characters so the box cannot grow wider than the screen; the
                // roster this reads is a campaign's, and nothing stops an
                // author naming somebody at length.
                char sentence[40];
                std::snprintf(
                    sentence, sizeof sentence, "%.20s %s",
                    ascii_only(character_called(event.unit_id)).c_str(),
                    fall_word(event.unit_id)
                );
                const int width =
                    static_cast<int>(std::strlen(sentence)) * 8 + 16;
                const int left = (320 - width) / 2;
                grandleon::n64audio::play(grandleon::n64audio::Sfx::defeat);
                for (int blink = 0; blink < 6; ++blink) {
                    surface_t* disp = display_get();
                    render(
                        disp, previous_,
                        blink % 2 == 0 ? event.unit_id : 0
                    );
                    graphics_draw_box(
                        disp, left - 2, 198, width + 4, 24, colour(179, 72, 63)
                    );
                    graphics_draw_box(
                        disp, left, 200, width, 20, colour(16, 26, 27)
                    );
                    graphics_set_color(colour(245, 234, 210), 0);
                    graphics_draw_text(disp, left + 8, 206, sentence);
                    grandleon::n64audio::pump();
                    show(disp);
                }
#ifdef GRANDLEON_N64_AUTOPILOT
                // The one moment in a battle the trail could never photograph.
                //
                // A checkpoint is a settled board by construction: the script
                // cannot reach one while anything is still moving. So the
                // blink above has never been in a single frame of a slideshow.
                // What the slideshow shows instead is a bandit standing under a
                // forecast reading `KO` in one still, and a board that simply
                // does not contain him in the next. Read off the stills alone
                // that looks like defeated units never leaving the
                // battlefield: they do leave, and the camera was never once
                // pointed at them leaving. A trail of only settled boards
                // silently omits every event in the game.
                //
                // So the recording instrument draws the dying token once more,
                // steady rather than blinking, with the fall sentence still up,
                // says `CHECKPOINT` so the harness knows to look, and holds it
                // for exactly as long as a scripted checkpoint holds, because
                // the same harness with the same poll is doing the looking.
                //
                // Nothing here is staged. The token, the emphasis and the
                // sentence are what `render` and the blink above already put on
                // screen for every player; this repaints that frame rather than
                // composing a new one, and only the autopilot build does it, so
                // an interactive ROM's pacing is untouched. It is deliberately
                // *not* the pre-blow frame held longer: that frame is a living
                // unit, and holding it would photograph the opposite of the
                // thing being reported.
                //
                // The forecast that named this blow lethal is the checkpoint
                // immediately before, so the two sit adjacent in the trail and
                // a viewer reading stills alone gets `DMG 11 HP 10>0 KO` and
                // then the bandit going down under it.
                //
                // Not done for every landed blow, and that is a judgement
                // rather than an oversight. An ordinary hit already has a
                // photograph either side of it: the forecast that names the
                // damage, and the settled board that shows it taken. One more
                // per blow would roughly double a slideshow whose length is
                // already a cost to whoever reads it. A defeat has neither:
                // nothing after it contains the unit at all.
                {
                    surface_t* disp = display_get();
                    render(disp, previous_, event.unit_id);
                    graphics_draw_box(
                        disp, left - 2, 198, width + 4, 24, colour(179, 72, 63)
                    );
                    graphics_draw_box(
                        disp, left, 200, width, 20, colour(16, 26, 27)
                    );
                    graphics_set_color(colour(245, 234, 210), 0);
                    graphics_draw_text(disp, left + 8, 206, sentence);
                    show(disp);
                    report_line("CHECKPOINT defeat\n");
                    for (int held = 0; held < autopilot_checkpoint_hold;
                         ++held) {
                        grandleon::n64audio::pump();
                        wait_ms(16);
                    }
                }
#endif
                return;
            }
            case sim::EventType::unit_endured: {
                // A blow that would have felled somebody was caught by their
                // health floor. Said for the reason the defeat above is said:
                // otherwise a killing blow lands, the character keeps standing,
                // and nothing anywhere on the screen explains why.
                //
                // It borrows the defeat's message box and nothing else: there
                // is no defeat sound and no blink out, because the whole point
                // is that this is not that. The token is left where it is and
                // the sentence is put under it in the panel's amber rather than
                // the refusal's red, so a glance tells the two apart before a
                // word is read.
                char sentence[40];
                std::snprintf(
                    sentence, sizeof sentence, "%.20s HELD ON",
                    ascii_only(character_called(event.unit_id)).c_str()
                );
                const int width =
                    static_cast<int>(std::strlen(sentence)) * 8 + 16;
                const int left = (320 - width) / 2;
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                for (int beat = 0; beat < 6; ++beat) {
                    surface_t* disp = display_get();
                    render(disp, previous_, 0);
                    graphics_draw_box(
                        disp, left - 2, 198, width + 4, 24, colour(242, 193, 78)
                    );
                    graphics_draw_box(
                        disp, left, 200, width, 20, colour(16, 26, 27)
                    );
                    graphics_set_color(colour(245, 234, 210), 0);
                    graphics_draw_text(disp, left + 8, 206, sentence);
                    grandleon::n64audio::pump();
                    show(disp);
                }
                return;
            }
            case sim::EventType::unit_talked: {
                // Departure has to look like leaving and not like falling, so
                // it borrows nothing from the defeat blink: no defeat sound,
                // and the tile fades in the panel's amber rather than the
                // character flashing out of it. Somebody walked off; the board
                // is simply one lighter for it.
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                if (!camera_.visible(event.position.x, event.position.y)) {
                    return;
                }
                for (int flash = 0; flash < 6; ++flash) {
                    surface_t* disp = display_get();
                    render(disp, previous_, 0);
                    if (flash % 2 == 0) {
                        graphics_draw_box(
                            disp,
                            px(event.position.x),
                            py(event.position.x, event.position.y),
                            tile_ - 1,
                            tile_ - 1,
                            colour(242, 193, 78)
                        );
                    }
                    grandleon::n64audio::pump();
                    show(disp);
                }
                return;
            }
            case sim::EventType::item_dropped: {
                // The defeat blink has just played over this tile. A drop is
                // the moment after it, so it is the same shape in the panel's
                // amber and struck once: something was left where somebody
                // fell.
                grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
                if (!camera_.visible(event.position.x, event.position.y)) {
                    return;
                }
                for (int flash = 0; flash < 4; ++flash) {
                    surface_t* disp = display_get();
                    render(disp, previous_, 0);
                    if (flash % 2 == 0) {
                        graphics_draw_box(
                            disp,
                            px(event.position.x) + 4,
                            py(event.position.x, event.position.y) + 4,
                            tile_ - 9,
                            tile_ - 9,
                            colour(242, 193, 78)
                        );
                    }
                    grandleon::n64audio::pump();
                    show(disp);
                }
                return;
            }
            case sim::EventType::unit_waited: {
                // An enemy that holds still resolves in a single frame, which
                // reads as the turn never having happened. Flash the acting
                // unit so even an idle activation is visible.
                if (actor == nullptr || actor->side == player_side_) return;
                if (!camera_.visible(actor->position.x, actor->position.y)) {
                    return;
                }
                const int x = px(actor->position.x);
                const int y = py(actor->position.x, actor->position.y);
                for (int flash = 0; flash < 6; ++flash) {
                    surface_t* disp = display_get();
                    render(disp, previous_, 0);
                    if (flash % 2 == 0) {
                        const std::uint32_t c = colour(245, 234, 210);
                        graphics_draw_box(disp, x, y, tile_ - 1, 2, c);
                        graphics_draw_box(
                            disp, x, y + tile_ - 3, tile_ - 1, 2, c
                        );
                        graphics_draw_box(disp, x, y, 2, tile_ - 1, c);
                        graphics_draw_box(
                            disp, x + tile_ - 3, y, 2, tile_ - 1, c
                        );
                    }
                    grandleon::n64audio::pump();
                    show(disp);
                    wait_ms(50);
                }
                return;
            }
            default:
                return;
        }
    }

    // Fire Emblem's board grammar, drawn from the engine's own queries so the
    // lit tiles can never disagree with what apply() will accept.
    //
    // **A lit tile is a tile the selected character may step onto**: blue where
    // nothing threatens it, red where something does, and dark where it cannot
    // go at all. The union of the two lit sets is exactly the reachable set, so
    // the board answers one question, *where may I go, and which of those
    // places is safe*, rather than two laid over each other.
    //
    // Painting the opposing side's whole threat range over the reach is the
    // obvious alternative, and on a board where the enemy covers most of the
    // field that is most of the screen in red with the player's own moves
    // buried underneath. On a cartridge it reads as noise rather than as a
    // warning.
    //
    // The whole zone is not gone, because seeing where the enemy can reach
    // before choosing a destination is a real tactic: **R turns it on**, and it
    // is off unless the player asks. What must not be the default is a board
    // nobody can read.
    //
    // **And once a pick is held, the board answers the pick instead.** Taking
    // ATTACK, a weapon, a spell or TALK out of the menu lights, in amber, the
    // tiles that gesture can be aimed at. That is `sim::aimable_tiles`, the
    // engine's own answer on the same terms as the two above, so a lit tile and
    // an accepted confirm still cannot disagree. What that buys is the half no
    // forecast panel can give: **a character who can reach nobody sees an unlit
    // board without moving the cursor at all**, where otherwise the only way to
    // find out is to sweep the cursor over the board and read a refusal.
    //
    // Both sets still come from the engine and neither is derived here; they
    // are intersected rather than stacked. Drawn as a one-pixel checker so the
    // terrain stays readable, and so the sampled cell centres stay exact for
    // the probes.
    void draw_highlights(
        surface_t* disp,
        const sim::EncounterSnapshot& snapshot
    ) {
        if (selected_ == 0) return;
        // The tiles under the marker are whichever rule is in force: the pick
        // while one is held, the region while the board is being arranged, the
        // movement range otherwise. All three come from the engine, so a lit
        // tile and an accepted command cannot disagree.
        const bool aiming = aim_ != Aim::none;
        const std::vector<sim::Position> reach =
            aiming ? sim::aimable_tiles(
                         snapshot, selected_, aimed_gesture(), weapons_,
                         abilities_
                     )
            : snapshot.deploying
                ? sim::deployable_tiles(snapshot, selected_)
                : sim::reachable_tiles(snapshot, selected_);
        // Amber for a pick and blue for a range, and amber for *every* pick
        // including a walk. The tiles a walk aims at are the tiles it could
        // reach anyway, so nothing about the set changes. But the rule the
        // player learns is then one sentence, *amber is where this gesture can
        // land*, rather than one sentence with an exception in it. It also
        // makes taking the WALK row visibly do something, which it did not.
        for (const sim::Position& tile : reach) {
            stipple(disp, tile, aiming ? aim_colour() : move_colour());
        }
        // The warning is about standing somewhere, so it is drawn where the lit
        // tiles are places this character will be standing: its movement range,
        // and the walk aimed at one of them. A strike, a cast or a talk lights
        // where the gesture *lands*, and shading those would be warning about a
        // move nobody is making. R still turns the whole zone on, because that
        // is the player asking for the board rather than for their own pick.
        if (aiming && aim_ != Aim::walk && !threat_view_) return;
        const sim::Side enemy = player_side_ == sim::Side::first
                                    ? sim::Side::second
                                    : sim::Side::first;
        const std::vector<sim::Position> threat =
            sim::danger_tiles(snapshot, enemy, weapons_, abilities_);
        for (const sim::Position& tile : threat) {
            if (!threat_view_ && !within(reach, tile)) continue;
            stipple(disp, tile, colour(216, 64, 52));
        }
    }

    // The board's three overlay colours, named once because three places read
    // them: the two draws above and the checkpoint that asserts the framebuffer
    // against the engine's own queries.
    //
    // Blue is where you may go, red is where you may be struck, and amber is
    // where the pick in your hand can land. Amber is the one hue neither of the
    // other two occupies, and it is far brighter than the red, so the two
    // separate by luminance as well as by hue, which is what keeps them apart
    // for a player who cannot tell red from green. It is also this genre's own
    // word for a range about to be spent.
    [[nodiscard]] static std::uint32_t move_colour() {
        return colour(88, 168, 232);
    }
    [[nodiscard]] static std::uint32_t aim_colour() {
        return colour(248, 200, 56);
    }

    // The pick the player is holding, said in the engine's words.
    //
    // A translation and not a decision: this front end already stores exactly
    // the three values `sim::AimedGesture` carries, because it has to send them
    // in a command, and handing them over is what stops it deciding for itself
    // what its own pick can reach.
    [[nodiscard]] sim::AimedGesture aimed_gesture() const {
        sim::AimedGesture gesture;
        switch (aim_) {
            case Aim::walk: gesture.kind = sim::Gesture::walk; break;
            case Aim::strike: gesture.kind = sim::Gesture::strike; break;
            case Aim::cast: gesture.kind = sim::Gesture::cast; break;
            case Aim::talk: gesture.kind = sim::Gesture::talk; break;
            case Aim::none: break;
        }
        gesture.weapon_id = aim_weapon_;
        gesture.ability_id = aim_ability_;
        return gesture;
    }

    // Whether a tile is one of a short list. Linear over the tiles around one
    // character, which is what both lists are.
    [[nodiscard]] static bool within(
        const std::vector<sim::Position>& tiles, sim::Position tile
    ) noexcept {
        for (const sim::Position& candidate : tiles) {
            if (candidate == tile) return true;
        }
        return false;
    }

    // Every second interior pixel of the tile, on the odd diagonal, which
    // leaves the exact centre untouched for classify() and the samplers.
    void stipple(surface_t* disp, sim::Position tile, std::uint32_t highlight) {
        if (!camera_.visible(tile.x, tile.y)) return;
        const int left = px(tile.x);
        const int top = py(tile.x, tile.y);
        for (int dy = 1; dy < tile_ - 2; ++dy) {
            for (int dx = 1 + (dy & 1); dx < tile_ - 2; dx += 2) {
                graphics_draw_pixel(disp, left + dx, top + dy, highlight);
            }
        }
    }

    // The action menu, over the settled board: a bordered list, the d-pad to
    // choose, A to confirm, B to back out. Returns the chosen row or -1.
    //
    // `opening_row` is where the caret starts, which is the top for a menu the
    // player just opened and the INFO row for a menu reopening behind a sheet
    // the player just closed. Coming back to a different row than the one that
    // was looked up would read as the menu having been restarted.
    int run_menu(
        const sim::EncounterSnapshot& snapshot,
        const MenuChoice* choices,
        int count,
        int opening_row
    ) {
        grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
        menu_open_ = true;
        // What is on screen, so a checkpoint can assert the rows rather than
        // only that a box was drawn. The storage belongs to the caller and
        // outlives this call, and nothing reads it while the menu is closed.
        menu_choices_ = choices;
        menu_count_ = count;
        int chosen = opening_row;
        if (chosen < 0 || chosen >= count) chosen = 0;
        menu_chosen_ = chosen;
        bool dirty = true;
        while (true) {
            if (dirty) {
                surface_t* disp = display_get();
                render(disp, snapshot, 0);
                draw_highlights(disp, snapshot);
                draw_cursor(disp, snapshot);
                draw_info_panel(disp, snapshot);
                draw_menu(disp, choices, count, chosen);
                show(disp);
                dirty = false;
            }
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            // The caret wraps at both ends. A menu whose rows are ordered by
            // what they cost puts the two cheapest (the sheet and the way out)
            // and the row that ends the character's turn at the bottom, which
            // is the far end of a walk down a menu that keeps growing rows;
            // wrapping puts them one press from the top instead of five. It is
            // also what lets a generated press table reach the tail of a menu
            // whose length it cannot know: three UPs from the opening row are
            // the last three rows of every menu this ROM draws.
            if (pressed.d_up && count > 0) {
                chosen = chosen > 0 ? chosen - 1 : count - 1;
                menu_chosen_ = chosen;
                dirty = true;
            }
            if (pressed.d_down && count > 0) {
                chosen = chosen + 1 < count ? chosen + 1 : 0;
                menu_chosen_ = chosen;
                dirty = true;
            }
            if (pressed.b) {
                menu_open_ = false;
                menu_choices_ = nullptr;
                return -1;
            }
            if (pressed.a) {
                menu_open_ = false;
                menu_choices_ = nullptr;
                return chosen;
            }
            wait_ms(16);
        }
    }

    void draw_menu(
        surface_t* disp,
        const MenuChoice* choices,
        int count,
        int chosen
    ) {
        int width_chars = 0;
        for (int i = 0; i < count; ++i) {
            const int length = static_cast<int>(std::strlen(choices[i].text));
            if (length > width_chars) width_chars = length;
        }
        const int width = (width_chars + 2) * 8 + 12;
        const int height = count * 11 + 8;
        const int left = 20;
        // The box hangs from y 140, which is where every menu this ROM has
        // drawn has hung and where the checkpoints sample its frame. Eight rows
        // end at 236 on a 240-line display; a ninth would fall off it, so a
        // menu that tall rises to meet the same floor instead. Every menu the
        // shipped content produces is shorter than that and is drawn exactly
        // where it always was.
        const int floor_y = 236;
        const int top = floor_y - height < 140 ? floor_y - height : 140;
        graphics_draw_box(
            disp, left - 2, top - 2, width + 4, height + 4,
            colour(242, 193, 78)
        );
        graphics_draw_box(disp, left, top, width, height, colour(16, 26, 27));
        // Where it landed, kept for the checkpoints. The panel is drawn after
        // the highlights and over them, so a board cell underneath it is a cell
        // whose overlay colour nothing can be claimed for. The rectangle is
        // recorded from the draw rather than recomputed beside it, so the two
        // cannot drift.
        panel_left_ = left - 2;
        panel_top_ = top - 2;
        panel_width_ = width + 4;
        panel_height_ = height + 4;
        for (int i = 0; i < count; ++i) {
            graphics_set_color(
                i == chosen ? colour(242, 193, 78) : colour(245, 234, 210), 0
            );
            char row[32];
            std::snprintf(
                row, sizeof row, "%s%s", i == chosen ? "> " : "  ",
                choices[i].text
            );
            graphics_draw_text(disp, left + 6, top + 4 + i * 11, row);
        }
    }

    // The full information sheet, as a screen rather than a panel.
    //
    // A panel was tried and cannot be made to fit. This display is 320 pixels
    // across, which is forty characters, and the sheet is eleven lines of them,
    // over a board whose cells are the thing a hovering panel exists to stay
    // out of the way of. So the sheet takes the whole screen, the way a
    // genre's status screen always has, and the hovering panel keeps doing the
    // job it is good at: the one line a player reads while steering.
    //
    // Not one number here is worked out on this machine. `grandleon::sheet`
    // composes the lines from the snapshot and from the registries the
    // encounter was created with, and both consoles and the terminal draw the
    // identical text.
    void run_info_sheet(
        const sim::EncounterSnapshot& snapshot,
        const sim::UnitSnapshot& subject
    ) {
        grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
        sheet_ = grandleon::sheet::build(
            snapshot, subject, campaign_name_of(subject.id), weapons_,
            abilities_, items_, nullptr, package_
        );
        sheet_open_ = true;
        bool dirty = true;
        while (true) {
            if (dirty) {
                surface_t* disp = display_get();
                draw_info_sheet(disp);
                show(disp);
                dirty = false;
            }
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            // Any way out is the way out: B is the back-out this client uses
            // everywhere, and A is what a player who opened the sheet with A
            // reaches for again. Neither commits anything, because the sheet
            // is a thing to read and never a thing to choose.
            if (pressed.b || pressed.a || pressed.start) {
                sheet_open_ = false;
                return;
            }
            wait_ms(16);
        }
    }

    void draw_info_sheet(surface_t* disp) {
        graphics_fill_screen(disp, colour(16, 26, 27));
        // The same amber frame the action menu wears, so a player reads the
        // sheet as the menu's own and not as a screen the game changed to.
        graphics_draw_box(disp, 12, 12, 320 - 24, 240 - 24, colour(242, 193, 78));
        graphics_draw_box(disp, 14, 14, 320 - 28, 240 - 28, colour(16, 26, 27));
        // Twelve lines apart, from y 24, which puts the sixteenth line the
        // sheet can hold clear of the way out at the foot of the frame. The
        // shipped vocabulary fills eight.
        for (int i = 0; i < sheet_.count; ++i) {
            // The header is the character; everything under it is its numbers.
            graphics_set_color(
                i == 0 ? colour(242, 193, 78) : colour(245, 234, 210), 0
            );
            graphics_draw_text(disp, 24, 24 + i * 12, sheet_.lines[i]);
        }
        graphics_set_color(colour(242, 193, 78), 0);
        graphics_draw_text(disp, 24, 218, "B  BACK");
    }

    // The board menu, on START, at any point in a battle.
    //
    // It answers the questions that are about the battle rather than about one
    // of its characters: end this side's turn, and leave. A character's own
    // questions (walk, strike, cast, drink, finish) are the unit action
    // menu's, one press away on a character the player has picked up, and
    // neither menu holds a row belonging to the other.
    //
    // Reachable whether or not anything is selected, which is the whole reason
    // it exists. A START that meant "wait", and only while a character was in
    // hand, would be the sole way to finish anybody, would say so nowhere, and
    // would do nothing at all with nothing selected.
    //
    // Returns the intent the player asked for, or kind none when they went back
    // to the battle.
    client::Intent board_menu(const sim::EncounterSnapshot& snapshot) {
        // Three rows, and the first of them is the way out. A menu opened by a
        // button a player pressed to find out what it did should say how to
        // undo that before it says anything else. B backs out of it too, as B
        // backs out of everything here, but a row a player can read is not the
        // same as a button they have to already know.
        // Four slots because a campaign whose author asked for the Stage picker
        // adds a row to this menu; a game that did not builds the three it has
        // always built, into the same array.
        MenuChoice rows[4]{};
        int count = 3;
        rows[0] = {
            "BACK TO BATTLE", false, false, false, 0, 0, 0, false, false
        };
        // The side, by the name the phase banner just announced it under. A
        // player who read YOUR TURN across the screen a moment ago is being
        // offered the end of that same turn, in the same word. That word is
        // what keeps this row from reading as the character menu's END
        // CHARACTER TURN, which ends one activation and not the side's.
        //
        // This menu only ever ends the side the player holds, so the row does
        // not ask which side that is: it is theirs either way.
        rows[1] = {
            "END YOUR TURN", false, false, false, 0, 0, 0, false, false
        };
        rows[2] = {
            "LEAVE BATTLE", false, false, false, 0, 0, 0, false, false
        };
#ifdef GRANDLEON_N64_CAMPAIGN
        // And the Stage picker, when there is one. The row exists exactly when
        // the session handed this board a list of Stages, which it does only
        // for a campaign whose author asked for the picker: nothing here reads a
        // setting, and a player who never turned it on never sees this row.
        //
        // Last, under the way out, because it is a testing aid and not one of
        // the two questions this menu exists to answer.
        if (board_ != nullptr && !board_->stages.empty()) {
            rows[count] = {
                "GO TO ANOTHER STAGE", false, false, false, 0, 0, 0, false,
                false
            };
            ++count;
        }
#endif
        board_menu_open_ = true;
        const int chosen = run_menu(snapshot, rows, count, 0);
        board_menu_open_ = false;
        client::Intent intent;
        if (chosen == 1) {
            // Every character on this side that still owes the board an
            // activation is finished, one WAIT at a time, by the drain at the
            // head of `controller_intent`. Nothing is decided here beyond that
            // the player asked for it: which characters those are is
            // `client::unfinished_unit`'s answer, and it is the engine's fields
            // it reads.
            finishing_ = true;
            drain_last_ = 0;
            selected_ = 0;
            clear_aim();
            reopen_menu_ = 0;
            return intent;
        }
        if (chosen == 2 && confirm_leave()) {
            intent.kind = client::IntentKind::quit;
            return intent;
        }
#ifdef GRANDLEON_N64_CAMPAIGN
        if (chosen == 3 && board_ != nullptr) {
            const std::uint64_t going = run_stage_picker(board_->stages);
            // Backing out of the picker puts the player back on the board with
            // whatever they had in hand, exactly as backing out of this menu
            // does. Only a Stage actually chosen leaves the battle, and it
            // leaves it on the same terms LEAVE BATTLE does: this fight is not
            // kept, because no format describes a board mid-fight.
            if (going != 0U) {
                intent.kind = client::IntentKind::jump_to_stage;
                intent.stage_id = going;
            }
            return intent;
        }
#endif
        return intent;
    }

    // What leaving actually costs, said before it is paid.
    //
    // This screen is the honest answer to "save the game", and the reason there
    // is no row above it that says so. A save on this cartridge holds a
    // *campaign*: the company, its store, what every battle did to it. It is
    // written when a battle finishes and when a gesture is made in the
    // company between battles. It cannot hold a battle in progress: nothing in
    // the format describes a board mid-fight, and `client::run_persistent_
    // campaign` is explicit that an unfinished fight is not an outcome and
    // commits nothing. So a row reading SAVE would either write bytes identical
    // to the ones already in the slot while letting the player believe this
    // board was kept, or it would be a lie outright. A menu row that quietly
    // does something other than what it says is worse than no row.
    //
    // What a player can be told instead is where their save actually is, which
    // is what this screen says, in the words the slot screen's CONTINUE uses.
    [[nodiscard]] bool confirm_leave() {
        grandleon::n64audio::play(grandleon::n64audio::Sfx::select);
        leaving_open_ = true;
        static const char* const lines[] = {
            "THIS BATTLE IS NOT KEPT. NOTHING",
            "THAT HAPPENS ON THIS BOARD IS",
            "WRITTEN ANYWHERE.",
            "",
#ifdef GRANDLEON_N64_CAMPAIGN
            "YOUR CAMPAIGN IS SAVED AS IT STOOD",
            "AFTER THE LAST BATTLE YOU FINISHED.",
            "CONTINUE PICKS IT UP THERE.",
#else
            "THIS CARTRIDGE KEEPS NO CAMPAIGN,",
            "SO THERE IS NOTHING ELSE TO LOSE",
            "AND NOTHING TO COME BACK TO.",
#endif
        };
        bool dirty = true;
        while (true) {
            if (dirty) {
                surface_t* disp = display_get();
                graphics_fill_screen(disp, colour(16, 26, 27));
                // The same amber frame the menu and the sheet wear, so this
                // reads as the menu's own question rather than as a screen the
                // game changed to underneath the player.
                graphics_draw_box(
                    disp, 12, 12, 320 - 24, 240 - 24, colour(242, 193, 78)
                );
                graphics_draw_box(
                    disp, 14, 14, 320 - 28, 240 - 28, colour(16, 26, 27)
                );
                graphics_set_color(colour(242, 193, 78), 0);
                graphics_draw_text(disp, 24, 32, "LEAVE THIS BATTLE?");
                graphics_set_color(colour(245, 234, 210), 0);
                int y = 64;
                for (const char* line : lines) {
                    graphics_draw_text(disp, 24, y, line);
                    y += 12;
                }
                graphics_set_color(colour(242, 193, 78), 0);
                graphics_draw_text(disp, 24, 200, "A  LEAVE");
                graphics_draw_text(disp, 168, 200, "B  STAY");
                show(disp);
                dirty = false;
            }
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            // B and START both stay, because both are this console's way of
            // putting a screen down and a player who opened this with START is
            // as likely to close it the same way.
            if (pressed.b || pressed.start) {
                leaving_open_ = false;
                return false;
            }
            if (pressed.a) {
                leaving_open_ = false;
                return true;
            }
            wait_ms(16);
        }
    }

    // Opens the unit action menu for `actor`: everything this character can be
    // told to do, derived from the character rather than from whatever the
    // cursor happens to be resting on. MOVE first, then a row per carried
    // weapon where there is more than one and a single ATTACK row where there
    // is not, then a row per ability, the carried items, TALK where one would
    // land, and last the three rows every menu ends with.
    //
    // That derivation is the whole point. A menu opened by aiming at an enemy
    // makes every row an answer to "with what do I strike this?", and waiting
    // is not an answer to that question. It is what one does *instead of*
    // choosing a target. No row can be grown for it; the opening gesture has to
    // be a different one, which is this one.
    //
    // MOVE is here because of what playing this on a cartridge shows: without
    // the row, the only way to walk is to press A on open ground, which is a
    // thing a board can be played for an hour without discovering, and a menu
    // that lists a character's every other choice while silently omitting its
    // first one is the menu teaching that walking is not among them. The row is
    // offered whether or not the character has already walked, on the same
    // terms the spent item row is: the engine is what refuses it, and it
    // refuses by name.
    //
    // Only the row that ends the character's turn is a command on its own. A
    // walk, a strike, a cast or a talk closes the menu and hands the player
    // back the cursor with the pick held (`aim_`), because the character has
    // said what it will do and not yet where. So this returns an intent only
    // for the row that ends a turn and for a spent item; kind none otherwise,
    // and the selection stands either way until an intent is committed.
    client::Intent unit_action_menu(
        const sim::EncounterSnapshot& snapshot,
        const sim::UnitSnapshot& actor
    ) {
        // Nine rows is what the box can draw. It hangs from y 140 and each row
        // is eleven lines, so eight rows end at 236 on a 240-line display and a
        // ninth rises to meet the same floor rather than falling off it.
        // `draw_menu` is where that is arithmetic rather than prose. The three
        // rows every menu ends with (END CHARACTER TURN, INFO and CANCEL) are
        // reserved before anything else is offered, so a character carrying
        // more than the box holds loses a spell rather than the way out or the
        // way to read itself. WALK sits above them when the engine would take
        // one, and the character carrying the most is the mage, at one strike
        // row, two spells and a draught. That is five, which fits beneath a
        // WALK row and leaves a sixth free without one.
        MenuChoice choices[9];
        // The cap on everything above the tail. A character that may still walk
        // spends the first of these on WALK; one that has already walked has
        // the whole of it for its own rows, which is the menu getting shorter
        // rather than narrower.
        const int capacity =
            static_cast<int>(sizeof choices / sizeof choices[0]) - 3;
        int count = 0;
        // The first thing a turn does, and so the first row. WALK rather than
        // MOVE because the d-pad already moves the cursor and the controls
        // screen already says so, and because a row and a gesture that share a
        // word are a row and a gesture a player has to tell apart. It names no
        // tile: taking it hands the player back the board with the engine's own
        // aimable set already lit under the cursor.
        //
        // **Offered only while the engine would take it**, which for a walk is
        // "this character has not already walked this turn". That is
        // `sim::gesture_available`, asked rather than tracked here, so a row the
        // menu offers and a command the engine refuses cannot disagree.
        //
        // The line that draws is between the gesture and its aim, and it is
        // deliberate that only the first side of it moves a row. A walk after
        // walking is refused before the engine looks at a destination, so the
        // row goes. ATTACK with nobody in reach is a fact about the board this
        // turn rather than about the gesture, and it is exactly what the amber
        // highlight is there to show. A row that came and went for a reason
        // the board does not draw would be teaching a rule nobody can
        // see. Nothing else here moves either: an item row prints its own count
        // and a spent one says so, TALK is already offered only where
        // `forecast_talk` says one could land, and the three tail rows are
        // never refused.
        if (sim::gesture_available(
                snapshot, actor.id, {sim::Gesture::walk, 0, 0}, weapons_,
                abilities_
            )) {
            choices[count++] =
                {"WALK", true, false, false, 0, 0, 0, false, false};
        }
        if (actor.weapon_ids.size() <= 1) {
            choices[count++] =
                {"ATTACK", false, true, false, 0, 0, 0, false, false};
        } else {
            // Carried order, the weapon in hand first. That first row
            // names no weapon: absence is how the engine is told to use
            // the hand, and the command is the one it always saw.
            for (std::size_t i = 0; i < actor.weapon_ids.size(); ++i) {
                if (count >= capacity) break;
                const sim::ContentId weapon = actor.weapon_ids[i];
                choices[count++] = {
                    weapon_name(weapon), false, true, false, 0,
                    i == 0 ? sim::ContentId{0} : weapon, 0, false, false
                };
            }
        }
        for (const sim::ContentId ability : actor.ability_ids) {
            if (count < capacity) {
                choices[count++] = {
                    ability_name(ability), false, false, false, ability, 0, 0,
                    false, false
                };
            }
        }
        // The item rows, between the spells and WAIT: the position this menu
        // has held open for them since it was built. A row that has run out is
        // still offered and still says so, because the engine is what refuses
        // it, the same reason aiming a strike at empty ground sends the
        // command and earns `unknown_target` rather than being swallowed here.
        for (std::size_t i = 0;
             i < actor.item_ids.size() &&
             i < static_cast<std::size_t>(menu_item_labels);
             ++i) {
            if (count >= capacity) break;
            const sim::ContentId item = actor.item_ids[i];
            std::snprintf(
                item_labels_[i], menu_item_label_size, "%s x%d",
                item_name(item),
                static_cast<int>(actor.item_counts[i])
            );
            choices[count++] = {
                item_labels_[i], false, false, false, 0, 0, item, false, false
            };
        }
        // TALK, after the item rows and before WAIT. The rows of this menu are
        // ordered by what each one costs, and a talk costs exactly what a
        // strike costs: one action point, and the activation closes behind it.
        // So it belongs among the rows that spend the turn rather than below
        // WAIT with the rows that spend nothing. It goes below the items
        // because an item is the gesture a player already knows, and a menu
        // that reshuffles its familiar rows to make room for a new one is a
        // menu the player has to relearn.
        //
        // Offered only when the engine says a talk could land. Every neighbour
        // is put to `forecast_talk`, which is the same judgement `apply` makes:
        // who is adjacent, who authored a record, who is still standing and who
        // has already walked away are all its questions, and asking them here
        // would be a second copy of the rule that could disagree with the
        // first. No shipped board authors a talk, so no shipped menu grows this
        // row.
        if (count < capacity && any_talkable_neighbour(snapshot, actor)) {
            choices[count++] =
                {"TALK", false, false, false, 0, 0, 0, true, false};
        }
        // The tail, in the order of how much each row commits. The row that
        // ends this character's turn spends its activation and is the last row
        // that decides anything. INFO and CANCEL both put the player back where
        // they were, one after reading and one immediately, which is why INFO
        // joins the tail rather than taking the row a carried item is still
        // holding open between the spells and it.
        //
        // It says END CHARACTER TURN and not WAIT. WAIT is the genre's word and
        // it is the wrong one to meet first: it says what the character does
        // and not what it costs, and a player on a cartridge cannot find from
        // it how to finish anybody. The word it must not be confused with is
        // the board menu's END YOUR TURN, which finishes the whole side. So
        // each row names the scope it ends, and the two differ on exactly the
        // word that differs.
        choices[count++] = {
            "END CHARACTER TURN", false, false, true, 0, 0, 0, false, false
        };
        const int info_row = count;
        choices[count++] =
            {"INFO", false, false, false, 0, 0, 0, false, true};
        choices[count++] =
            {"CANCEL", false, false, false, 0, 0, 0, false, false};

        // The sheet is read out of the menu and hands the menu straight back,
        // caret still on the row that opened it. It is the only row that
        // returns here rather than out of it, because it is the only row that
        // is a question rather than an order.
        int opening_row = 0;
        while (true) {
            const int chosen =
                run_menu(snapshot, choices, count, opening_row);
            client::Intent intent;
            if (chosen < 0 || chosen == count - 1) return intent;
            if (choices[chosen].info) {
                run_info_sheet(snapshot, actor);
                opening_row = info_row;
                continue;
            }
            if (choices[chosen].wait) {
                intent.kind = client::IntentKind::wait;
                intent.unit_id = actor.id;
                selected_ = 0;
                clear_aim();
                return intent;
            }
            if (choices[chosen].item != 0) {
                intent.kind = client::IntentKind::use_item;
                intent.unit_id = actor.id;
                intent.item_id = choices[chosen].item;
                selected_ = 0;
                clear_aim();
                return intent;
            }
            if (choices[chosen].move) {
                // Handed back to the cursor, over tiles the board is already
                // lighting. `draw_highlights` paints the engine's own
                // `reachable_tiles` for whoever is selected, so taking this row
                // adds no highlight and invents no rule: it renames what the
                // next A press means and says so on the prompt line.
                aim_ = Aim::walk;
                aim_weapon_ = 0;
                aim_ability_ = 0;
                std::snprintf(aim_text_, sizeof aim_text_, "WALK WHERE");
                return intent;
            }
            if (choices[chosen].talk) {
                // Aimed rather than committed, even when only one neighbour
                // could answer. A talk names somebody, and the gesture that
                // names somebody on this console is the cursor. So the row
                // hands the player back the board exactly as a strike does,
                // and whoever the cursor lands on is the engine's to judge.
                // Committing straight at the single candidate would have made
                // one row mean two different gestures depending on how many
                // people were standing nearby, which is a rule a player would
                // have to be told.
                aim_ = Aim::talk;
                aim_weapon_ = 0;
                aim_ability_ = 0;
                std::snprintf(aim_text_, sizeof aim_text_, "TALK TO WHOM");
                snap_aim(snapshot);
                return intent;
            }
            if (choices[chosen].attack) {
                aim_ = Aim::strike;
                aim_weapon_ = choices[chosen].weapon;
                aim_ability_ = 0;
                // A named weapon is worth repeating back, because the player
                // picked it out of several and the cursor is about to be
                // somewhere else. The weapon in hand needs no name: it is the
                // strike the plain gesture already makes.
                if (aim_weapon_ != 0) {
                    std::snprintf(
                        aim_text_, sizeof aim_text_, "STRIKE WITH %s",
                        choices[chosen].text
                    );
                } else {
                    std::snprintf(aim_text_, sizeof aim_text_, "PICK A TARGET");
                }
                snap_aim(snapshot);
                return intent;
            }
            aim_ = Aim::cast;
            aim_ability_ = choices[chosen].ability;
            aim_weapon_ = 0;
            std::snprintf(
                aim_text_, sizeof aim_text_, "AIM %s", choices[chosen].text
            );
            return intent;
        }
    }

    // Where the pick lands. Nothing here asks whether it may: a cast goes to
    // the tile under the cursor and a strike goes at whoever is standing
    // there, and the engine answers both, including the empty tile, which is
    // refused as having no target rather than swallowed as a button that did
    // nothing.
    client::Intent commit_aim(const sim::EncounterSnapshot& snapshot) {
        client::Intent intent;
        intent.unit_id = selected_;
        if (aim_ == Aim::walk) {
            // The one aim that keeps its character. A walk does not finish
            // anybody: the point it spent was the turn's walk and the action
            // is still in hand. So the selection stands and the menu opens
            // again on the far side of it, which is the whole reason the row
            // exists: the player is asked what this character does next
            // instead of being handed a board and left to guess.
            intent.kind = client::IntentKind::move_to;
            intent.destination = {cursor_x_, cursor_y_};
            commanded_ = selected_;
            clear_aim();
            reopen_menu_ = selected_;
            return intent;
        }
        if (aim_ == Aim::cast) {
            intent.kind = client::IntentKind::ability;
            intent.destination = {cursor_x_, cursor_y_};
            intent.ability_id = aim_ability_;
        } else if (aim_ == Aim::talk) {
            // Whoever is standing there, named and handed over. Empty ground
            // names nobody and earns `unknown_target`; somebody with nothing
            // to say earns `not_talkable`; somebody who already walked away
            // earns `target_departed`. All three are the engine's to say.
            const sim::UnitSnapshot* occupant = nullptr;
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (sim::on_board(unit) && unit.position.x == cursor_x_ &&
                    unit.position.y == cursor_y_) {
                    occupant = &unit;
                }
            }
            intent.kind = client::IntentKind::talk;
            intent.target_id = occupant != nullptr ? occupant->id : 0;
        } else {
            const sim::UnitSnapshot* occupant = nullptr;
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (sim::on_board(unit) && unit.position.x == cursor_x_ &&
                    unit.position.y == cursor_y_) {
                    occupant = &unit;
                }
            }
            intent.kind = client::IntentKind::attack;
            intent.target_id = occupant != nullptr ? occupant->id : 0;
            intent.weapon_id = aim_weapon_;
        }
        selected_ = 0;
        clear_aim();
        return intent;
    }

    void clear_aim() {
        aim_ = Aim::none;
        aim_weapon_ = 0;
        aim_ability_ = 0;
        aim_text_[0] = '\0';
    }

    // Where the pick in hand can land, in the engine's own words. Asked rather
    // than kept, exactly as `draw_highlights` asks it: the answer moves only
    // when the board does, and a cached copy would be one more thing that could
    // disagree with the board it is drawn over.
    [[nodiscard]] std::vector<sim::Position> aim_tiles(
        const sim::EncounterSnapshot& snapshot
    ) const {
        if (aim_ == Aim::none || selected_ == 0) return {};
        return sim::aimable_tiles(
            snapshot, selected_, aimed_gesture(), weapons_, abilities_
        );
    }

    // A pick that names a character opens on one. Called the moment a menu row
    // hands the cursor back, so a player who took ATTACK is already pointing at
    // somebody the strike can reach instead of wherever the cursor had been
    // resting. A strike with nobody in reach moves nothing, which is the dark
    // board answering the question without a step.
    void snap_aim(const sim::EncounterSnapshot& snapshot) {
        if (aim_ == Aim::none) return;
        if (!client::gesture_names_a_character(aimed_gesture().kind)) return;
        const std::vector<sim::Position> tiles = aim_tiles(snapshot);
        if (tiles.empty()) return;
        if (within(tiles, {cursor_x_, cursor_y_})) return;
        const sim::Position landed =
            client::nearest_aim_tile(tiles, {cursor_x_, cursor_y_});
        cursor_x_ = landed.x;
        cursor_y_ = landed.y;
        camera_.follow(cursor_x_, cursor_y_, 2);
    }

    // A full-screen beat announcing whose turn it is, over a settled frame of
    // the new state.
    // Moves the camera until a tile is on the screen, drawing the frames it
    // takes. Nothing at all when it is already there, which is every board that
    // fits its screen and most events on one that does not.
    //
    // The same act as the opening reveal and drawn the same way; only the pace
    // differs, because a reveal is the board introducing itself and a pan is
    // the camera catching up with something already happening.
    void bring_into_view(sim::Position where) {
        if (!has_previous_) return;
        if (camera_.visible(where.x, where.y)) return;
        view::Camera settled = camera_;
        settled.follow(where.x, where.y, 2);
        const int from_x = camera_.x;
        const int from_y = camera_.y;
        const int to_x = settled.x;
        const int to_y = settled.y;
        const int across = to_x > from_x ? to_x - from_x : from_x - to_x;
        const int down = to_y > from_y ? to_y - from_y : from_y - to_y;
        const int frames = view::pan_frames_for(across > down ? across : down);
        for (int frame = 0; frame < frames; ++frame) {
            camera_.x = view::slide_between(from_x, to_x, frame, frames);
            camera_.y = view::slide_between(from_y, to_y, frame, frames);
            camera_.clamp();
            surface_t* disp = display_get();
            render(disp, previous_, 0);
            grandleon::n64audio::pump();
            show(disp);
        }
        camera_.x = to_x;
        camera_.y = to_y;
        camera_.clamp();
    }

    // A banner in the turn banner's own shape, for a phase rather than a side.
    //
    // It exists because the deployment phase announced itself nowhere: a player
    // who reached one met a board where pressing A picked somebody up, lit some
    // tiles and opened no menu, with nothing on the screen saying why. That is a
    // player who thinks the game is broken, and the report that it looked broken
    // is what this is here to answer.
    void phase_words(
        const sim::EncounterSnapshot& snapshot, const char* text,
        const char* under
    ) {
        // Drawn over the snapshot it is handed rather than over the last one
        // drawn: this phase opens before the board has ever been painted, so
        // there is no previous frame to put a banner on.
        const int left = (320 - static_cast<int>(std::strlen(text)) * 8) / 2;
        const int under_left =
            (320 - static_cast<int>(std::strlen(under)) * 8) / 2;
        surface_t* disp = display_get();
        render(disp, snapshot, 0);
        graphics_draw_box(disp, 0, 100, 320, 46, colour(16, 26, 27));
        graphics_draw_box(disp, 0, 100, 320, 2, colour(183, 140, 35));
        graphics_draw_box(disp, 0, 144, 320, 2, colour(183, 140, 35));
        graphics_set_color(colour(245, 234, 210), 0);
        graphics_draw_text(disp, left, 110, text);
        graphics_set_color(colour(198, 205, 200), 0);
        graphics_draw_text(disp, under_left, 128, under);
        show(disp);
        for (int i = 0; i < 90; ++i) {
            grandleon::n64audio::pump();
            wait_ms(16);
        }
    }

    void phase_banner(const sim::EncounterSnapshot& snapshot) {
        // The stripe is the side's own colour, which is a fact about the board:
        // the first side is drawn blue and the second red wherever either
        // appears. The words are the player's own question, and ask which side
        // they hold rather than assuming it is the first.
        const bool blue = snapshot.active_side == sim::Side::first;
        const std::uint32_t side_colour =
            blue ? colour(35, 117, 169) : colour(179, 72, 63);
        const char* text =
            snapshot.active_side == player_side_ ? "YOUR TURN" : "ENEMY TURN";
        const int left = (320 - static_cast<int>(std::strlen(text)) * 8) / 2;
        surface_t* disp = display_get();
        render(disp, snapshot, 0);
        graphics_draw_box(disp, 0, 100, 320, 36, colour(16, 26, 27));
        graphics_draw_box(disp, 0, 100, 320, 2, side_colour);
        graphics_draw_box(disp, 0, 134, 320, 2, side_colour);
        graphics_set_color(colour(245, 234, 210), 0);
        graphics_draw_text(disp, left, 114, text);
        show(disp);
        int hold = 45;
#ifdef GRANDLEON_N64_AUTOPILOT
        // The first hand-offs are checkpoints: the banner is asserted in the
        // framebuffer and held long enough for the harness to photograph.
        // Later banners run at their ordinary pace.
        static int banner_count = 0;
        if (banner_count < 3) {
            ++banner_count;
            report_line(
                "CHECKPOINT banner-%s-%d\n", blue ? "blue" : "red",
                banner_count
            );
            expect(
                surface_pixel(disp, 10, 100) ==
                    static_cast<std::uint16_t>(side_colour & 0xFFFFU),
                "the banner stripe carries the side's colour"
            );
            expect(
                surface_pixel(disp, 10, 110) == pack16(16, 26, 27),
                "the banner text sits on the house backdrop"
            );
            hold = 150;
        }
#endif
        for (int i = 0; i < hold; ++i) {
            grandleon::n64audio::pump();
            wait_ms(16);
        }
    }

    // The unit under the cursor, and the attack forecast when a selected
    // friendly is sizing up an enemy: the engine's own numbers, shown before
    // the player commits.
    void draw_info_panel(
        surface_t* disp,
        const sim::EncounterSnapshot& snapshot
    ) {
        panel_width_ = 0;
        panel_height_ = 0;
        const sim::UnitSnapshot* hovered = nullptr;
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) && unit.position.x == cursor_x_ &&
                unit.position.y == cursor_y_) {
                hovered = &unit;
            }
        }
        if (hovered == nullptr) return;
        // Seven rows, and room in each for more than any of them draws. The
        // width the panel is actually given is measured from these strings
        // below, so the slack costs a hundred bytes of stack and no pixels. It
        // is what lets the forecast row carry a chance in front of the numbers
        // without the formatter having to be trusted not to truncate.
        char lines[7][48];
        int count = 0;
        int forecast_line = -1;
        bool lethal = false;
        // Who, then what kind: `WREN ASHDOWN` over `ARCHER`. The name is what
        // a player refers to a character by, and the class says what a
        // character of that kind can do. Neither is a number: a place in the
        // board's list is the terminal client's addressing scheme and means
        // nothing to somebody holding a controller.
        std::snprintf(
            lines[count++], sizeof lines[0], "%s",
            grandleon::sheet::character_name(
                package_, snapshot, hovered->id,
                campaign_name_of(hovered->id)
            ).c_str()
        );
        if (const grandleon::sheet::ContentName kind =
                grandleon::sheet::class_name(package_, hovered->unit_type_id);
            kind.text[0] != '\0') {
            std::snprintf(lines[count++], sizeof lines[0], "%s", kind.c_str());
        }
        if (hovered->side == snapshot.active_side) {
            // The engine's own answer, so the panel and an accepted command
            // cannot disagree about what a half-spent character has left.
            // Restating the rule here is right only while one character can be
            // part-way through a turn; under `sideBlocks` several can be at
            // once and the side-wide count is empty there.
            const int points =
                static_cast<int>(sim::action_points_left(snapshot, hovered->id));
            // Somebody who has already taken their turn has no points to
            // offer, and the panel says so in the word as well as in the
            // number: the grey band on the token is the glance, and this is
            // the answer to "why can I not pick this one".
            if (hovered->has_acted) {
                std::snprintf(
                    lines[count++], sizeof lines[0], "HP %d/%d  SPENT",
                    static_cast<int>(hovered->health),
                    static_cast<int>(hovered->maximum_health)
                );
            } else {
                std::snprintf(
                    lines[count++], sizeof lines[0], "HP %d/%d  AP %d",
                    static_cast<int>(hovered->health),
                    static_cast<int>(hovered->maximum_health), points
                );
            }
        } else {
            std::snprintf(
                lines[count++], sizeof lines[0], "HP %d/%d",
                static_cast<int>(hovered->health),
                static_cast<int>(hovered->maximum_health)
            );
        }
        if (hovered->minimum_reach == hovered->maximum_reach) {
            std::snprintf(
                lines[count++], sizeof lines[0], "RNG %d",
                static_cast<int>(hovered->maximum_reach)
            );
        } else {
            std::snprintf(
                lines[count++], sizeof lines[0], "RNG %d-%d",
                static_cast<int>(hovered->minimum_reach),
                static_cast<int>(hovered->maximum_reach)
            );
        }
        std::snprintf(
            lines[count++], sizeof lines[0], "STR %d  DEF %d",
            static_cast<int>(hovered->strength),
            static_cast<int>(hovered->defense)
        );
        if (selected_ != 0 && selected_ != hovered->id &&
            hovered->side != player_side_) {
            const sim::AttackForecast forecast =
                sim::forecast_attack(snapshot, selected_, hovered->id);
            forecast_line = count;
            if (forecast) {
                lethal = forecast.lethal;
                // The chance first, because everything after it is what
                // happens only when the strike lands. It is the engine's own
                // number, drawn and not derived, and a certain strike says
                // nothing extra, so a panel over content that never misses
                // reads exactly as it always did.
                if (forecast.hit_chance >= 100U) {
                    std::snprintf(
                        lines[count++], sizeof lines[0], "DMG %d  HP %d>%d%s",
                        static_cast<int>(forecast.damage),
                        static_cast<int>(hovered->health),
                        static_cast<int>(forecast.target_health_after),
                        forecast.lethal ? " KO" : ""
                    );
                } else {
                    std::snprintf(
                        lines[count++], sizeof lines[0],
                        "%d%% DMG %d  HP %d>%d%s",
                        static_cast<int>(forecast.hit_chance),
                        static_cast<int>(forecast.damage),
                        static_cast<int>(hovered->health),
                        static_cast<int>(forecast.target_health_after),
                        forecast.lethal ? " KO" : ""
                    );
                }
            } else {
                // The refusal the forecast gave, in the words every client
                // says it in. Not a finer one worked out here: the engine
                // answers both ends of the reach band with
                // `target_out_of_range`, and a panel that measured the
                // distance itself would be restating the engine's range rule
                // on one console, where a change to that rule would leave this
                // line asserting a stale copy of it with nothing able to see
                // it.
                std::snprintf(
                    lines[count++], sizeof lines[0], "%s",
                    refusal_text(forecast.error)
                );
            }
        }
        int width_chars = 0;
        for (int i = 0; i < count; ++i) {
            const int length = static_cast<int>(std::strlen(lines[i]));
            if (length > width_chars) width_chars = length;
        }
        const int width = width_chars * 8 + 12;
        const int height = count * 11 + 10;
        const int left = px(cursor_x_) < 160 ? 320 - 16 - width : 16;
        const int top = 16;
        graphics_draw_box(
            disp, left - 2, top - 2, width + 4, height + 4,
            colour(242, 193, 78)
        );
        graphics_draw_box(disp, left, top, width, height, colour(16, 26, 27));
        // Where it landed, kept for the checkpoints. The panel is drawn after
        // the highlights and over them, so a board cell underneath it is a cell
        // whose overlay colour nothing can be claimed for. The rectangle is
        // recorded from the draw rather than recomputed beside it, so the two
        // cannot drift.
        panel_left_ = left - 2;
        panel_top_ = top - 2;
        panel_width_ = width + 4;
        panel_height_ = height + 4;
        for (int i = 0; i < count; ++i) {
            if (i == forecast_line) {
                graphics_set_color(
                    lethal ? colour(179, 72, 63) : colour(242, 193, 78), 0
                );
            } else {
                graphics_set_color(colour(245, 234, 210), 0);
            }
            graphics_draw_text(disp, left + 6, top + 5 + i * 11, lines[i]);
        }
    }
#endif

    // Whether the pixel a checkpoint would sample for this cell is under the
    // information panel. See `draw_info_panel` for why that is an exemption
    // rather than a failure.
    [[nodiscard]] bool sample_under_panel(int cell_x, int cell_y) const {
        if (panel_width_ <= 0) return false;
        const int sx = px(cell_x) + tile_ / 2;
        const int sy = py(cell_x, cell_y) + tile_ / 2 + 1;
        return sx >= panel_left_ && sx < panel_left_ + panel_width_ &&
               sy >= panel_top_ && sy < panel_top_ + panel_height_;
    }

    [[nodiscard]] int px(int cell_x) const {
        return projection_.cell_left(camera_, cell_x);
    }

    // A cell's top edge needs both coordinates because how high it is drawn
    // depends on what terrain it is. Everything that draws on the board goes
    // through here: the tiles, the tokens, the cursor, the range highlights,
    // the animations, and the probe's own sampler. So nothing can end up half
    // a step out of register with the ground.
    [[nodiscard]] int py(int cell_x, int cell_y) const {
        return projection_.cell_top(
            camera_, cell_y, elevation_at(cell_x, cell_y)
        );
    }

    // How many levels above the valley floor a cell's terrain reads as, from
    // the art library's own table (tools/placeholder_art/assets/themes.h).
    // Presentation only: no rule reads it, no snapshot carries it, and no
    // hashed value has ever seen it.
    [[nodiscard]] int elevation_at(int cell_x, int cell_y) const {
        if (cell_x < 0 || cell_y < 0 || board_width_ <= 0) return 0;
        const std::size_t index =
            static_cast<std::size_t>(cell_y) *
                static_cast<std::size_t>(board_width_) +
            static_cast<std::size_t>(cell_x);
        if (index >= terrain_.size()) return 0;
        const std::uint8_t kind = kind_of(terrain_[index]);
        return kind < terrain_kind_count
                   ? grandleon_terrain_elevation[kind]
                   : 0;
    }

    // The tallest terrain anywhere on the board, which is what the layout
    // reserves headroom for.
    [[nodiscard]] int highest_elevation(
        const sim::EncounterSnapshot& snapshot
    ) const {
        int highest = 0;
        for (int y = 0; y < snapshot.height; ++y) {
            for (int x = 0; x < snapshot.width; ++x) {
                const int level = elevation_at(x, y);
                if (level > highest) highest = level;
            }
        }
        return highest;
    }

    static std::uint32_t colour(int r, int g, int b) {
        return graphics_make_color(r, g, b, 255);
    }

    static std::uint16_t packed(int r, int g, int b) {
        return static_cast<std::uint16_t>(colour(r, g, b) & 0xFFFFU);
    }

    void draw_unit(
        surface_t* disp,
        const sim::EncounterSnapshot& snapshot,
        const sim::UnitSnapshot& unit
    ) {
        const int x = px(unit.position.x);
        const int y = py(unit.position.x, unit.position.y);
        if (unit.id == snapshot.active_unit_id) {
            graphics_draw_box(disp, x, y, tile_ - 1, 2, colour(242, 193, 78));
        } else if (unit.has_acted) {
            // Somebody who has already taken their turn, banded top and bottom
            // in the grey of a thing that is finished, over a figure the
            // renderer has already drawn in grey.
            //
            // Bands alone would be the cheap answer, and what makes them
            // tempting is a real constraint rather than a taste: every
            // framebuffer probe in this repository identifies a cell by the
            // colour of its *centre*, so repainting a character's own pixels
            // grey would put a colour no palette holds under every one of those
            // probes, while bands sit at the edges the probes never read.
            //
            // What answers it is `blit_sprite_spent`: the figure is drawn
            // through a greyed copy of its own palette rather than repainted
            // afterwards, so the pixel at the centre is still a palette entry
            // and is still derivable from the drawing: `view::spent_grey`
            // applied to the texel the sprite carries. The probe recomputes it
            // and asserts equality, which is a stronger claim than any probe
            // over a spent character that kept its colours could make: it says
            // both which drawing is on the cell and that the drawing was
            // greyed. The bands stay because they read at a glance and cost
            // nothing, not because anything depends on them.
            //
            // The character stays where it is, at full position, with its name
            // and its health bar in their own colours: a player needs to see
            // their whole line to plan the next one, and hiding a spent
            // character would take that away to say something grey already
            // says.
            graphics_draw_box(disp, x, y, tile_ - 1, 2, colour(112, 108, 102));
            graphics_draw_box(
                disp, x, y + tile_ - 2, tile_ - 1, 2, colour(112, 108, 102)
            );
        }
        // Health along the bottom edge, and nothing else written on the cell.
        //
        // Nothing on this console addresses a character by number: a cursor is
        // steered onto one and A is pressed. So no identifier is drawn on a
        // token, and who this is belongs in the panel the cursor carries, where
        // there is room to say it in words. The *terminal* client is the one
        // surface that draws the numbered roster, because there a player types
        // `move 3 5 2`.
        const int span = ((tile_ - 4) * unit.health) /
                         (unit.maximum_health > 0 ? unit.maximum_health : 1);
        graphics_draw_box(
            disp, x + 2, y + tile_ - 4, span > 0 ? span : 1, 2,
            colour(121, 208, 110)
        );
    }

    // The cursor's colour: white once a character is in hand, amber while the
    // board is only being looked at.
    [[nodiscard]] std::uint32_t cursor_colour() const {
        return selected_ != 0 ? colour(255, 255, 255) : colour(242, 193, 78);
    }

    // One cell's worth of cursor: the rectangle, and nothing that breathes.
    // Factored out because a splash draws several of them.
    void draw_cursor_ring(surface_t* disp, std::int16_t tx, std::int16_t ty) {
        const int x = px(tx);
        const int y = py(tx, ty);
        const std::uint32_t c = cursor_colour();
        graphics_draw_box(disp, x, y, tile_ - 1, 1, c);
        graphics_draw_box(disp, x, y + tile_ - 2, tile_ - 1, 1, c);
        graphics_draw_box(disp, x, y, 1, tile_ - 1, c);
        graphics_draw_box(disp, x + tile_ - 2, y, 1, tile_ - 1, c);
    }

    void draw_cursor(surface_t* disp, const sim::EncounterSnapshot& snapshot) {
        // An area cast lands on more than one tile, so the cursor is more than
        // one tile.
        //
        // Drawn as the cursor rather than as a fourth overlay colour, and that
        // is the whole design: the cursor already means *where the next confirm
        // lands*, so a splash is the same statement about a wider landing
        // rather than a new one to learn. It also costs no palette entry, which
        // is what lets a machine with a fixed palette draw the same picture on
        // a board that has no spare entries to give it.
        //
        // `sim::area_tiles` is the same membership test `apply` walks the units
        // against, so the ring and the character it catches cannot disagree.
        // Only the plain ring, not the pulse: four rings breathing at once
        // would read as four cursors rather than as one wide one.
#ifndef GRANDLEON_N64_PROBE
        if (aim_ == Aim::cast) {
            for (const sim::Position& tile : sim::area_tiles(
                     snapshot, aim_ability_, {cursor_x_, cursor_y_}, abilities_
                 )) {
                if (tile.x == cursor_x_ && tile.y == cursor_y_) continue;
                if (!camera_.visible(tile.x, tile.y)) continue;
                draw_cursor_ring(disp, tile.x, tile.y);
            }
        }
#else
        // The probe ROM has no controller, so nothing is ever picked up and
        // there is never a splash to draw. The snapshot is still taken, because
        // one signature is easier to keep true than two.
        (void)snapshot;
#endif
        draw_cursor_ring(disp, cursor_x_, cursor_y_);
#ifndef GRANDLEON_N64_PROBE
        const int x = px(cursor_x_);
        const int y = py(cursor_x_, cursor_y_);
        const std::uint32_t c = cursor_colour();
        // The pulse: a second ring inside the cursor's own rectangle, up for
        // half of every period. It is drawn strictly nearer the cell's edge
        // than its centre, guaranteed by `cursor_emphasis_inset`, so the pixel
        // every framebuffer probe in this repository samples to identify a
        // cell can never be one of these. And at phase zero it draws nothing
        // at all, leaving the plain cursor.
        if (!view::cursor_emphasised(pulse_frame_)) return;
        const int inset = view::cursor_emphasis_inset(tile_);
        const int span = tile_ - 1 - 2 * inset;
        if (inset <= 0 || span <= 1) return;
        graphics_draw_box(disp, x + inset, y + inset, span, 1, c);
        graphics_draw_box(
            disp, x + inset, y + inset + span - 1, span, 1, c
        );
        graphics_draw_box(disp, x + inset, y + inset, 1, span, c);
        graphics_draw_box(
            disp, x + inset + span - 1, y + inset, 1, span, c
        );
#endif
    }

    // Samples the centre of every board cell and classifies it against the
    // palette. This is what the probe asserts: not that draw() ran, but that
    // the framebuffer contains the board it claims to contain. The rdpq
    // classification checks each cell against the exact palette values its
    // own sheet or billboard would have put there.
    void classify(surface_t* disp, const sim::EncounterSnapshot& snapshot) {
        classified = {};
        const int last_x = camera_.x + camera_.view_w;
        const int last_y = camera_.y + camera_.view_h;
        for (int y = camera_.y; y < last_y && y < snapshot.height; ++y) {
            for (int x = camera_.x; x < last_x && x < snapshot.width; ++x) {
                const std::uint16_t value = sample(disp, x, y);
                // The same predicate the draw loop uses. A census wider than
                // what was drawn expects a sprite in a cell nobody painted.
                const sim::UnitSnapshot* occupant = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (sim::on_board(unit) && unit.position.x == x &&
                        unit.position.y == y) {
                        occupant = &unit;
                    }
                }
                if (occupant != nullptr &&
                    sample_matches(
                        value, unit_sprite(*occupant), 0, 0,
                        occupant->has_acted
                    )) {
                    if (occupant->side == sim::Side::first) ++classified.blue;
                    else ++classified.red;
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
                    sprite_t* chosen = unit_sprite(*occupant);
                    sprite_t* own = roster_sprite(*occupant);
                    if (chosen != own) {
                        ++classified.extra;
                        if (tellable_apart(chosen, own)) {
                            ++classified.extra_tellable;
                            if (!sample_matches(
                                    value, own, 0, 0, occupant->has_acted
                                )) {
                                ++classified.extra_distinct;
                            }
                        }
                    }
#endif
                    continue;
                }
                const std::size_t index =
                    static_cast<std::size_t>(y) * snapshot.width +
                    static_cast<std::size_t>(x);
                const std::uint64_t id =
                    index < terrain_.size() ? terrain_[index] : 0;
                const std::uint8_t kind = kind_of(id);
                sprite_t* sheet = terrain_sprite(id);
                if (sheet != nullptr &&
                    value ==
                        filtered_sample(
                            sheet, terrain_variant(x, y) * 32, 0
                        )) {
                    if (kind == kind_named("grass")) ++classified.grass;
                    else if (kind == kind_named("water")) ++classified.water;
                    else if (kind == kind_named("road")) ++classified.road;
                    else if (kind == kind_named("forest")) ++classified.forest;
                    else ++classified.other_terrain;
                } else {
                    ++classified.unknown;
                }
            }
        }
        last_buffer_ = disp;
    }

    [[nodiscard]] std::uint16_t sample(
        surface_t* disp,
        int cell_x,
        int cell_y
    ) const {
        const int sample_x = px(cell_x) + tile_ / 2;
        const int sample_y = py(cell_x, cell_y) + tile_ / 2;
        // Read what the RDP wrote through the uncached segment, past any
        // stale cache line.
        const auto* base =
            static_cast<const std::uint8_t*>(UncachedAddr(disp->buffer));
        const auto* row =
            base + static_cast<std::size_t>(sample_y) * disp->stride;
        return reinterpret_cast<const std::uint16_t*>(row)[sample_x];
    }

    [[nodiscard]] std::uint16_t sample_at(int cell_x, int cell_y) const {
        return last_buffer_ != nullptr
                   ? sample(last_buffer_, cell_x, cell_y)
                   : 0;
    }

#ifdef GRANDLEON_N64_AUTOPILOT
    // The stipple pixel of a cell, where draw_highlights plots its checker one
    // row below the sampled centre. Read through the CPU's own view, because
    // the checker is CPU-drawn under both renderers.
    [[nodiscard]] std::uint16_t stipple_pixel(int cell_x, int cell_y) const {
        if (last_buffer_ == nullptr) return 0;
        const int x = px(cell_x) + tile_ / 2;
        const int y = py(cell_x, cell_y) + tile_ / 2 + 1;
        const auto* base =
            static_cast<const std::uint8_t*>(last_buffer_->buffer);
        const auto* row =
            base + static_cast<std::size_t>(y) * last_buffer_->stride;
        return reinterpret_cast<const std::uint16_t*>(row)[x];
    }
#endif

#ifdef GRANDLEON_N64_PROBE
    client::Intent probe_intent(const sim::EncounterSnapshot& snapshot) {
        if (probe_step_ >= spent_probe_step) return spent_probe(snapshot);
        if (probe_step_ == 0) {
            ++probe_step_;
            // The Fordlight's terrain inside this machine's window, minus the
            // eight cells covered by units (all of which stand on grass). The
            // board is wider than the window, so this is what the camera holds
            // at rest rather than the whole authored board: sixteen columns by
            // eight, which is a hundred and twenty-eight cells and every one of
            // them classified. The crossing itself is entirely inside it, which
            // is why the water and the road below did not move when the board
            // grew and the two open terrains did.
            expect(classified.grass == 64, "framebuffer shows 64 grass cells");
            expect(classified.forest == 28, "framebuffer shows 28 forest cells");
            expect(classified.water == 12, "framebuffer shows 12 water cells");
            expect(classified.road == 16, "framebuffer shows 16 road cells");
            expect(classified.blue == 4, "framebuffer shows 4 blue units");
            expect(classified.red == 4, "framebuffer shows 4 red units");
            expect(classified.unknown == 0, "no unclassifiable cell");
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
            // Only a ROM carrying a second combination compiles these, and
            // they are the whole claim such a ROM exists to make. Content-free
            // on purpose: whichever project is being built, at least one unit
            // has to be standing in a drawing beyond the project's own, and
            // every such unit's pixels have to be that drawing's rather than
            // the roster's.
            report_line(
                "DRAWINGS %d embedded, %d units drawn from them, %d of those "
                "tellable from the roster here, %d telling\n",
                static_cast<int>(extra_drawing_count),
                classified.extra,
                classified.extra_tellable,
                classified.extra_distinct
            );
            expect(
                classified.extra > 0,
                "at least one unit is drawn from a combination beyond the "
                "project's own"
            );
            expect(
                classified.extra_tellable > 0,
                "and at least one of them draws a texel the roster's own "
                "drawing never draws"
            );
            expect(
                classified.extra_distinct == classified.extra_tellable,
                "and every one that can be told apart here shows that "
                "combination's texels rather than the roster's"
            );
#endif

            // The knight standing at (0,3) walks onto the bridge road.
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (unit.position.x == 0 && unit.position.y == 3) {
                    client::Intent intent;
                    intent.kind = client::IntentKind::move_to;
                    intent.unit_id = unit.id;
                    intent.destination = {1, 3};
                    return intent;
                }
            }
            expect(false, "the knight is where the source placed it");
        }
        if (probe_step_ == 1) {
            ++probe_step_;
            // The guard's block is still open behind the knight, which is the
            // frame this game spends most of its time drawing: a character that
            // has taken its turn standing among characters that have not.
            //
            // Nobody has crossed the river and nobody has been asked to fall,
            // so red is still four and no cell is unreadable.
            expect(
                classified.red == 4, "and the Coil has not been asked to move"
            );
            expect(classified.unknown == 0, "still no unclassifiable cell");
            // The knight is spent and standing, and the census has to find it
            // among its own four rather than lose it to the terrain.
            // `blit_sprite_spent` draws the figure through a greyed copy of
            // its own palette, so the cell is still a palette entry and is
            // still countable. Which greyed drawing it is belongs to
            // `probe_the_spent_figure`, which recomputes the texel and asserts
            // the framebuffer carries the greyed reading of it and not the
            // plain one; what is claimed here is only that the figure is on
            // the board at all.
            expect(
                classified.blue == 4,
                "and the knight is drawn among its own four, spent"
            );
            // The round the block belongs to. Red is never asked to play until
            // this number moves, which is what makes it the drain's own
            // stopping condition rather than a count of characters kept here.
            probe_round_ = snapshot.round;
        }
        if (probe_step_ >= 2 && snapshot.round == probe_round_) {
            // Close the block. The engine names no actor under one, so the side
            // is finished one character at a time and the turn passes when the
            // last of them is done, which is the only way red is ever asked to
            // play. Every one of these is a `wait`, so the board red answers is
            // the board the knight's walk left.
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (unit.side != player_side_ || !sim::on_board(unit)) continue;
                if (unit.has_acted) continue;
                client::Intent intent;
                intent.kind = client::IntentKind::wait;
                intent.unit_id = unit.id;
                return intent;
            }
        }
        if (probe_step_ >= 2) {
            // Red has taken a whole block of its own between these frames.
            expect(classified.blue == 4, "both sides survive the exchange");
            expect(classified.red == 4, "the red side acted and survived");
            expect(classified.unknown == 0, "no unclassifiable cell after it");
            expect(
                cell_is_blue(snapshot, 1, 3),
                "the knight still holds the bridge road"
            );
            report_line(
                "RESULT %s %d/%d\n",
                failures == 0 ? "PASS" : "FAIL", checks - failures, checks
            );
        }
        client::Intent intent;
        intent.kind = client::IntentKind::quit;
        return intent;
    }
    int probe_step_{0};
    // The round the guard's block was opened in, so the drain in
    // `probe_intent` stops when the turn has been round to red and back rather
    // than after a number of characters written down here.
    std::uint32_t probe_round_{0};


    // Where the scripted playthrough stands while it is on the board it plays
    // for the renderer rather than for the rules. Far enough above the
    // campaign's own steps that the two scripts can never be confused for one
    // another, and the board hands the counter back to zero on its way out.
    static constexpr int spent_probe_step = 100;

public:
    // Point the script at that board. Called once, before the board is played.
    void begin_spent_probe() {
        probe_step_ = spent_probe_step;
        spent_id_ = 0;
    }

private:
    // Whether the greyed reading of the cell a spent character stands in is a
    // different colour from the plain one, over every texel the sampler will
    // accept. Without it the assertion below would pass on a renderer that
    // never greyed anything, on any drawing whose sampled block happens to be
    // grey already. So the check states what it is entitled to state, and this
    // is what entitles it.
    [[nodiscard]] bool greying_is_visible(sprite_t* drawing) const {
        const int base = sampled_texel();
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                int tx = base + dx;
                int ty = base + dy;
                if (tx > 31) tx = 31;
                if (ty > 31) ty = 31;
                const std::uint16_t texel = texel16(drawing, tx, ty);
                if (spent_entry(texel) == texel) return false;
            }
        }
        return true;
    }

    // The board a character can be finished on, and the only claim made here:
    // that a character who has taken their turn is on the screen, in grey.
    //
    // One `wait` and one frame is the whole script. Nothing is moved and
    // nobody is struck, because the picture under test is a *standing* figure
    // whose turn is over, which is what most of a side block looks like and
    // what a player spends most of the game looking at.
    client::Intent spent_probe(const sim::EncounterSnapshot& snapshot) {
        client::Intent intent;
        if (probe_step_ == spent_probe_step) {
            ++probe_step_;
            // Close one character's turn. Under an order that finishes
            // characters one at a time the side keeps the board, so nobody
            // else moves between this frame and the next and the only thing
            // that changed is how this one is drawn.
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (unit.side != player_side_ || !sim::on_board(unit)) continue;
                if (unit.has_acted) continue;
                spent_id_ = unit.id;
                intent.kind = client::IntentKind::wait;
                intent.unit_id = unit.id;
                return intent;
            }
            expect(false, "the block opens with somebody who has not acted");
        } else if (probe_step_ == spent_probe_step + 1) {
            check_the_spent_figure(snapshot);
        }
        // Back to the start, because the campaign's own script is next and it
        // counts from zero.
        probe_step_ = 0;
        intent.kind = client::IntentKind::quit;
        return intent;
    }

    void check_the_spent_figure(const sim::EncounterSnapshot& snapshot) {
        const sim::UnitSnapshot* spent = nullptr;
        int side_on_board = 0;
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (!sim::on_board(unit)) continue;
            if (unit.side == player_side_) ++side_on_board;
            if (unit.id == spent_id_) spent = &unit;
        }
        expect(spent != nullptr, "the character that waited is still standing");
        if (spent == nullptr) return;
        expect(spent->has_acted, "and the engine has it as finished");
        sprite_t* drawing = unit_sprite(*spent);
        expect(drawing != nullptr, "and it is drawn from a loaded sheet");
        if (drawing == nullptr) return;
        const int cell_x = spent->position.x;
        const int cell_y = spent->position.y;
        const std::uint16_t value = sample_at(cell_x, cell_y);
        const int base = sampled_texel();
        const std::uint16_t texel = texel16(drawing, base, base);
        // Both readings of the same texel, so a failure says which picture
        // reached the framebuffer instead of only that the right one did not.
        //
        // The census travels with them and is reported rather than asserted.
        // Cells this board draws through the bilinear filter are not pinned
        // here: `filtered_sample` is exact on a 25-pixel cell and disagrees
        // with the hardware by one step of one channel on the 22-pixel cell a
        // twelve-wide board is drawn at, wherever the sheet has a gradient
        // under the sample. What *is* asserted is the point-sampled figures,
        // which is what this board is being drawn for.
        report_line(
            "SPENT cell=%d,%d framebuffer=%04x greyed=%04x plain=%04x "
            "side=%d unknown=%d\n",
            cell_x, cell_y, static_cast<unsigned>(value),
            static_cast<unsigned>(spent_entry(texel)),
            static_cast<unsigned>(texel), classified.blue, classified.unknown
        );
        expect(
            greying_is_visible(drawing),
            "the greyed reading of that cell is not the colour the drawing "
            "carries"
        );
        expect(
            sample_matches(value, drawing, 0, 0, true),
            "a character that has taken its turn is drawn on the board in grey"
        );
        expect(
            !sample_matches(value, drawing, 0, 0, false),
            "and not in the colours it is drawn in while its turn is open"
        );
        expect(
            classified.blue == side_on_board,
            "and every other character of that side is still on screen"
        );
    }

    // Who the script finished, so the frame after can be asked about them.
    sim::UnitId spent_id_{};
#else
    static void wait_for_a() {
        while (true) {
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            if (pressed.a) return;
            wait_ms(16);
        }
    }

    // Opens `id`'s action menu, if `id` is still somebody the player may
    // command. Returns the intent the menu produced, or kind none when the
    // player backed out of it or when the character is no longer there.
    client::Intent menu_for(
        const sim::EncounterSnapshot& snapshot, sim::UnitId id
    ) {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id == id && sim::on_board(unit)) {
                return unit_action_menu(snapshot, unit);
            }
        }
        return {};
    }

    // The turn the player asked the board menu to finish, one character per
    // call. `unfinished_unit` names whoever still owes the board an activation
    // on this side and answers zero when nobody does, so the drain stops of its
    // own accord rather than on a count kept here, which is what makes it
    // right under all three turn orders. Under the alternating order both
    // shipped campaigns play, a side holds one activation, so this sends one
    // WAIT and `draw` clears the flag as the turn passes; under `side_blocks`
    // it sends one per character still standing idle, so ending the side's turn
    // spends every unspent action rather than one of them.
    //
    // Kind none when the player never asked, or when the side owes nothing.
    client::Intent drain_intent(const sim::EncounterSnapshot& snapshot) {
        client::Intent intent;
        if (!finishing_) return intent;
        const sim::UnitId owed =
            client::unfinished_unit(snapshot, player_side_);
        if (owed == 0 || owed == drain_last_) {
            finishing_ = false;
            drain_last_ = 0;
            return intent;
        }
        drain_last_ = owed;
        selected_ = 0;
        clear_aim();
        reopen_menu_ = 0;
        intent.kind = client::IntentKind::wait;
        intent.unit_id = owed;
        return intent;
    }

    client::Intent controller_intent(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster
    ) {
        {
            const client::Intent owed = drain_intent(snapshot);
            if (owed.kind != client::IntentKind::none) return owed;
        }
        // A character that has just walked is asked what it does next, rather
        // than being left standing on a board with nothing said about it. This
        // is the same menu the selection opens, reopened on the far side of the
        // engine accepting the walk, which is why it is a flag consumed here
        // and not a call inside `commit_aim`: the walk has to reach the engine
        // and come back before there is a new board to open a menu over.
        if (reopen_menu_ != 0) {
            const sim::UnitId walked = reopen_menu_;
            reopen_menu_ = 0;
            if (walked == selected_) {
                // Unless the board has nothing left to offer it, in which case
                // its turn ends where it stands rather than a menu opening
                // whose every committing row the engine would refuse.
                // `client::nothing_left_to_do` is the judgement, and it is the
                // same one every other front end asks, so no two machines can
                // disagree about when a turn ends itself.
                if (client::nothing_left_to_do(
                        snapshot, walked, weapons_, abilities_, items_
                    )) {
                    client::Intent done;
                    done.kind = client::IntentKind::wait;
                    done.unit_id = walked;
                    selected_ = 0;
                    clear_aim();
                    return done;
                }
                const client::Intent intent =
                    menu_for(snapshot, walked);
                if (intent.kind != client::IntentKind::none) return intent;
                draw(snapshot, roster);
            }
        }
        while (true) {
            grandleon::n64audio::pump();
            const joypad_buttons_t pressed = poll_buttons();
            bool moved = false;
            if (aim_ != Aim::none &&
                client::gesture_names_a_character(aimed_gesture().kind)) {
                // A pick that names a character turns the d-pad into "which of
                // these". The candidates are the engine's own aiming answer,
                // already lit under the cursor, and the step between them is
                // `client::next_aim_tile`, the same function the other console
                // steps with, so a press means the same thing on both.
                int dx = 0;
                int dy = 0;
                if (pressed.d_up) dy = -1;
                if (pressed.d_down) dy = 1;
                if (pressed.d_left) dx = -1;
                if (pressed.d_right) dx = 1;
                if (dx != 0) dy = 0;
                if (dx != 0 || dy != 0) {
                    const sim::Position landed = client::next_aim_tile(
                        aim_tiles(snapshot), {cursor_x_, cursor_y_}, dx, dy
                    );
                    if (!(landed == sim::Position{cursor_x_, cursor_y_})) {
                        cursor_x_ = landed.x;
                        cursor_y_ = landed.y;
                        moved = true;
                    }
                }
            } else {
                if (pressed.d_up && cursor_y_ > 0) { --cursor_y_; moved = true; }

                if (pressed.d_down && cursor_y_ + 1 < snapshot.height) {
                    ++cursor_y_;
                    moved = true;
                }
                if (pressed.d_left && cursor_x_ > 0) {
                    --cursor_x_;
                    moved = true;
                }
                if (pressed.d_right && cursor_x_ + 1 < snapshot.width) {
                    ++cursor_x_;
                    moved = true;
                }
            }

            if (pressed.b) {
                // One step back per press: a pick is put down before a
                // selection is, so backing out of a spell does not also lose
                // the character.
                if (aim_ != Aim::none) {
                    clear_aim();
                    moved = true;
                } else if (selected_ != 0) {
                    selected_ = 0;
                    moved = true;
                }
            }
            // START opens the board menu, at any point in a battle and whether
            // or not a character is selected. A START that sent a WAIT for the
            // selected character and did nothing at all otherwise is the defect
            // a cartridge shows: the one thing this console's "I am done"
            // button did would be undiscoverable, and nothing anywhere on a
            // board would offer to end a turn or to leave. What ends a
            // character's turn has a row in that character's own menu, and
            // START answers the questions that are about the battle rather than
            // about one of its characters.
            if (pressed.start) {
                client::Intent intent = board_menu(snapshot);
                if (intent.kind == client::IntentKind::none) {
                    intent = drain_intent(snapshot);
                }
                if (intent.kind != client::IntentKind::none) return intent;
                moved = true;
            }
            // Z opens the unit action menu: everything this character can be
            // told to do, walking and waiting among it, and nothing needs to be
            // aimed at to ask. Selecting a character opens the same menu, so Z
            // is what reopens it for a character already picked up, after a
            // CANCEL or after a strike was aimed and put back down.
            // It is not a list of the character's spells over a cursor already
            // pointed at something: this menu holds rows that answer a
            // different question, so the gesture that opens it means "what can
            // this character do" rather than "what can it cast at that".
            // R asks for the whole of what the opposing side threatens,
            // rather than only the part of it this character could walk into.
            // A toggle rather than a hold: a shoulder button held while the
            // d-pad steers is a grip, and an edge is also the one gesture an
            // autopilot's press table can express.
            if (pressed.r) {
                threat_view_ = !threat_view_;
                moved = true;
            }
            if (pressed.z && selected_ != 0 && aim_ == Aim::none) {
                const sim::UnitSnapshot* actor = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (unit.id == selected_) actor = &unit;
                }
                if (actor != nullptr) {
                    client::Intent intent =
                        unit_action_menu(snapshot, *actor);
                    if (intent.kind != client::IntentKind::none) {
                        return intent;
                    }
                    moved = true;
                }
            }
            if (pressed.a && aim_ != Aim::none) {
                return commit_aim(snapshot);
            }
            if (pressed.a) {
                const sim::UnitSnapshot* occupant = nullptr;
                for (const sim::UnitSnapshot& unit : snapshot.units) {
                    if (sim::on_board(unit) &&
                        unit.position.x == cursor_x_ &&
                        unit.position.y == cursor_y_) {
                        occupant = &unit;
                    }
                }
                // The same gesture rule every client uses: an enemy under the
                // cursor is an attack, an empty tile is a move, one of yours
                // is a selection. The strike uses the weapon in hand; a
                // character that wants the other one, or a spell, asks in the
                // menu. The shortcut is never the only path and never the
                // wrong one.
                if (selected_ != 0 && occupant != nullptr &&
                    occupant->side != player_side_) {
                    client::Intent intent;
                    intent.kind = client::IntentKind::attack;
                    intent.unit_id = selected_;
                    intent.target_id = occupant->id;
                    selected_ = 0;
                    return intent;
                }
                if (occupant != nullptr && occupant->side == player_side_ &&
                    occupant->has_acted) {
                    // Spent, but not gone. The cursor still rests on them, the
                    // panel still reads them out and the sheet still opens on
                    // them, because a player needs to see their whole line.
                    // The only thing they cannot do is be picked up again.
                    // Said in the same banner every other refusal uses, rather
                    // than by a press that quietly does nothing, and it says
                    // whose turn is over, because a player picking their own
                    // order needs to know which of their line is spent.
                    commanded_ = occupant->id;
                    refused(sim::CommandError::already_acted);
                } else if (occupant != nullptr &&
                           occupant->side == player_side_) {
                    grandleon::n64audio::play(
                        grandleon::n64audio::Sfx::select
                    );
                    selected_ = occupant->id;
                    // And the menu opens on it. A pick-up that only lit the
                    // character's reach and said nothing else would leave the
                    // whole of what it can be told to do behind a shoulder
                    // button nobody has been told about. The front door is the
                    // press a player already makes, and everything below it is
                    // the shortcut for a row the menu names: walking onto a
                    // lit tile, striking whoever the cursor rests on.
                    // CANCEL closes it and leaves the character in hand, which
                    // is exactly the state a bare pick-up would leave behind.
                    const client::Intent intent =
                        menu_for(snapshot, selected_);
                    if (intent.kind != client::IntentKind::none) return intent;
                    moved = true;
                } else if (selected_ != 0 && occupant == nullptr) {
                    client::Intent intent;
                    intent.kind = client::IntentKind::move_to;
                    intent.unit_id = selected_;
                    intent.destination = {cursor_x_, cursor_y_};
                    commanded_ = selected_;
                    // And the selection stays, and the menu opens again behind
                    // the walk. A walk does not finish a character: the point
                    // it walked with was one of its turn's and the action is
                    // the other. So the next thing the player wants is almost
                    // always this character's strike or the row that ends its
                    // turn, and dropping the selection would make them find it
                    // on the board again first. `draw` lets it go the moment
                    // the engine says the character is done.
                    //
                    // This is the second half of a defect a cartridge shows in
                    // two parts: a walk that locks out everybody else, and a
                    // walk after which nothing says what to do with the
                    // character that walked. The second is answered by the same
                    // menu the WALK row's own aim comes back to, because both
                    // are the same walk.
                    reopen_menu_ = selected_;
                    return intent;
                }
            }
            if (moved) {
                camera_.follow(cursor_x_, cursor_y_, 2);
                draw(snapshot, roster);
            } else {
                advance_pulse(snapshot);
            }
            wait_ms(16);
        }
    }
#endif

    // The terrain kind a cell's identity draws as, out of the package's own
    // join. A cell the package cannot place reads as unknown and draws the
    // neutral cell rather than nothing. That happens when it names a terrain
    // no art-library keyword matched, or when the package predates the join.
    [[nodiscard]] std::uint8_t kind_of(std::uint64_t id) const {
        const std::uint8_t kind = shown_.kind_of_terrain(id);
        return kind < terrain_kind_count ? kind : gc::terrain_kind_unknown;
    }

    sim::Side player_side_{sim::Side::first};
    // The season this game's ground is drawn in, from its package.
    std::uint8_t theme_{gc::default_theme};
    // Everything the package says about how its content looks: the theme, the
    // colour each unit type's characters wear, the archetype they wear it as,
    // and the kind each terrain identity draws as.
    pr::Presentation shown_;
    // Borrowed, never owned, and outliving this presenter: the package is
    // loaded in `main` and this is built on top of it.
    const pf::LoadedPackage* package_{nullptr};
    std::vector<std::uint64_t> terrain_;
    client::Roster roster_;
    // Whose command went out last, so a refusal about a character can name
    // them. Set beside every intent this presenter hands back, and by the one
    // refusal it raises itself.
    sim::UnitId commanded_{0};
    int tile_{16};
    int origin_x_{20};
    int origin_y_{20};
    // The board's pixel geometry, shared with the desktop and the editor
    // through platform/view. Everything that asks where a cell is asks this.
    view::Projection projection_{20, 20, 16, 0};
    // How wide the terrain vector's rows are, so a cell's elevation can be
    // looked up without a snapshot in hand.
    int board_width_{0};
    std::int16_t cursor_x_{0};
    std::int16_t cursor_y_{0};
    sim::UnitId selected_{0};
    // Whether the player has asked to see everything the opposing side
    // threatens, rather than only the part of it the selected character could
    // walk into. Off unless R has been pressed, because the default view is
    // the one a player has to be able to read.
    bool threat_view_{false};
    bool menu_open_{false};
    // Whether the box on screen is the board's menu rather than a character's.
    // The two wear the same frame on purpose and are told apart by what they
    // hold, so a checkpoint has to be able to say which it is looking at.
    bool board_menu_open_{false};
    // Whether the screen asking what leaving costs is up.
    bool leaving_open_{false};
    // Whether the player took END <SIDE> TURN and the side is still being
    // drained of the activations it had not spent.
    bool finishing_{false};
    // Who the drain waited last. A side that comes back round to the player
    // with the same character still owing an activation is a side the drain
    // cannot finish: under the alternating order nothing marks a character as
    // having acted, and an opposing side with nobody standing hands the turn
    // straight back. So the same answer twice ends it rather than sending the
    // same WAIT for ever. It is also what stops a refused WAIT looping: a
    // refusal changes nothing, so the next answer is the same answer.
    sim::UnitId drain_last_{0};
    // The character whose menu opens again once the engine has taken its walk,
    // or zero. Set where the walk is committed and consumed one call later,
    // because a menu opened before the command reached the engine would be a
    // menu over the board the character has already left.
    sim::UnitId reopen_menu_{0};
    // Where the information panel last drew itself, or a zero width when it
    // drew nothing. Read only by the checkpoints; see `sample_under_panel`.
    int panel_left_{0};
    int panel_top_{0};
    int panel_width_{0};
    int panel_height_{0};
#ifndef GRANDLEON_N64_PROBE
    // The rows on screen, so a checkpoint can assert the menu's shape rather
    // than only that a box was drawn. The storage belongs to `run_menu`'s
    // caller and outlives the call; nothing reads it while the menu is closed.
    const MenuChoice* menu_choices_{nullptr};
    int menu_count_{0};
    int menu_chosen_{0};
    // The information sheet the INFO row opened, held while it is on screen so
    // a checkpoint can assert the lines rather than only that a frame was
    // filled. Composed by `grandleon::sheet`; this file only draws it.
    bool sheet_open_{false};
    grandleon::sheet::UnitSheet sheet_{};
    // What the menu handed back, waiting for a tile, and the prompt that says
    // so. Drawn at the free end of the status line the frame already carries,
    // so the unit action menu costs this screen no panel of its own.
    //
    // The probe ROM has no controller and no menu, and the types these are
    // written in live with the rest of the interactive presenter.
    Aim aim_{Aim::none};
    sim::ContentId aim_weapon_{0};
    sim::ContentId aim_ability_{0};
    char aim_text_[32]{};
    // The item rows' labels. Beside the aim text rather than beside the
    // registries below, because it belongs to the menu and the probe build has
    // no menu, the same reason the aim state above is here.
    char item_labels_[menu_item_labels][menu_item_label_size]{};
#endif
    // The encounter's own registries, kept so the danger zone can be asked
    // what every carried weapon and every damaging ability threatens, and so a
    // menu row can say what spending a carried item would do.
    std::vector<sim::WeaponDefinition> weapons_;
    std::vector<sim::AbilityDefinition> abilities_;
    std::vector<sim::ItemDefinition> items_;
    // How many rounds this board is won by surviving; zero when nothing on it
    // is. No global constructor: a plain integer with a static initialiser.
    std::uint32_t rounds_to_survive_{0};
    surface_t* last_buffer_{nullptr};
    sim::EncounterSnapshot previous_{};
    // Where the last saying was drawn, and who it was about. Zero width is a
    // board nobody has spoken over yet.
    int said_left_{0};
    int said_top_{0};
    int said_width_{0};
    int said_height_{0};
    sim::UnitId said_about_{0};
    // Where on the screen the speaker stood when the bubble was drawn. Kept
    // rather than looked up again, because a checkpoint fires against whatever
    // board it is handed and a board's own opening scene precedes that board.
    int said_at_x_{-1};
    int said_at_y_{-1};

    // What this board says while it is fought, empty for a board nobody speaks
    // over, which is every board authored before moments existed.
    std::vector<grandleon::package_runtime::EncounterMoment> moments_;
    std::vector<grandleon::package_runtime::PlacementIdentity> moment_placements_;
    bool opening_moments_played_{false};
    bool has_previous_{false};
    view::Camera camera_{};
    // The frame's draw list, rebuilt from the snapshot every frame and put
    // in depth order by the shared model rather than by this file.
    view::DrawList draw_list_{draw_storage, draw_capacity};
#ifndef GRANDLEON_N64_PROBE
    // The cursor's pulse phase, in drawn frames. It advances only while the
    // player is holding the board, never during an animation, a menu, a banner
    // or a cutscene, and every board the player is shown starts it at rest. So
    // it is a property of one board rather than of the machine's uptime.
    std::uint32_t pulse_frame_{0};
    // Scratch for the route a slide is drawn along: which cells the engine's
    // reachability query returned, and how far each is from where the unit
    // stood. Fixed and owned by the presenter, so an animation allocates
    // nothing; a board with more cells than this draws a straight line.
    std::uint8_t route_reachable_[route_cells]{};
    std::uint8_t route_distance_[route_cells]{};
#endif
    sprite_t* terrain_sprites_[terrain_kind_count]{};
    sprite_t* unit_sprites_[archetype_count][faction_count]{};
    sprite_t* unit_frames_[archetype_count][faction_count]{};
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
    // As long as the content is wide, not as long as the art library's menus
    // are: a ROM whose project draws one combination declares neither of
    // these, and one whose content draws two archers of the same style,
    // figure, archetype and colour declares one entry for both of them.
    sprite_t* extra_sprites_[extra_drawing_count]{};
    sprite_t* extra_frames_[extra_drawing_count]{};
#endif
    sprite_t* shadow_sprite_{nullptr};
    // A CI4 lookup table's worth of scratch for the water rotation, aligned
    // because `rdpq_tex_upload_tlut` requires its source to be.
    static constexpr int water_tlut_entries = 16;
    alignas(8) std::uint16_t water_tlut_[water_tlut_entries]{};
    // The greyed copy of one character drawing's palette, and the drawing it
    // belongs to. Sixteen entries because every character sheet in this
    // repository is CI4, which is the art contract's own bound rather than an
    // assumption here; eight-byte aligned because `rdpq_tex_upload_tlut`
    // asserts its source is, and a `std::uint16_t` array is promised two.
    static constexpr int spent_tlut_entries = 16;
    struct SpentTlut final {
        sprite_t* drawing{nullptr};
        alignas(8) std::uint16_t tlut[spent_tlut_entries]{};
    };
    // One per drawing this ROM can put on a board: the grid of archetypes in
    // faction colours, plus whatever combinations beyond the project's own it
    // was built carrying. A bound rather than a count: a ROM whose content
    // fields three archetypes builds three tables and leaves the rest empty.
    static constexpr std::size_t spent_tlut_capacity =
        archetype_count * faction_count
#ifdef GRANDLEON_N64_EXTRA_DRAWINGS
        + extra_drawing_count
#endif
        ;
    SpentTlut spent_tluts_[spent_tlut_capacity]{};
    std::size_t spent_tlut_count_{0};
    // Resolved from the art library's own registry rather than written down as
    // a number, so a terrain kind inserted before water cannot make this ROM
    // shimmer the road.
    static constexpr int water_kind_ =
        static_cast<int>(kind_named(shimmered_terrain));
};

#ifdef GRANDLEON_N64_PROBE
// One board played for the renderer's sake, before the campaign's own.
//
// A character who has taken their turn is drawn greyed, and on a board whose
// turns alternate between the sides there is never such a character: closing a
// turn there hands the board to the other side and marks nobody finished. So
// this is the one picture the campaign's first board cannot be asked about,
// and it is checked on a board whose turns are taken in blocks.
//
// Which board that is, is asked of the content rather than written down: every
// encounter the package holds is opened and the first that resolves turns any
// other way is the one played. A game that renames its boards, reorders them,
// or authors a second one first is still checked, and a game that authors none
// fails here rather than quietly checking nothing.
//
// Before the campaign so that the frame left on the screen at the end of the
// run is the campaign's, which is the frame this ROM has always ended on.
void probe_the_spent_figure(
    const pf::LoadedPackage& package, N64Presenter& presenter
) {
    const pf::SectionView* encounters =
        package.find(pf::SectionType::encounters);
    if (encounters != nullptr) {
        for (const pf::RecordView& record : encounters->records) {
            const auto board = pr::load_encounter(package, record.stable_id);
            if (!board) continue;
            if (board.definition.turn_order == sim::TurnOrder::alternating) {
                continue;
            }
            presenter.begin_spent_probe();
            client::BattleReport report;
            const auto status = client::play_battle(
                board, sim::Side::first, presenter, report
            );
            expect(
                status == client::SessionError::none,
                "the board whose turns are taken in blocks plays"
            );
            return;
        }
    }
    expect(
        false, "this game authors a board whose turns are taken in blocks"
    );
}
#endif

#if !defined(GRANDLEON_N64_PROBE) && !defined(GRANDLEON_N64_AUTOPILOT)

// Edge triggered, plus a held direction that keeps stepping.
//
// One press moved the cursor one cell, so crossing a board the width of the
// Fordlight cost twenty presses of the same direction. A direction leaned on
// now repeats: after a third of a second, ten cells a second, which is the pace
// a character walks a tile at and so a speed the player has already been
// taught. `view::repeat_due` holds both numbers, because the PlayStation reads
// its own pad and two copies of this rule would drift.
//
// Directions only. A repeating A would confirm things nobody chose and a
// repeating START would open and close a menu as fast as it could draw one.
//
// Here rather than in the client, and this build is the only one that compiles
// it: a probe and an autopilot have their own `poll_buttons` above, so a
// recorded script cannot meet a repeat and every expectation derived from one
// stays exactly what it was.
joypad_buttons_t poll_buttons() {
    joypad_poll();
    joypad_buttons_t edges = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    const joypad_buttons_t held = joypad_get_buttons(JOYPAD_PORT_1);
    const unsigned leaning =
        (held.d_up ? 1U : 0U) | (held.d_down ? 2U : 0U) |
        (held.d_left ? 4U : 0U) | (held.d_right ? 8U : 0U);
    // Static, because a hold is a fact about the thumb rather than about
    // whichever loop happens to be asking: the cursor, the company rows and the
    // board menu all poll through here and a lean carries across them.
    static unsigned leaned_on = 0;
    static int leaned_frames = 0;
    const bool stepped =
        edges.d_up || edges.d_down || edges.d_left || edges.d_right;
    if (leaning != leaned_on || stepped) {
        leaned_on = leaning;
        leaned_frames = 0;
    } else if (leaning != 0) {
        ++leaned_frames;
        if (view::repeat_due(leaned_frames)) {
            edges.d_up = held.d_up;
            edges.d_down = held.d_down;
            edges.d_left = held.d_left;
            edges.d_right = held.d_right;
        }
    }
    return edges;
}

#elif defined(GRANDLEON_N64_AUTOPILOT)

namespace ap = grandleon::n64autopilot;

std::size_t autopilot_index = 0;
int autopilot_hold = 0;
bool autopilot_done = false;
// How many presses the current `cursor_to` has spent, and the most it may.
// Generous next to any board this ROM plays and small next to the harness's
// clock, so a script that cannot reach the tile it named fails in a second
// rather than timing the run out.
int autopilot_steer = 0;
constexpr int autopilot_steer_budget = 64;

// Which script is playing. A campaign build chooses between two in `main`, out
// of what the cartridge is holding, which is the property the persistence
// check exists to test, so the choice cannot be a flag the ROM carries.
#ifdef GRANDLEON_N64_CAMPAIGN
const ap::Step* autopilot_script = ap::campaign_autopilot_found;
#else
const ap::Step* autopilot_script = ap::fordlight_autopilot;
#endif

joypad_buttons_t buttons_from(std::uint16_t mask) {
    joypad_buttons_t out{};
    out.a = (mask & ap::button_a) != 0U;
    out.b = (mask & ap::button_b) != 0U;
    out.start = (mask & ap::button_start) != 0U;
    out.z = (mask & ap::button_z) != 0U;
    out.d_up = (mask & ap::button_d_up) != 0U;
    out.d_down = (mask & ap::button_d_down) != 0U;
    out.d_left = (mask & ap::button_d_left) != 0U;
    out.d_right = (mask & ap::button_d_right) != 0U;
    return out;
}

// The colour a scene's backdrop puts on one row of the frame, read out of the
// generated band table with the same arithmetic `draw_scene_backdrop` paints
// with. A scene naming no backdrop is the house colour, which is what this
// screen was before the menu existed.
std::uint16_t expected_scene_pixel(std::uint8_t backdrop, int y) {
    if (!scene_has_backdrop(backdrop)) return pack16(16, 26, 27);
    const int index = backdrop - 1;
    for (int band = 0; band < grandleon_backdrop_band_count[index]; ++band) {
        const auto& entry = grandleon_backdrop_bands[index][band];
        if (y >= backdrop_row_top(entry[0]) &&
            y < backdrop_row_top(entry[0] + entry[1])) {
            return pack16(entry[2], entry[3], entry[4]);
        }
    }
    // Unreachable while the bands fill the frame, which the generator refuses
    // to emit a backdrop without; stated rather than assumed.
    return pack16(16, 26, 27);
}

#ifdef GRANDLEON_N64_CAMPAIGN
// Whether the portrait on screen is `expected` rather than `other`.
//
// A portrait is drawn texel by texel at twice its size from (24, 44), opaque
// texels only (`draw_portrait`), so the screen pixel a texel landed on is
// arithmetic rather than a guess. The comparison is made only where the two
// drawings actually differ, and the count of those places is reported, so a
// pair of drawings this could not tell apart fails here instead of passing
// vacuously. That is the difference between evidence and a coincidence: a
// match against a drawing indistinguishable from the wrong one says nothing.
bool portrait_shows(sprite_t* expected, sprite_t* other) {
    if (expected == nullptr || other == nullptr) return false;
    int distinguishing = 0;
    int agreeing = 0;
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 32; ++tx) {
            const std::uint16_t mine = texel16(expected, tx, ty);
            if ((mine & 1U) == 0U) continue;
            const std::uint16_t theirs = texel16(other, tx, ty);
            if (mine == theirs) continue;
            ++distinguishing;
            if (surface_pixel(autopilot_screen, 24 + tx * 2, 44 + ty * 2) ==
                mine) {
                ++agreeing;
            }
        }
    }
    report_line(
        "cast %d texels tell the two drawings apart, %d of them agree\n",
        distinguishing, agreeing
    );
    return distinguishing > 0 && agreeing == distinguishing;
}
#endif

// The screen checkpoints, verified from the last shown frame; board
// checkpoints are handed to the presenter, which owns the geometry.
void autopilot_checkpoint(ap::Check check) {
    if (autopilot_screen == nullptr) {
        expect(false, "a checkpoint fired before any frame was shown");
        return;
    }
    switch (check) {
        case ap::Check::title: {
            int logo_x = -1;
            int logo_y = -1;
            unsigned char index = 0;
            for (int y = 0; y < grandleon_logo_height && logo_x < 0; ++y) {
                for (int x = 0; x < grandleon_logo_width && logo_x < 0;
                     ++x) {
                    index =
                        grandleon_logo_indices[y * grandleon_logo_width + x];
                    if (index != 0) {
                        logo_x = x;
                        logo_y = y;
                    }
                }
            }
            expect(logo_x >= 0, "the logo carries an opaque pixel");
            expect(
                logo_x >= 0 &&
                    surface_pixel(
                        autopilot_screen,
                        (320 - grandleon_logo_width * 2) / 2 + logo_x * 2,
                        28 + logo_y * 2
                    ) == static_cast<std::uint16_t>(
                             logo_colour(index) & 0xFFFFU
                         ),
                "the title screen shows the logo"
            );
            expect(
                surface_pixel(autopilot_screen, 4, 4) == pack16(16, 26, 27),
                "the title backdrop is the house colour"
            );
            // The game's own name, under the mark. Counted rather than
            // sampled: this is text on a flat colour, and a point assertion
            // would assert the font's shape rather than that the line is
            // there. The band is the one the drawing reserved, named once.
            expect(
                ink_in_band(0, 320, title_band_top, title_band_bottom) > 0,
                "the title screen names the game on the cartridge"
            );
            return;
        }
        case ap::Check::controls:
            expect(
                surface_pixel(autopilot_screen, 4, 4) == pack16(16, 26, 27),
                "the controls screen backdrop is shown"
            );
            return;
        case ap::Check::cutscene:
            expect(
                surface_pixel(autopilot_screen, 22, 42) ==
                    pack16(242, 193, 78),
                "the dialogue portrait frame is on screen"
            );
            // What the scene is set against, derived rather than remembered:
            // the row this samples is looked up in the same generated band
            // table the screen painted from, so a backdrop whose bands moved
            // fails here instead of being quietly redrawn. A scene that names
            // none is still the flat house colour it always was.
            expect(
                surface_pixel(autopilot_screen, 4, 4) ==
                    expected_scene_pixel(autopilot_scene_backdrop, 4),
                "the dialogue backdrop is what the scene names, and the house "
                "colour when it names nothing"
            );
            return;
#ifdef GRANDLEON_N64_CAMPAIGN
        // The line whose speaker the scene cast. Both drawings come out of the
        // same roster table the board draws from, at the archetype and colour
        // the host derived from the compiled package. So what is asserted is
        // that the screen holds the character the *package* names, and that it
        // is not the one the speaker's display name would have chosen.
        case ap::Check::cutscene_cast: {
            namespace want = grandleon::tarnholt;
            expect(
                portrait_shows(
                    portrait_from_roster(
                        want::opening_cast_archetype,
                        want::opening_cast_colour
                    ),
                    portrait_from_roster(
                        want::opening_uncast_archetype,
                        want::opening_uncast_colour
                    )
                ),
                "the portrait is the character the scene cast, and not the "
                "one the speaker's name spells"
            );
            return;
        }
        // The three campaign screens. What each of them is *about*, meaning the
        // roster, the kits, the store and the standing node, is asserted by the
        // narrator against the host-derived expectations at the moment the
        // session hands it over, which is the only moment those facts exist.
        // These assert the half only a frame can be wrong about: that the
        // screen the script is pressing into was actually drawn.
        //
        // A band rather than a pixel, deliberately. A glyph's top-left corner
        // is background in most of the font, so sampling one point asserts the
        // font's shape and not the screen's content; counting lit pixels across
        // the line a screen reserves for something asserts the something is
        // there.
        case ap::Check::campaign_slot:
            expect(
                surface_pixel(autopilot_screen, 4, 4) == pack16(16, 26, 27),
                "the slot screen backdrop is the house colour"
            );
            expect(
                ink_in_band(20, 300, 92, 122) > 40,
                "the slot screen says what the cartridge is holding"
            );
            expect(
                ink_in_band(20, 300, 144, 178) > 40,
                "and offers a row with the caret on it"
            );
            return;
        case ap::Check::campaign_roster:
            expect(
                surface_pixel(autopilot_screen, 4, 4) == pack16(16, 26, 27),
                "the company sheet backdrop is the house colour"
            );
            expect(
                ink_in_band(20, 300, 40, 132) > 200,
                "the company sheet lists the roster"
            );
            expect(
                ink_in_band(20, 300, 138, 182) > 40,
                "and names the store beneath it"
            );
            return;
        case ap::Check::campaign_management:
            expect(
                surface_pixel(autopilot_screen, 4, 4) == pack16(16, 26, 27),
                "the management stage backdrop is the house colour"
            );
            expect(
                ink_in_band(20, 300, 40, 132) > 200,
                "the management stage lists the company"
            );
            expect(
                ink_in_band(20, 300, 206, 232) > 40,
                "and names what the two buttons do"
            );
            return;
#endif
        default:
            if (autopilot_presenter != nullptr) {
                autopilot_presenter->autopilot_check(check);
            } else {
                expect(false, "a board checkpoint fired before any board");
            }
            return;
    }
}

// The scripted controller. Each poll consumes at most one scripted event, so
// the real input loops pace the script exactly as a thumb would.
joypad_buttons_t poll_buttons() {
    joypad_poll();
    const joypad_buttons_t none{};
    if (autopilot_hold > 0) {
        --autopilot_hold;
        return none;
    }
    while (!autopilot_done) {
        const ap::Step& step = autopilot_script[autopilot_index];
        switch (step.op) {
            case ap::Op::press:
                ++autopilot_index;
                return buttons_from(step.value);
            case ap::Op::wait:
                ++autopilot_index;
                if (step.value > 0) {
                    autopilot_hold = step.value - 1;
                    return none;
                }
                continue;
            case ap::Op::cursor_to: {
                if (autopilot_presenter == nullptr) {
                    ++autopilot_index;
                    continue;
                }
                const std::int16_t cx = autopilot_presenter->cursor_x();
                const std::int16_t cy = autopilot_presenter->cursor_y();
                if (cx == step.x && cy == step.y) {
                    autopilot_steer = 0;
                    ++autopilot_index;
                    continue;
                }
                // A budget, because a press is not always one cell. While a
                // pick that names a character is held the cursor steps between
                // the tiles the engine lit, so a script naming a tile the
                // gesture cannot reach would press for ever and the run would
                // die on the harness's clock with nothing said. It says so
                // instead, loudly, and goes on to the next step.
                if (++autopilot_steer > autopilot_steer_budget) {
                    autopilot_steer = 0;
                    expect(
                        false,
                        "the script steered the cursor somewhere it cannot go"
                    );
                    ++autopilot_index;
                    continue;
                }
                if (cx < step.x) return buttons_from(ap::button_d_right);
                if (cx > step.x) return buttons_from(ap::button_d_left);
                if (cy < step.y) return buttons_from(ap::button_d_down);
                return buttons_from(ap::button_d_up);
            }
            case ap::Op::checkpoint:
                // The settle rule, applied in the one place every checkpoint
                // goes through: the cursor's pulse is put back to its resting
                // phase, and the frame repainted if it was drawn emphasised,
                // before a single pixel is asserted. Nothing is switched off:
                // the pulse runs through the whole autopilot exactly as it
                // runs for a player, and the assertions simply sample it at
                // the documented phase. Everything else that animates has
                // finished by construction: an animation is a bounded run of
                // frames inside event handling and consumes no script step, so
                // the script cannot reach a checkpoint while a token is still
                // moving.
                if (autopilot_presenter != nullptr) {
                    autopilot_presenter->settle_pulse();
                }
                report_line("CHECKPOINT %s\n", step.name);
                autopilot_checkpoint(static_cast<ap::Check>(step.value));
                ++autopilot_index;
                // Hold the frame long enough for the harness to photograph.
                autopilot_hold = autopilot_checkpoint_hold;
                return none;
            case ap::Op::finish:
#ifndef GRANDLEON_N64_CAMPAIGN
                report_line(
                    "RESULT %s %d/%d\n",
                    failures == 0 ? "PASS" : "FAIL", checks - failures,
                    checks
                );
#endif
                // A campaign run's verdict is reported by `main`, after the
                // session has returned and the stack has been measured: the
                // last thing a kept campaign proves is what it left behind,
                // and that is not something a button press can be holding.
                autopilot_done = true;
                return none;
        }
    }
    return none;
}

#endif

}  // namespace

int main() {
#ifdef GRANDLEON_N64_CAMPAIGN
    // Before anything is allocated and before the deepest frame this ROM will
    // ever have exists, so the mark read back at the end is the whole run's.
    paint_the_stack();
#endif
    debug_init_emulog();
    display_init(
        RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE
    );
    rdpq_init();
#ifndef GRANDLEON_N64_PROBE
    joypad_init();
    grandleon::n64audio::init();
#endif

    // The embedded source project, parsed and compiled on the console. The
    // same JSON the editor loads and the host compiler compiles; there is no
    // console-specific content path to drift.
    //
    // Which project it is was decided at configure time by
    // `GRANDLEON_N64_PROJECT`, so a ROM of an author's own game reaches this
    // line by the same route the shipped game does and differs from it only in
    // the bytes.
    const auto parsed = gc::parse_source_project_json(
        std::string_view(
            reinterpret_cast<const char*>(project_source_json),
            project_source_json_size
        )
    );
    expect(static_cast<bool>(parsed), "embedded source project parses");
    const auto compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "source project compiles on target");

    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "package loads on target");

    // Which game this ROM is, in its own words, on every build.
    //
    // This is the line a check standing outside the machine reads to know that
    // a ROM built for one project is not quietly running another's. Nothing on
    // the host can make that statement: the host can compare bytes, but only a
    // boot can say which campaign the running program actually asked for.
    report_line(
        "game campaign=%s slot=%s source=%u\n",
        project_campaign_key,
        project_campaign_slot,
        static_cast<unsigned>(project_source_json_size)
    );
#if defined(GRANDLEON_N64_AUTOPILOT) && defined(GRANDLEON_N64_CAMPAIGN)
    // The scripted run is a run of the shipped game, and the expectations
    // header beside it says which game that is. So this build, and only this
    // build, can check the derivation against a second, independently written
    // statement of the same two facts. A derivation that started producing the
    // wrong campaign key would otherwise fail much later and much less clearly,
    // as a campaign that could not be found.
    expect(
        std::string_view(project_campaign_key) ==
            std::string_view(grandleon::tarnholt::campaign_key),
        "the derived campaign key is the shipped game's"
    );
    expect(
        std::string_view(project_campaign_slot) ==
            std::string_view(grandleon::tarnholt::campaign_slot),
        "the derived slot name is the shipped game's"
    );
    // And the name the title screen announces, on the same terms: the ROM
    // reads it off the project it embeds, and the header says which project
    // that must be.
    expect(
        parsed.source.title == grandleon::tarnholt::game_title,
        "the name the title screen announces is the shipped game's"
    );
#endif

#ifdef GRANDLEON_N64_CAMPAIGN
    // The cartridge, and the slot directory over it. Both are constructed here
    // rather than inside the session so that what the cartridge is holding can
    // be read *before* the campaign begins: the slot screen offers CONTINUE
    // only when there is something to continue. And for the persistence check,
    // which of the two scripts plays is decided by the cartridge and by
    // nothing the ROM carries.
    // `static` so the cartridge's 32 KiB shadow lives in .bss. A local would
    // put it in `main`'s own frame, which is both wasteful on a console and
    // would leave the stack watermark below measuring a smaller stack than the
    // ROM actually stands on.
    static grandleon::n64save::SramWindow cartridge;
    storage::ByteWindowSlotStorage device(
        cartridge, grandleon::n64save::cartridge_budget()
    );
    expect(device.available(), "the cartridge answers as a save device");
    // One question per slot, because the slot screen's CONTINUE is per row and
    // the row has to know. Slot one is `campaign_slot` unchanged, so a cartridge
    // written before this screen existed is offered under the first row rather
    // than orphaned by it. To the persistence check, `holds_a_campaign` still
    // means "is the first row a resume".
    static bool slot_holds[view::slot_menu_rows];
    // The slot the player took, kept where the readback below can name it. It
    // starts as slot one's name so that a run that never reached the screen (a
    // build that failed a check before it) still says something true.
    static SlotDecision decision;
    view::slot_name_at(
        project_campaign_slot, 0, decision.slot, sizeof decision.slot
    );
    for (int row = 0; row < view::slot_menu_rows; ++row) {
        char name[view::slot_menu_name_size];
        view::slot_name_at(project_campaign_slot, row, name, sizeof name);
        slot_holds[row] = device.contains(name);
    }
    const bool holds_a_campaign = slot_holds[0];
    report_line(
        "cartridge formatted=%d slots=%d holds=%d bytes=%u of %u\n",
        device.was_formatted() ? 1 : 0,
        static_cast<int>(device.slots().size()),
        holds_a_campaign ? 1 : 0,
        static_cast<unsigned>(storage::used_bytes(device)),
        static_cast<unsigned>(device.budget().maximum_total_bytes)
    );
#ifdef GRANDLEON_N64_AUTOPILOT
    autopilot_script = holds_a_campaign ? ap::campaign_autopilot_resume
                                        : ap::campaign_autopilot_found;
#endif
#endif

    if (failures == 0) {
#ifndef GRANDLEON_N64_PROBE
        // The name on the cartridge is the project's own `title`, which every
        // project carries because the schema requires it. Read off the parsed
        // source rather than baked into a generated header: the ROM already
        // holds the project it was built for, so a second statement of the
        // same string is a second thing to keep in step.
        title_screen(parsed.source.title);
        controls_screen();
#endif
        // What the package says its content looks like, asked once and handed
        // to the renderer. Everything the renderer draws that is not the
        // simulation's own state comes from here.
        //
        // Not checked separately, and deliberately: a refused section leaves
        // every terrain identity and every unit type unresolved, so the board
        // would draw as background under knights in side colours, and the
        // framebuffer checks below would fail on what was drawn rather than on
        // what was read. That is the stronger statement of the two.
        const auto shown = pr::load_presentation(loaded.package);
        N64Presenter presenter(shown.presentation, loaded.package);
#ifdef GRANDLEON_N64_AUTOPILOT
        autopilot_presenter = &presenter;
#endif
#ifdef GRANDLEON_N64_PROBE
        probe_the_spent_figure(loaded.package, presenter);
#endif
#ifdef GRANDLEON_N64_CAMPAIGN
        // The only question this ROM asks before the campaign begins, and the
        // only difference between this build and the play ROM: which of the
        // cartridge's saves is about to be played, and whether it is being
        // resumed or founded over. Everything after it is
        // `client::run_persistent_campaign`: the six steps, the management
        // stage, the saves. It is the same driver the terminal runs, deriving
        // every campaign fact for all four clients.
        // The same name the title screen announced, off the same parsed
        // source: the screen that asks which save to take says which game the
        // save is of, and it must be this cartridge's game rather than the one
        // the repository happens to ship.
        decision = campaign_slot_screen(
            project_campaign_slot, slot_holds, parsed.source.title
        );
        client::CampaignSessionOptions options;
        options.slot = decision.slot;
        options.resume = decision.resume;
        options.player_side = sim::Side::first;
        const auto status = client::run_persistent_campaign(
            loaded.package,
            core::stable_content_id_v1(project_campaign_key),
            presenter,
            presenter,
            device,
            options
        );
        expect(
            status == client::CampaignSessionError::none,
            "the kept campaign runs"
        );
#else
        const auto status = client::run_campaign(
            loaded.package,
            core::stable_content_id_v1(project_campaign_key),
            sim::Side::first,
            presenter
        );
        expect(
            status == client::SessionError::none,
            "the client session runs"
        );
#endif
    }

#ifdef GRANDLEON_N64_CAMPAIGN
    // Did the bytes actually reach the cartridge?
    //
    // Every `commit` on the window returns true, because a PI DMA has no
    // failure to report, so "the device took the save" is a weaker claim than
    // it sounds. This is the strong version that does not require switching the
    // machine off: throw the RDRAM shadow away, read the cartridge back over
    // the top of it, and build a second directory out of what came back. A
    // cartridge that never received the transfer answers with whatever it held
    // before, which is not an image, and the second directory reads as an empty
    // device.
    //
    // It is the half of the persistence claim a single boot can prove. The
    // other half, that it is still there when the emulator process is not, is
    // `platform/nintendo64/ares/run-ares-persistence.sh`.
    {
        const std::size_t written = storage::used_bytes(device);
        cartridge.reload();
        storage::ByteWindowSlotStorage after(
            cartridge, grandleon::n64save::cartridge_budget()
        );
        report_line(
            "cartridge readback formatted=%d slots=%d bytes=%u (wrote %u)\n",
            after.was_formatted() ? 1 : 0,
            static_cast<int>(after.slots().size()),
            static_cast<unsigned>(storage::used_bytes(after)),
            static_cast<unsigned>(written)
        );
        expect(
            after.was_formatted(),
            "the cartridge reads back as a device this build wrote"
        );
        // The slot the player took, which is `campaign_slot` for every scripted
        // run because the caret opens on the first row and every script presses
        // A on it.
        expect(
            after.contains(decision.slot),
            "and is holding the campaign slot after a round trip through the bus"
        );
        expect(
            storage::used_bytes(after) == written && written > 0,
            "and gave back exactly the bytes that were written to it"
        );
    }

    // The deepest the run ever got, read out of the pattern rather than
    // budgeted. Reported by every campaign build, and asserted by the checked
    // one: a ROM one struct field away from being plausibly wrong is the exact
    // failure this discipline exists to catch.
    {
        const std::size_t mark = stack_high_water();
        report_line(
            "STACK high water %u of %u painted bytes\n",
            static_cast<unsigned>(mark),
            static_cast<unsigned>(stack_probe_bytes)
        );
#ifdef GRANDLEON_N64_AUTOPILOT
        expect(
            mark > 0,
            "the stack watermark measured something"
        );
        expect(
            mark + 4096U < stack_probe_bytes,
            "and the run left a kilobyte of the painted window untouched"
        );
        report_line(
            "RESULT %s %d/%d\n",
            failures == 0 ? "PASS" : "FAIL", checks - failures, checks
        );
#endif
    }
#endif

#ifdef GRANDLEON_N64_PROBE
    if (failures != 0) {
        report_line("RESULT FAIL %d/%d\n", checks - failures, checks);
    }
    while (true) wait_ms(1000);
#else
    while (true) {
        grandleon::n64audio::pump();
        wait_ms(50);
    }
#endif
}
