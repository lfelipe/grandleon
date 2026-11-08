// SPDX-License-Identifier: MIT
// The PlayStation turn executable: a board, played.
//
// This is the console's one playable renderer. The cursor, the selection, the
// engine's reachable and
// threatened tiles, a move, a priced strike, the unit action menu, the full
// information sheet: everything a player expresses is
// `platform/client/src/turn_client.cpp`, the same translation unit the host
// derivation compiles. This file is the half of a client that a PlayStation is
// and a host is not: a GPU, a controller, a frame, and the pixel claims that
// make a picture checkable.
//
// ---------------------------------------------------------------------------
// What is this machine's, and what is not
//
// Almost none of it. The two functions the client requires are `paint` and
// `next_press`; three more with do-nothing defaults draw the animations. What
// is above that seam is written here and nowhere else:
//
//   * **A frame.** This board is steered and animated, so it waits for a
//     vertical retrace. `psx_runtime.h` explains what a retrace is on a
//     machine with no interrupt handler installed.
//
//   * **A font.** This console has none; `ui_font.h` is Grandleon's original
//     variable-width television typeface. This file expands its transparent,
//     shadowed texels once into a 4bpp sheet in the corner of VRAM that the
//     framebuffer and the CLUT band between them leave empty.
//
//   * **A controller.** SIO0, bit-banged, with its acknowledgement budget set
//     by measurement. `psx_pad.h` carries the numbers.
//
//   * **The interface's colours.** This is where a PlayStation is easiest:
//     every draw names its own CLUT and a flat rectangle names its colour
//     outright, so the five below are chosen for legibility and checked
//     against the backdrop rather than negotiated with the art.
//
// ---------------------------------------------------------------------------
// The window, and where its numbers come from
//
// Ten columns by six rows. 320 pixels over a 32-pixel cell is ten, and 240
// lines less the four eight-pixel rows of the message bar is 208, which holds
// six whole rows with sixteen lines over. Those sixteen become the headroom a
// raised cell lifts into rather than a seventh row.
//
// It matters more than a layout usually does, because the camera scrolls when
// the cursor reaches the edge of the window. A host derivation made against a
// different window would put the camera somewhere this executable never puts
// it, and every coordinate downstream would differ. The numbers are derived
// from this machine's own constants below and `static_assert`ed against the
// client's.
//
// ---------------------------------------------------------------------------
// The script, and which builds carry one
//
// A harness that presses buttons needs an emulator with pad ports. This one is
// headless and offers none, so the script is compiled into the executable and
// paced by counting frames, which is the Nintendo 64's arrangement.
// `platform/client/autopilot/fordlight_pad.h` carries the script, and the host
// derivation replays the same file.
//
// `GRANDLEON_PSX_AUTOPILOT` is what decides whether this build has one, and it
// is a build-time question rather than something detected at run time. An
// autopilot build plays the script and reports; a played build has no script
// linked into it at all and waits on the pad, so it stops on the title screen
// and on every screen after it, exactly as the cartridge does.
//
// Deciding it at build time rather than by asking the machine is the whole
// point. A run-time test — is a pad plugged in, is this an emulator — would
// have to be right on hardware nobody here can try, and would leave the played
// build carrying a script it must never reach. Two builds of one translation
// unit is the arrangement the Nintendo 64 already uses for the same reason,
// and it keeps the checked artifact and the played one the same code.

#include "grandleon/view/board_view.hpp"
#include "grandleon/view/console_ui.hpp"
#include "grandleon/view/landmark.hpp"

#include <grandleon/client/session.hpp>
#include <grandleon/client/console_localization.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/package_runtime/target_budget.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/view/motion.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "console_art.h"
#include "console_renderer.h"
#include "psx_gpu.h"
#include "psx_pad.h"
#include "psx_runtime.h"
#include "themes.h"
#include "ui_font.h"
#include "ui_layout.h"

#ifdef GRANDLEON_PSX_AUTOPILOT
#include "fordlight_pad.h"
#endif

// Which project's bytes this executable carries, and which game those bytes
// are. Tarnholt's board, unless a campaign build says otherwise, because a
// turn executable's subject is a board with two sides, real terrain and
// mountains on it, and the demo is a teaching board.
//
// Both headers are generated from a project by the build: the package by
// `grandleon_content_compile`, the identity by
// `cmake/GrandleonProjectIdentity.cmake`. The two identity headers declare the
// same two symbols, so exactly one of them can be here.
#ifdef GRANDLEON_PSX_DEMO_CAMPAIGN
#include "generated/demo_identity.h"
#include "generated/demo_package.h"
#else
#include "generated/board_identity.h"
#include "generated/board_package.h"
#endif

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
#include "psx_card.h"

#include <grandleon/storage/byte_window_storage.hpp>
#include <grandleon/storage/slot_storage.hpp>
#include <grandleon/view/slot_menu.hpp>

// A scene's backdrop is a run of flat horizontal bands in the master palette
// and not a picture, which is the whole reason this machine can afford one: a
// band is a `gpu::fill`, and this executable already draws sixty of those a
// frame for the board.
#include "backdrops.h"

#ifdef GRANDLEON_PSX_AUTOPILOT
#include "campaign_pad.h"
#endif
#endif

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace view = grandleon::view;
namespace client = grandleon::client;
namespace turn = grandleon::client::turn;
namespace ui_text = grandleon::client::console_localization;

namespace psx = grandleon::playstation;
namespace art = grandleon::playstation::art;
namespace console = grandleon::playstation::console_renderer;
namespace gpu = grandleon::playstation::gpu;
namespace pad = grandleon::playstation::pad;
namespace font = grandleon::playstation::ui_font;
namespace layout = grandleon::playstation::ui_layout;
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
namespace card = grandleon::playstation::card;
namespace storage = grandleon::storage;
#endif

namespace {

using psx::Line;

// The campaign this executable plays, by authored path, and the bytes it plays
// it out of.
//
// The path is `project_campaign_key` out of the identity header above rather
// than a name written here, because the two have to be the same project: an
// executable carrying one game's package and asking it for another game's
// campaign loads nothing, and there is no line of C++ in this file that could
// notice.
//
// Two projects, and one of them is only ever a campaign build: the turn
// executable's subject is the Fordlight Crossing, the board
// `fordlight_pad.h`'s script was written against. `campaign_pad.h` carries a
// script for each.
constexpr const char* campaign_path = project_campaign_key;
constexpr std::uint64_t ancient_grove_terrain =
    core::stable_content_id_v1("mountain_ancient_grove");
constexpr std::uint64_t village_house_terrain =
    core::stable_content_id_v1("mountain_village_house");
constexpr std::uint64_t castle_wall_terrain =
    core::stable_content_id_v1("mountain_castle_wall");
constexpr std::uint64_t castle_door_terrain =
    core::stable_content_id_v1("paved_castle_door");
constexpr std::uint64_t castle_floor_terrain =
    core::stable_content_id_v1("paved_castle_floor");
constexpr std::uint64_t castle_dais_terrain =
    core::stable_content_id_v1("paved_castle_dais");
#ifdef GRANDLEON_PSX_DEMO_CAMPAIGN
constexpr const std::uint8_t* campaign_package_bytes = demo_package_bytes;
constexpr std::size_t campaign_package_size = demo_package_size;
#else
constexpr const std::uint8_t* campaign_package_bytes = board_package_bytes;
constexpr std::size_t campaign_package_size = board_package_size;
#endif

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
// What the campaign is kept under on the card, out of the same generated
// header. A storage name: lower case, digits, `_` and `-`.
// `view::slot_name_at` makes rows two to four out of it.
//
// What the title screen calls the project is *not* here, and its absence is the
// point: it is read off the package this executable is holding, through
// `package_runtime::project_title`. A name compiled in beside the package would
// be one binary telling every author's disc it was somebody else's game, and it
// would also be the one string on this machine that cannot be checked against
// anything, because a title is free text an author wrote rather than an
// identifier the source contract constrains.
constexpr const char* campaign_slot_base = project_campaign_slot;
#endif

// The dark navy behind a fitted board. It remains distinct from every art and
// interface colour, which keeps an undrawn probe unambiguous without putting
// diagnostic-magenta bars around a real game on a television.
constexpr std::uint16_t backdrop = 0x10C4;

// The interface's five colours, as fifteen-bit words.
//
// Chosen rather than measured: there is no palette to fit into, so what
// decides these is legibility and the one hard rule. None of them may be the
// backdrop, or a pixel nobody drew would be indistinguishable from a pixel the
// interface drew. `main` checks that rather than trusting this comment.
constexpr std::uint16_t ink_colour = 0x7FFF;      // white
constexpr std::uint16_t paper_colour = 0x0842;    // near-black, and opaque
constexpr std::uint16_t cursor_colour = 0x7FFF;   // white
constexpr std::uint16_t second_cursor_colour = 0x7FE0;  // cyan
constexpr std::uint16_t move_colour = 0x7E86;     // cold blue
// Where the pick in the player's hand can land, once a row has been taken out
// of the menu. Amber because it is the one hue neither the blue above nor the
// red below occupies. It is also the brightest of the three, so the lit wash
// and the warning separate by weight as well as by colour for a player who
// cannot tell the last two apart. Distinct from `flash_colour`,
// which is also warm, so a pixel claim can never confuse a lit tile with a
// blow.
constexpr std::uint16_t aim_colour = 0x035F;      // gold
constexpr std::uint16_t danger_colour = 0x18DF;   // red
// The frame around somebody who has already taken their turn: a flat grey, and
// deliberately neither of the two above, because a spent character is not a
// place you may walk to and not a place you may be struck from.
constexpr std::uint16_t spent_colour = 0x39CE;    // grey
constexpr std::uint16_t flash_colour = 0x131F;    // amber, for a blow
// The front-end palette mirrors the N64 menu: warm gold for the game and the
// active row, quiet green-grey for secondary copy, and the existing near-black
// paper as the full-screen ground. Unlike the old diagnostic magenta border,
// every one of these is meant to be seen.
constexpr std::uint16_t menu_gold_colour = gpu::rgb15(30, 24, 9);
constexpr std::uint16_t menu_muted_colour = gpu::rgb15(15, 17, 16);

[[nodiscard]] constexpr std::uint16_t console_ui_colour(
    view::UiTone tone
) noexcept {
    switch (tone) {
        case view::UiTone::shadow: return gpu::rgb15(2, 3, 4);
        case view::UiTone::frame:
        case view::UiTone::accent: return menu_gold_colour;
        case view::UiTone::header: return gpu::rgb15(6, 10, 10);
        case view::UiTone::focus: return gpu::rgb15(9, 13, 12);
        case view::UiTone::meter_track: return gpu::rgb15(2, 4, 5);
        case view::UiTone::meter_fill: return gpu::rgb15(12, 25, 12);
        case view::UiTone::surface: return paper_colour;
    }
    return paper_colour;
}

void paint_console_ui(view::UiRect rectangle, view::UiTone tone) {
    if (!rectangle.valid()) return;
    gpu::fill(
        rectangle.x, rectangle.y, rectangle.width, rectangle.height,
        console_ui_colour(tone)
    );
}

// The two host callback slots the harness installs itself on, and the only
// numbers this file and `harness/playstation_turn_probe.lua` have to agree
// about. Neither is the render executables' 13, on purpose: a harness written
// for one must not silently photograph the other.
//
//   14  a settled checkpoint: photograph what is on screen now
//   15  the run is over: write your verdict
constexpr int harness_slot = 14;
// A campaign screen is photographed on a slot of its own. The observer holds a
// board and a page to different claims: the strongest thing that can be said
// about one is false of the other. This slot is what tells it which it is
// looking at. `platform/playstation/harness/playstation_turn_probe.lua` says
// why at greater length.
constexpr int harness_screen_slot = 12;
constexpr int harness_done_slot = 15;

// The board's cell at its native size, and the screen's own numbers.
//
// `native_tile` is how many texels the art is drawn with, and so the largest
// cell worth putting on screen. It is no longer *the* cell: a board is fitted
// to the frame by `turn::board_fit` and may be drawn at anything between the
// smallest readable cell and this one. What it still fixes is the window a
// board gets when no shrinking is needed, which is the window every
// expectation was derived against before boards were fitted.
constexpr int native_tile = art::cell_size;
constexpr int glyph = gpu::glyph_texels;
constexpr int screen_cols = gpu::screen_width / glyph;
constexpr int screen_rows = gpu::screen_height / glyph;
constexpr int bar_rows = 4;
constexpr int bar_top = gpu::screen_height - bar_rows * glyph;
constexpr int view_cols = gpu::screen_width / native_tile;
constexpr int view_rows = bar_top / native_tile;

static_assert(
    view_cols == turn::viewport_cols && view_rows == turn::viewport_rows,
    "the window the bar left is not the window the expectations assume"
);
// The fit rule the shared client is handed has to describe this screen, or the
// board would be fitted to a frame this executable does not have.
static_assert(
    turn::board_fit.frame_w == gpu::screen_width &&
        turn::board_fit.frame_h == bar_top,
    "the fit rule's frame is not the frame the bar leaves"
);
static_assert(
    turn::board_fit.largest_tile == native_tile,
    "the fit rule would draw a cell larger than the art is"
);
static_assert(
    screen_cols == grandleon::sheet::unit_sheet_columns,
    "a sheet line is not this display's width"
);

// Which rows of the bar say what, and where the two screens that cover the
// board put themselves.
constexpr int row_hovered = screen_rows - 4;
constexpr int row_message = screen_rows - 3;
constexpr int row_aiming = screen_rows - 2;
constexpr int row_hint = screen_rows - 1;
constexpr int sheet_footer_row = screen_rows - 2;

#ifdef GRANDLEON_PSX_AUTOPILOT
// How many frames a scripted press waits before it is made. Twenty is two
// thirds of the cursor's own pulse period, so a person watching the executable
// play itself sees the pulse rather than a cursor that only ever steps.
//
// A campaign is five times as many presses as a turn, and most of them are on a
// screen where there is no pulse to see: twenty frames each would be half a
// minute of emulated time spent waiting for nothing, and several minutes of a
// gate's. Six is still a beat rather than a jump: a fifth of a second, about
// as fast as a thumb goes, and what the pacing is worth here.
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
constexpr int script_dwell_frames = 6;
#else
constexpr int script_dwell_frames = 20;
#endif
#endif

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
// ---------------------------------------------------------------------------
// Where a campaign screen goes on this display
//
// The shared client composes a screen as a page of text, thirty-eight columns
// by sixteen rows with a caret beside one of them and a footer, and says in
// writing that placing it is the platform's whole job. This is that placement,
// and it is deliberately arithmetic and not judgement: two consoles that put
// the same page in different places are still drawing the same screen.
//
// Column one is the caret's and a page row starts at column two, so the page's
// thirty-eight columns still end exactly at column forty. Column zero stays
// clear for the backdrop probe and the shared page frame; a page over a scene
// remains legible rather than tangled with it.
// ---------------------------------------------------------------------------
constexpr int screen_caret_column = 1;
constexpr int screen_page_left = 2;
constexpr int screen_page_top = 5;
constexpr int screen_panel_top = screen_page_top - 1;
constexpr int screen_panel_rows = turn::page_capacity + 2;
constexpr int screen_footer_row = screen_rows - 2;
static_assert(
    screen_page_left + turn::page_columns <= screen_cols,
    "a page row and the caret beside it have to fit the display"
);
static_assert(
    screen_panel_top + screen_panel_rows < screen_footer_row,
    "the page's panel and the footer cannot share a row"
);
static_assert(
    turn::footer_columns <= screen_cols,
    "a centred footer has to fit the display. `write_at` stops at the edge, "
    "so one that does not is cut in the middle of its last word rather than "
    "reported, and the last word is where a footer puts a button"
);

// The speaker's face, in the band the panel leaves above itself.
//
// It is drawn at the size the art is drawn at rather than scaled to whatever
// room is left: the character standing on a board is thirty-two texels and this
// is that same drawing, so a face scaled to fit would be the same person drawn
// at two sizes on one machine. The band above the panel is exactly thirty-two
// pixels, which is why the page starts on the row it does; the `static_assert`
// below is what stops that being a coincidence somebody moves.
//
// Left-aligned with the page's own text, so the face sits over the name that
// names it. Column zero remains untouched for the backdrop claim.
constexpr int screen_face_size = 32;
constexpr int screen_face_left = screen_page_left * glyph;
constexpr int screen_face_top = screen_panel_top * glyph - screen_face_size;
static_assert(
    screen_face_top >= 0,
    "the speaker's face has to fit in the band above the page's panel"
);
static_assert(
    screen_face_left > 0,
    "the face cannot cover column zero, where the backdrop is claimed"
);

// A backdrop row scaled to this display's height, by the same arithmetic the
// Nintendo 64 scales it by, so the band a probe looks at and the band the
// screen paints are one calculation rather than two copies of one.
[[nodiscard]] constexpr int backdrop_row_top(int row) noexcept {
    return (row * gpu::screen_height + grandleon_backdrop_rows / 2) /
           grandleon_backdrop_rows;
}

// Whether a scene names a backdrop the art library can draw. Zero, and anything
// past the end of the menu it published, is a scene that names none.
[[nodiscard]] constexpr bool scene_has_backdrop(std::uint8_t which) noexcept {
    return which != 0 &&
           which <= static_cast<std::uint8_t>(grandleon_backdrop_count);
}

// The master palette is eight bits a channel and this display is five, so a
// band's colour is the library's own value shifted rather than a second table
// of colours somebody has to keep in step with the first.
[[nodiscard]] constexpr std::uint16_t band_colour(
    unsigned char red, unsigned char green, unsigned char blue
) noexcept {
    return gpu::rgb15(
        static_cast<int>(red) >> 3, static_cast<int>(green) >> 3,
        static_cast<int>(blue) >> 3
    );
}
#endif

// The console's deterministic interior-variant choice, exactly as the render
// executable makes it and for the same reason.
// Which of the two meanings the lit wash carries this frame: blue where the
// player is choosing where to put a character down, amber where a pick is
// waiting for a tile. `Overlay::aiming` is non-null exactly while one is held,
// and the tile list under `Overlay::moves` is the pick's rather than the
// character's for exactly as long. This reads the one flag both facts hang off
// rather than deciding anything of its own.
[[nodiscard]] std::uint16_t lit_colour(const turn::Overlay& overlay) noexcept {
    return overlay.aiming != nullptr ? aim_colour : move_colour;
}

[[nodiscard]] int elevation_of_kind(int kind) noexcept {
    if (kind < 0 || kind >= grandleon_terrain_kind_count) return 0;
    return grandleon_terrain_elevation[kind];
}

[[nodiscard]] constexpr std::uint16_t terrain_clut_colour(
    std::uint16_t source, int kind
) noexcept {
    if (source == 0U) return 0U;
    const unsigned red = source & 0x1FU;
    const unsigned green = (source >> 5U) & 0x1FU;
    const unsigned blue = (source >> 10U) & 0x1FU;
    const std::uint8_t luminance = static_cast<std::uint8_t>(
        (red * 2U + green * 3U + blue) / 6U
    );
    const view::TerrainColour target = view::terrain_base_colour(
        kind >= 0 ? static_cast<std::uint8_t>(kind) : 9U
    );
    return gpu::rgb15(
        view::console_terrain_tint_level(luminance, target.red),
        view::console_terrain_tint_level(luminance, target.green),
        view::console_terrain_tint_level(luminance, target.blue)
    );
}


int checks = 0;
int failures = 0;
int probes = 0;

#ifdef GRANDLEON_PSX_AUTOPILOT
// The script this run plays, and how long it is.
//
// A turn executable has one script and knows it at compile time. A campaign
// executable has two: founding and resuming. Which one plays is decided by
// what the memory card is holding, in `main`, *after* the card has been
// asked. It must not be a flag: a card that forgot would take the founding
// script a second time and the harness would find out on a screen rather than
// on the save.
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
#ifdef GRANDLEON_PSX_DEMO_CAMPAIGN
const std::uint16_t* script_presses = turn::demo_campaign_found;
std::size_t script_press_count = turn::demo_campaign_found_count;
const std::uint16_t* const resume_presses = turn::demo_campaign_resume;
const std::size_t resume_press_count = turn::demo_campaign_resume_count;
#else
const std::uint16_t* script_presses = turn::tarnholt_campaign_found;
std::size_t script_press_count = turn::tarnholt_campaign_found_count;
const std::uint16_t* const resume_presses = turn::tarnholt_campaign_resume;
const std::size_t resume_press_count = turn::tarnholt_campaign_resume_count;
#endif
#else
const std::uint16_t* const script_presses = turn::fordlight_presses;
const std::size_t script_press_count = turn::fordlight_press_count;
#endif
#endif

void expect(bool condition, const char* name) {
    ++checks;
    if (!condition) ++failures;
    Line().text("CHECK ").text(name).text(condition ? " PASS" : " FAIL").flush();
}

[[noreturn]] void give_up() {
    Line().text("RESULT FAIL 0/1").flush();
    *reinterpret_cast<volatile std::int16_t*>(0x1f802082) = 1;
    for (;;) {
    }
}

// ---------------------------------------------------------------------------
// VRAM residency
//
// Everything the board can ever draw is uploaded before the first press, which
// is not an economy but a property: a repaint costs primitives and no
// transfers, so the cost of a frame does not depend on what is on it.
// ---------------------------------------------------------------------------

int terrain_cell_of[art::terrain_kind_count][art::variant_count];
int terrain_clut_of[art::terrain_kind_count];
int font_clut = -1;
int font_gold_clut = -1;
int font_muted_clut = -1;

int cells_used = 0;
int cluts_used = 0;
bool cells_fit = true;
bool cluts_fit = true;

// The font, expanded. Four kilobytes of `.bss`, built once and uploaded once;
// the compact two-tone recipe stays in `.rodata` in `ui_font.h`.
std::uint16_t font_sheet[gpu::font_lines][gpu::font_halfwords_across];

[[nodiscard]] int claim_cell(const unsigned short* texels) {
    if (cells_used >= gpu::cell_capacity) {
        cells_fit = false;
        return -1;
    }
    const int cell = cells_used++;
    gpu::upload(
        gpu::cell_texture_x(cell), gpu::cell_texture_y(cell),
        art::halfwords_per_row, art::cell_size, texels
    );
    return cell;
}

[[nodiscard]] int claim_clut(const unsigned short* entries) {
    if (cluts_used >= gpu::clut_capacity) {
        cluts_fit = false;
        return -1;
    }
    const int clut = cluts_used++;
    gpu::upload(
        gpu::clut_x_of(clut), gpu::clut_y_of(clut), art::clut_size, 1, entries
    );
    return clut;
}

// Four texels to a halfword, the leftmost in the *low* nibble. That is the one
// thing this format reverses against every other 4bpp layout in this
// repository, so the packing order is stated explicitly here.
void build_font() {
    for (auto& row : font_sheet) {
        for (std::uint16_t& word : row) word = 0;
    }
    for (int slot = 0; slot < font::glyph_count && slot < gpu::font_capacity;
         ++slot) {
        const int base_line = gpu::glyph_texture_v(slot);
        const int base_word = gpu::glyph_texture_u(slot) / art::texels_per_halfword;
        for (int row = 0; row < gpu::glyph_texels; ++row) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                const std::uint8_t texel = font::texel(slot, x, row);
                if (texel == 0U) continue;
                const int word = base_word + x / art::texels_per_halfword;
                const int shift = 4 * (x % art::texels_per_halfword);
                font_sheet[base_line + row][word] = static_cast<std::uint16_t>(
                    font_sheet[base_line + row][word] |
                    static_cast<std::uint16_t>(texel << shift)
                );
            }
        }
    }
    gpu::upload(
        gpu::font_x, gpu::font_y, gpu::font_halfwords_across, gpu::font_lines,
        &font_sheet[0][0]
    );
}

// The font's CLUT: index 0 is transparent, index 1 its dark offset and index 2
// its face. Every surface
// that writes text paints its panel, page, bar or chip first, so the glyph must
// preserve that surface between its strokes. Making index 0 paper-coloured
// stamped an opaque 8x8 black tile around every letter; over a header or focus
// row those little tiles made the text appear to alternate backgrounds. The
// Nintendo 64 preserves the row beneath its glyphs, and this does the same.
[[nodiscard]] int claim_font_clut(std::uint16_t ink) {
    unsigned short entries[art::clut_size]{};
    entries[1] = console_ui_colour(view::UiTone::shadow);
    entries[2] = ink;
    return claim_clut(entries);
}

void build_font_clut() {
    font_clut = claim_font_clut(ink_colour);
    font_gold_clut = claim_font_clut(menu_gold_colour);
    font_muted_clut = claim_font_clut(menu_muted_colour);
}

// ---------------------------------------------------------------------------
// The report
//
// A probe line is a claim about one pixel: where it is on the 320x240 display
// and what colour this executable believes it drew there, computed from
// `grandleon::view`, from the art library and from the interface's own
// constants, with nothing the GPU has touched. A readback line answers a
// different question: what the GPU actually stored at that address. The joiner
// requires them, and the emulator's own frame, to agree.
//
// Each claim belongs to a settled checkpoint of the played session.
// ---------------------------------------------------------------------------

void report_colour(Line& line, std::uint16_t colour) {
    line.text(" ")
        .decimal(static_cast<std::uint32_t>(gpu::red_of(colour)))
        .text(" ")
        .decimal(static_cast<std::uint32_t>(gpu::green_of(colour)))
        .text(" ")
        .decimal(static_cast<std::uint32_t>(gpu::blue_of(colour)));
}

void probe(const char* label, int x, int y, std::uint16_t claimed) {
    ++probes;
    Line claim;
    claim.text("PROBE ")
        .text(label)
        .text(" ")
        .decimal(static_cast<std::uint32_t>(x))
        .text(" ")
        .decimal(static_cast<std::uint32_t>(y));
    report_colour(claim, claimed);
    claim.flush();

    Line seen;
    seen.text("READBACK ").text(label);
    report_colour(seen, gpu::read_back(x, y));
    seen.flush();
}

// Which texel of a cell a screen pixel shows, when the cell is drawn `drawn`
// pixels across.
//
// This direction and not its inverse, and that is the whole of a mistake worth
// recording. Asking "where does texel T land" has no answer when a cell is
// shrunk: 32 texels over 26 pixels means six texels are drawn nowhere at all,
// so an inverse either invents a pixel for them or quietly claims a
// neighbour's. Asking "what does pixel P show" is always answerable, and it is
// also the question the probe is really making a claim about.
//
// At the native size a cell is drawn by a textured rectangle, which samples one
// texel per pixel, so the answer is the pixel itself and every claim this
// executable made before boards were fitted is unmoved.
[[nodiscard]] constexpr int texel_under(
    int screen, int drawn, int source_side
) noexcept {
    if (drawn <= 0 || source_side <= 0) return 0;
    // The quad carries `u` at its near edge and `u + source_side - 1` at its
    // far one, and the hardware samples at a pixel's *centre*, half a pixel in.
    // Both halves of that matter and each was got wrong once here: dropping the
    // half-pixel reads a texel early over most of the cell, and spanning 32
    // texels rather than 31 reads past the cell's own last column.
    return ((2 * screen + 1) * (source_side - 1)) / (2 * drawn);
}

// Whether every texel touching this one carries the same colour, so that a
// claim about it stays true whichever of them the hardware's interpolation
// actually reaches. One texel of margin, because the smallest cell the fit rule
// allows is half the art and a pixel therefore spans two texels.
[[nodiscard]] bool neighbours_agree(
    const art::Asset& asset, int tx, int ty
) noexcept {
    const std::uint16_t here = art::colour_at(asset, tx, ty);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = tx + dx;
            const int y = ty + dy;
            if (x < 0 || y < 0 || x >= asset.side || y >= asset.side) {
                return false;
            }
            if (art::colour_at(asset, x, y) != here) return false;
        }
    }
    return true;
}

// The opaque texel of a cell nearest its centre, searched outward in rings and
// confined to the middle half so the texel it finds can never be one the
// cursor's brackets or a wash's frame is drawn over.
//
// On a shrunk cell it wants more than an opaque texel: it wants one whose
// neighbours are the same colour. The reason is that a probe claims an exact
// pixel, and on a quad which texel a pixel samples is decided by the hardware's
// own interpolation rounding rather than by arithmetic this file can reproduce
// exactly. A texel sitting in a patch of one colour is claimed correctly
// whichever of its neighbours the GPU actually reaches, so the claim stays
// exact about the picture while ceasing to be a claim about the rounding. One
// texel of margin is enough: the smallest cell this rule allows is half the
// art, so a pixel spans two texels and the sampling can be off by one.
//
// At the native size no margin is asked for, so the texel found is the texel
// that was always found.
[[nodiscard]] bool opaque_near_centre(
    const art::Asset& asset, int drawn, bool mirrored, int& out_sx, int& out_sy,
    int& out_tx, int& out_ty
) {
    const int centre = drawn / 2;
    const int inset = drawn / 4;
    for (int radius = 0; radius < inset; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0 && dx > -radius && dx < radius && dy > -radius &&
                    dy < radius) {
                    continue;
                }
                const int sx = centre + dx;
                const int sy = centre + dy;
                if (sx < inset || sy < inset || sx >= drawn - inset ||
                    sy >= drawn - inset) {
                    continue;
                }
                // Never the quad's own diagonal. A shrunk cell is two
                // triangles meeting along `sx == sy`, and a pixel on that seam
                // may be rasterised by either of them; which one decides what
                // it samples, to a texel or two. Every other pixel is inside
                // one triangle and is not a question. The native cell is a
                // rectangle with no seam, so nothing is skipped there.
                if (drawn < native_tile && sx - sy <= 1 && sy - sx <= 1) {
                    continue;
                }
                const int sampled_tx = texel_under(sx, drawn, asset.side);
                const int tx = mirrored ? asset.side - 1 - sampled_tx
                                        : sampled_tx;
                const int ty = texel_under(sy, drawn, asset.side);
                const std::uint16_t here = art::colour_at(asset, tx, ty);
                if (here == 0) continue;
                // The margin the note above this function describes, which
                // until a board scrolled was described and not taken: on a
                // shrunk cell a pixel spans more than one texel and which of
                // them the hardware reaches is its own rounding, so a texel is
                // only claimable when its neighbours carry the same colour. At
                // the native size a pixel is a texel and nothing is asked.
                if (drawn < asset.side && !neighbours_agree(asset, tx, ty)) {
                    continue;
                }
                out_sx = sx;
                out_sy = sy;
                out_tx = tx;
                out_ty = ty;
                return true;
            }
        }
    }
    return false;
}

class TeletypeSink final : public turn::ReportSink {
public:
    void line(const char* value) override { psx::print(value); }
};

// The box the action menu occupies, in exact pixels. Painting, text bounds and
// framebuffer exclusions all read this one geometry; a panel cannot reserve a
// 16-pixel header while its text mistakenly advances by two unrelated cells.
struct MenuBox final {
    view::ConsolePanel panel{};
    int heading_x{0};
    int heading_y{0};
    int row_text_x{0};
    int row_text_y{0};
    int footer_text_x{0};
    int footer_text_y{0};

    [[nodiscard]] bool valid() const noexcept { return panel.outer.valid(); }

    [[nodiscard]] bool covers(
        int x, int y, int width, int height
    ) const noexcept {
        return layout::overlaps(panel.outer, {x, y, width, height});
    }

    // Whether one *pixel* is under the box, rather than one cell. A cell is
    // asked about before it is claimed at its corner; a character's token is
    // claimed at whatever texel of it is nearest the middle, and on a board
    // small enough for the menu to stand over a character that texel can be
    // one the menu is painted on.
    [[nodiscard]] bool covers_pixel(int x, int y) const noexcept {
        return panel.outer.contains(x, y);
    }
};

// The fit rule's largest possible camera window, reported at boot so the
// executable states the board range it can show. These no longer size a
// rectangular draw list: the production painter walks isometric diagonals
// directly.
constexpr int widest_view =
    turn::board_fit.frame_w / turn::board_fit.smallest_tile;
constexpr int tallest_view =
    turn::board_fit.frame_h / turn::board_fit.smallest_tile;

// ---------------------------------------------------------------------------
// The console's half of the client
// ---------------------------------------------------------------------------
class PlayStationClient final : public turn::TurnClient {
public:
    PlayStationClient(turn::ReportSink& sink, const pr::Presentation& shown) noexcept
        : TurnClient(sink),
          shown_(&shown),
          theme_(shown.theme < grandleon_theme_count ? shown.theme : 0) {}

    void battle_route(
        std::uint64_t stage_id,
        std::uint64_t encounter_id,
        std::uint64_t map_id
    ) override {
        static_cast<void>(stage_id);
        static_cast<void>(encounter_id);
        static_cast<void>(map_id);
    }

    // ----- what the platform supplies -------------------------------------

    void paint(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) override {
        Line()
            .text("PAINT frame ")
            .decimal(psx::vblank_waits())
            .text(" heap ")
            .decimal(psx::heap_census().free_bytes)
            .flush();
        held_ = &snapshot;
        last_overlay_ = &overlay;
        draw(snapshot, overlay, false);
    }

    // Blocks until something is asked for, and answers with every button that
    // went down.
    //
    // This loop is the board's only free-running clock, and so it is where the
    // cursor pulses. The phase is counted from zero every time the client comes
    // back here and `draw` puts the cursor back at rest before returning. A
    // checkpoint always follows a paint and never follows this, so it sees the
    // board exactly as it was drawn. That is the settle rule every client
    // holds: every animation ends exactly at rest, and phase zero is rest.
    // Nothing is switched off to get it, so a player waiting here sees the
    // pulse, and so does the script.
    std::uint16_t next_press() override {
        return next_controller_press().buttons;
    }

    [[nodiscard]] bool offers_fps_display() const noexcept override {
        return true;
    }

    [[nodiscard]] grandleon::sheet::TextEncoding text_encoding()
        const noexcept override {
        return grandleon::sheet::TextEncoding::latin_utf8;
    }

    [[nodiscard]] bool fps_display_visible() const noexcept override {
        return display_options_.show_fps;
    }

    void set_fps_display_visible(bool visible) noexcept override {
        display_options_.show_fps = visible;
        fps_window_started_ = 0;
        fps_frames_ = 0;
        fps_value_ = 0;
    }

    turn::ControllerPress next_controller_press() override {
        std::uint32_t frame = 0;
        bool emphasised = false;
        // Which direction is being leaned on, and for how many frames. This
        // state deliberately survives the returned repeat: resetting it on
        // every one-cell answer made every repeated step pay the full initial
        // delay again, so holding across a map was barely faster than tapping.
        // A changed or released direction resets below, and non-directional
        // buttons never repeat.
        constexpr pad::Port ports[2] = {pad::Port::one, pad::Port::two};
        for (;;) {
            psx::wait_vblank();
            for (std::uint8_t player = 0; player < 2; ++player) {
                pending_buttons_[player] = static_cast<std::uint16_t>(
                    pending_buttons_[player] | pad::pressed(ports[player])
                );
            }
            int ready = -1;
            if (pending_buttons_[pad_priority_] != 0) {
                ready = pad_priority_;
            } else if (pending_buttons_[1U - pad_priority_] != 0) {
                ready = 1U - pad_priority_;
            }
            if (ready >= 0) {
#ifdef GRANDLEON_PSX_AUTOPILOT
                // Somebody has picked up a controller during a scripted run.
                // From here the script is over and the board is theirs; a run
                // under the harness never reaches this, because a headless
                // emulator has no pad to press.
                steered_ = true;
#endif
                if (emphasised) draw_held(false);
                const std::uint16_t buttons = pending_buttons_[ready];
                pending_buttons_[ready] = 0;
                pad_priority_ = static_cast<std::uint8_t>(1 - ready);
                return {
                    static_cast<std::uint8_t>(ready), buttons
                };
            }
#ifdef GRANDLEON_PSX_AUTOPILOT
            if (!steered_ &&
                frame >= static_cast<std::uint32_t>(script_dwell_frames)) {
                if (emphasised) draw_held(false);
                if (consumed_ >= script_press_count) {
                    return {0, turn::pad_end_of_script};
                }
                return {0, script_presses[consumed_++]};
            }
#endif
            // A direction still held keeps the cursor moving, so crossing a
            // board is one lean rather than twenty presses. Directions only:
            // a repeating A would confirm things nobody chose, and a repeating
            // START would open and close a menu as fast as it could draw it.
            //
            // Counted here rather than in the client because the client is
            // handed a list of presses by a script under the harness, and a
            // repeat that reached it would change what that list means.
            for (std::uint8_t player = 0; player < 2; ++player) {
                const std::uint16_t held = static_cast<std::uint16_t>(
                    pad::read(ports[player]) &
                    (turn::pad_up | turn::pad_down | turn::pad_left |
                     turn::pad_right)
                );
                if (held_repeats_[player].update(held)) {
                    pending_buttons_[player] = static_cast<std::uint16_t>(
                        pending_buttons_[player] | held
                    );
                }
            }

            // A played build falls through to here and waits, which is the
            // whole of the difference: no press is invented, so a screen that
            // asks a question keeps asking it until somebody answers.
            ++frame;
            const bool now = view::cursor_emphasised(frame);
            if (now == emphasised && !display_options_.show_fps) continue;
            emphasised = now;
            draw_held(emphasised);
        }
    }

    // A whole frame between the facts and the photograph, so the rasteriser has
    // drained and the observer's capture is of a finished picture. It costs
    // sixteen milliseconds of emulated time per checkpoint and buys the one
    // thing a single-buffered machine cannot otherwise promise.
    void hold_for_checkpoint() override { psx::wait_vblank(); }

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // ----- a campaign screen -----------------------------------------------

    // A page of text, placed. Everything about *what* is on it was decided by
    // `platform/client/src/turn_client.cpp`, which is the same translation unit
    // the host compiles to derive this run's expectations. A screen this
    // console draws differently from the host is a placement bug and can be
    // nothing else.
    void paint_screen(const turn::ScreenView& view) override {
        const std::uint32_t opened = psx::clock_ticks();
        draw_screen(view);
        const std::uint32_t microseconds =
            (psx::clock_ticks() - opened) * 1000u /
            psx::ticks_per_1000_microseconds;
        ++paints_;
        paint_microseconds_ += microseconds;
        if (microseconds < paint_shortest_) paint_shortest_ = microseconds;
        if (microseconds > paint_longest_) paint_longest_ = microseconds;
        Line()
            .text("SCREEN ")
            .text(turn::screen_name(view.screen))
            .text(" rows ")
            .decimal(
                static_cast<std::uint32_t>(view.page == nullptr ? 0 : view.page->count)
            )
            .text(" caret ")
            .signed_decimal(view.caret_row)
            .text(" backdrop ")
            .decimal(static_cast<std::uint32_t>(view.backdrop))
            .text(" us ")
            .decimal(microseconds)
            .flush();
    }

    // What this console claims it drew on a screen, joined against the frame
    // the observer photographs at the same instant. The same idiom the board's
    // `after_facts` uses and for the same reason: a page reported over the
    // teletype is a page the executable *composed*, and the only thing that
    // says it reached the display is a pixel.
    void after_screen(const turn::ScreenView& view) override {
        const int before = probes;

        if (view.screen == turn::Screen::title) {
            probe("titlebg", 0, 0, paper_colour);
            probe("titlebar", 24, 18, menu_gold_colour);
            if (view.page != nullptr && view.page->count > 2) {
                const char* title = view.page->line(2);
                claim_first_glyph_at_pixel(
                    "gametitle", centred_x(title, 8, 304), 11 * glyph, title,
                    menu_gold_colour
                );
            }
            char footer[128]{};
            control_text(view.footer, footer, sizeof footer);
            if (view.choices > 0 && view.page != nullptr &&
                view.caret_row >= view.first_choice_row &&
                view.caret_row < view.page->count) {
                const int choice = view.caret_row - view.first_choice_row;
                const int y = 122 + choice * 24;
                probe("launchchoice", 58, y, menu_gold_colour);
                const char* choice_text = view.page->line(view.caret_row);
                claim_first_glyph_at_pixel(
                    "launchtext", centred_x(choice_text, 66, 188), y + 5,
                    choice_text,
                    menu_gold_colour
                );
            }
            claim_first_control_glyph(
                "titlefooter", view.choices <= 0 ? 26 : 27, footer
            );
            close_probes(before, harness_screen_slot);
            return;
        }

        if (view.screen == turn::Screen::help) {
            probe("helpbg", 0, 0, paper_colour);
            probe("helpbar", 24, 14, menu_gold_colour);
            probe("helppanel", 28, 54, menu_muted_colour);
            char footer[128]{};
            control_text(view.footer, footer, sizeof footer);
            claim_first_control_glyph("helpfooter", 27, footer);
            close_probes(before, harness_screen_slot);
            return;
        }

        if (view.screen == turn::Screen::slot) {
            probe("slotbg", 0, 0, paper_colour);
            probe("slotbar", 24, 14, menu_gold_colour);
            const int choice = view.caret_row - view.first_choice_row;
            if (view.page != nullptr && choice >= 0 &&
                choice < view.choices && view.caret_row < view.page->count) {
                const int row = 14 + choice * 2;
                probe(
                    "slotchoice", 24, row * glyph - 2, menu_gold_colour
                );
                claim_first_glyph_at(
                    "slottext", 6, row, view.page->line(view.caret_row),
                    menu_gold_colour
                );
            }
            char footer[128]{};
            control_text(view.footer, footer, sizeof footer);
            claim_first_control_glyph("slotfooter", 27, footer);
            close_probes(before, harness_screen_slot);
            return;
        }

        // The panel, at both ends, which is a claim no unpainted screen and no
        // screen drawn a row off can satisfy.
        const int page_rows = view.page == nullptr
            ? 0 : view.page->count < turn::page_capacity
                ? view.page->count : turn::page_capacity;
        const int panel_height = glyph + page_rows * glyph + 4;
        probe("panel", 2, screen_panel_top * glyph - 2, menu_gold_colour);
        probe(
            "panelfar", 317,
            (screen_panel_top * glyph - 2) + panel_height - 1,
            menu_gold_colour
        );

        // The first ink of the first row that has any, which is what says the
        // page is text rather than an empty box.
        if (view.page != nullptr) {
            for (int row = 0; row < view.page->count; ++row) {
                const char* text = view.page->line(row);
                if (text[0] == '\0') continue;
                claim_first_glyph_at(
                    "pagetext", screen_page_left, screen_page_top + row, text
                );
                break;
            }
        }

        // The caret, where there is one. A screen that asks a question and
        // draws no caret is a screen a player cannot answer.
        if (view.caret_row >= 0 && view.caret_row < turn::page_capacity) {
            claim_glyph(
                "screencaret", screen_caret_column,
                screen_page_top + view.caret_row, '>', menu_gold_colour
            );
        }

        if (view.footer != nullptr && view.footer[0] != '\0') {
            probe(
                "footer", 2, screen_footer_row * glyph - glyph,
                menu_gold_colour
            );
            char footer[128]{};
            control_text(view.footer, footer, sizeof footer);
            claim_first_control_glyph(
                "footertext", screen_footer_row, footer
            );
        }

        // The scene's own backdrop, above the panel where nothing else is
        // drawn. Claimed by the same table the band was filled from, so a
        // console that drew the wrong scene fails on the colour.
        if (scene_has_backdrop(view.backdrop)) {
            const auto& first =
                grandleon_backdrop_bands[view.backdrop - 1][0];
            probe(
                "backdrop", 0, backdrop_row_top(first[0]),
                band_colour(first[2], first[3], first[4])
            );
        } else {
            probe("nobackdrop", 0, 0, backdrop);
        }

        // The speaker's face, claimed by the texel the art library says is
        // there rather than by the fact that something was drawn. A console
        // that drew the wrong character fails on the colour, which is the whole
        // reason this is claimed at all: the words already name the speaker, so
        // a face nobody checks could be anybody and the page would still read
        // correctly.
        if (view.speaker != nullptr) {
            const art::Asset asset = speaker_asset(*view.speaker);
            int px = 0;
            int py = 0;
            int sx = 0;
            int sy = 0;
            if (opaque_near_centre(
                    asset, screen_face_size, false, sx, sy, px, py
                )) {
                probe(
                    "face", screen_face_left + sx, screen_face_top + sy,
                    art::colour_at(asset, px, py)
                );
            }
        }

        close_probes(before, harness_screen_slot);
    }
#endif

    // The board as it was last painted, repainted. Everything in the press loop
    // and every animation frame goes through this, so a repaint before the
    // first paint is a no-op rather than a null dereference.
    void draw_held(bool emphasised) {
        if (held_ == nullptr || last_overlay_ == nullptr) return;
        draw(*held_, *last_overlay_, emphasised);
    }

    // ----- motion ---------------------------------------------------------
    //
    // The shared client's timing, frame-counted from `view::motion`'s
    // own constants: six frames a tile, six for a landed hit with the first
    // three knocked back, three for a miss, and a cursor pulse of period 32.
    // Every one of them ends exactly at rest, which is what lets the pixel
    // assertions stay exact while the board moves.
    //
    // What a frame costs here is a whole repaint. This machine has no sprite
    // table and no plane: a primitive is drawn where it is drawn, so moving one
    // means drawing the picture again. The `ANIM` line reports what each gesture actually
    // cost in microseconds beside the frames it took, so a gesture that overran
    // a frame would say so rather than be assumed not to.

    // One frame drawn while the client is moving the camera - the reveal a wide
    // board opens with, or the pan that follows the action out of view. The
    // client has moved the camera and counts the frames; this draws where it
    // now stands.
    //
    // `layout` is called here and not once before, because it reads the camera
    // to work out where a cell lands on the screen and the camera is the thing
    // that is moving. It draws from the snapshot and overlay it is handed rather
    // than from the last painted ones, which a board being revealed has not got
    // yet.
    void camera_frame(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) override {
        layout(snapshot);
        psx::wait_vblank();
        draw(snapshot, overlay, false);
    }

    void animate_move(
        const sim::EncounterSnapshot& before,
        sim::UnitId unit,
        const view::RouteTile* route,
        int length,
        sim::Position destination
    ) override {
        const sim::UnitSnapshot* actor = unit_of(before, unit);
        if (actor == nullptr || last_overlay_ == nullptr) return;
        const std::uint32_t opened = psx::clock_ticks();
        layout(before);

        moving_ = unit;
        // Begin from the settled board's team-facing convention. Each route
        // leg below then owns the moving silhouette; a first north/south leg
        // safely retains this view until authored compass frames exist.
        move_facing_ = actor->side != player_side() ? -1 : 1;
        int frames = 0;
        int tiles = 0;
        if (route != nullptr && length >= 2) {
            tiles = length - 1;
            for (int leg = 0; leg + 1 < length; ++leg) {
                frames += slide_leg(
                    before, route[leg].x, route[leg].y, route[leg + 1].x,
                    route[leg + 1].y
                );
            }
        } else {
            // No route inside the engine's own reachable set, so the straight
            // line.
            int steps = destination.x - actor->position.x;
            if (steps < 0) steps = -steps;
            int down = destination.y - actor->position.y;
            if (down < 0) down = -down;
            steps += down;
            tiles = steps;
            frames += slide_leg(
                before, actor->position.x, actor->position.y, destination.x,
                destination.y, view::slide_frames_for(steps)
            );
        }
        moving_ = 0;
        report_animation("move", tiles, frames, opened);
    }

    void animate_hit(
        const sim::EncounterSnapshot& before,
        sim::UnitId struck,
        sim::UnitId striker,
        int toward_x,
        int toward_y,
        view::AttackGesture gesture,
        int separation,
        int damage
    ) override {
        play_attack(
            before, struck, striker, toward_x, toward_y, gesture, separation,
            true, damage
        );
    }

    void animate_miss(
        const sim::EncounterSnapshot& before,
        sim::Position cell,
        sim::UnitId struck,
        sim::UnitId striker,
        view::AttackGesture gesture,
        int separation
    ) override {
        static_cast<void>(cell);
        play_attack(
            before, struck, striker, 0, 0, gesture, separation, false, 0
        );
    }

    void draw_close_exchange(
        const sim::UnitSnapshot& attacker,
        const sim::UnitSnapshot& defender,
        art::Pose attacker_pose,
        art::Pose defender_pose,
        bool show_result,
        bool landed,
        int damage
    ) {
        console::draw_close_background(close_terrain_);
        const bool attacker_left = attacker.side == sim::Side::first;
        const bool defender_left = defender.side == sim::Side::first;
        console::draw_close_character(
            drawing_index_of(attacker), attacker_pose, attacker_left,
            attacker_left ? 0 : 1
        );
        console::draw_close_character(
            drawing_index_of(defender), defender_pose, defender_left,
            defender_left ? 0 : 1
        );

        // The scenery and figures still own the frame. The two zero-row panels
        // leave the centre open and hide less total scene area than the old
        // full-width strip, but now identify both fixed sides, their health,
        // team colour and proportion at a glance. A counter changes poses,
        // never these anchors.
        constexpr int chip_width = 15 * glyph;
        constexpr int chip_header = 16;
        const view::ConsolePanel left_chip = view::console_panel(
            0, 0, chip_width, 0, glyph, chip_header, 0
        );
        const view::ConsolePanel right_chip = view::console_panel(
            gpu::screen_width - chip_width, 0, chip_width, 0, glyph,
            chip_header, 0
        );
        view::paint_console_panel(
            left_chip,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        view::paint_console_panel(
            right_chip,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        const sim::UnitSnapshot& left_unit =
            attacker.side == sim::Side::first ? attacker : defender;
        const sim::UnitSnapshot& right_unit =
            attacker.side == sim::Side::first ? defender : attacker;
        gpu::fill(
            left_chip.header.x, left_chip.header.y, 3,
            left_chip.header.height, move_colour
        );
        gpu::fill(
            right_chip.header.x, right_chip.header.y, 3,
            right_chip.header.height, danger_colour
        );
        Line left_status;
        left_status.text("P1 HP ")
            .decimal(static_cast<std::uint32_t>(left_unit.health))
            .text("/")
            .decimal(static_cast<std::uint32_t>(left_unit.maximum_health));
        Line right_status;
        right_status.text("P2 HP ")
            .decimal(static_cast<std::uint32_t>(right_unit.health))
            .text("/")
            .decimal(static_cast<std::uint32_t>(right_unit.maximum_health));
        write_at_with_clut(1, 1, left_status.c_str(), font_gold_clut);
        write_at_with_clut(26, 1, right_status.c_str(), font_gold_clut);
        view::paint_meter(
            {left_chip.header.x + 7, 16, left_chip.header.width - 13, 2},
            left_unit.health, left_unit.maximum_health,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        view::paint_meter(
            {right_chip.header.x + 7, 16, right_chip.header.width - 13, 2},
            right_unit.health, right_unit.maximum_health,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
#ifdef GRANDLEON_PSX_AUTOPILOT
        static bool close_chrome_checked = false;
        if (!close_chrome_checked) {
            close_chrome_checked = true;
            expect(
                gpu::read_back(left_chip.outer.x, left_chip.outer.y) ==
                    menu_gold_colour &&
                    gpu::read_back(right_chip.outer.x, right_chip.outer.y) ==
                    menu_gold_colour,
                "the close battle frames both side status chips"
            );
        }
#endif
        if (show_result) {
            Line result;
            if (landed) {
                result.text("DAMAGE ").decimal(
                    static_cast<std::uint32_t>(damage < 0 ? 0 : damage)
                );
            } else {
                result.text("MISS");
            }
            int length = 0;
            while (result.c_str()[length] != '\0') ++length;
            const int width = length * glyph + 20;
            const int left = (gpu::screen_width - width) / 2;
            const view::ConsolePanel result_panel = view::console_panel(
                left, 190, width, 0, glyph, 16, 0
            );
            view::paint_console_panel(
                result_panel,
                [](view::UiRect rectangle, view::UiTone tone) {
                    paint_console_ui(rectangle, tone);
                }
            );
            write_at_with_clut(
                (screen_cols - length) / 2, 24, result.c_str(), font_gold_clut
            );
#ifdef GRANDLEON_PSX_AUTOPILOT
            static bool close_result_checked = false;
            if (!close_result_checked) {
                close_result_checked = true;
                expect(
                    gpu::read_back(
                        result_panel.outer.x, result_panel.outer.y
                    ) == menu_gold_colour,
                    "the close battle frames its damage result"
                );
            }
#endif
        }
    }

    // The same accepted attack event now drives both board timing and the
    // production close presentation. Attacker and defender are placed by
    // faction side, not by who happens to strike, so a following counterattack
    // reverses the poses without making the two characters swap sides.
    void play_attack(
        const sim::EncounterSnapshot& before,
        sim::UnitId struck,
        sim::UnitId striker,
        int toward_x,
        int toward_y,
        view::AttackGesture gesture,
        int separation,
        bool landed,
        int damage
    ) {
        const sim::UnitSnapshot* target = unit_of(before, struck);
        if (target == nullptr || last_overlay_ == nullptr) return;
        const std::uint32_t opened = psx::clock_ticks();
        layout(before);
        const sim::UnitSnapshot* thrower =
            striker != 0 ? unit_of(before, striker) : nullptr;
        int frames = 0;
        striking_ = thrower != nullptr ? thrower->id : 0;
        if (thrower != nullptr) {
            console::prepare_close();
        }
        close_terrain_ = terrain_composition_at(
            target->position.x, target->position.y
        );

        // Establish the two characters before the strike. A swing has no
        // ordinary lead, which made its first close frame the impact and let
        // the entire scene pass almost invisibly on hardware.
        if (thrower != nullptr) {
            for (int frame = 0; frame < view::close_intro_hold_frames; ++frame) {
                static_cast<void>(frame);
                psx::wait_vblank();
                draw_close_exchange(
                    *thrower, *target, art::Pose::windup, art::Pose::idle,
                    false, landed, damage
                );
                ++frames;
            }
        }

        // ---- the opening -------------------------------------------------
        const int lead = view::gesture_lead_frames(gesture, separation);
        for (int frame = 0; frame < lead; ++frame) {
            strike_pose_ = frame * 2 < lead
                ? art::Pose::windup : art::Pose::attack;
            if (gesture == view::AttackGesture::shot && thrower != nullptr) {
                // The same interpolation a token walks on, read back to a cell:
                // the mark is a tile of the board rather than a pixel on it,
                // because a tile is the unit this renderer's flash speaks in.
                flash_x_ = view::slide_between(
                    thrower->position.x, target->position.x, frame, lead
                );
                flash_y_ = view::slide_between(
                    thrower->position.y, target->position.y, frame, lead
                );
                flash_lit_ = true;
            } else {
                flash_x_ = target->position.x;
                flash_y_ = target->position.y;
                flash_lit_ = (frame % 2) == 0;
            }
            psx::wait_vblank();
            if (thrower != nullptr) {
                draw_close_exchange(
                    *thrower, *target, strike_pose_, art::Pose::idle,
                    false, landed, damage
                );
            } else {
                draw(before, *last_overlay_, false);
            }
            ++frames;
        }

        // ---- the resolution ----------------------------------------------
        flinching_ = landed ? struck : 0;
        flash_x_ = target->position.x;
        flash_y_ = target->position.y;
        // Nothing is knocked anywhere on a miss, and that is the difference the
        // gesture exists to say; `miss_frames` is how long that difference
        // lasts, and it is the shared model's number rather than this file's.
        const int resolve = landed ? view::flinch_frames : view::miss_frames;
        for (int frame = 0; frame < resolve; ++frame) {
            if (landed) {
                flinch_dx_ = view::flinch_offset(frame, toward_x, drawn_tile());
                flinch_dy_ = view::flinch_offset(frame, toward_y, drawn_tile());
            }
            flash_lit_ = (frame % 2) == 0;
            psx::wait_vblank();
            if (thrower != nullptr) {
                draw_close_exchange(
                    *thrower, *target, art::Pose::attack,
                    landed ? art::Pose::hurt : art::Pose::idle,
                    true, landed, damage
                );
            } else {
                draw(before, *last_overlay_, false);
            }
            ++frames;
        }
        if (thrower != nullptr) {
            // Hold the result separately from the flinch, so the damage or
            // miss remains readable even when a counterattack follows.
            for (int frame = 0; frame < view::close_result_hold_frames; ++frame) {
                static_cast<void>(frame);
                psx::wait_vblank();
                draw_close_exchange(
                    *thrower, *target, art::Pose::attack,
                    landed ? art::Pose::hurt : art::Pose::idle,
                    true, landed, damage
                );
                ++frames;
            }
        }
        flinch_dx_ = 0;
        flinch_dy_ = 0;
        flinching_ = 0;
        flash_x_ = -1;
        flash_y_ = -1;
        flash_lit_ = false;
        striking_ = 0;
        strike_pose_ = art::Pose::idle;
        report_animation(landed ? "hit" : "miss", 1, frames, opened);
    }

    // ----- the pixel claims ------------------------------------------------
    //
    // What the executable says it drew, where it says it drew it. The observer
    // captures the display at exactly this instant: it is called from inside
    // the store that signals it, so there is no frame to agree about. The
    // joiner requires the claim, the GPU's own readback and that frame to
    // agree, with no tolerance at all.
    [[nodiscard]] bool visible_isometric_colour(
        int cell_x, int cell_y, std::uint16_t colour,
        int& found_x, int& found_y
    ) const {
        const int centre_x =
            cell_left(cell_x, cell_y) + drawn_tile() / 2;
        const int top = isometric_diamond_top(
            cell_x, cell_y, elevation_at(cell_x, cell_y)
        );
        const int bottom = top + 2 * iso_half_height_;
        for (int y = top; y <= bottom; ++y) {
            if (y < 0 || y >= bar_top) continue;
            for (int x = centre_x - iso_half_width_;
                 x <= centre_x + iso_half_width_; ++x) {
                if (x < 0 || x >= gpu::screen_width) continue;
                const int dx = x >= centre_x ? x - centre_x : centre_x - x;
                const int dy = y >= top + iso_half_height_
                    ? y - (top + iso_half_height_)
                    : (top + iso_half_height_) - y;
                // Search only the two-pixel ring the painter can own, not the
                // whole bounding box where a neighbouring cell or token may
                // coincidentally use the same team colour.
                const int edge =
                    dx * iso_half_height_ + dy * iso_half_width_;
                const int outer = iso_half_width_ * iso_half_height_;
                const int tolerance = iso_half_width_ * 2;
                if (edge < outer - tolerance || edge > outer + tolerance) {
                    continue;
                }
                if (gpu::read_back(x, y) != colour) continue;
                found_x = x;
                found_y = y;
                return true;
            }
        }
        return false;
    }

    // Find a texel of this exact cutout which survived the painter. A unit can
    // stand behind a higher cell in the isometric projection, so the old flat
    // board's fixed centre claim is not necessarily visible even though the
    // figure is correctly drawn. Derive each candidate from the same scaled
    // texture mapping as the GPU, then accept only an opaque asset colour the
    // completed framebuffer actually contains. Fully occluded figures make no
    // false claim; every visible one still proves its own art reached VRAM.
    [[nodiscard]] bool visible_isometric_unit_pixel(
        const art::Asset& asset, int drawn, bool mirrored,
        int cell_x, int cell_y, const turn::Overlay& overlay,
        const MenuBox& box, int& found_x, int& found_y,
        std::uint16_t& found_colour
    ) const {
        const int left = cell_left(cell_x, cell_y) +
            drawn_tile() / 2 - drawn / 2;
        const int top = cell_top(cell_x, cell_y) + drawn_tile() - drawn;
        for (int sy = 0; sy < drawn; ++sy) {
            const int y = top + sy;
            if (y < 0 || y >= bar_top) continue;
            for (int sx = 0; sx < drawn; ++sx) {
                const int x = left + sx;
                if (x < 0 || x >= gpu::screen_width ||
                    box.covers_pixel(x, y) ||
                    cursor_cell_covers_pixel(overlay, x, y)) {
                    continue;
                }
                const int sampled_tx = texel_under(sx, drawn, asset.side);
                const int tx = mirrored
                    ? asset.side - 1 - sampled_tx : sampled_tx;
                const int ty = texel_under(sy, drawn, asset.side);
                const std::uint16_t colour = art::colour_at(asset, tx, ty);
                if (colour == 0 || gpu::read_back(x, y) != colour) continue;
                found_x = x;
                found_y = y;
                found_colour = colour;
                return true;
            }
        }
        return false;
    }
    void after_facts(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) override {
        const int before = probes;

        if (overlay.sheet != nullptr) {
            // The sheet covers the board, so there is no lit tile, no unit and
            // no cursor left to claim: claiming one would be claiming a pixel
            // the interface is deliberately drawing over. What is claimed
            // instead is that the sheet is really there: its shared amber
            // frame, the paper body, and the first ink pixel of its title.
            const int hp_line = sheet_line(*overlay.sheet, "HP ");
            const int inventory_line = hp_line >= 0 ? hp_line + 3 : 1;
            const int inventory_rows = overlay.sheet->count > inventory_line
                ? overlay.sheet->count - inventory_line : 0;
            const int shown_inventory_rows = inventory_rows < 8
                ? inventory_rows : 8;
            const layout::Sheet placed = layout::sheet(shown_inventory_rows);
            probe("sheet", placed.page.outer.x, placed.page.outer.y,
                  menu_gold_colour);
            probe("sheetstatus", placed.status.outer.x, placed.status.outer.y,
                  menu_gold_colour);
            probe("sheetstatusdivider", placed.status.rows.x,
                  placed.status.rows.y, menu_gold_colour);
            probe("sheetloadout", placed.loadout.outer.x,
                  placed.loadout.outer.y, menu_gold_colour);
            probe("sheetloadoutdivider", placed.loadout.rows.x,
                  placed.loadout.rows.y, menu_gold_colour);
            probe("sheetfooter", placed.page.footer.x,
                  placed.page.footer.y + 1, paper_colour);
            if (overlay.sheet->count > 0) {
                claim_first_glyph_at_pixel(
                    "sheettext", 16, 10, overlay.sheet->line(0),
                    menu_gold_colour
                );
            }
#ifdef GRANDLEON_PSX_AUTOPILOT
            static bool sheet_layout_checked = false;
            if (!sheet_layout_checked) {
                sheet_layout_checked = true;
                expect(
                    layout::sheet_regions_are_separate(shown_inventory_rows),
                    "sheet headings rows meter and footer reserve separate pixels"
                );
            }
#endif
            close_probes(before);
            return;
        }

        const MenuBox box = menu_box(overlay);
        for (int y = camera().y; y < camera().y + camera().view_h; ++y) {
            for (int x = camera().x; x < camera().x + camera().view_w; ++x) {
                const bool splash = turn::contains(overlay.splash, x, y);
                const bool danger = turn::contains(overlay.danger, x, y);
                const bool move = turn::contains(overlay.moves, x, y);
                if (!danger && !move && !splash) continue;
                if (occupied(snapshot, x, y)) continue;
                if (x == overlay.cursor_x && y == overlay.cursor_y) continue;
                const int left = cell_left(x, y);
                const int top = cell_top(x, y);
                if (box.covers(left, top, drawn_tile(), drawn_tile())) continue;
                Line label;
                label.text(splash ? "s" : move ? "m" : "d")
                    .decimal(static_cast<std::uint32_t>(x))
                    .text("_")
                    .decimal(static_cast<std::uint32_t>(y));
                int probe_x = left;
                int probe_y = top;
                const std::uint16_t expected =
                    splash ? cursor_colour
                           : move ? lit_colour(overlay) : danger_colour;
                if (!visible_isometric_colour(
                        x, y, expected, probe_x, probe_y
                    ) || box.covers_pixel(probe_x, probe_y)) {
                    continue;
                }
                probe(
                    label.c_str(), probe_x, probe_y, expected
                );
            }
        }
        for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
            const sim::UnitSnapshot& unit = snapshot.units[i];
            // The same predicate the draw loop uses. A census wider than what
            // was drawn claims a texel nobody painted.
            if (!sim::on_board(unit)) continue;
            if (!camera().visible(unit.position.x, unit.position.y)) continue;
            const art::Asset asset = unit_asset(unit);
            int px = 0;
            int py = 0;
            const int drawn = drawn_unit_size();
            int sx = 0;
            int sy = 0;
            if (!opaque_near_centre(
                    asset, drawn, unit.side != player_side(), sx, sy, px, py
                )) {
                continue;
            }
            const int cell_x = cell_left(unit.position.x, unit.position.y);
            const int cell_y = cell_top(unit.position.x, unit.position.y);
            const int at_x = cell_x + drawn_tile() / 2 - drawn / 2 + sx;
            const int at_y = cell_y + drawn_tile() - drawn + sy;
            // Claiming a texel the menu is drawn over would be claiming a
            // pixel the interface is deliberately hiding. That is the reason a
            // lit tile under the box is skipped a few lines above, and the
            // reason a sheet claims nothing about the board at all.
            int visible_x = at_x;
            int visible_y = at_y;
            std::uint16_t expected =
                unit.has_acted &&
                        wash_covers_pixel(cell_x, cell_y, at_x, at_y)
                    ? spent_colour
                    : art::colour_at(asset, px, py);
            if (!visible_isometric_unit_pixel(
                    asset, drawn, unit.side != player_side(),
                    unit.position.x, unit.position.y, overlay, box,
                    visible_x, visible_y, expected
                )) {
                continue;
            }
            Line label;
            label.text("u")
                .decimal(static_cast<std::uint32_t>(unit.position.x))
                .text("_")
                .decimal(static_cast<std::uint32_t>(unit.position.y));
            probe(label.c_str(), visible_x, visible_y, expected);
        }
        if (camera().visible(overlay.cursor_x, overlay.cursor_y)) {
            int cursor_at_x =
                cell_left(overlay.cursor_x, overlay.cursor_y);
            int cursor_at_y =
                cell_top(overlay.cursor_x, overlay.cursor_y);
            const bool cursor_visible = visible_isometric_colour(
                overlay.cursor_x, overlay.cursor_y, cursor_colour,
                cursor_at_x, cursor_at_y
            );
            // And the cursor is claimed on the same terms as the figures above.
            // A character picked up on a tile the action menu is drawn over is
            // a cursor the interface is deliberately hiding, and claiming its
            // colour there would be claiming a pixel nobody painted.
            if (cursor_visible &&
                !box.covers_pixel(cursor_at_x, cursor_at_y)) {
                probe("cursor", cursor_at_x, cursor_at_y, cursor_colour);
            }
        }
        // Whoever the cursor rests on is named on the bar's first row, and a
        // name nothing photographs is a row this executable only claims to
        // draw. The first ink of it, by the same idiom the message row uses.
        if (overlay.hovered_name != nullptr) {
            claim_first_glyph_at(
                "hovertext", 1, row_hovered, overlay.hovered_name
            );
        }
        if (overlay.message != nullptr) {
            probe("bar", (screen_cols - 1) * glyph, row_message * glyph,
                  paper_colour);
            claim_first_glyph_at("bartext", 1, row_message, overlay.message);
        } else if (overlay.hovered_class != nullptr) {
            // The class, on the row the message would have taken. Claimed on
            // the same terms, so the row is photographed whichever of the two
            // is standing in it.
            probe("bar", (screen_cols - 1) * glyph, row_message * glyph,
                  paper_colour);
            claim_first_glyph_at(
                "classtext", 1, row_message, overlay.hovered_class
            );
        }
        if (overlay.aiming != nullptr) {
            probe("aim", (screen_cols - 1) * glyph, row_aiming * glyph,
                  paper_colour);
        }
        if (overlay.menu != nullptr && overlay.menu_rows > 0) {
            probe(
                "menu", box.panel.outer.x, box.panel.outer.y, menu_gold_colour
            );
            claim_glyph_at_pixel(
                "menucaret", box.row_text_x,
                box.row_text_y + overlay.menu_row * box.panel.row_height,
                '>', menu_gold_colour
            );
            claim_glyph_background_at_pixel(
                "menutextbg", box.row_text_x,
                box.row_text_y + overlay.menu_row * box.panel.row_height, '>',
                console_ui_colour(view::UiTone::focus)
            );
            // The footer's first pixel and the row above it prove that the
            // choices end before the divider rather than merely being painted
            // over by it, which was the old action-menu regression.
            probe(
                "menufooter", box.panel.footer.x,
                box.panel.footer.y + 1, paper_colour
            );
#ifdef GRANDLEON_PSX_AUTOPILOT
            static bool menu_layout_checked = false;
            if (!menu_layout_checked) {
                menu_layout_checked = true;
                bool labels_fit = true;
                for (int row = 0; row < overlay.menu_rows; ++row) {
                    if (font::measure(overlay.menu[row].label) >
                        box.panel.rows.width - 28) {
                        labels_fit = false;
                    }
                }
                expect(
                    !layout::overlaps(box.panel.header, box.panel.rows) &&
                    !layout::overlaps(box.panel.rows, box.panel.footer) &&
                    labels_fit,
                    "action rows reserve their header footer and label pixels"
                );
            }
#endif
        }
        close_probes(before);
    }

private:
    [[nodiscard]] int drawn_unit_size() const noexcept {
        const int shared = view::console_board_unit_size(drawn_tile());
        // The 2:1 floor already gives each cell less vertical screen area than
        // a square source tile. Keep the detailed Godot-derived silhouette,
        // but make it a board token rather than a portrait: three quarters of
        // the shared console size is 18 px on fitted large maps and 36 px at
        // the native 32 px cell size.
        const int scaled = shared * 3 / 4;
        return scaled < 16 ? 16 : scaled;
    }

    [[nodiscard]] bool wash_covers_pixel(
        int left, int top, int x, int y
    ) const noexcept {
        // The isometric wash is a narrow diamond ring on the ground plane;
        // the opaque unit texel used by the framebuffer claim is inside it.
        static_cast<void>(left);
        static_cast<void>(top);
        static_cast<void>(x);
        static_cast<void>(y);
        return false;
    }

    [[nodiscard]] bool cursor_cell_covers_pixel(
        const turn::Overlay& overlay, int x, int y
    ) const noexcept {
        if (!camera().visible(overlay.cursor_x, overlay.cursor_y)) return false;
        const int left = cell_left(overlay.cursor_x, overlay.cursor_y);
        const int top = cell_top(overlay.cursor_x, overlay.cursor_y);
        return x >= left && x < left + drawn_tile() && y >= top &&
               y < top + drawn_tile();
    }

    void close_probes(int before) { close_probes(before, harness_slot); }

    void close_probes(int before, int slot) {
        Line()
            .text("PROBE-COUNT ")
            .decimal(static_cast<std::uint32_t>(probes - before))
            .flush();
        // The instant the observer photographs, named from inside the store
        // that names it. With no observer attached this is a write to a port
        // nothing reads.
        psx::signal_host(slot);
    }

    // ----- the picture ------------------------------------------------------

    // What a frame of this board costs, measured every time it is drawn.
    //
    // It is a whole repaint: backdrop, textured diamond cells, elevation
    // faces, overlays, tokens, cursor and status bar. This machine has no
    // sprite table or retained plane, so the measured full-frame cost is the
    // useful budget rather than a partial-update estimate.
    void draw(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay,
        bool emphasised
    ) {
        const std::uint32_t opened = psx::clock_ticks();
        sample_fps(opened);
        draw_picture(snapshot, overlay, emphasised);
        const std::uint32_t microseconds =
            (psx::clock_ticks() - opened) * 1000u /
            psx::ticks_per_1000_microseconds;
        ++paints_;
        paint_microseconds_ += microseconds;
        if (microseconds < paint_shortest_) paint_shortest_ = microseconds;
        if (microseconds > paint_longest_) paint_longest_ = microseconds;
    }

    void draw_picture(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay,
        bool emphasised
    ) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, backdrop);
        if (overlay.sheet != nullptr) {
            draw_sheet(*overlay.sheet);
            return;
        }
        // A quiet three-band stage gives the projected board a horizon and a
        // grounded lower field instead of leaving a tiny diamond floating in
        // one flat clear colour. These are native flat GPU fills, consume no
        // VRAM, and stay behind every playable cell and interface element.
        gpu::fill(0, 0, gpu::screen_width, 58, gpu::rgb15(3, 6, 7));
        gpu::fill(0, 58, gpu::screen_width, 72, gpu::rgb15(4, 8, 7));
        gpu::fill(
            0, 130, gpu::screen_width, bar_top - 130,
            gpu::rgb15(3, 5, 4)
        );
        layout(snapshot);
        draw_isometric_board(snapshot, overlay);
        draw_cursor(overlay, emphasised);
        draw_bar(snapshot, overlay);
        if (overlay.menu != nullptr) draw_menu(overlay);
        draw_fps();
    }

    // A live cadence over the frames actually drawn. When enabled, the input
    // loop above repaints once per vertical retrace instead of only when the
    // cursor pulse changes, so this is a frame-rate counter rather than a
    // count of deliberate idle repaints.
    void sample_fps(std::uint32_t now) {
        if (!display_options_.show_fps) return;
        constexpr std::uint32_t sample_ticks =
            psx::ticks_per_1000_microseconds * 500U;
        if (fps_window_started_ == 0) {
            fps_window_started_ = now;
            fps_frames_ = 1;
            return;
        }
        ++fps_frames_;
        const std::uint32_t elapsed = now - fps_window_started_;
        if (elapsed < sample_ticks || fps_frames_ <= 1) return;
        fps_value_ = static_cast<std::uint32_t>(
            ((fps_frames_ - 1U) * psx::ticks_per_1000_microseconds * 1000U +
             elapsed / 2U) /
            elapsed
        );
        if (fps_value_ > 999U) fps_value_ = 999U;
        fps_window_started_ = now;
        fps_frames_ = 1;
    }

    void draw_fps() {
        if (!display_options_.show_fps) return;
        gpu::fill(32 * glyph, 0, 8 * glyph, glyph, paper_colour);
        Line label;
        label.text("FPS ");
        if (fps_value_ == 0) {
            label.text("--");
        } else {
            label.decimal(fps_value_);
        }
        write_at(33, 0, label.c_str());
    }


    void layout(const sim::EncounterSnapshot& snapshot) {
        board_width_ = snapshot.width;
        const int drawn = drawn_tile();
        // A 32-pixel source cell becomes a 2:1 diamond. The fit-selected
        // `drawn` value governs camera extent and terrain scale, while board
        // units use their own smaller footprint. Even the widest 20x13 view
        // occupies 264x132 pixels at the 16-pixel floor, leaving room for the
        // HUD and raised terrain on a 320x240 display.
        const int preferred_half_width = drawn * 3 / 4;
        const int fitted_half_width =
            (gpu::screen_width - 16) /
            (camera().view_w + camera().view_h);
        iso_half_width_ = preferred_half_width < fitted_half_width
            ? preferred_half_width : fitted_half_width;
        iso_half_height_ = (iso_half_width_ + 1) / 2;
        if (iso_half_width_ < 2) iso_half_width_ = 2;
        if (iso_half_height_ < 2) iso_half_height_ = 2;
        iso_elevation_step_ = drawn >= native_tile ? 5 : 3;
        const int footprint_width =
            (camera().view_w + camera().view_h) * iso_half_width_;
        const int footprint_height =
            (camera().view_w + camera().view_h) * iso_half_height_;
        iso_origin_x_ =
            (gpu::screen_width - footprint_width) / 2 +
            camera().view_h * iso_half_width_;
        iso_origin_y_ = (bar_top - footprint_height) / 2;
        if (iso_origin_y_ < drawn / 2) iso_origin_y_ = drawn / 2;
    }

    [[nodiscard]] int isometric_lift(int elevation) const noexcept {
        if (elevation <= 0) return 0;
        const int raw = elevation * iso_elevation_step_;
        const int limit = drawn_tile() / 2;
        return raw < limit ? raw : limit;
    }

    [[nodiscard]] int isometric_diamond_top(
        int cell_x, int cell_y, int elevation
    ) const noexcept {
        const int dx = cell_x - camera().x;
        const int dy = cell_y - camera().y;
        return iso_origin_y_ + (dx + dy) * iso_half_height_ -
            isometric_lift(elevation);
    }

    [[nodiscard]] static std::uint16_t terrain_tone_colour(
        const view::TerrainComposition& composition, view::TerrainTone tone
    ) noexcept {
        const view::TerrainColour colour =
            view::terrain_colour(composition.kind, tone);
        return gpu::rgb15(
            static_cast<int>(colour.red) >> 3,
            static_cast<int>(colour.green) >> 3,
            static_cast<int>(colour.blue) >> 3
        );
    }

    // A flat-colour diamond ring made from four quads (two triangles each).
    // It leaves the textured centre visible, unlike a rectangular wash over a
    // projected cell, and gives move/danger/cursor state the same ground plane
    // as the terrain it describes.
    void draw_isometric_outline(
        int left, int cell_top, std::uint16_t colour, int inset = 0
    ) const {
        int outer_w = iso_half_width_ - inset;
        int outer_h = iso_half_height_ - inset / 2;
        if (outer_w < 2) outer_w = 2;
        if (outer_h < 2) outer_h = 2;
        const int thickness = outer_h >= 6 ? 2 : 1;
        const int inner_w = outer_w - thickness * 2 > 0
            ? outer_w - thickness * 2 : 1;
        const int inner_h = outer_h - thickness > 0
            ? outer_h - thickness : 1;
        const int centre_x = left + drawn_tile() / 2;
        const int centre_y =
            cell_top + drawn_tile() - 2 * iso_half_height_ +
            iso_half_height_;
        const int ox[4] = {
            centre_x, centre_x + outer_w, centre_x, centre_x - outer_w
        };
        const int oy[4] = {
            centre_y - outer_h, centre_y, centre_y + outer_h, centre_y
        };
        const int ix[4] = {
            centre_x, centre_x + inner_w, centre_x, centre_x - inner_w
        };
        const int iy[4] = {
            centre_y - inner_h, centre_y, centre_y + inner_h, centre_y
        };
        for (int edge = 0; edge < 4; ++edge) {
            const int next = (edge + 1) & 3;
            gpu::draw_triangle(
                ox[edge], oy[edge], ox[next], oy[next],
                ix[next], iy[next], colour
            );
            gpu::draw_triangle(
                ox[edge], oy[edge], ix[next], iy[next],
                ix[edge], iy[edge], colour
            );
        }
    }

    void draw_isometric_castle(
        int left, int cell_top, view::CastleTile tile
    ) const {
        if (tile == view::CastleTile::none ||
            tile == view::CastleTile::floor ||
            tile == view::CastleTile::wall) {
            return;
        }
        const int centre_x = left + drawn_tile() / 2;
        const int centre_y =
            cell_top + drawn_tile() - iso_half_height_;
        const int half_w = iso_half_width_ / 2;
        const int half_h = iso_half_height_ / 2;
        const std::uint16_t colour = tile == view::CastleTile::door
            ? gpu::rgb15(12, 6, 3) : gpu::rgb15(27, 22, 12);
        gpu::draw_triangle(
            centre_x, centre_y - half_h,
            centre_x + half_w, centre_y,
            centre_x, centre_y + half_h, colour
        );
        gpu::draw_triangle(
            centre_x, centre_y - half_h,
            centre_x, centre_y + half_h,
            centre_x - half_w, centre_y, colour
        );
    }

    void draw_isometric_terrain(
        int cell_x, int cell_y, int kind, int variation,
        const turn::Overlay& overlay
    ) {
        const int elevation = elevation_of_kind(kind);
        const int left = cell_left(cell_x, cell_y);
        const int cell_top = this->cell_top(cell_x, cell_y);
        const int top = isometric_diamond_top(cell_x, cell_y, elevation);
        const int centre_x = left + drawn_tile() / 2;
        const int right = centre_x + iso_half_width_;
        const int bottom = top + 2 * iso_half_height_;
        const int left_point = centre_x - iso_half_width_;
        const int lift = isometric_lift(elevation);
        const view::TerrainComposition composition =
            terrain_composition_at(cell_x, cell_y);
        if (lift > 0) {
            const std::uint16_t side = terrain_tone_colour(
                composition, view::TerrainTone::shadow
            );
            const std::uint16_t front = terrain_tone_colour(
                composition, view::TerrainTone::seam
            );
            gpu::draw_triangle(
                right, top + iso_half_height_, centre_x, bottom,
                centre_x, bottom + lift, side
            );
            gpu::draw_triangle(
                right, top + iso_half_height_, centre_x, bottom + lift,
                right, top + iso_half_height_ + lift, side
            );
            gpu::draw_triangle(
                left_point, top + iso_half_height_, centre_x, bottom,
                centre_x, bottom + lift, front
            );
            gpu::draw_triangle(
                left_point, top + iso_half_height_, centre_x, bottom + lift,
                left_point, top + iso_half_height_ + lift, front
            );
        }
        const int cell = terrain_cell_of[kind][variation];
        const int clut = terrain_clut_of[kind];
        if (cell >= 0 && clut >= 0) {
            gpu::draw_cell_triangle(
                centre_x, top, 16, 0,
                right, top + iso_half_height_, 31, 16,
                centre_x, bottom, 16, 31,
                cell, clut
            );
            gpu::draw_cell_triangle(
                centre_x, top, 16, 0,
                centre_x, bottom, 16, 31,
                left_point, top + iso_half_height_, 0, 16,
                cell, clut
            );
        }

        const bool splash = turn::contains(overlay.splash, cell_x, cell_y);
        const bool danger = turn::contains(overlay.danger, cell_x, cell_y);
        const bool move = turn::contains(overlay.moves, cell_x, cell_y);
        if (danger || move || splash) {
            draw_isometric_outline(
                left, cell_top,
                splash ? cursor_colour
                       : move ? lit_colour(overlay) : danger_colour
            );
        }
        if (flash_lit_ && cell_x == flash_x_ && cell_y == flash_y_) {
            draw_isometric_outline(left, cell_top, flash_colour);
        }

        // Houses and groves remain compact vertical billboards grounded at
        // the diamond's lower point. Castle cells do not use the rejected
        // whole-room plate: their authored wall elevation, textured top, door
        // and dais marks are composed cell by cell on the ground plane.
        draw_landmark(
            left, cell_top, drawn_tile(), landmark_at(cell_x, cell_y)
        );
        draw_isometric_castle(left, cell_top, castle_at(cell_x, cell_y));
    }

    void draw_isometric_board(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) {
        const int diagonals = camera().view_w + camera().view_h - 1;
        for (int diagonal = 0; diagonal < diagonals; ++diagonal) {
            for (int dy = 0; dy < camera().view_h; ++dy) {
                const int dx = diagonal - dy;
                if (dx < 0 || dx >= camera().view_w) continue;
                const int x = camera().x + dx;
                const int y = camera().y + dy;
                const int kind = kind_at(x, y);
                if (kind < 0) continue;
                draw_isometric_terrain(
                    x, y, kind, terrain_composition_at(x, y).variation,
                    overlay
                );
            }
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (!sim::on_board(unit) ||
                    !camera().visible(unit.position.x, unit.position.y) ||
                    (unit.position.x - camera().x) +
                            (unit.position.y - camera().y) != diagonal ||
                    (moving_ != 0 && unit.id == moving_)) {
                    continue;
                }
                int x = cell_left(unit.position.x, unit.position.y);
                int y = cell_top(unit.position.x, unit.position.y);
                if (flinching_ != 0 && unit.id == flinching_) {
                    x += flinch_dx_;
                    y += flinch_dy_;
                }
                draw_unit(unit, x, y);
                if (unit.has_acted) draw_wash(x, y, spent_colour);
            }
        }
        if (moving_ != 0) {
            const sim::UnitSnapshot* unit = unit_of(snapshot, moving_);
            if (unit != nullptr) draw_unit(*unit, move_px_, move_py_);
        }
    }

    void draw_unit(const sim::UnitSnapshot& unit, int x, int y) {
        const int drawing = drawing_index_of(unit);
        if (drawing < 0) return;
        art::Pose pose = art::Pose::idle;
        int stream_slot = 0;
        if (unit.id == moving_) {
            pose = art::Pose::move;
        } else if (unit.id == striking_) {
            pose = strike_pose_;
        } else if (unit.id == flinching_) {
            pose = art::Pose::hurt;
            stream_slot = 1;
        }
        const int size = drawn_unit_size();
        const bool facing_left = unit.id == moving_
            ? move_facing_ < 0 : unit.side != player_side();
        console::draw_character(
            drawing, pose, x + drawn_tile() / 2,
            y + drawn_tile() - (unit.id == moving_ ? move_bob_ : 0),
            size, facing_left, stream_slot
        );
    }

    void draw_wash(int left, int top, std::uint16_t colour) {
        draw_isometric_outline(left, top, colour);
    }

    // Four corner brackets. At rest they sit on the cell's own corners; the
    // pulse moves them inward by `view::cursor_emphasis_inset`, which is
    // deliberately short of the centre so the pulse can never change what a
    // centre-sampling probe reads.
    void draw_cursor_at(
        std::int16_t cell_x, std::int16_t cell_y, std::uint16_t colour,
        bool emphasised
    ) {
        if (!camera().visible(cell_x, cell_y)) return;
        const int drawn = drawn_tile();
        const int inset = emphasised ? view::cursor_emphasis_inset(drawn) : 0;
        draw_isometric_outline(
            cell_left(cell_x, cell_y), cell_top(cell_x, cell_y), colour, inset
        );
    }

    void draw_cursor(const turn::Overlay& overlay, bool emphasised) {
        // The parked player's cyan brackets remain on the board while the
        // active player's white brackets pulse. Drawn first so two cursors on
        // one tile still identify whose input is active.
        if (overlay.second_cursor) {
            draw_cursor_at(
                overlay.second_cursor_x, overlay.second_cursor_y,
                second_cursor_colour, false
            );
        }
        draw_cursor_at(
            overlay.cursor_x, overlay.cursor_y, cursor_colour, emphasised
        );
    }

    void draw_bar(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) {
        const sim::UnitSnapshot* hovered = nullptr;
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) && unit.position.x == overlay.cursor_x &&
                unit.position.y == overlay.cursor_y) {
                hovered = &unit;
            }
        }
        gpu::fill(
            0, row_hovered * glyph, gpu::screen_width, bar_rows * glyph,
            paper_colour
        );
        // Separators and an owner strip make the fixed four-row HUD a panel;
        // it still reserves exactly the same 32 lines from the board.
        for (int row = row_hovered; row <= row_hint; ++row) {
            gpu::fill(
                4, (row + 1) * glyph - 1, gpu::screen_width - 4, 1,
                menu_muted_colour
            );
        }
        if (hovered != nullptr) {
            const std::uint16_t owner_colour = hovered->side != player_side()
                ? danger_colour
                : hovered->local_player == 2U ? second_cursor_colour
                                               : move_colour;
            gpu::fill(
                0, row_hovered * glyph, 4, bar_rows * glyph, owner_colour
            );
            const char* source_name =
                overlay.hovered_name != nullptr ? overlay.hovered_name : "";
            (void)write_pixels_with_clut(
                8, row_hovered * glyph, 144, source_name, font_clut
            );
            const char* owner = hovered->side != player_side()
                ? "FOE" : hovered->local_player == 2U ? "P2" : "P1";
            (void)write_pixels_with_clut(
                158, row_hovered * glyph, 30,
                hovered->has_acted ? "DONE" : owner,
                hovered->has_acted ? font_muted_clut : font_gold_clut
            );
            (void)write_pixels_with_clut(
                190, row_hovered * glyph, 16, "HP", font_muted_clut, false
            );
            Line health;
            health.decimal(static_cast<std::uint32_t>(hovered->health))
                .text("/")
                .decimal(static_cast<std::uint32_t>(hovered->maximum_health));
            const int health_width = text_width(health.c_str());
            const int health_x = gpu::screen_width - 8 - health_width;
            const int meter_x = 210;
            const int meter_width = health_x - meter_x - 6;
            view::paint_meter(
                {meter_x, row_hovered * glyph + 2, meter_width, 4},
                hovered->health, hovered->maximum_health,
                [](view::UiRect rectangle, view::UiTone tone) {
                    paint_console_ui(rectangle, tone);
                }
            );
            (void)write_pixels_with_clut(
                health_x, row_hovered * glyph, health_width,
                health.c_str(), font_clut, false
            );
        } else if (overlay.round != nullptr) {
            // Where a board is won by outlasting rounds, the count goes on the
            // hovered row and only when the cursor rests on nobody, so nothing
            // a player was already reading is taken away.
            write_at(1, row_hovered, overlay.round);
        }
        // What kind of character that is, on the row directly under their
        // name, and only while that row has nothing more urgent on it.
        //
        // This display is four rows of forty columns and every one of them is
        // spoken for, so the class sits where the round count already sits when
        // the cursor rests on nobody: in a row's spare moment. Under the name
        // rather than beside it because the two do not fit beside each other:
        // `ASHEN STORMCALLER` and `STORMCALLER` and the health are forty-five
        // columns, and a bar that clipped would drop the health to keep the
        // class, which is the wrong thing to lose.
        if (overlay.message != nullptr) {
            (void)write_pixels_with_clut(
                8, row_message * glyph, 304, overlay.message, font_clut
            );
        } else if (overlay.hovered_class != nullptr) {
            (void)write_pixels_with_clut(
                8, row_message * glyph, 304, overlay.hovered_class, font_clut
            );
        }
        if (overlay.aiming != nullptr) {
            (void)write_pixels_with_clut(
                8, row_aiming * glyph, 304, overlay.aiming, font_clut
            );
        }
        const view::ButtonLegend buttons = view::button_legend(
            view::ConsolePlatform::playstation
        );
        draw_button_hint(1, row_hint, buttons.confirm, "ACT");
        draw_button_hint(7, row_hint, buttons.back, "BACK");
        draw_button_hint(14, row_hint, buttons.menu, "MENU");
        draw_button_hint(21, row_hint, buttons.wait, "WAIT");
    }

    [[nodiscard]] static bool begins_with(
        const char* value, const char* prefix
    ) noexcept {
        if (value == nullptr || prefix == nullptr) return false;
        for (int index = 0; prefix[index] != '\0'; ++index) {
            if (value[index] != prefix[index]) return false;
        }
        return true;
    }

    [[nodiscard]] static int sheet_line(
        const grandleon::sheet::UnitSheet& sheet, const char* prefix
    ) noexcept {
        for (int line = 0; line < sheet.count; ++line) {
            if (begins_with(sheet.line(line), prefix)) return line;
        }
        return -1;
    }

    [[nodiscard]] static int decimal_after(
        const char* value, const char* prefix
    ) noexcept {
        if (!begins_with(value, prefix)) return 0;
        int at = 0;
        while (prefix[at] != '\0') ++at;
        int number = 0;
        while (value[at] >= '0' && value[at] <= '9') {
            number = number * 10 + value[at] - '0';
            ++at;
        }
        return number;
    }

    // Character details are a full screen because forty columns leave no
    // useful board beside them. They are no longer a raw text dump, however:
    // identity, battle numbers and carried actions each have their own visual
    // card, with a real health meter and section bands that can be read at a
    // television distance.
    void draw_sheet(const grandleon::sheet::UnitSheet& sheet) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        const int hp_line = sheet_line(sheet, "HP ");
        const int level_line = sheet_line(sheet, "LEVEL ");
        const int inventory_line = hp_line >= 0 ? hp_line + 3 : 1;
        const int inventory_rows = sheet.count > inventory_line
            ? sheet.count - inventory_line : 0;
        const int shown_inventory_rows = inventory_rows < 8
            ? inventory_rows : 8;
        const layout::Sheet placed = layout::sheet(shown_inventory_rows);
        view::paint_console_page(
            placed.page,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        if (sheet.count > 0) {
            const int level_width = level_line >= 0
                ? text_width(sheet.line(level_line)) : 0;
            (void)write_pixels_with_clut(
                16, 10, 288 - (level_width > 0 ? level_width + 12 : 0),
                sheet.line(0), font_gold_clut
            );
        }
        if (sheet.count > 1) {
            (void)write_pixels_with_clut(
                16, 21, 288, sheet.line(1), font_clut
            );
        }
        if (level_line >= 0) {
            const char* level = sheet.line(level_line);
            const int width = text_width(level);
            (void)write_pixels_with_clut(
                304 - width, 10, width, level, font_gold_clut, false
            );
        }

        view::paint_console_panel(
            placed.status,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        (void)write_pixels_with_clut(
            placed.status.header.x + 8, placed.status.header.y + 3,
            placed.status.header.width - 16,
            ui_text::text(language(), ui_text::Message::status),
            font_gold_clut
        );
        if (hp_line >= 0) {
            for (int row = 0; row < 3 && hp_line + row < sheet.count; ++row) {
                (void)write_pixels_with_clut(
                    placed.status.rows.x + 8,
                    placed.status.rows.y + 2 + row * placed.status.row_height,
                    placed.status.rows.width - 16,
                    sheet.line(hp_line + row), font_clut
                );
            }
            const char* health = sheet.line(hp_line);
            const int current = decimal_after(health, "HP ");
            int slash = 0;
            while (health[slash] != '\0' && health[slash] != '/') ++slash;
            const int maximum = health[slash] == '/'
                ? decimal_after(health + slash + 1, "") : 0;
            view::paint_meter(
                {
                    placed.status.rows.x + 8,
                    placed.status.rows.y + 3 * placed.status.row_height + 4,
                    placed.status.rows.width - 16, 4,
                },
                current, maximum,
                [](view::UiRect rectangle, view::UiTone tone) {
                    paint_console_ui(rectangle, tone);
                }
            );
        }

        view::paint_console_panel(
            placed.loadout,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        (void)write_pixels_with_clut(
            placed.loadout.header.x + 8, placed.loadout.header.y + 3,
            placed.loadout.header.width - 16,
            ui_text::text(language(), ui_text::Message::loadout),
            font_gold_clut
        );
        for (int row = 0; row < shown_inventory_rows; ++row) {
            const char* text = sheet.line(inventory_line + row);
            const bool section = begins_with(text, "WEAPONS") ||
                begins_with(text, "ARMAS") ||
                begins_with(text, "ABILITIES") ||
                begins_with(text, "HABILIDADES") ||
                begins_with(text, "ITEMS") ||
                begins_with(text, "OBJETOS") || begins_with(text, "ITENS");
            // Keep the card body on one surface, as the N64 sheet does. The
            // section names are already gold; repainting their whole rows as
            // headers made the interleaved heading/content pairs look like
            // alternating stripes rather than one readable loadout.
            (void)write_pixels_with_clut(
                placed.loadout.rows.x + 8,
                placed.loadout.rows.y + 2 + row * placed.loadout.row_height,
                placed.loadout.rows.width - 16, text,
                section ? font_gold_clut : font_clut
            );
        }
        // This screen is shared by the ordinary turn executable and the
        // campaign executable, while `write_controls_centred` belongs to the
        // campaign page renderer below. Keep the same centred controller row
        // without making the turn build depend on a campaign-only helper.
        draw_sheet_footer();
    }

    void draw_menu(const turn::Overlay& overlay) {
        const MenuBox box = menu_box(overlay);
        if (!box.valid()) return;
        view::paint_console_panel(
            box.panel,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        view::paint_focus_row(
            box.panel, overlay.menu_row,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        const char* first = overlay.menu_rows > 0
            ? overlay.menu[0].label : nullptr;
        const char* battle_row = ui_text::text(
            language(), ui_text::Message::back_to_battle
        );
        bool battle = first != nullptr;
        for (int index = 0; battle_row[index] != '\0'; ++index) {
            if (first == nullptr || first[index] != battle_row[index]) {
                battle = false;
                break;
            }
        }
        (void)write_pixels_with_clut(
            box.heading_x, box.heading_y,
            box.panel.header.x + box.panel.header.width - box.heading_x - 6,
            ui_text::text(
                language(), battle ? ui_text::Message::battle_heading
                                    : ui_text::Message::action_heading
            ),
            font_gold_clut
        );
        for (int row = 0; row < overlay.menu_rows; ++row) {
            // Focus is redundant by design: the framed row gives the glance,
            // and the caret keeps the choice legible without colour.
            Line line;
            line.text(row == overlay.menu_row ? ">" : " ")
                .text(" ")
                .text(overlay.menu[row].label);
            (void)write_pixels_with_clut(
                box.row_text_x,
                box.row_text_y + row * box.panel.row_height,
                box.panel.rows.x + box.panel.rows.width - box.row_text_x - 6,
                line.c_str(),
                row == overlay.menu_row ? font_gold_clut : font_clut
            );
        }
        const view::ButtonLegend buttons = view::button_legend(
            view::ConsolePlatform::playstation
        );
        draw_button_hint_pixels(
            box.footer_text_x, box.footer_text_y,
            buttons.confirm,
            ui_text::text(language(), ui_text::Message::select)
        );
        const char* back = ui_text::text(
            language(), ui_text::Message::back_button
        );
        const int back_width = button_hint_width(buttons.back, back);
        draw_button_hint_pixels(
            box.panel.footer.x + box.panel.footer.width - back_width - 8,
            box.footer_text_y, buttons.back, back
        );
    }

    [[nodiscard]] static std::uint32_t controller_mark(
        const char* value
    ) noexcept {
        if (value == nullptr) return '?';
        if (value[0] == 'X' && value[1] == '\0') return font::controller_cross;
        if (value[0] == 'O' && value[1] == '\0') return font::controller_circle;
        if (value[0] == '^' && value[1] == '\0') return font::controller_triangle;
        return static_cast<unsigned char>(value[0]);
    }

    [[nodiscard]] static int button_hint_width(
        const char* glyph_text, const char* label
    ) noexcept {
        return font::advance(controller_mark(glyph_text)) + 4 +
            font::measure(label);
    }

    void draw_button_hint_pixels(
        int x, int y, const char* glyph_text, const char* label
    ) {
        const std::uint32_t mark = controller_mark(glyph_text);
        const int badge_width = font::advance(mark);
        gpu::fill(
            x, y, badge_width, glyph,
            console_ui_colour(view::UiTone::focus)
        );
        gpu::draw_glyph(x, y, font::slot_for(mark), font_gold_clut);
        (void)write_pixels_with_clut(
            x + badge_width + 4, y,
            gpu::screen_width - (x + badge_width + 4),
            label, font_clut
        );
    }

    void draw_button_hint(
        int column, int row, const char* glyph_text, const char* label
    ) {
        draw_button_hint_pixels(column * glyph, row * glyph, glyph_text, label);
    }

    void draw_sheet_footer() {
        const char* or_word = ui_text::text(
            language(), ui_text::Message::or_word
        );
        const char* back = ui_text::text(
            language(), ui_text::Message::back_button
        );
        const int cross_width = font::advance(font::controller_cross);
        const int circle_width = font::advance(font::controller_circle);
        const int total = cross_width + 4 + text_width(or_word) + 6 +
            circle_width + 4 + text_width(back);
        int x = (gpu::screen_width - total) / 2;
        const int y = sheet_footer_row * glyph;
        gpu::draw_glyph(
            x, y, font::slot_for(font::controller_cross), font_gold_clut
        );
        x += cross_width + 4;
        x += write_pixels_with_clut(
            x, y, total, or_word, font_clut, false
        ) + 6;
        gpu::draw_glyph(
            x, y, font::slot_for(font::controller_circle), font_gold_clut
        );
        x += circle_width + 4;
        (void)write_pixels_with_clut(
            x, y, gpu::screen_width - x - 8, back, font_clut, false
        );
    }

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    [[nodiscard]] static int centred_x(
        const char* value, int left = 0, int width = gpu::screen_width
    ) noexcept {
        const int measured = font::measure(value);
        return measured < width ? left + (width - measured) / 2 : left;
    }

    // Shared campaign pages name the three abstract actions A/B/C so recorded
    // scripts stay portable. Those are not names a PlayStation player sees on
    // their controller. Translate the leading A action and standalone B/C
    // actions. A later A can be an article -- `A START A NEW COMPANY` is the
    // important case -- while the shared footers never put a second A action
    // after the first. This keeps the prose intact and still gives this machine
    // its actual Cross, Circle and Triangle legend.
    static void control_text(
        const char* source, char* output, std::size_t capacity
    ) noexcept {
        if (output == nullptr || capacity == 0U) return;
        std::size_t at = 0;
        for (std::size_t i = 0; source != nullptr && source[i] != '\0'; ++i) {
            const char value = source[i];
            const bool token =
                (i == 0U || source[i - 1U] == ' ') &&
                (source[i + 1U] == '\0' || source[i + 1U] == ' ');
            const char* replacement = nullptr;
            // A leading abstract A is Confirm. The compact help/title copy
            // also aligns a later button token with two spaces; an ordinary
            // article has one, as in `A START A NEW COMPANY`, so it remains A.
            if (token && value == 'A' &&
                (i == 0U ||
                 (i >= 2U && source[i - 1U] == ' ' &&
                  source[i - 2U] == ' '))) {
                replacement = "X";
            }
            if (token && value == 'B') replacement = "O";
            if (token && value == 'C') replacement = "^";
            if (replacement == nullptr) {
                if (at + 1U < capacity) output[at++] = value;
                continue;
            }
            for (std::size_t j = 0; replacement[j] != '\0' &&
                                    at + 1U < capacity;
                 ++j) {
                output[at++] = replacement[j];
            }
        }
        output[at] = '\0';
    }

    [[nodiscard]] static std::uint16_t control_first_colour(
        const char* translated
    ) noexcept {
        if (translated == nullptr) return ink_colour;
        if ((translated[0] == 'X' || translated[0] == 'O' ||
             translated[0] == '^') &&
            (translated[1] == '\0' || translated[1] == ' ')) {
            return menu_gold_colour;
        }
        constexpr char start[] = "START";
        for (int index = 0; start[index] != '\0'; ++index) {
            if (translated[index] == '\0' ||
                translated[index] != start[index]) {
                return ink_colour;
            }
        }
        return translated[5] == '\0' || translated[5] == ' '
            ? menu_gold_colour : ink_colour;
    }

    [[nodiscard]] static bool inside_start_token(
        const char* text, std::size_t at
    ) noexcept {
        constexpr char start[] = "START";
        for (std::size_t offset = 0U; offset < 5U && offset <= at; ++offset) {
            const std::size_t first = at - offset;
            if (first > 0U && text[first - 1U] != ' ') continue;
            bool matches = true;
            for (std::size_t letter = 0U; letter < 5U; ++letter) {
                if (text[first + letter] != start[letter]) {
                    matches = false;
                    break;
                }
            }
            if (matches &&
                (text[first + 5U] == '\0' || text[first + 5U] == ' ')) {
                return true;
            }
        }
        return false;
    }

    void write_centred(int row, const char* value, int clut) {
        const int x = centred_x(value, 8, gpu::screen_width - 16);
        (void)write_pixels_with_clut(
            x, row * glyph, gpu::screen_width - 8 - x, value, clut
        );
    }

    void write_centred_in(
        int left, int top, int width, const char* value, int clut
    ) {
        const int x = centred_x(value, left, width);
        (void)write_pixels_with_clut(
            x, top, left + width - x, value, clut
        );
    }

    void write_controls_centred(int row, const char* value, int clut) {
        char translated[128]{};
        control_text(value, translated, sizeof translated);
        constexpr int content_left = 8;
        constexpr int content_width = gpu::screen_width - 16;
        const int full_width = text_width(translated);
        const bool shortened = full_width > content_width;
        const int dots = shortened ? text_width("...") : 0;
        const int available = content_width - dots;
        const int x = centred_x(
            translated, content_left,
            shortened ? content_width : content_width
        );
        int used = 0;
        for (std::size_t at = 0U; translated[at] != '\0';) {
            const font::Decoded decoded = font::decode(translated + at);
            if (decoded.bytes == 0U) break;
            const int advance = font::advance(decoded.codepoint);
            if (used + advance > available) break;
            const bool begins = at == 0U || translated[at - 1U] == ' ';
            const bool one = decoded.bytes == 1U &&
                (translated[at + 1U] == '\0' || translated[at + 1U] == ' ');
            std::uint32_t shown = decoded.codepoint;
            int shown_clut = clut;
            if (begins && one && decoded.codepoint == 'X') {
                shown = font::controller_cross;
                shown_clut = font_gold_clut;
            } else if (begins && one && decoded.codepoint == 'O') {
                shown = font::controller_circle;
                shown_clut = font_gold_clut;
            } else if (begins && one && decoded.codepoint == '^') {
                shown = font::controller_triangle;
                shown_clut = font_gold_clut;
            } else if (inside_start_token(translated, at)) {
                shown_clut = font_gold_clut;
            }
            if (shown != ' ') {
                gpu::draw_glyph(x + used, row * glyph, font::slot_for(shown),
                                shown_clut);
            }
            used += advance;
            at += decoded.bytes;
        }
        if (shortened) {
            for (int dot = 0; dot < 3; ++dot) {
                gpu::draw_glyph(
                    x + used, row * glyph, font::slot_for('.'), clut
                );
                used += font::advance('.');
            }
        }
    }

    [[nodiscard]] const char* screen_heading(
        turn::Screen screen
    ) const noexcept {
        switch (screen) {
            case turn::Screen::story:
                return ui_text::text(language(), ui_text::Message::story);
            case turn::Screen::company:
                return ui_text::text(language(), ui_text::Message::company);
            case turn::Screen::member:
                return ui_text::text(language(), ui_text::Message::inventory);
            case turn::Screen::name:
                return ui_text::text(
                    language(), ui_text::Message::character_name
                );
            case turn::Screen::aftermath:
                return ui_text::text(
                    language(), ui_text::Message::after_battle
                );
            case turn::Screen::refusal:
                return ui_text::text(language(), ui_text::Message::notice);
            case turn::Screen::joined:
                return ui_text::text(
                    language(), ui_text::Message::new_company_member
                );
            case turn::Screen::stages:
                return ui_text::text(language(), ui_text::Message::stages);
            case turn::Screen::ended:
                return ui_text::text(
                    language(), ui_text::Message::campaign_complete
                );
            case turn::Screen::none:
            case turn::Screen::title:
            case turn::Screen::help:
            case turn::Screen::slot: return "GRANDLEON";
        }
        return "GRANDLEON";
    }

    void draw_title_screen(const turn::ScreenView& view) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        const view::ConsolePage page = view::console_page(
            16, 12, 288, 216, 52, 24
        );
        view::paint_console_page(
            page,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        gpu::fill(24, 18, 272, 2, menu_gold_colour);
        gpu::fill(52, 70, 216, 1, menu_muted_colour);
        gpu::fill(52, 198, 216, 1, menu_muted_colour);
        const int mark_width = scaled_text_width("GRANDLEON", 16);
        write_scaled_at(
            (gpu::screen_width - mark_width) / 2, 30,
            "GRANDLEON", 16, font_gold_clut
        );
        if (view.page != nullptr && view.page->count > 2) {
            write_centred(11, view.page->line(2), font_gold_clut);
        }
        if (view.choices <= 0) {
            write_centred(
                14,
                ui_text::text(language(), ui_text::Message::campaign_tagline),
                font_muted_clut
            );
        } else {
            write_centred(
                14,
                ui_text::text(language(), ui_text::Message::choose_how_to_play),
                font_muted_clut
            );
            for (int choice = 0; choice < view.choices; ++choice) {
                const int page_row = view.first_choice_row + choice;
                if (view.page == nullptr || page_row >= view.page->count) break;
                const int y = 122 + choice * 24;
                const bool selected = page_row == view.caret_row;
                if (selected) {
                    view::paint_focus(
                        {58, y, 204, 18},
                        [](view::UiRect rectangle, view::UiTone tone) {
                            paint_console_ui(rectangle, tone);
                        }
                    );
                } else {
                    gpu::fill(58, y, 204, 18, menu_muted_colour);
                    gpu::fill(60, y + 2, 200, 14, paper_colour);
                }
                write_centred_in(
                    66, y + 5, 188, view.page->line(page_row),
                    selected ? font_gold_clut : font_clut
                );
            }
        }
        write_controls_centred(
            view.choices <= 0 ? 26 : 27, view.footer, font_clut
        );
    }

    void draw_help_screen(const turn::ScreenView& view) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        const view::ConsolePage page = view::console_page(
            16, 12, 288, 216, 40, 24
        );
        view::paint_console_page(
            page,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        gpu::fill(24, 14, 272, 2, menu_gold_colour);
        const int mark_width = scaled_text_width("GRANDLEON", 12);
        write_scaled_at(
            (gpu::screen_width - mark_width) / 2, 22,
            "GRANDLEON", 12, font_gold_clut
        );
        gpu::fill(28, 54, 264, 140, menu_muted_colour);
        gpu::fill(30, 56, 260, 136, paper_colour);
        if (view.page != nullptr) {
            for (int row = 0; row < view.page->count; ++row) {
                char translated[screen_cols + 1]{};
                control_text(
                    view.page->line(row), translated, sizeof translated
                );
                (void)write_pixels_with_clut(
                    48, (8 + row * 2) * glyph, 224, translated,
                    row == 0 ? font_gold_clut : font_clut
                );
            }
        }
        write_controls_centred(27, view.footer, font_clut);
    }

    void draw_slot_screen(const turn::ScreenView& view) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        const view::ConsolePage page = view::console_page(
            16, 12, 288, 216, 80, 24
        );
        view::paint_console_page(
            page,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        gpu::fill(24, 14, 272, 2, menu_gold_colour);
        const int mark_width = scaled_text_width("GRANDLEON", 12);
        write_scaled_at(
            (gpu::screen_width - mark_width) / 2, 22,
            "GRANDLEON", 12, font_gold_clut
        );
        if (view.page != nullptr && view.page->count > 0) {
            write_centred(7, view.page->line(0), font_gold_clut);
        }
        write_centred(
            10, ui_text::text(language(), ui_text::Message::choose_save),
            font_muted_clut
        );

        for (int choice = 0; choice < view.choices; ++choice) {
            const int page_row = view.first_choice_row + choice;
            if (view.page == nullptr || page_row >= view.page->count) break;
            const int row = 14 + choice * 2;
            const bool selected = page_row == view.caret_row;
            if (selected) {
                view::paint_focus(
                    {24, row * glyph - 2, 272, 12},
                    [](view::UiRect rectangle, view::UiTone tone) {
                        paint_console_ui(rectangle, tone);
                    }
                );
                write_at_with_clut(4, row, ">", font_gold_clut);
            }
            (void)write_pixels_with_clut(
                48, row * glyph, 240, view.page->line(page_row),
                selected ? font_gold_clut : font_clut
            );
        }
        write_centred(
            24,
            ui_text::text(language(), ui_text::Message::game_saves_itself),
            font_muted_clut
        );
        write_controls_centred(27, view.footer, font_clut);
    }

    // A campaign screen, drawn: the scene behind it, the panel the words sit
    // on, the words, the caret and the footer. Five `gpu::fill`s and a few
    // hundred glyphs, which is less than the board costs.
    void draw_screen(const turn::ScreenView& view) {
        if (view.screen == turn::Screen::title) {
            draw_title_screen(view);
            return;
        }
        if (view.screen == turn::Screen::help) {
            draw_help_screen(view);
            return;
        }
        if (view.screen == turn::Screen::slot) {
            draw_slot_screen(view);
            return;
        }
        draw_scene_backdrop(view.backdrop);
        draw_speaker_face(view);
        const int page_rows = view.page == nullptr
            ? 0 : view.page->count < turn::page_capacity
                ? view.page->count : turn::page_capacity;
        const view::ConsolePage page = view::console_page(
            2, screen_panel_top * glyph - 2, 316,
            glyph + page_rows * glyph + 4, glyph, 0
        );
        view::paint_console_page(
            page,
            [](view::UiRect rectangle, view::UiTone tone) {
                paint_console_ui(rectangle, tone);
            }
        );
        (void)write_pixels_with_clut(
            screen_page_left * glyph, screen_panel_top * glyph,
            296, screen_heading(view.screen), font_gold_clut
        );
        if (view.caret_row >= 0 && view.caret_row < turn::page_capacity) {
            view::paint_focus(
                {
                    page.body.x, (screen_page_top + view.caret_row) * glyph,
                    page.body.width, glyph,
                },
                [](view::UiRect rectangle, view::UiTone tone) {
                    paint_console_ui(rectangle, tone);
                }
            );
        }
        if (view.page != nullptr) {
            for (int row = 0; row < view.page->count &&
                              row < turn::page_capacity;
                 ++row) {
                (void)write_pixels_with_clut(
                    screen_page_left * glyph,
                    (screen_page_top + row) * glyph,
                    296, view.page->line(row), font_clut
                );
            }
        }
        // The caret is a glyph in a column of its own rather than a colour on
        // the row, for the reason the board's action menu gives: a caret costs
        // no palette entry on any machine, and two consoles that named a
        // selection differently would be two different menus.
        if (view.caret_row >= 0 && view.caret_row < turn::page_capacity) {
            write_at_with_clut(
                screen_caret_column, screen_page_top + view.caret_row, ">",
                font_gold_clut
            );
        }
        if (view.footer != nullptr && view.footer[0] != '\0') {
            const view::ConsolePage footer = view::console_page(
                2, screen_footer_row * glyph - glyph, 316, glyph * 3, 0, 0
            );
            view::paint_console_page(
                footer,
                [](view::UiRect rectangle, view::UiTone tone) {
                    paint_console_ui(rectangle, tone);
                }
            );
            write_controls_centred(screen_footer_row, view.footer, font_clut);
        }
    }

    // Who is talking, drawn over the scene and above the words they say.
    //
    // Nothing when the page names nobody, which is every screen that is not a
    // story page and every scene authored before a scene could cast its
    // speakers. Those pages draw exactly as they always drew, which is the
    // contract `ScreenView::speaker` states.
    //
    // It costs no new art and no VRAM: this executable already holds the
    // style's roster because the board draws units from it, and the cell and
    // CLUT were uploaded once at start-up alongside every other character.
    void draw_speaker_face(const turn::ScreenView& view) {
        if (view.speaker == nullptr) return;
        const int drawing = speaker_drawing_index(*view.speaker);
        if (drawing < 0) return;
        console::draw_character(
            drawing, art::Pose::idle,
            screen_face_left + screen_face_size / 2,
            screen_face_top + screen_face_size,
            screen_face_size, false
        );
    }

    // Flat horizontal bands out of the art library's own table, or the house
    // colour where a scene names none. A backdrop costs no texture and no VRAM
    // on any client, which is the whole point of drawing them as bands rather
    // than as a picture, and on this machine it costs six rectangles.
    void draw_scene_backdrop(std::uint8_t which) {
        if (!scene_has_backdrop(which)) {
            gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, backdrop);
            return;
        }
        const int index = which - 1;
        for (int band = 0; band < grandleon_backdrop_band_count[index]; ++band) {
            const auto& entry = grandleon_backdrop_bands[index][band];
            const int top = backdrop_row_top(entry[0]);
            const int bottom = backdrop_row_top(entry[0] + entry[1]);
            gpu::fill(
                0, top, gpu::screen_width, bottom - top,
                band_colour(entry[2], entry[3], entry[4])
            );
        }
    }
#endif

    void write_row(int row, const char* value) { write_at(0, row, value); }

    [[nodiscard]] static int text_width(const char* value) noexcept {
        return font::measure(value);
    }

    [[nodiscard]] static int scaled_text_width(
        const char* value, int size
    ) noexcept {
        int width = 0;
        for (std::size_t at = 0U;
             value != nullptr && value[at] != '\0';) {
            const font::Decoded decoded = font::decode(value + at);
            if (decoded.bytes == 0U) break;
            width += (font::advance(decoded.codepoint) * size + glyph - 1) /
                glyph;
            at += decoded.bytes;
        }
        return width;
    }

    // Draws within an exact pixel budget. If the authored or translated copy
    // does not fit, the final three dots are inside that same budget; no glyph
    // is allowed to wander into a meter, divider or frame and be hidden later.
    int write_pixels_with_clut(
        int x, int y, int max_width, const char* value, int clut,
        bool ellipsis = true
    ) {
        if (value == nullptr || clut < 0 || max_width <= 0) return 0;
        if (y < 0 || y + glyph > gpu::screen_height) return 0;
        const int dots_width = ellipsis ? text_width("...") : 0;
        const font::Fit fit = font::fit(value, max_width, dots_width);
        int used = 0;
        std::size_t at = 0U;
        while (at < fit.bytes) {
            const font::Decoded decoded = font::decode(value + at);
            if (decoded.bytes == 0U) break;
            const int advance = font::advance(decoded.codepoint);
            if (decoded.codepoint != ' ') {
                gpu::draw_glyph(
                    x + used, y, font::slot_for(decoded.codepoint), clut
                );
            }
            used += advance;
            at += decoded.bytes;
        }
        if (fit.truncated && ellipsis) {
            for (int dot = 0; dot < 3; ++dot) {
                gpu::draw_glyph(
                    x + used, y, font::slot_for('.'), clut
                );
                used += font::advance('.');
            }
        }
        return used;
    }

    // Grid coordinates remain useful for the shared page contract, but only
    // the anchor is a grid coordinate now. Every following glyph advances by
    // its real width and the display edge is an explicit clipping boundary.
    void write_at_with_clut(
        int column, int row, const char* value, int clut
    ) {
        if (row < 0 || row >= screen_rows) return;
        const int x = column * glyph;
        (void)write_pixels_with_clut(
            x, row * glyph, gpu::screen_width - x, value, clut
        );
    }

    void write_at(int column, int row, const char* value) {
        write_at_with_clut(column, row, value, font_clut);
    }

    void write_scaled_at(
        int x, int y, const char* value, int size, int clut
    ) {
        if (value == nullptr || clut < 0 || size <= 0) return;
        int used = 0;
        for (std::size_t at = 0U; value[at] != '\0';) {
            const font::Decoded decoded = font::decode(value + at);
            if (decoded.bytes == 0U) break;
            if (decoded.codepoint != ' ') {
                gpu::draw_glyph_scaled(
                    x + used, y, size, font::slot_for(decoded.codepoint), clut
                );
            }
            used += (font::advance(decoded.codepoint) * size + glyph - 1) /
                glyph;
            at += decoded.bytes;
        }
    }

    // ----- a word said during a fight --------------------------------------

    // How wide and how tall a bubble is, on the glyph grid this executable
    // writes text on. Narrower and shorter than the sheet the story pages use,
    // and deliberately: this one sits on the board with the fight still under
    // it, so it has to leave the fight visible and fit beside a character
    // standing anywhere on the screen.
    static constexpr int bubble_columns = 30;
    static constexpr int bubble_rows = 3;
    static constexpr int bubble_width = bubble_columns * glyph;
    static constexpr int bubble_row_bytes = 96;

    // One saying over the board, beside whoever is speaking.
    //
    // The full-screen page is right for a story node, which has the screen to
    // itself between battles. It is wrong for a word said during a fight: the
    // board is what the words are about, and a page hides it at the moment
    // somebody is talking about it. The client has already brought the camera
    // to the speaker, so the cell this is drawn against is on the screen.
    bool hold_saying(
        const sim::EncounterSnapshot& snapshot,
        const turn::Overlay& overlay,
        std::uint64_t speaker,
        const char* who,
        const char* what
    ) override {
        if (last_overlay_ == nullptr && held_ == nullptr) return false;

        // Where the speaker stands, if the scene cast somebody who is here. A
        // scene that cast nobody still gets its words; the bubble simply takes
        // the foot of the screen rather than a cell.
        int against_column = -1;
        int against_row = -1;
        if (speaker != 0) {
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (!sim::on_board(unit)) continue;
                if (unit.unit_type_id != speaker) continue;
                against_column =
                    cell_left(unit.position.x, unit.position.y) / glyph;
                against_row =
                    cell_top(unit.position.x, unit.position.y) / glyph;
                break;
            }
        }

        // Wrapped here rather than by the client, because how many columns fit
        // is this display's business and the two consoles have different ones.
        char rows[bubble_rows][bubble_row_bytes + 1] = {};
        int row_count = 0;
        int row_bytes = 0;
        int row_width = 0;
        for (int i = 0; what != nullptr && what[i] != '\0';) {
            // The next UTF-8 word, and whether its measured pixels still fit.
            int word_bytes = 0;
            int word_width = 0;
            while (what[i + word_bytes] != '\0' &&
                   what[i + word_bytes] != ' ') {
                const font::Decoded decoded = font::decode(
                    what + i + word_bytes
                );
                if (decoded.bytes == 0U) break;
                word_width += font::advance(decoded.codepoint);
                word_bytes += static_cast<int>(decoded.bytes);
            }
            const int spacing = row_bytes > 0 ? font::advance(' ') : 0;
            if (row_bytes > 0 && row_width + spacing + word_width > bubble_width) {
                if (++row_count >= bubble_rows) break;
                row_bytes = 0;
                row_width = 0;
            }
            if (row_bytes > 0 && row_bytes < bubble_row_bytes) {
                rows[row_count][row_bytes++] = ' ';
                row_width += font::advance(' ');
            }
            for (int c = 0; c < word_bytes && row_bytes < bubble_row_bytes; ++c) {
                rows[row_count][row_bytes++] = what[i + c];
            }
            row_width += word_width;
            i += word_bytes;
            while (what[i] == ' ') ++i;
        }
        if (row_bytes > 0) ++row_count;
        if (row_count == 0) row_count = 1;

        // Against the speaker, and never off the screen: above them when they
        // are low on the board and below when they are high, so the bubble does
        // not cover the character it is about.
        const int height = row_count + 1;
        int left = 1;
        int top = screen_rows - height - bar_rows - 1;
        if (against_column >= 0) {
            left = against_column - bubble_columns / 2;
            top = against_row > screen_rows / 2 ? against_row - height - 1
                                                : against_row + 2;
        }
        if (left < 0) left = 0;
        if (left + bubble_columns > screen_cols) {
            left = screen_cols - bubble_columns;
        }
        if (top < 1) top = 1;
        if (top + height > screen_rows - bar_rows) {
            top = screen_rows - bar_rows - height;
        }

        psx::wait_vblank();
        gpu::begin(paper_colour);
        draw(snapshot, overlay, false);
        gpu::fill(
            left * glyph - 2, top * glyph - 2, bubble_columns * glyph + 4,
            height * glyph + 4, ink_colour
        );
        gpu::fill(
            left * glyph, top * glyph, bubble_columns * glyph, height * glyph,
            paper_colour
        );
        (void)write_pixels_with_clut(
            left * glyph, top * glyph, bubble_width, who, font_gold_clut
        );
        for (int row = 0; row < row_count; ++row) {
            (void)write_pixels_with_clut(
                left * glyph, (top + 1 + row) * glyph,
                bubble_width, rows[row], font_clut
            );
        }
        gpu::show();
        next_press();
        return true;
    }

    // ----- what motion needs -----------------------------------------------

    // One leg of a slide: six frames a tile, and the last of them is the
    // destination exactly rather than a rounding error short of it.
    int slide_leg(
        const sim::EncounterSnapshot& before, int from_x, int from_y, int to_x,
        int to_y, int frames = view::slide_frames_per_tile
    ) {
        const int start_x = cell_left(from_x, from_y);
        const int start_y = cell_top(from_x, from_y);
        const int end_x = cell_left(to_x, to_y);
        const int end_y = cell_top(to_x, to_y);
        // A move of no tiles is a gesture of no frames, and its token still has
        // to be somewhere: `draw_list` draws the moving token out of these two
        // and the compose left it out of the list.
        move_px_ = end_x;
        move_py_ = end_y;
        move_world_x_ = to_x * 256 + 128;
        move_world_z_ = to_y * 256 + 128;
        const view::BoardHeading heading = view::board_heading_between(
            from_x, from_y, to_x, to_y
        );
        move_facing_ = view::board_heading_faces_left(
            heading, move_facing_ < 0
        ) ? -1 : 1;
        for (int frame = 1; frame <= frames; ++frame) {
            move_px_ = view::slide_between(start_x, end_x, frame, frames);
            move_py_ = view::slide_between(start_y, end_y, frame, frames);
            move_world_x_ = view::slide_between(
                from_x * 256 + 128, to_x * 256 + 128, frame, frames
            );
            move_world_z_ = view::slide_between(
                from_y * 256 + 128, to_y * 256 + 128, frame, frames
            );
            const int step = frame % 4;
            move_bob_ = step == 1 || step == 3 ? 3 : step == 2 ? 5 : 0;
            move_lean_ = step < 2 ? 10 : -10;
            psx::wait_vblank();
            draw(before, *last_overlay_, false);
        }
        move_bob_ = 0;
        move_lean_ = 0;
        return frames;
    }

    void report_animation(
        const char* what, int tiles, int frames, std::uint32_t opened
    ) {
        const std::uint32_t elapsed = psx::clock_ticks() - opened;
        // Multiplying first is exact and overflows a 32-bit product past a
        // second; a gesture longer than that divides first and loses the
        // fraction of a millisecond instead of the whole answer.
        const std::uint32_t microseconds =
            elapsed > 4000000u
                ? (elapsed / psx::ticks_per_1000_microseconds) * 1000u
                : elapsed * 1000u / psx::ticks_per_1000_microseconds;
        Line()
            .text("ANIM ")
            .text(what)
            .text(" tiles ")
            .decimal(static_cast<std::uint32_t>(tiles))
            .text(" frames ")
            .decimal(static_cast<std::uint32_t>(frames))
            .text(" us ")
            .decimal(microseconds)
            .flush();
    }

    // ----- the board's own arithmetic --------------------------------------

    [[nodiscard]] int cell_left(int x, int y) const {
        const int dx = x - camera().x;
        const int dy = y - camera().y;
        // `cell_top`/`cell_left` expose a square bounding box for unit
        // anchoring, menus and motion; the terrain painter uses the 2:1
        // diamond within it.
        return iso_origin_x_ + (dx - dy) * iso_half_width_ -
            drawn_tile() / 2;
    }
    [[nodiscard]] int cell_top(int x, int y) const {
        const int dx = x - camera().x;
        const int dy = y - camera().y;
        const int diamond_top = iso_origin_y_ +
            (dx + dy) * iso_half_height_ -
            isometric_lift(elevation_at(x, y));
        return diamond_top - (drawn_tile() - 2 * iso_half_height_);
    }
    [[nodiscard]] int elevation_at(int x, int y) const {
        return elevation_of_kind(kind_at(x, y));
    }
    [[nodiscard]] int kind_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= board_width_) return -1;
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(board_width_) +
            static_cast<std::size_t>(x);
        if (index >= terrain().size()) return -1;
        return kind_of(terrain()[index]);
    }
    [[nodiscard]] view::TerrainComposition terrain_composition_at(
        int x, int y
    ) const {
        int kind = kind_at(x, y);
        if (kind < 0) {
            kind = static_cast<int>(view::ConsoleTerrainKind::grass);
        }
        return view::compose_console_terrain(
            static_cast<std::uint8_t>(kind), x, y,
            kind_at(x, y - 1), kind_at(x + 1, y), kind_at(x, y + 1),
            kind_at(x - 1, y), elevation_at(x, y), elevation_at(x + 1, y),
            elevation_at(x, y + 1)
        );
    }
    [[nodiscard]] int kind_of(std::uint64_t identity) const {
        const std::uint8_t kind = shown_->kind_of_terrain(identity);
        return kind < art::terrain_kind_count ? static_cast<int>(kind) : -1;
    }
    [[nodiscard]] view::LandmarkCell landmark_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= board_width_) return {};
        const std::size_t index =
            static_cast<std::size_t>(y) *
                static_cast<std::size_t>(board_width_) +
            static_cast<std::size_t>(x);
        if (index >= terrain().size()) return {};
        const std::uint64_t identity = terrain()[index];
        const std::uint64_t left = x > 0 ? terrain()[index - 1U] : 0U;
        const std::uint64_t above = y > 0
            ? terrain()[index - static_cast<std::size_t>(board_width_)] : 0U;
        return view::landmark_cell(
            identity, left, above, ancient_grove_terrain,
            village_house_terrain
        );
    }
    [[nodiscard]] view::CastleTile castle_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= board_width_) return view::CastleTile::none;
        const std::size_t index =
            static_cast<std::size_t>(y) *
                static_cast<std::size_t>(board_width_) +
            static_cast<std::size_t>(x);
        if (index >= terrain().size()) return view::CastleTile::none;
        return view::castle_tile(
            terrain()[index], castle_wall_terrain, castle_door_terrain,
            castle_floor_terrain, castle_dais_terrain
        );
    }
    static void draw_landmark(
        int left, int top, int size, view::LandmarkCell cell
    ) {
        if (!cell) return;
        static constexpr std::uint16_t house[] = {
            gpu::rgb15(7, 4, 3), gpu::rgb15(27, 24, 17),
            gpu::rgb15(18, 7, 6), gpu::rgb15(12, 7, 4),
            gpu::rgb15(30, 26, 13),
        };
        static constexpr std::uint16_t grove[] = {
            gpu::rgb15(6, 5, 3), gpu::rgb15(6, 13, 7),
            gpu::rgb15(4, 9, 5), gpu::rgb15(13, 9, 5),
            gpu::rgb15(12, 18, 9),
        };
        view::paint_landmark_cell(
            cell, left, top, size,
            [cell](int x, int y, int width, int height,
                   view::LandmarkTone tone) {
                const std::size_t index = static_cast<std::size_t>(tone);
                const std::uint16_t colour =
                    cell.landmark == view::TacticalLandmark::village_house
                        ? house[index] : grove[index];
                gpu::fill(x, y, width, height, colour);
            }
        );
    }
    [[nodiscard]] int archetype_of(std::uint64_t unit_type) const {
        const std::uint8_t archetype = shown_->archetype_of_unit_type(unit_type);
        return archetype < art::archetype_count ? static_cast<int>(archetype) : 0;
    }
    [[nodiscard]] int style_of(std::uint64_t unit_type) const {
        const std::uint8_t style =
            shown_->character_style_of_unit_type(unit_type);
        return style == pr::character_style_unresolved ||
                       style >= art::style_count
                   ? art::project_default_style
                   : static_cast<int>(style);
    }
    [[nodiscard]] int figure_of(std::uint64_t unit_type) const {
        const std::uint8_t figure =
            shown_->character_figure_of_unit_type(unit_type);
        return figure == pr::character_figure_unresolved ||
                       figure >= art::figure_count
                   ? art::project_default_figure
                   : static_cast<int>(figure);
    }
    [[nodiscard]] int colour_of(const sim::UnitSnapshot& unit) const {
        const std::uint8_t colour = shown_->colour_of_unit_type(unit.unit_type_id);
        if (colour == pr::colour_unresolved || colour >= art::faction_colour_count) {
            return unit.side == sim::Side::first ? 0 : 1;
        }
        return static_cast<int>(colour);
    }
    [[nodiscard]] art::Asset unit_asset(const sim::UnitSnapshot& unit) const {
        return art::character(drawing_index_of(unit));
    }
    [[nodiscard]] int drawing_index_of(
        const sim::UnitSnapshot& unit
    ) const {
        return art::character_index(
            style_of(unit.unit_type_id), figure_of(unit.unit_type_id),
            archetype_of(unit.unit_type_id), colour_of(unit)
        );
    }
    // The colour a speaker's face is drawn in.
    //
    // A character belonging to no faction has no colour in the package on
    // purpose. On a board the client draws it in the column of whichever side
    // it turned up on, which is what `colour_of` above does with the snapshot
    // it has; a speaker stands on no board and there is no side to ask, so it
    // takes the first column. That is the same answer the cartridge gives, and
    // the same one an uncast speaker gets there.
    [[nodiscard]] int speaker_colour_of(std::uint64_t unit_type) const {
        const std::uint8_t colour = shown_->colour_of_unit_type(unit_type);
        if (colour == pr::colour_unresolved ||
            colour >= art::faction_colour_count) {
            return 0;
        }
        return static_cast<int>(colour);
    }
    // The drawing the scene cast for this page's saying, or nothing when it
    // cast nobody. The same `art::character` grid the board draws units from,
    // so a speaker and the figure of that same character standing on a board
    // are one drawing rather than two that have to agree.
    [[nodiscard]] art::Asset speaker_asset(std::uint64_t unit_type) const {
        return art::character(speaker_drawing_index(unit_type));
    }
    [[nodiscard]] int speaker_drawing_index(std::uint64_t unit_type) const {
        return art::character_index(
            style_of(unit_type), figure_of(unit_type), archetype_of(unit_type),
            speaker_colour_of(unit_type)
        );
    }
    [[nodiscard]] static const sim::UnitSnapshot* unit_of(
        const sim::EncounterSnapshot& snapshot, sim::UnitId unit
    ) {
        for (const sim::UnitSnapshot& candidate : snapshot.units) {
            if (candidate.id == unit) return &candidate;
        }
        return nullptr;
    }
    [[nodiscard]] static bool occupied(
        const sim::EncounterSnapshot& snapshot, int x, int y
    ) {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) && unit.position.x == x &&
                unit.position.y == y) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] MenuBox menu_box(const turn::Overlay& overlay) const {
        MenuBox box;
        if (overlay.menu == nullptr || overlay.menu_rows <= 0) return box;
        int widest = 0;
        for (int row = 0; row < overlay.menu_rows; ++row) {
            const char* label = overlay.menu[row].label;
            const int width = font::measure(label);
            if (width > widest) widest = width;
        }
        const int cursor_centre =
            cell_left(overlay.cursor_x, overlay.cursor_y) + drawn_tile() / 2;
        const layout::Menu sized =
            layout::menu_at(widest, overlay.menu_rows, 0, 0);
        const int width = sized.panel.outer.width;
        const int height = sized.panel.outer.height;
        const int opposite_left = cursor_centre < gpu::screen_width / 2
            ? gpu::screen_width - 16 - width : 16;
        const int same_left = opposite_left == 16
            ? gpu::screen_width - 16 - width : 16;
        int low_top = bar_top - height - 6;
        if (low_top < 6) low_top = 6;
        const int candidate_left[4] = {
            opposite_left, opposite_left, same_left, same_left
        };
        const int candidate_top[4] = {low_top, 6, 6, low_top};
        int best = 0;
        int best_score = 0x7fffffff;
        for (int candidate = 0; candidate < 4; ++candidate) {
            const view::UiRect panel{
                candidate_left[candidate], candidate_top[candidate],
                width, height
            };
            int score = 0;
            view::UiRect cursor_rect{
                cell_left(overlay.cursor_x, overlay.cursor_y),
                cell_top(overlay.cursor_x, overlay.cursor_y),
                drawn_tile(), drawn_tile()
            };
            cursor_rect.y = isometric_diamond_top(
                overlay.cursor_x, overlay.cursor_y,
                elevation_at(overlay.cursor_x, overlay.cursor_y)
            );
            cursor_rect.height = iso_half_height_ * 2;
            if (layout::overlaps(panel, cursor_rect)) score += 10000;
            if (held_ != nullptr) {
                const int unit_size = drawn_unit_size();
                for (const sim::UnitSnapshot& unit : held_->units) {
                    if (!sim::on_board(unit) ||
                        !camera().visible(unit.position.x, unit.position.y)) {
                        continue;
                    }
                    const int cell_x =
                        cell_left(unit.position.x, unit.position.y);
                    const int cell_y =
                        cell_top(unit.position.x, unit.position.y);
                    const view::UiRect unit_rect{
                        cell_x + drawn_tile() / 2 - unit_size / 2,
                        cell_y + drawn_tile() - unit_size,
                        unit_size, unit_size
                    };
                    if (layout::overlaps(panel, unit_rect)) score += 1000;
                }
            }
            if (score < best_score) {
                best = candidate;
                best_score = score;
            }
        }
        const layout::Menu placed = layout::menu_at(
            widest, overlay.menu_rows,
            candidate_left[best], candidate_top[best]
        );
        box.panel = placed.panel;
        box.heading_x = placed.heading_x;
        box.heading_y = placed.heading_y;
        box.row_text_x = placed.row_text_x;
        box.row_text_y = placed.row_text_y;
        box.footer_text_x = placed.footer_text_x;
        box.footer_text_y = placed.footer_text_y;
        return box;
    }

    // The first ink pixel of a glyph, computed out of the font this executable
    // uploaded rather than looked for in the frame. A claim about a pixel the
    // letter itself sets is a claim no blank box can satisfy.
    void claim_glyph_at_pixel(
        const char* label, int at_x, int at_y, std::uint32_t codepoint,
        std::uint16_t colour = ink_colour
    ) {
        const int slot = font::slot_for(codepoint);
        for (int y = 0; y < gpu::glyph_texels; ++y) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                if (font::texel(slot, x, y) != 2U) continue;
                probe(label, at_x + x, at_y + y, colour);
                return;
            }
        }
        // A glyph with no ink at all is a space, and the already-painted
        // surface is the whole of what there is to claim about one.
        probe(label, at_x, at_y, paper_colour);
    }

    void claim_glyph(
        const char* label, int column, int row, char value,
        std::uint16_t colour = ink_colour
    ) {
        claim_glyph_at_pixel(
            label, column * glyph, row * glyph,
            static_cast<unsigned char>(value), colour
        );
    }

    // One pixel inside a glyph cell that the glyph itself does not draw. This
    // is the pixel that catches an opaque font background: on a focused row it
    // must remain the row's colour rather than changing back to paper around
    // every letter.
    void claim_glyph_background_at_pixel(
        const char* label, int at_x, int at_y, std::uint32_t codepoint,
        std::uint16_t background
    ) {
        const int slot = font::slot_for(codepoint);
        for (int y = 0; y < gpu::glyph_texels; ++y) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                if (font::texel(slot, x, y) != 0U) continue;
                probe(label, at_x + x, at_y + y, background);
                return;
            }
        }
    }

    void claim_glyph_background(
        const char* label, int column, int row, char value,
        std::uint16_t background
    ) {
        claim_glyph_background_at_pixel(
            label, column * glyph, row * glyph,
            static_cast<unsigned char>(value), background
        );
    }

    void claim_first_glyph(const char* label, int row, const char* text) {
        claim_first_glyph_at(label, 0, row, text);
    }

    // The same, for text that does not start in column zero. A campaign page
    // is inset by the caret's own column, so its first letter is not at the
    // left edge and a claim that assumed it was would be a claim about the
    // wrong pixel.
    void claim_first_glyph_at(
        const char* label, int column, int row, const char* text,
        std::uint16_t colour = ink_colour
    ) {
        if (text == nullptr) return;
        int used = 0;
        for (std::size_t at = 0U; text[at] != '\0';) {
            const font::Decoded decoded = font::decode(text + at);
            if (decoded.bytes == 0U) break;
            if (column * glyph + used >= gpu::screen_width) return;
            if (decoded.codepoint == ' ') {
                used += font::advance(decoded.codepoint);
                at += decoded.bytes;
                continue;
            }
            claim_glyph_at_pixel(
                label, column * glyph + used, row * glyph,
                decoded.codepoint, colour
            );
            return;
        }
    }

    void claim_first_glyph_at_pixel(
        const char* label, int x, int y, const char* text,
        std::uint16_t colour = ink_colour
    ) {
        if (text == nullptr) return;
        int used = 0;
        for (std::size_t at = 0U; text[at] != '\0';) {
            const font::Decoded decoded = font::decode(text + at);
            if (decoded.bytes == 0U) break;
            if (decoded.codepoint == ' ') {
                used += font::advance(decoded.codepoint);
                at += decoded.bytes;
                continue;
            }
            claim_glyph_at_pixel(
                label, x + used, y, decoded.codepoint, colour
            );
            return;
        }
    }

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    void claim_first_control_glyph(
        const char* label, int row, const char* translated
    ) {
        if (translated == nullptr || translated[0] == '\0') return;
        const int x = centred_x(translated, 8, gpu::screen_width - 16);
        const font::Decoded decoded = font::decode(translated);
        std::uint32_t shown = decoded.codepoint;
        if (decoded.codepoint == 'X' &&
            (translated[1] == '\0' || translated[1] == ' ')) {
            shown = font::controller_cross;
        } else if (decoded.codepoint == 'O' &&
                   (translated[1] == '\0' || translated[1] == ' ')) {
            shown = font::controller_circle;
        } else if (decoded.codepoint == '^' &&
                   (translated[1] == '\0' || translated[1] == ' ')) {
            shown = font::controller_triangle;
        }
        claim_glyph_at_pixel(
            label, x, row * glyph, shown, control_first_colour(translated)
        );
    }
#endif

    // What this board is drawn at. The shared client works it out from the
    // fit rule when the battle opens, so this asks rather than deciding: a
    // renderer that decided for itself would be the second answer the fit rule
    // exists to prevent.
    [[nodiscard]] int drawn_tile() const noexcept {
        const int given = tile();
        return given > 0 ? given : native_tile;
    }

    const pr::Presentation* shown_;
    int theme_ = 0;
    int board_width_ = 0;
    int iso_origin_x_{gpu::screen_width / 2};
    int iso_origin_y_{native_tile / 2};
    int iso_half_width_{native_tile / 2};
    int iso_half_height_{native_tile / 4};
    int iso_elevation_step_{5};

    const sim::EncounterSnapshot* held_ = nullptr;
    const turn::Overlay* last_overlay_ = nullptr;

    std::size_t consumed_ = 0;
    bool steered_ = false;
    std::uint16_t pending_buttons_[2]{};
    std::uint8_t pad_priority_{0};
    view::HeldDirectionRepeat held_repeats_[2]{};
    view::ConsoleDisplayOptions display_options_{};
    std::uint32_t fps_window_started_{0};
    std::uint32_t fps_frames_{0};
    std::uint32_t fps_value_{0};

    sim::UnitId moving_ = 0;
    int move_px_ = 0;
    int move_py_ = 0;
    int move_world_x_ = 0;
    int move_world_z_ = 0;
    int move_bob_ = 0;
    int move_facing_ = 1;
    int move_lean_ = 0;
    sim::UnitId flinching_ = 0;
    sim::UnitId striking_ = 0;
    art::Pose strike_pose_ = art::Pose::idle;
    int flinch_dx_ = 0;
    int flinch_dy_ = 0;
    int flash_x_ = -1;
    int flash_y_ = -1;
    bool flash_lit_ = false;
    view::TerrainComposition close_terrain_{};

    std::uint32_t paints_ = 0;
    std::uint32_t paint_microseconds_ = 0;
    std::uint32_t paint_shortest_ = 0xFFFFFFFFu;
    std::uint32_t paint_longest_ = 0;

public:
    [[nodiscard]] std::size_t consumed() const noexcept { return consumed_; }
    [[nodiscard]] bool steered() const noexcept { return steered_; }
    [[nodiscard]] std::uint32_t paints() const noexcept { return paints_; }
    [[nodiscard]] std::uint32_t paint_mean_microseconds() const noexcept {
        return paints_ == 0 ? 0 : paint_microseconds_ / paints_;
    }
    [[nodiscard]] std::uint32_t paint_shortest_microseconds() const noexcept {
        return paints_ == 0 ? 0 : paint_shortest_;
    }
    [[nodiscard]] std::uint32_t paint_longest_microseconds() const noexcept {
        return paint_longest_;
    }
};

}  // namespace

int main() {
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    Line().text("GRANDLEON PLAYSTATION CAMPAIGN ").text(campaign_path).flush();
#else
    Line().text("GRANDLEON PLAYSTATION TURN").flush();
#endif

    static_assert(
        grandleon_terrain_kind_count == art::terrain_kind_count,
        "the art library's terrain registry and its PlayStation header disagree"
    );
    static_assert(
        font::glyph_count <= gpu::font_capacity,
        "the owned Latin/controller font does not fit its VRAM atlas"
    );

    {
        constexpr std::uint32_t required[] = {
            'A', 'Z', '0', '9', font::inverted_exclamation,
            font::inverted_question, 0x00C1U, 0x00C9U, 0x00CDU, 0x00D1U,
            0x00D3U, 0x00DAU, 0x00DCU, 0x00C7U, 0x00C3U, 0x00D5U,
            0x00E1U, 0x00E9U, 0x00EDU, 0x00F1U, 0x00F3U, 0x00FAU,
            0x00FCU, 0x00E7U, 0x00E3U, 0x00F5U,
            font::controller_cross, font::controller_circle,
            font::controller_triangle, font::controller_square,
        };
        bool coverage = true;
        for (std::uint32_t codepoint : required) {
            if (!font::supports(codepoint)) coverage = false;
        }
        expect(
            coverage,
            "the UI font covers EN ES PT-BR and every PlayStation mark"
        );
        expect(
            font::measure(u8"AÇÃO  ¿MOVER?  NIÑO  △○×□") > 0,
            "UTF-8 text has real proportional pixel metrics"
        );
        const int dots = font::measure("...");
        const font::Fit clipped = font::fit(
            "A VERY LONG TRANSLATED LABEL", 48, dots
        );
        expect(
            clipped.truncated && clipped.width + dots <= 48,
            "bounded labels reserve ellipsis without crossing their panel"
        );
    }

    psx::start_clock();
    pad::begin();
    const auto loaded = [] {
        const std::vector<std::uint8_t> package_bytes(
            campaign_package_bytes,
            campaign_package_bytes + campaign_package_size
        );
        pf::LoadOptions options{};
        options.engine_version = {0, 1, 0};
        options.target = pf::TargetProfile::portable;
        options.supported_features = 0;
        options.maximum_sections = 32;
        options.maximum_records_per_section = 4096;
        options.maximum_units_per_encounter =
            pr::target_budget::playstation_units_per_encounter;
        return pf::load_mock_package(package_bytes, options);
    }();
    expect(loaded.error == pf::Error::none, "the compiled package opens");
    if (loaded.error != pf::Error::none) give_up();

    std::uint8_t local_players = 1U;
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    turn::TurnClient::LaunchMenu launch_menu;
#endif
    bool second_controller_connected = false;
#ifndef GRANDLEON_PSX_AUTOPILOT
    // Presence is learned by a real poll, not by an answer counter left at
    // zero after pad::begin(). Consuming an edge here also prevents a button
    // held during boot from becoming the first board command.
    (void)pad::pressed(pad::Port::two);
    second_controller_connected = pad::answers(pad::Port::two) > 0;
#endif
    const auto launched_campaign = pr::load_campaign(
        loaded.package, core::stable_content_id_v1(campaign_path)
    );
    expect(
        static_cast<bool>(launched_campaign),
        "the campaign launch policy loads"
    );
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    launch_menu.offer = true;
    launch_menu.second_controller_connected = second_controller_connected;
#endif
    if (launched_campaign) {
        switch (launched_campaign.definition.player_mode) {
            case pr::LocalPlayerMode::one:
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
                launch_menu.allow_one = true;
                launch_menu.allow_two = false;
#else
                local_players = 1U;
#endif
                break;
            case pr::LocalPlayerMode::two:
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
                launch_menu.allow_one = false;
                launch_menu.allow_two = true;
#else
                expect(
                    second_controller_connected,
                    "the two-player campaign has controller two"
                );
                if (!second_controller_connected) give_up();
                local_players = 2U;
#endif
                break;
            case pr::LocalPlayerMode::choice:
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
                launch_menu.allow_one = true;
                launch_menu.allow_two = true;
#else
                local_players = second_controller_connected ? 2U : 1U;
#endif
                break;
        }
    }

    const auto presentation = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(presentation), "the presentation section reads");

    const int theme = presentation.presentation.theme < art::theme_count
                          ? presentation.presentation.theme
                          : 0;

    // Nothing the art library holds, and nothing the interface draws, may be
    // the backdrop, or a pixel nobody drew would be indistinguishable from a
    // pixel somebody drew.
    {
        bool separated = true;
        for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
            for (int variant = 0; variant < art::variant_count; ++variant) {
                const art::Asset asset = art::terrain(theme, kind, variant);
                for (int i = 0; i < art::clut_size; ++i) {
                    if (asset.clut[i] == backdrop) separated = false;
                }
            }
        }
        for (int drawing = 0; drawing < art::character_count; ++drawing) {
            const art::Asset asset = art::character(drawing);
            for (int i = 0; i < art::clut_size; ++i) {
                if (asset.clut[i] == backdrop) separated = false;
            }
        }
        const std::uint16_t interface_colours[] = {
            ink_colour, paper_colour, cursor_colour, second_cursor_colour,
            move_colour, aim_colour, danger_colour, flash_colour,
            menu_gold_colour, menu_muted_colour
        };
        for (std::uint16_t colour : interface_colours) {
            if (colour == backdrop) separated = false;
            // Paper has to be opaque, and a CLUT word of zero is what the GPU
            // skips. Checked here rather than asserted in a comment.
            if (colour == 0) separated = false;
        }
        expect(separated, "no colour the board or the interface draws is the backdrop");
    }

    gpu::begin(backdrop);

    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        terrain_clut_of[kind] = -1;
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell_of[kind][variant] = -1;
        }
    }
    // Terrain and every idle drawing are resident before the first press.
    // Animation poses use two reserved streaming slots owned by the production
    // console renderer.
    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        const art::Asset first = art::terrain(theme, kind, 0);
        std::uint16_t tinted_clut[art::clut_size]{};
        for (int entry = 0; entry < art::clut_size; ++entry) {
            tinted_clut[entry] = terrain_clut_colour(first.clut[entry], kind);
        }
        terrain_clut_of[kind] = claim_clut(tinted_clut);
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell_of[kind][variant] =
                claim_cell(art::terrain(theme, kind, variant).texels);
        }
    }
    expect(
        console::initialize(),
        "the project-specific Godot-derived character atlas fits in VRAM"
    );
    build_font();
    build_font_clut();

    expect(cells_fit, "every cell the board needs fits in the texture pages");
    expect(cluts_fit, "every CLUT the board needs fits in the CLUT band");
    expect(
        font_clut >= 0 && font_gold_clut >= 0 && font_muted_clut >= 0,
        "the interface fonts have CLUTs"
    );

    Line vram_report;
    vram_report.text("VRAM cells ")
        .decimal(static_cast<std::uint32_t>(cells_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::cell_capacity))
        .text("; cluts ")
        .decimal(static_cast<std::uint32_t>(cluts_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::clut_capacity))
        .text("; font ")
        .decimal(static_cast<std::uint32_t>(gpu::font_halfwords_across * gpu::font_lines))
        .text(" halfwords");
    vram_report.flush();

    gpu::show();
    const std::uint32_t gpu_status = gpu::status();
    expect(
        (gpu_status & gpu::status_display_disabled) == 0,
        "the GPU says the display is enabled"
    );
    Line()
        .text("DISPLAY ")
        .decimal(static_cast<std::uint32_t>(gpu::screen_width))
        .text("x")
        .decimal(static_cast<std::uint32_t>(gpu::screen_height))
        .text(" gpustat ")
        .hex64(gpu_status)
        .flush();
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // ----- the card, and the script it decides -----------------------------
    //
    // Before the layout line, because the layout line reports how long the
    // script is and the card is what chooses between the two of them.
    //
    // Static for the reason the client is: the window shadows sixteen
    // kilobytes of the card, and the stack reserve is thirty-two.
    static card::MemoryCardWindow card_window;
    Line()
        .text("CARD present ")
        .decimal(card_window.present() ? 1u : 0u)
        .text(" fault ")
        .text(card::fault_name(card_window.fault()))
        .text(" bytes ")
        .decimal(static_cast<std::uint32_t>(card_window.size()))
        .text(" block ")
        .decimal(static_cast<std::uint32_t>(card_window.first_block()))
        .text(" adopted ")
        .decimal(card_window.adopted() ? 1u : 0u)
        .flush();
    expect(card_window.present(), "a memory card answered on port one");

    static storage::ByteWindowSlotStorage device(
        card_window, card::card_budget()
    );
    expect(device.available(), "the slot device accepted the card");

    // Which slots the card is already holding a campaign in. Asked of the
    // device rather than remembered, because the answer is a fact about a
    // card the player may have brought from another machine.
    bool holds[view::slot_menu_rows] = {};
    for (int row = 0; row < view::slot_menu_rows; ++row) {
        char name[view::slot_menu_name_size] = {};
        view::slot_name_at(campaign_slot_base, row, name, sizeof name);
        holds[row] = device.contains(name);
    }

    // Whether this run is picking a campaign up rather than founding one.
    // `holds[0]` is the first slot, which is the row the caret opens on and the
    // row a card with one campaign on it always uses. It is a fact about the
    // card rather than about this build, and both builds' end-of-run
    // assertions turn on it.
    const bool resuming = holds[0];

#ifdef GRANDLEON_PSX_AUTOPILOT
    // Which script plays, decided by the card and by nothing this executable
    // carries.
    if (resuming) {
        script_presses = resume_presses;
        script_press_count = resume_press_count;
    }
    Line()
        .text("SCRIPT ")
        .text(resuming ? "resume" : "found")
        .text(" presses ")
        .decimal(static_cast<std::uint32_t>(script_press_count))
        .flush();
#endif

#endif

    // How long the script is, and zero where there is not one. A played build
    // reports the same line with a zero in it rather than a shorter line, so
    // one reader parses both and the difference is a number rather than a
    // missing field.
    Line()
        .text("LAYOUT tile ")
        .decimal(static_cast<std::uint32_t>(native_tile))
        .text("..")
        .decimal(static_cast<std::uint32_t>(turn::board_fit.smallest_tile))
        .text(" view ")
        .decimal(static_cast<std::uint32_t>(view_cols))
        .text("x")
        .decimal(static_cast<std::uint32_t>(view_rows))
        .text("..")
        .decimal(static_cast<std::uint32_t>(widest_view))
        .text("x")
        .decimal(static_cast<std::uint32_t>(tallest_view))
        .text(" bar ")
        .decimal(static_cast<std::uint32_t>(bar_top))
        .text(" script ")
#ifdef GRANDLEON_PSX_AUTOPILOT
        .decimal(static_cast<std::uint32_t>(script_press_count))
#else
        .decimal(0u)
#endif
        .flush();

    static TeletypeSink sink;
    // The client lives in `.bss` rather than in `main`'s frame. It holds a
    // whole `EncounterSnapshot`, three definition registries and a sheet, and a
    // machine that measures its own heap should not be surprised by its own
    // stack. A function-local static gets there without a placement new: this
    // toolchain's `cxxglue.c` supplies `__cxa_guard_acquire` and
    // `__cxa_guard_release`, which is exactly what one costs.
    static PlayStationClient game(sink, presentation.presentation);
    // Fitted rather than windowed: the client works the cell and the window
    // out per board from this rule, and the host derivation is handed the same
    // one. `set_viewport` is what a platform calls when its cell cannot move.
    game.set_fit_rule(turn::board_fit);
    // The cartridge's own package, so every name this client draws is the name
    // its author wrote rather than the shipped table's guess at it.
    game.set_package(&loaded.package);

    const std::uint64_t campaign_id = core::stable_content_id_v1(campaign_path);
    game.set_campaign_id(campaign_id);
    const std::uint32_t started = psx::clock_ticks();

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // ----- the campaign ----------------------------------------------------
    //
    // A campaign is a battle with the rest of the game around it: a title, a
    // slot to keep it in, the company between maps, an aftermath, and a save
    // the machine can be switched off in front of. Every one of those screens
    // is composed by the shared client and placed by `paint_screen` above;
    // what the block above added is the device the campaign is kept on, and
    // what this one adds is the question a player answers before it starts.
    const turn::TurnClient::SlotChoice chosen = game.open_campaign(
        pr::project_title(loaded.package), campaign_slot_base, holds,
        view::slot_menu_rows, launch_menu
    );
    local_players = chosen.local_players;
    Line()
        .text("SLOT ")
        .text(chosen.slot)
        .text(chosen.resume ? " resume" : " fresh")
        .flush();

    client::CampaignSessionOptions options;
    options.slot = chosen.slot;
    options.resume = chosen.resume;
    options.player_side = sim::Side::first;
    options.local_players = local_players;
    const client::CampaignSessionError outcome = client::run_persistent_campaign(
        loaded.package, campaign_id, game, game, device, options
    );
    const bool ran = outcome == client::CampaignSessionError::none;
    const std::uint32_t elapsed = psx::clock_ticks() - started;

    Line()
        .text("CAMPAIGN screens ")
        .decimal(static_cast<std::uint32_t>(game.screens()))
        .text(" battles ")
        .decimal(static_cast<std::uint32_t>(game.battles()))
        .text(" commits ")
        .decimal(static_cast<std::uint32_t>(game.commits()))
        .text(" saves ")
        .decimal(static_cast<std::uint32_t>(game.saves()))
        .text(" failed ")
        .decimal(static_cast<std::uint32_t>(game.save_failures()))
        .text(" resumed ")
        .decimal(game.resumed() ? 1u : 0u)
        .flush();
    Line()
        .text("CARD frames read ")
        .decimal(card_window.frames_read())
        .text(" written ")
        .decimal(card_window.frames_written())
        .text(" commits ")
        .decimal(card_window.commits())
        .text(" us ")
        .decimal(card_window.microseconds())
        .flush();

    // What a pass has to have done, and the two passes are asked different
    // things because they are for different things. A founding pass has to
    // reach a battle and leave the campaign on the card; a resuming one has to
    // find what was left and must not have had to write anything to find it.
    // Asserting the founding list of a resuming pass would be asserting that
    // the save did not work.
    expect(ran, "the campaign ran to its end");
    expect(game.screens() > 0, "and put a screen on the display");
    expect(game.save_failures() == 0, "with no save the card refused");
    if (resuming) {
        expect(game.resumed(), "the campaign came back off the card");
        expect(
            card_window.frames_written() == 0,
            "and not one frame was written to find it"
        );
    } else {
        expect(!game.resumed(), "an empty card founded a campaign");
        expect(game.battles() > 0, "and it fought at least one battle");
        expect(game.saves() > 0, "and wrote the campaign to the card");
        expect(game.checkpoints() > 0, "and settled on a board somewhere");
    }
    // The proof the bytes left the machine, and the same argument
    // `psx_card.h` makes about `reload`: every commit's answer is about a
    // transfer, so the region is fetched back over the copy it was written
    // from, after a scribble that makes a silent no-op look like the empty
    // device it would be.
    card_window.reload();
    storage::ByteWindowSlotStorage after(
        card_window, card::card_budget()
    );
    expect(
        after.was_formatted(), "the region read back off the card parses"
    );
    expect(
        after.contains(chosen.slot), "and holds the slot the campaign chose"
    );
    Line()
        .text("CARD held ")
        .decimal(static_cast<std::uint32_t>(storage::used_bytes(after)))
        .text(" bytes")
        .flush();
#else
    client::SessionOptions session_options;
    session_options.local_players = local_players;
    const client::SessionError status = client::run_campaign(
        loaded.package, campaign_id, sim::Side::first, game, session_options
    );
    const std::uint32_t elapsed = psx::clock_ticks() - started;

    expect(status == client::SessionError::none, "the session ran to its end");
#endif

#ifndef GRANDLEON_TURN_CLIENT_CAMPAIGN
    expect(game.checkpoints() > 0, "the run settled somewhere");
#endif
    expect(game.failures() == 0, "every check the client made passed");
#ifdef GRANDLEON_PSX_AUTOPILOT
    expect(
        game.steered() || game.consumed() == script_press_count,
        "the script was played to its last press"
    );
#endif

    checks += game.checks();
    failures += game.failures();

    Line()
        .text("SESSION checkpoints ")
        .decimal(static_cast<std::uint32_t>(game.checkpoints()))
        .text(" presses ")
        .decimal(static_cast<std::uint32_t>(game.consumed()))
        .text(" frames ")
        .decimal(psx::vblank_waits())
        .text(" retraces ")
        .decimal(psx::vblank_retraces())
        .text(" ms ")
        .decimal(elapsed / psx::ticks_per_1000_microseconds)
        .flush();
    Line()
        .text("FRAME paints ")
        .decimal(game.paints())
        .text(" us mean ")
        .decimal(game.paint_mean_microseconds())
        .text(" min ")
        .decimal(game.paint_shortest_microseconds())
        .text(" max ")
        .decimal(game.paint_longest_microseconds())
        .text(" budget 16667")
        .flush();
    Line()
        .text("PAD polls ")
        .decimal(pad::polls())
        .text(" answered ")
        .decimal(pad::answers())
        .text(" id ")
        .hex64(pad::identifier())
        .text(" us mean ")
        .decimal(pad::polls() == 0 ? 0u : pad::total_microseconds() / pad::polls())
        .text(" min ")
        .decimal(pad::shortest_microseconds())
        .text(" max ")
        .decimal(pad::longest_microseconds())
        .text(" acks ")
        .decimal(pad::acknowledgements())
        .text(" of ")
        .decimal(pad::acknowledgement_waits())
        .flush();

    const psx::HeapCensus census = psx::heap_census();
    Line()
        .text("HEAP capacity ")
        .decimal(psx::heap_capacity())
        .text(" free ")
        .decimal(census.free_bytes)
        .text(" peak ")
        .decimal(census.peak_allocated_bytes)
        .text(" live ")
        .decimal(census.allocated_bytes)
        .text(" allocations ")
        .decimal(census.allocations)
        .flush();

    Line()
        .text("PROBE-TOTAL ")
        .decimal(static_cast<std::uint32_t>(probes))
        .flush();
    Line()
        .text("RESULT ")
        .text(failures == 0 ? "PASS " : "FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/")
        .decimal(static_cast<std::uint32_t>(checks))
        .flush();

    // The run is over. The observer writes its verdict here rather than on any
    // emulator event, because the events this emulator publishes are about the
    // machine and none of them means "the program has finished photographing
    // itself". With no observer attached this is a write to a port nothing
    // reads.
    psx::signal_host(harness_done_slot);

    return failures == 0 ? 0 : 1;
}
