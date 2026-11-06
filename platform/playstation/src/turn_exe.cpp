// SPDX-License-Identifier: MIT
// The PlayStation turn executable: a board, played.
//
// `src/play_exe.cpp` opens on the board the content authored and holds it.
// This one is steered. The cursor, the selection, the engine's reachable and
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
//   * **A font.** This console has none; the sixty-four glyphs are
//     `grandleon/view/glyphs.hpp`'s and what this file
//     does is expand them, once, into a 4bpp sheet in the corner of VRAM that
//     the framebuffer and the CLUT band between them leave empty.
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

#include <grandleon/client/session.hpp>
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
#include <grandleon/view/glyphs.hpp>
#include <grandleon/view/motion.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef GRANDLEON_PSX_3D
#include <algorithm>
#endif

#include "psx_art.h"
#include "psx_gpu.h"
#include "psx_pad.h"
#include "psx_runtime.h"
#include "themes.h"

#ifdef GRANDLEON_PSX_3D
#include "sevenfold_campaign_proxies.h"
#endif

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

namespace psx = grandleon::playstation;
namespace art = grandleon::playstation::art;
namespace gpu = grandleon::playstation::gpu;
namespace pad = grandleon::playstation::pad;
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

// The backdrop, and why it is this colour: nothing the art library can draw is
// (31, 0, 31), so a probe that comes back as the backdrop is a pixel nothing
// drew rather than a pixel drawn in a colour that happens to match.
// `play_exe.cpp` checks that separation against the art and so does this.
constexpr std::uint16_t backdrop = 0x7C1F;

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
constexpr int menu_left = 2;
constexpr int menu_top = 2;
constexpr int sheet_top = 3;
constexpr int sheet_footer_row = screen_rows - 2;

// How thick the wash around a lit cell is, and why it is a frame rather than a
// fill.
//
// This machine has a blender and could wash the whole cell translucently. Then
// every probe over a lit tile would be a claim about the blender's rounding
// rather than about the picture, which is a much weaker thing to assert and a
// much harder thing to be exactly right about. A frame in a flat colour is
// exact, leaves the cell's own art legible in the middle, and leaves the
// centre-sampling probe idiom `play_exe.cpp` established untouched.
constexpr int wash_thickness = 3;

// The cursor's four corner brackets: how long an arm is and how thick. The arm
// is a quarter of whatever cell this board is drawn at, so the bracket stays
// the same shape on a shrunk board rather than closing up into a box.
[[nodiscard]] constexpr int bracket_arm_for(int cell) noexcept {
    const int arm = cell / 4;
    return arm > 0 ? arm : 1;
}
constexpr int bracket_thickness = 2;

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
// Column zero is the caret's, so a page row starts at column one and the page's
// thirty-eight columns end at thirty-nine, which is the display's width. The
// panel is a row of paper above and below the page, so a page over a scene's
// backdrop is legible rather than tangled with it.
// ---------------------------------------------------------------------------
constexpr int screen_caret_column = 0;
constexpr int screen_page_left = 1;
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
    screen_page_left + turn::footer_columns <= screen_cols,
    "a footer has to fit the display. `write_at` stops at the edge, so one that "
    "does not is cut in the middle of its last word rather than reported, and "
    "the last word is where a footer puts the button a player has yet to meet"
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
// names it. Column zero is left alone deliberately: the backdrop is claimed
// there, and a face that covered it would take away the probe that proves the
// scene behind it was drawn.
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

[[nodiscard]] constexpr int terrain_variant(int x, int y) noexcept {
    return (x + y * 3) % art::variant_count;
}

[[nodiscard]] int elevation_of_kind(int kind) noexcept {
    if (kind < 0 || kind >= grandleon_terrain_kind_count) return 0;
    return grandleon_terrain_elevation[kind];
}

#ifdef GRANDLEON_PSX_3D
// ---------------------------------------------------------------------------
// Production 3D board primitives
// ---------------------------------------------------------------------------
//
// The campaign, controller and rules above this point are unchanged. This is
// only another way of painting the same snapshot and overlay. The diagnostic
// roster viewer uses the same generated proxies; keeping the models in one
// table means improving a role improves the viewer, the campaign board and the
// close attack view together.
struct ThreeDCamera final {
    int distance;
    int focal;
    int horizon;
};

struct ThreeDProjectedVertex final {
    std::int16_t x;
    std::int16_t y;
    std::int32_t depth;
};

struct ThreeDOrderedTriangle final {
    std::uint16_t index;
    std::int32_t depth;
};

struct ThreeDScreenBounds final {
    int left = gpu::screen_width;
    int top = gpu::screen_height;
    int right = 0;
    int bottom = 0;
    bool any = false;
};

constexpr int three_d_max_vertices = 512;
constexpr int three_d_max_triangles = sevenfold_high_max_triangles;
constexpr int three_d_tile = 44;
constexpr int three_d_height_step = 10;
ThreeDProjectedVertex three_d_projected[three_d_max_vertices];
ThreeDOrderedTriangle three_d_ordered[three_d_max_triangles];

constexpr std::uint8_t three_d_team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};

[[nodiscard]] constexpr int three_d_channel(int value) noexcept {
    return value < 0 ? 0 : (value > 31 ? 31 : value);
}

[[nodiscard]] constexpr std::uint16_t three_d_colour(
    const std::uint8_t* source, int light = 255
) noexcept {
    return gpu::rgb15(
        three_d_channel((static_cast<int>(source[0]) * light / 255) >> 3),
        three_d_channel((static_cast<int>(source[1]) * light / 255) >> 3),
        three_d_channel((static_cast<int>(source[2]) * light / 255) >> 3)
    );
}

[[nodiscard]] ThreeDProjectedVertex three_d_project(
    int x, int y, int z, const ThreeDCamera& camera
) noexcept {
    // Fixed-point 30-degree downward view. These are the same measured
    // projection constants as the standalone hardware viewer and require no
    // floating-point runtime on the R3000A.
    const std::int32_t vertical = y * 218 + z * 128;
    std::int32_t depth = camera.distance * 256 - z * 218 + y * 128;
    if (depth < 64 * 256) depth = 64 * 256;
    return {
        static_cast<std::int16_t>(160 + x * camera.focal * 256 / depth),
        static_cast<std::int16_t>(
            camera.horizon - vertical * camera.focal / depth
        ),
        depth,
    };
}

[[nodiscard]] int three_d_order_triangles(const SevenfoldProxy& proxy) {
    int count = 0;
    for (int index = 0; index < proxy.triangles_count; ++index) {
        const SevenfoldProxyTriangle& triangle = proxy.triangles[index];
        const ThreeDProjectedVertex& a = three_d_projected[triangle.a];
        const ThreeDProjectedVertex& b = three_d_projected[triangle.b];
        const ThreeDProjectedVertex& c = three_d_projected[triangle.c];
        const std::int32_t area =
            static_cast<std::int32_t>(b.x - a.x) * (c.y - a.y) -
            static_cast<std::int32_t>(b.y - a.y) * (c.x - a.x);
        if (area >= 0) continue;
        three_d_ordered[count++] = {
            static_cast<std::uint16_t>(index),
            a.depth + b.depth + c.depth,
        };
    }
    std::sort(
        three_d_ordered, three_d_ordered + count,
        [](const ThreeDOrderedTriangle& left,
           const ThreeDOrderedTriangle& right) {
            return left.depth > right.depth;
        }
    );
    return count;
}

void three_d_draw_model(
    const SevenfoldProxy& proxy, int origin_x, int origin_y, int origin_z,
    int team, int quarter_turn, const ThreeDCamera& camera,
    int lean = 0, bool spent = false
) {
    if (proxy.vertices_count > three_d_max_vertices ||
        proxy.triangles_count > three_d_max_triangles) {
        return;
    }
    for (int index = 0; index < proxy.vertices_count; ++index) {
        const SevenfoldProxyVertex& source = proxy.vertices[index];
        int x = source.x;
        int z = source.z;
        if (quarter_turn < 0) {
            x = source.z;
            z = -source.x;
        } else if (quarter_turn > 0) {
            x = -source.z;
            z = source.x;
        }
        x += source.y * lean / 256;
        three_d_projected[index] = three_d_project(
            x + origin_x, source.y + origin_y, z + origin_z, camera
        );
    }
    const int count = three_d_order_triangles(proxy);
    for (int order = 0; order < count; ++order) {
        const SevenfoldProxyTriangle& triangle =
            proxy.triangles[three_d_ordered[order].index];
        const ThreeDProjectedVertex& a = three_d_projected[triangle.a];
        const ThreeDProjectedVertex& b = three_d_projected[triangle.b];
        const ThreeDProjectedVertex& c = three_d_projected[triangle.c];
        const std::uint8_t* material = triangle.material == 0
            ? three_d_team_colours[team]
            : sevenfold_style_materials[proxy.style][triangle.material];
        int light = 128 + triangle.shade / 2;
        if (spent) light = light * 5 / 8;
        gpu::draw_triangle(
            a.x, a.y, b.x, b.y, c.x, c.y,
            three_d_colour(material, light)
        );
        // Keep the 16-bit hardware counter extended during a dense model. It
        // is also what makes the frame telemetry truthful past 15.5 ms.
        if ((order & 63) == 63) static_cast<void>(psx::clock_ticks());
    }
}

void three_d_quad(
    int x0, int y0, int z0, int x1, int y1, int z1,
    std::uint16_t colour, const ThreeDCamera& camera
) {
    const ThreeDProjectedVertex a = three_d_project(x0, y0, z0, camera);
    const ThreeDProjectedVertex b = three_d_project(x1, y0, z0, camera);
    const ThreeDProjectedVertex c = three_d_project(x1, y1, z1, camera);
    const ThreeDProjectedVertex d = three_d_project(x0, y1, z1, camera);
    gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
    gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
}

void three_d_triangle(
    int ax, int ay, int az,
    int bx, int by, int bz,
    int cx, int cy, int cz,
    std::uint16_t colour,
    const ThreeDCamera& camera
) {
    const ThreeDProjectedVertex a = three_d_project(ax, ay, az, camera);
    const ThreeDProjectedVertex b = three_d_project(bx, by, bz, camera);
    const ThreeDProjectedVertex c = three_d_project(cx, cy, cz, camera);
    gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
}

void three_d_include_model_bounds(
    ThreeDScreenBounds& bounds, const SevenfoldProxy* proxy,
    int origin_x, int origin_y, int origin_z, int quarter_turn, int lean,
    const ThreeDCamera& camera
) {
    if (proxy == nullptr) return;
    int source_left = proxy->vertices[0].x;
    int source_bottom = proxy->vertices[0].y;
    int source_back = proxy->vertices[0].z;
    int source_right = source_left;
    int source_top = source_bottom;
    int source_front = source_back;
    for (int index = 0; index < proxy->vertices_count; ++index) {
        const SevenfoldProxyVertex& source = proxy->vertices[index];
        if (source.x < source_left) source_left = source.x;
        if (source.x > source_right) source_right = source.x;
        if (source.y < source_bottom) source_bottom = source.y;
        if (source.y > source_top) source_top = source.y;
        if (source.z < source_back) source_back = source.z;
        if (source.z > source_front) source_front = source.z;
    }

    // Project the eight corners of a conservative object-space box. Scanning
    // the model only performs comparisons; the expensive software divisions
    // happen eight times per pose rather than once per vertex.
    for (int xi = 0; xi < 2; ++xi) {
        for (int yi = 0; yi < 2; ++yi) {
            for (int zi = 0; zi < 2; ++zi) {
                int x = xi == 0 ? source_left : source_right;
                const int y = yi == 0 ? source_bottom : source_top;
                int z = zi == 0 ? source_back : source_front;
                if (quarter_turn < 0) {
                    const int turned_x = z;
                    z = -x;
                    x = turned_x;
                } else if (quarter_turn > 0) {
                    const int turned_x = -z;
                    z = x;
                    x = turned_x;
                }
                x += y * lean / 256;
                const ThreeDProjectedVertex point = three_d_project(
                    x + origin_x, y + origin_y, z + origin_z, camera
                );
                if (!bounds.any) {
                    bounds.left = point.x;
                    bounds.right = point.x;
                    bounds.top = point.y;
                    bounds.bottom = point.y;
                    bounds.any = true;
                } else {
                    if (point.x < bounds.left) bounds.left = point.x;
                    if (point.x > bounds.right) bounds.right = point.x;
                    if (point.y < bounds.top) bounds.top = point.y;
                    if (point.y > bounds.bottom) bounds.bottom = point.y;
                }
            }
        }
    }
}

[[nodiscard]] ThreeDCamera three_d_attack_camera(
    const SevenfoldProxy* attacker, const SevenfoldProxy* target
) {
    // Fit the complete motion envelope, not the current pose. The camera then
    // stays still while a strike lunges and the defender recoils. The top band
    // and a small overscan margin are kept clear on every side.
    constexpr int safe_left = 10;
    constexpr int safe_top = 32;
    constexpr int safe_right = gpu::screen_width - 10;
    constexpr int safe_bottom = gpu::screen_height - 16;
    constexpr int distance = 360;
    constexpr int minimum_focal = 300;
    constexpr int maximum_focal = 560;

    for (int focal = maximum_focal; focal >= minimum_focal; focal -= 10) {
        const ThreeDCamera probe{distance, focal, 0};
        ThreeDScreenBounds bounds;
        three_d_include_model_bounds(
            bounds, attacker, -76, 0, 0, 1, 0, probe
        );
        three_d_include_model_bounds(
            bounds, attacker, -28, 0, 0, 1, 22, probe
        );
        three_d_include_model_bounds(
            bounds, target, 76, 0, 0, -1, 0, probe
        );
        three_d_include_model_bounds(
            bounds, target, 90, 4, 0, -1, -22, probe
        );
        if (!bounds.any || bounds.left < safe_left ||
            bounds.right > safe_right ||
            bounds.bottom - bounds.top > safe_bottom - safe_top) {
            continue;
        }
        const int horizon = safe_bottom - bounds.bottom;
        if (bounds.top + horizon >= safe_top) {
            return {distance, focal, horizon};
        }
    }
    return {distance, minimum_focal, 214};
}
#endif

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
constexpr int character_cache_capacity =
    art::character_count > 0 ? art::character_count : 1;
int character_cell_of[character_cache_capacity];
int character_clut_of[character_cache_capacity];
int font_clut = -1;
int font_gold_clut = -1;
int font_muted_clut = -1;

int cells_used = 0;
int cluts_used = 0;
bool cells_fit = true;
bool cluts_fit = true;

// The font, expanded. Two kilobytes of `.bss`, built once and uploaded once;
// the compact one-bit form stays in `.rodata` where `grandleon::view` put it.
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
// repository, and `psx_art.h` records why it is easy to get backwards.
void build_font() {
    for (auto& row : font_sheet) {
        for (std::uint16_t& word : row) word = 0;
    }
    for (int slot = 0; slot < view::glyph_count && slot < gpu::font_capacity;
         ++slot) {
        const int base_line = gpu::glyph_texture_v(slot);
        const int base_word = gpu::glyph_texture_u(slot) / art::texels_per_halfword;
        for (int row = 0; row < gpu::glyph_texels; ++row) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                if (!view::glyph_pixel(view::first_glyph + slot, x, row)) continue;
                const int word = base_word + x / art::texels_per_halfword;
                const int shift = 4 * (x % art::texels_per_halfword);
                font_sheet[base_line + row][word] = static_cast<std::uint16_t>(
                    font_sheet[base_line + row][word] |
                    static_cast<std::uint16_t>(1u << shift)
                );
            }
        }
    }
    gpu::upload(
        gpu::font_x, gpu::font_y, gpu::font_halfwords_across, gpu::font_lines,
        &font_sheet[0][0]
    );
}

// The font's CLUT: index 0 is paper and index 1 is ink, and every other entry
// is paper because no glyph ever names one.
//
// Paper is opaque, which is the whole reason it is not left as CLUT zero: the
// GPU skips a texel whose CLUT word is zero, and a message bar that let the
// board show through between its letters would be a message bar nobody could
// read over a mountain.
[[nodiscard]] int claim_font_clut(std::uint16_t ink) {
    unsigned short entries[art::clut_size];
    for (int i = 0; i < art::clut_size; ++i) entries[i] = paper_colour;
    entries[1] = ink;
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
// This is `play_exe.cpp`'s idiom, extended from one still picture to a
// checkpoint of a session.
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
[[nodiscard]] constexpr int texel_under(int screen, int drawn) noexcept {
    if (drawn >= native_tile) return screen;
    // The quad carries `u` at its near edge and `u + native_tile - 1` at its
    // far one, and the hardware samples at a pixel's *centre*, half a pixel in.
    // Both halves of that matter and each was got wrong once here: dropping the
    // half-pixel reads a texel early over most of the cell, and spanning 32
    // texels rather than 31 reads past the cell's own last column.
    return ((2 * screen + 1) * (native_tile - 1)) / (2 * drawn);
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
            if (x < 0 || y < 0 || x >= native_tile || y >= native_tile) {
                return false;
            }
            if (art::colour_at(asset, x, y) != here) return false;
        }
    }
    return true;
}

// The opaque texel of a cell nearest its centre, searched outward in rings and
// confined to the middle half so the texel it finds can never be one the
// cursor's brackets or a wash's frame is drawn over. `play_exe.cpp` searches
// the whole cell because it draws neither.
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
    const art::Asset& asset, int drawn, int& out_sx, int& out_sy,
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
                const int tx = texel_under(sx, drawn);
                const int ty = texel_under(sy, drawn);
                const std::uint16_t here = art::colour_at(asset, tx, ty);
                if (here == 0) continue;
                // The margin the note above this function describes, which
                // until a board scrolled was described and not taken: on a
                // shrunk cell a pixel spans more than one texel and which of
                // them the hardware reaches is its own rounding, so a texel is
                // only claimable when its neighbours carry the same colour. At
                // the native size a pixel is a texel and nothing is asked.
                if (drawn < native_tile && !neighbours_agree(asset, tx, ty)) {
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

// The box the action menu occupies, in glyph cells, so a probe over a lit tile
// can tell whether the menu is standing on it.
struct MenuBox final {
    int left{0};
    int top{0};
    int width{0};
    int height{0};

    [[nodiscard]] bool covers(int column, int row, int drawn) const noexcept {
        if (width <= 0 || height <= 0) return false;
        const int span = drawn / glyph > 0 ? drawn / glyph : 1;
        return column + span > left && column < left + width &&
               row + span > top && row < top + height;
    }

    // Whether one *pixel* is under the box, rather than one cell. A cell is
    // asked about before it is claimed at its corner; a character's token is
    // claimed at whatever texel of it is nearest the middle, and on a board
    // small enough for the menu to stand over a character that texel can be
    // one the menu is painted on.
    [[nodiscard]] bool covers_pixel(int x, int y) const noexcept {
        if (width <= 0 || height <= 0) return false;
        return x >= left * glyph && x < (left + width) * glyph &&
               y >= top * glyph && y < (top + height) * glyph;
    }
};

// Storage for the draw list: one item per visible cell and one per visible
// living unit.
//
// Sized from the *widest* window this executable can be asked for rather than
// from the native cell's ten by six, because a board is fitted now and a
// shrunk cell shows more of it. The widest window is what the smallest cell
// buys, which is twenty by thirteen, and a window can never exceed that
// because no board is ever drawn at a smaller cell than the rule's floor.
//
// The unit allowance is not scaled with it. A unit needs a cell to stand on,
// so a window's cells already bound how many units can be in it; the sixteen
// covers the units drawn beside their cell rather than on it, and that is a
// property of the drawing rather than of the window's size.
constexpr int widest_view = turn::board_fit.frame_w / turn::board_fit.smallest_tile;
constexpr int tallest_view = turn::board_fit.frame_h / turn::board_fit.smallest_tile;
constexpr int draw_capacity = widest_view * tallest_view + 16;
view::DrawItem draw_storage[draw_capacity];

// ---------------------------------------------------------------------------
// The console's half of the client
// ---------------------------------------------------------------------------
class PlayStationClient final : public turn::TurnClient {
public:
    PlayStationClient(turn::ReportSink& sink, const pr::Presentation& shown) noexcept
        : TurnClient(sink),
          shown_(&shown),
          theme_(shown.theme < grandleon_theme_count ? shown.theme : 0) {}

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

    turn::ControllerPress next_controller_press() override {
        std::uint32_t frame = 0;
        bool emphasised = false;
        // Which direction is being leaned on, and for how many frames. Held
        // across calls would be wrong: every call starts a fresh question, and
        // a thumb still resting on a direction from the last answer should have
        // to hold it again rather than run away with the next screen.
        std::uint16_t leaning[2]{};
        int leaned_frames[2]{};
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
                if (held != leaning[player]) {
                    leaning[player] = held;
                    leaned_frames[player] = 0;
                } else if (held != 0) {
                    ++leaned_frames[player];
                    if (view::repeat_due(leaned_frames[player])) {
                        pending_buttons_[player] = static_cast<std::uint16_t>(
                            pending_buttons_[player] | held
                        );
                    }
                }
            }

            // A played build falls through to here and waits, which is the
            // whole of the difference: no press is invented, so a screen that
            // asks a question keeps asking it until somebody answers.
            ++frame;
            const bool now = view::cursor_emphasised(frame);
            if (now == emphasised) continue;
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

    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
#ifndef GRANDLEON_PSX_AUTOPILOT
        // Match the Godot player's direct-start rule. An authored fixed
        // formation with no inventory choices is already ready; opening the full
        // company editor only hides the battle behind an unexplained START
        // press. Real deployment choices, inventory management and roster
        // refusals still use the complete screen. Debug Stages remain in the
        // in-battle menu.
        if (client::company_can_start_directly(company)) {
            return {client::ManagementVerb::proceed, {}, {}};
        }
#endif
        return turn::TurnClient::next_management_intent(company);
    }

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
                claim_first_glyph_at(
                    "gametitle", centred_column(title), 11, title,
                    menu_gold_colour
                );
            }
            char footer[screen_cols + 1]{};
            control_text(view.footer, footer, sizeof footer);
            claim_first_glyph_at(
                "titlefooter", centred_column(footer), 26, footer
            );
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
            char footer[screen_cols + 1]{};
            control_text(view.footer, footer, sizeof footer);
            claim_first_glyph_at(
                "slotfooter", centred_column(footer), 27, footer
            );
            close_probes(before, harness_screen_slot);
            return;
        }

        // The panel, at both ends, which is a claim no unpainted screen and no
        // screen drawn a row off can satisfy.
        probe("panel", 0, screen_panel_top * glyph, paper_colour);
        probe(
            "panelfar", (screen_cols - 1) * glyph,
            (screen_panel_top + screen_panel_rows - 1) * glyph, paper_colour
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
                screen_page_top + view.caret_row, '>'
            );
        }

        if (view.footer != nullptr && view.footer[0] != '\0') {
            probe(
                "footer", (screen_cols - 1) * glyph, screen_footer_row * glyph,
                paper_colour
            );
            char footer[screen_cols + 1]{};
            control_text(view.footer, footer, sizeof footer);
            claim_first_glyph_at(
                "footertext", screen_page_left, screen_footer_row, footer
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
            if (opaque_near_centre(asset, screen_face_size, sx, sy, px, py)) {
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

    // What this machine can say about the three gestures, and what it cannot.
    //
    // **It can say the timing and the travel.** A shot opens with a mark
    // crossing the board from the shooter to the target and a cast opens with a
    // flare held on the target tile, both for exactly the frame counts the
    // shared model states, so a player watching this machine sees the same
    // three gestures take the same three lengths of time they take everywhere
    // else. The mark costs nothing new: it is the cell flash the board already
    // draws for a struck tile, moved a tile at a time instead of parked.
    //
    // **It cannot say the pose.** The generated PlayStation art header carries
    // one 25x25 standing cell per figure and no frame dimension at all, so
    // there is no lunge to draw and no cast pose to hold. `art::character`
    // takes an archetype and a colour and there is no third index to give it.
    // Slices 2 and 3 recorded that; slice 4 records what it now costs, which is
    // the pose half of every attack rather than only of a walk. See
    // Reaching it means `frame_count` and `frame_names` on the generated
    // header, a `..._frames.png` read in `playstation_header.emit_characters`,
    // and four more cells per figure in the header.
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
#ifndef GRANDLEON_PSX_3D
        static_cast<void>(damage);
#endif
        const sim::UnitSnapshot* target = unit_of(before, struck);
        if (target == nullptr || last_overlay_ == nullptr) return;
        const std::uint32_t opened = psx::clock_ticks();
        layout(before);
        const sim::UnitSnapshot* thrower =
            striker != 0 ? unit_of(before, striker) : nullptr;
        int frames = 0;

        // ---- the opening -------------------------------------------------
        const int lead = view::gesture_lead_frames(gesture, separation);
        for (int frame = 0; frame < lead; ++frame) {
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
#ifdef GRANDLEON_PSX_3D
            draw_three_d_attack(
                before, *target, thrower, gesture, frame, lead, false,
                landed, damage
            );
#else
            draw(before, *last_overlay_, false);
#endif
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
#ifdef GRANDLEON_PSX_3D
            draw_three_d_attack(
                before, *target, thrower, gesture, frame, resolve, true,
                landed, damage
            );
#else
            draw(before, *last_overlay_, false);
#endif
            ++frames;
        }
        flinch_dx_ = 0;
        flinch_dy_ = 0;
        flinching_ = 0;
        flash_x_ = -1;
        flash_y_ = -1;
        flash_lit_ = false;
        report_animation(landed ? "hit" : "miss", 1, frames, opened);
    }

    // ----- the pixel claims ------------------------------------------------
    //
    // What the executable says it drew, where it says it drew it. The observer
    // captures the display at exactly this instant: it is called from inside
    // the store that signals it, so there is no frame to agree about. The
    // joiner requires the claim, the GPU's own readback and that frame to
    // agree, with no tolerance at all.
    void after_facts(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) override {
        const int before = probes;

        if (overlay.sheet != nullptr) {
            // The sheet covers the board, so there is no lit tile, no unit and
            // no cursor left to claim: claiming one would be claiming a pixel
            // the interface is deliberately drawing over. What is claimed
            // instead is that the sheet is really there: solid paper at both
            // ends of the screen, and the first ink pixel of the first letter
            // of its first line, which is a claim no blank box can satisfy.
            probe("sheet", 0, 0, paper_colour);
            probe(
                "sheetfar", (screen_cols - 1) * glyph, sheet_top * glyph,
                paper_colour
            );
            if (overlay.sheet->count > 0) {
                claim_first_glyph("sheettext", sheet_top, overlay.sheet->line(0));
            }
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
                const int left = cell_left(x);
                const int top = cell_top(x, y);
                if (box.covers(left / glyph, top / glyph, drawn_tile())) continue;
                Line label;
                label.text(splash ? "s" : danger ? "d" : "m")
                    .decimal(static_cast<std::uint32_t>(x))
                    .text("_")
                    .decimal(static_cast<std::uint32_t>(y));
                probe(
                    label.c_str(), left, top,
                    splash ? cursor_colour
                           : danger ? danger_colour : lit_colour(overlay)
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
            const int drawn = drawn_tile();
            int sx = 0;
            int sy = 0;
            if (!opaque_near_centre(asset, drawn, sx, sy, px, py)) continue;
            const int at_x = cell_left(unit.position.x) + sx;
            const int at_y = cell_top(unit.position.x, unit.position.y) + sy;
            // Claiming a texel the menu is drawn over would be claiming a
            // pixel the interface is deliberately hiding. That is the reason a
            // lit tile under the box is skipped a few lines above, and the
            // reason a sheet claims nothing about the board at all.
            if (box.covers_pixel(at_x, at_y)) continue;
            Line label;
            label.text("u")
                .decimal(static_cast<std::uint32_t>(unit.position.x))
                .text("_")
                .decimal(static_cast<std::uint32_t>(unit.position.y));
            probe(label.c_str(), at_x, at_y, art::colour_at(asset, px, py));
        }
        if (camera().visible(overlay.cursor_x, overlay.cursor_y)) {
            const int cursor_at_x = cell_left(overlay.cursor_x);
            const int cursor_at_y =
                cell_top(overlay.cursor_x, overlay.cursor_y);
            // And the cursor is claimed on the same terms as the figures above.
            // A character picked up on a tile the action menu is drawn over is
            // a cursor the interface is deliberately hiding, and claiming its
            // colour there would be claiming a pixel nobody painted.
            if (!box.covers_pixel(cursor_at_x, cursor_at_y)) {
                probe("cursor", cursor_at_x, cursor_at_y, cursor_colour);
            }
        }
        // Whoever the cursor rests on is named on the bar's first row, and a
        // name nothing photographs is a row this executable only claims to
        // draw. The first ink of it, by the same idiom the message row uses.
        if (overlay.hovered_name != nullptr) {
            claim_first_glyph("hovertext", row_hovered, overlay.hovered_name);
        }
        if (overlay.message != nullptr) {
            probe("bar", (screen_cols - 1) * glyph, row_message * glyph,
                  paper_colour);
            claim_first_glyph("bartext", row_message, overlay.message);
        } else if (overlay.hovered_class != nullptr) {
            // The class, on the row the message would have taken. Claimed on
            // the same terms, so the row is photographed whichever of the two
            // is standing in it.
            probe("bar", (screen_cols - 1) * glyph, row_message * glyph,
                  paper_colour);
            claim_first_glyph("classtext", row_message, overlay.hovered_class);
        }
        if (overlay.aiming != nullptr) {
            probe("aim", (screen_cols - 1) * glyph, row_aiming * glyph,
                  paper_colour);
        }
        if (overlay.menu != nullptr && overlay.menu_rows > 0) {
            probe("menu", box.left * glyph, box.top * glyph, paper_colour);
            claim_glyph(
                "menucaret", box.left, box.top + overlay.menu_row, '>'
            );
        }
        close_probes(before);
    }

private:
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
    // The number matters because a *three-dimensional* moving frame of the
    // same board measures 1,862 µs, and the two-dimensional counterpart is
    // what this draws. It is a whole repaint: the backdrop, sixty cells, the
    // washes, the tokens, the cursor and the bar. This machine has no sprite
    // table and no plane to leave standing, so there is no cheaper frame to
    // report.
    void draw(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay,
        bool emphasised
    ) {
        const std::uint32_t opened = psx::clock_ticks();
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
#ifdef GRANDLEON_PSX_3D
        layout(snapshot);
        draw_three_d_board(snapshot, overlay, emphasised);
        draw_bar(snapshot, overlay);
        if (overlay.menu != nullptr) draw_menu(overlay);
#else
        layout(snapshot);
        compose(snapshot);
        draw_list(snapshot, overlay);
        draw_cursor(overlay, emphasised);
        draw_bar(snapshot, overlay);
        if (overlay.menu != nullptr) draw_menu(overlay);
#endif
    }

#ifdef GRANDLEON_PSX_3D
    [[nodiscard]] ThreeDCamera strategic_camera() const noexcept {
        int span = camera().view_w;
        if (camera().view_h > span) span = camera().view_h;
        return {
            490 + (span > 5 ? (span - 5) * 34 : 0),
            330,
            174,
        };
    }

    [[nodiscard]] int three_d_x(int fixed_cell) const noexcept {
        const int centre = camera().x * 256 + camera().view_w * 128;
        return (fixed_cell - centre) * three_d_tile / 256;
    }

    [[nodiscard]] int three_d_z(int fixed_cell) const noexcept {
        const int centre = camera().y * 256 + camera().view_h * 128;
        return (fixed_cell - centre) * three_d_tile / 256;
    }

    [[nodiscard]] std::uint16_t three_d_terrain_colour(
        int kind, int light = 255
    ) const noexcept {
        if (kind < 0 || kind >= grandleon_terrain_kind_count) {
            return paper_colour;
        }
        return three_d_colour(grandleon_terrain_flat[theme_][kind], light);
    }

    void draw_three_d_marker(
        int cell_x, int cell_y, int inset, int lift, std::uint16_t colour,
        const ThreeDCamera& camera_view
    ) {
        const int kind = kind_at(cell_x, cell_y);
        const int high = elevation_of_kind(kind) * three_d_height_step + lift;
        three_d_quad(
            three_d_x(cell_x * 256) + inset, high,
            three_d_z(cell_y * 256) + inset,
            three_d_x((cell_x + 1) * 256) - inset, high,
            three_d_z((cell_y + 1) * 256) - inset, colour, camera_view
        );
    }

    void draw_three_d_units_in_row(
        const sim::EncounterSnapshot& snapshot, int row,
        const ThreeDCamera& camera_view
    ) {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (!sim::on_board(unit) ||
                !camera().visible(unit.position.x, unit.position.y)) {
                continue;
            }
            const int paint_row = moving_ != 0 && unit.id == moving_
                ? move_world_z_ / 256 : unit.position.y;
            if (paint_row != row) continue;
            int fixed_x = unit.position.x * 256 + 128;
            int fixed_z = unit.position.y * 256 + 128;
            int bob = 0;
            int facing = unit.side == sim::Side::first ? 1 : -1;
            if (moving_ != 0 && unit.id == moving_) {
                fixed_x = move_world_x_;
                fixed_z = move_world_z_;
                bob = move_bob_;
                facing = move_facing_;
            }
            const int kind = kind_at(unit.position.x, unit.position.y);
            int origin_y =
                elevation_of_kind(kind) * three_d_height_step + bob;
            int origin_x = three_d_x(fixed_x);
            int origin_z = three_d_z(fixed_z);
            if (flinching_ != 0 && unit.id == flinching_) {
                origin_x += flinch_dx_ * 2;
                origin_z += flinch_dy_ * 2;
            }
            const int style = style_of(unit.unit_type_id);
            const int figure = figure_of(unit.unit_type_id);
            const int archetype = archetype_of(unit.unit_type_id);
            if (style < 0 || style >= 7 || figure < 0 || figure >= 2 ||
                archetype < 0 || archetype >= 8) {
                continue;
            }
            const int team = unit.side == sim::Side::first ? 0 : 1;
            if (moving_ == 0 || unit.id != moving_) {
                draw_three_d_marker(
                    unit.position.x, unit.position.y, 13, 3,
                    three_d_colour(three_d_team_colours[team], 225),
                    camera_view
                );
            }
            if (unit.has_acted) {
                draw_three_d_marker(
                    unit.position.x, unit.position.y, 9, 3, spent_colour,
                    camera_view
                );
            }
            const SevenfoldProxy* proxy = sevenfold_campaign_proxy(
                style, figure, archetype, false
            );
            if (proxy != nullptr) {
                three_d_draw_model(
                    *proxy, origin_x, origin_y, origin_z, team, facing,
                    camera_view,
                    moving_ != 0 && unit.id == moving_ ? move_lean_ : 0,
                    unit.has_acted
                );
            }
        }
    }

    void draw_three_d_board(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay,
        bool emphasised
    ) {
        const ThreeDCamera camera_view = strategic_camera();

        // Ground first, far row to near row. Raised cells also receive two
        // darker faces, so height reads as volume rather than as a tile that
        // happened to move up the screen.
        for (int y = camera().y; y < camera().y + camera().view_h; ++y) {
            for (int x = camera().x; x < camera().x + camera().view_w; ++x) {
                const int kind = kind_at(x, y);
                if (kind < 0) continue;
                const int high =
                    elevation_of_kind(kind) * three_d_height_step;
                const int x0 = three_d_x(x * 256);
                const int x1 = three_d_x((x + 1) * 256);
                const int z0 = three_d_z(y * 256);
                const int z1 = three_d_z((y + 1) * 256);
                three_d_quad(
                    x0, high, z0, x1, high, z1,
                    three_d_terrain_colour(
                        kind, ((x + y) & 1) != 0 ? 238 : 255
                    ),
                    camera_view
                );
                if (high > 0) {
                    three_d_quad(
                        x0, 0, z1, x1, high, z1,
                        three_d_terrain_colour(kind, 150), camera_view
                    );
                    three_d_quad(
                        x1, 0, z0, x1, high, z1,
                        three_d_terrain_colour(kind, 180), camera_view
                    );
                }

                // A second inset tone breaks up the single-colour blocks.
                // A few terrain families also get a tiny silhouette carrier;
                // these stay off-centre so figures and movement markers remain
                // readable on the same cell.
                three_d_quad(
                    x0 + 4, high + 1, z0 + 4,
                    x1 - 4, high + 1, z1 - 4,
                    three_d_terrain_colour(
                        kind, ((x * 17 + y * 29 + kind * 11) % 3) == 0
                                  ? 210 : 225
                    ),
                    camera_view
                );
                const int detail_x = (x0 + x1) / 2 + ((x + y) % 3 - 1) * 7;
                const int detail_z = (z0 + z1) / 2 + ((x * 3 + y) % 3 - 1) * 6;
                if (kind == 2 || kind == 11) {
                    three_d_quad(
                        detail_x - 3, high, detail_z,
                        detail_x + 3, high + 23, detail_z,
                        gpu::rgb15(9, 7, 4), camera_view
                    );
                    three_d_triangle(
                        detail_x - 13, high + 17, detail_z,
                        detail_x + 13, high + 17, detail_z,
                        detail_x, high + 45, detail_z,
                        three_d_terrain_colour(kind, 220), camera_view
                    );
                } else if (kind == 3 || kind == 7) {
                    const int radius = kind == 3 ? 18 : 13;
                    const int peak = kind == 3 ? 31 : 20;
                    three_d_triangle(
                        detail_x - radius, high, detail_z - radius,
                        detail_x + radius, high, detail_z - radius,
                        detail_x, high + peak, detail_z,
                        three_d_terrain_colour(kind, 185), camera_view
                    );
                    three_d_triangle(
                        detail_x + radius, high, detail_z - radius,
                        detail_x + radius, high, detail_z + radius,
                        detail_x, high + peak, detail_z,
                        three_d_terrain_colour(kind, 225), camera_view
                    );
                } else if (kind == 8) {
                    three_d_quad(
                        detail_x - 15, high, detail_z,
                        detail_x + 15, high + 20, detail_z,
                        three_d_terrain_colour(kind, 180), camera_view
                    );
                }

                const bool splash = turn::contains(overlay.splash, x, y);
                const bool danger = turn::contains(overlay.danger, x, y);
                const bool move = turn::contains(overlay.moves, x, y);
                if (splash || danger || move) {
                    draw_three_d_marker(
                        x, y, 5, 2,
                        splash ? cursor_colour
                               : danger ? danger_colour : lit_colour(overlay),
                        camera_view
                    );
                }
                if (flash_lit_ && x == flash_x_ && y == flash_y_) {
                    draw_three_d_marker(
                        x, y, 2, 3, flash_colour, camera_view
                    );
                }
                if (x == overlay.cursor_x && y == overlay.cursor_y) {
                    draw_three_d_marker(
                        x, y, emphasised ? 7 : 2, 4, cursor_colour,
                        camera_view
                    );
                }
            }

            // The PlayStation GPU has no depth buffer. Finish a far row's
            // figures before painting the next, nearer row so raised ground
            // and scenery in front can correctly cover their lower portions.
            draw_three_d_units_in_row(snapshot, y, camera_view);
        }
    }

    void draw_three_d_attack(
        const sim::EncounterSnapshot& snapshot,
        const sim::UnitSnapshot& target,
        const sim::UnitSnapshot* attacker,
        view::AttackGesture gesture,
        int frame,
        int frames,
        bool resolving,
        bool landed,
        int damage
    ) {
        const int target_style = style_of(target.unit_type_id);
        const int target_figure = figure_of(target.unit_type_id);
        const int target_archetype = archetype_of(target.unit_type_id);
        const SevenfoldProxy* target_proxy = sevenfold_campaign_proxy(
            target_style, target_figure, target_archetype, true
        );
        const SevenfoldProxy* attacker_proxy = nullptr;
        if (attacker != nullptr) {
            attacker_proxy = sevenfold_campaign_proxy(
                style_of(attacker->unit_type_id),
                figure_of(attacker->unit_type_id),
                archetype_of(attacker->unit_type_id), true
            );
        }
        const ThreeDCamera close =
            three_d_attack_camera(attacker_proxy, target_proxy);
        const int kind = kind_at(target.position.x, target.position.y);
        const std::uint8_t* ground =
            kind >= 0 && kind < grandleon_terrain_kind_count
                ? grandleon_terrain_flat[theme_][kind]
                : grandleon_terrain_flat[0][9];
        const std::uint8_t sky[3] = {
            static_cast<std::uint8_t>(ground[0] / 3 + 12),
            static_cast<std::uint8_t>(ground[1] / 3 + 12),
            static_cast<std::uint8_t>(ground[2] / 3 + 18),
        };
        gpu::fill(
            0, 0, gpu::screen_width, gpu::screen_height,
            three_d_colour(sky)
        );
        gpu::fill(
            0, 154, gpu::screen_width, gpu::screen_height - 154,
            three_d_colour(ground)
        );

        // The authored row behind the defender becomes a terrain-specific
        // skyline. Mountains and hills peak, forest and bamboo branch upward,
        // ruins break their roof line, and open ground remains low.
        for (int x = 0; x < gpu::screen_width; x += 40) {
            int source_x = x / 40;
            if (source_x >= snapshot.width) source_x = snapshot.width - 1;
            int skyline_kind = kind_at(source_x, target.position.y);
            if (skyline_kind < 0 ||
                skyline_kind >= grandleon_terrain_kind_count) {
                skyline_kind = kind;
            }
            const std::uint16_t silhouette =
                three_d_terrain_colour(skyline_kind, 165);
            const int variation =
                (source_x * 5 + target.position.y * 3 + skyline_kind) % 4;
            if (skyline_kind == 3 || skyline_kind == 7) {
                const int height =
                    (skyline_kind == 3 ? 62 : 42) + variation * 7;
                gpu::draw_triangle(
                    x - 4, 154, x + 44, 154, x + 20, 154 - height,
                    silhouette
                );
            } else if (skyline_kind == 2 || skyline_kind == 11) {
                const int height = 48 + variation * 8;
                gpu::fill(
                    x + 17, 154 - height / 2, 7, height / 2,
                    gpu::rgb15(8, 6, 4)
                );
                gpu::draw_triangle(
                    x - 4, 154 - height / 3,
                    x + 44, 154 - height / 3,
                    x + 20, 154 - height,
                    silhouette
                );
            } else {
                const int height = 20 + variation * 8 +
                    (skyline_kind == 8 ? 22 : 0);
                gpu::fill(x, 154 - height, 34, height, silhouette);
            }
        }

        // A sampled six-by-four floor patch gives the close view actual map
        // context instead of one generic ground colour.
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 6; ++column) {
                int source_x = target.position.x + column - 3;
                int source_y = target.position.y + row - 1;
                if (source_x < 0) source_x = 0;
                if (source_y < 0) source_y = 0;
                if (source_x >= snapshot.width) source_x = snapshot.width - 1;
                if (source_y >= snapshot.height) source_y = snapshot.height - 1;
                int floor_kind = kind_at(source_x, source_y);
                if (floor_kind < 0 ||
                    floor_kind >= grandleon_terrain_kind_count) {
                    floor_kind = kind;
                }
                const int x0 = (column - 3) * 70;
                const int x1 = x0 + 70;
                const int z0 = (row - 1) * 70;
                const int z1 = z0 + 70;
                three_d_quad(
                    x0, 0, z0, x1, 0, z1,
                    three_d_terrain_colour(
                        floor_kind,
                        ((source_x + source_y) & 1) != 0 ? 225 : 250
                    ),
                    close
                );
            }
        }

        const int progress =
            frames > 1 ? frame * 256 / (frames - 1) : 256;
        const int folded = progress <= 128 ? progress : 256 - progress;
        int attacker_x = -76;
        int attacker_y = 0;
        int attacker_lean = 0;
        if (gesture == view::AttackGesture::swing) {
            if (resolving) {
                attacker_x += (256 - progress) * 48 / 256;
                attacker_lean = (256 - progress) * 22 / 256;
            } else {
                attacker_x += progress * 48 / 256;
                attacker_lean = progress * 22 / 256;
            }
        } else if (!resolving) {
            attacker_y = folded * 5 / 128;
            attacker_lean = folded * 10 / 128;
        }
        const int recoil = resolving && landed ? folded * 14 / 128 : 0;
        const int target_lean = resolving && landed ? -folded * 22 / 128 : 0;

        // Flat, terrain-coloured contact shadows anchor the enlarged figures
        // without spending texture memory or adding geometry to every model.
        const std::uint16_t shadow = three_d_colour(ground, 82);
        if (attacker_proxy != nullptr) {
            three_d_quad(
                attacker_x - 27, 1, -12, attacker_x + 27, 1, 12,
                shadow, close
            );
        }
        if (target_proxy != nullptr) {
            three_d_quad(
                76 + recoil - 27, 1, -12, 76 + recoil + 27, 1, 12,
                shadow, close
            );
        }

        if (attacker != nullptr && attacker_proxy != nullptr) {
            three_d_draw_model(
                *attacker_proxy, attacker_x, attacker_y, 0,
                attacker->side == sim::Side::first ? 0 : 1, 1, close,
                attacker_lean
            );
        }
        if (target_proxy != nullptr) {
            three_d_draw_model(
                *target_proxy, 76 + recoil,
                resolving && landed ? folded * 4 / 128 : 0, 0,
                target.side == sim::Side::first ? 0 : 1, -1, close,
                target_lean
            );
        }

        // A projectile, spell flare, or contact spark makes the three shared
        // gestures visually distinct even though these proxy models have no
        // skeletal pose data.
        int effect_x = 0;
        int effect_y = 58;
        bool effect = false;
        std::uint16_t effect_colour = gpu::rgb15(30, 29, 20);
        if (!resolving && gesture == view::AttackGesture::shot) {
            effect_x = -58 + progress * 116 / 256;
            effect_y += folded * 34 / 128;
            effect = true;
        } else if (!resolving && gesture == view::AttackGesture::cast) {
            effect_x = 66;
            effect_y = 38 + folded * 46 / 128;
            effect_colour = gpu::rgb15(15, 27, 31);
            effect = true;
        } else if (resolving && (frame & 1) == 0) {
            effect_x = 60;
            effect_colour = landed ? gpu::rgb15(31, 29, 18)
                                   : gpu::rgb15(30, 23, 9);
            effect = true;
        }
        if (effect) {
            const ThreeDProjectedVertex point =
                three_d_project(effect_x, effect_y, -18, close);
            const int radius = resolving
                ? 8 + (landed ? folded * 6 / 128 : 0) : 4;
            gpu::draw_triangle(
                point.x - radius, point.y,
                point.x, point.y - radius,
                point.x + radius, point.y,
                effect_colour
            );
            gpu::draw_triangle(
                point.x - radius, point.y,
                point.x + radius, point.y,
                point.x, point.y + radius,
                effect_colour
            );
            if (resolving && landed) {
                gpu::fill(
                    point.x - radius - 5, point.y - 1,
                    radius * 2 + 10, 3, effect_colour
                );
                gpu::fill(
                    point.x - 1, point.y - radius - 5,
                    3, radius * 2 + 10, effect_colour
                );
            }
        }

        gpu::fill(
            0, 0, gpu::screen_width, glyph * 2,
            three_d_colour(sky, 105)
        );
        int length = 0;
        char label[32]{};
        const char* word = !resolving ? "ATTACK"
                         : landed ? "HIT" : "MISS";
        while (word[length] != '\0') {
            label[length] = word[length];
            ++length;
        }
        char feedback[16]{};
        int feedback_length = 0;
        if (resolving && landed) {
            feedback[feedback_length++] = '-';
            unsigned int value = static_cast<unsigned int>(
                damage < 0 ? -damage : damage
            );
            char reversed[10]{};
            int digits = 0;
            do {
                reversed[digits++] =
                    static_cast<char>('0' + static_cast<int>(value % 10U));
                value /= 10U;
            } while (value != 0U && digits < 10);
            while (digits > 0 && feedback_length + 1 <
                   static_cast<int>(sizeof feedback)) {
                feedback[feedback_length++] = reversed[--digits];
            }
        } else if (resolving) {
            const char* miss = "MISS";
            while (miss[feedback_length] != '\0') {
                feedback[feedback_length] = miss[feedback_length];
                ++feedback_length;
            }
        }
        label[length] = '\0';
        write_at((screen_cols - length) / 2, 0, label);
        if (resolving) {
            feedback[feedback_length] = '\0';
            write_scaled_at(
                (gpu::screen_width - feedback_length * 12) / 2, 18,
                feedback, 12, landed ? font_gold_clut : font_clut
            );
        }
    }
#endif

    void layout(const sim::EncounterSnapshot& snapshot) {
        board_width_ = snapshot.width;
        int highest = 0;
        for (std::size_t i = 0; i < terrain().size(); ++i) {
            const int elevation = elevation_of_kind(kind_of(terrain()[i]));
            if (elevation > highest) highest = elevation;
        }
        const int drawn = drawn_tile();
        const int step = view::elevation_step_for(drawn);
        int reserved = view::headroom(highest, step, drawn);
        int slack = bar_top - drawn * camera().view_h;
        if (slack < 0) slack = 0;
        if (reserved > slack) reserved = slack;
        // No rounding. A primitive here is positioned per pixel, so the
        // projection's own origin is used, with nothing snapped to a grid.
        const int origin_x = (gpu::screen_width - drawn * camera().view_w) / 2;
        const int origin_y =
            reserved + (bar_top - reserved - drawn * camera().view_h) / 2;
        projection_ = view::Projection{origin_x, origin_y, drawn, step};
    }

    void compose(const sim::EncounterSnapshot& snapshot) {
        list_.clear();
        for (int y = camera().y; y < camera().y + camera().view_h; ++y) {
            for (int x = camera().x; x < camera().x + camera().view_w; ++x) {
                const int kind = kind_at(x, y);
                if (kind < 0) continue;
                list_.add(
                    view::terrain_item(
                        camera(), projection_, x, y, elevation_of_kind(kind),
                        kind, terrain_variant(x, y),
                        static_cast<std::uint32_t>(kind)
                    ),
                    kind
                );
            }
        }
        for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
            const sim::UnitSnapshot& unit = snapshot.units[i];
            // Only what is standing there is drawn: somebody talked off the
            // board or still marching towards it holds no tile to draw them on.
            if (!sim::on_board(unit)) continue;
            // The token in motion is drawn afterwards, at the pixel the slide
            // has reached, so it is left out of the list rather than drawn
            // twice.
            if (moving_ != 0 && unit.id == moving_) continue;
            if (!camera().visible(unit.position.x, unit.position.y)) continue;
            list_.add(
                view::billboard_item(
                    camera(), projection_, view::Layer::unit, unit.position.x,
                    unit.position.y, elevation_at(unit.position.x, unit.position.y),
                    0, static_cast<std::uint32_t>(i)
                )
            );
        }
        list_.sort();
    }

    // Walking the sorted list forwards is the whole of the occlusion rule: the
    // GPU paints in command order and owns no depth buffer.
    void draw_list(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) {
        for (int i = 0; i < list_.size(); ++i) {
            const view::DrawItem& item = list_[i];
            if (item.layer == view::Layer::terrain) {
                const int kind = static_cast<int>(item.subject);
                const int cell = terrain_cell_of[kind][item.variant];
                const int clut = terrain_clut_of[kind];
                if (cell >= 0 && clut >= 0) {
                    gpu::draw_cell_scaled(
                        item.x, item.y, drawn_tile(), cell, clut
                    );
                }
                // The wash, drawn where the cell was so the depth order the
                // list computed is the depth order it is painted in. A lit
                // tile behind a raised one is washed before that cell lifts
                // over it.
                // The splash wins over both, because it is not a third
                // meaning at all: it is the cursor saying how wide it is, and
                // an area cast lands on more than one tile.
                const bool splash =
                    turn::contains(overlay.splash, item.cell_x, item.cell_y);
                const bool danger = turn::contains(overlay.danger, item.cell_x, item.cell_y);
                const bool move = turn::contains(overlay.moves, item.cell_x, item.cell_y);
                if (danger || move || splash) {
                    draw_wash(
                        item.x, item.y,
                        splash ? cursor_colour
                               : danger ? danger_colour : lit_colour(overlay)
                    );
                }
                if (flash_lit_ && item.cell_x == flash_x_ && item.cell_y == flash_y_) {
                    gpu::fill(item.x, item.y, drawn_tile(), drawn_tile(), flash_colour);
                }
            } else if (item.layer == view::Layer::unit) {
                const sim::UnitSnapshot& unit = snapshot.units[item.subject];
                int x = item.x;
                int y = item.y;
                if (flinching_ != 0 && unit.id == flinching_) {
                    x += flinch_dx_;
                    y += flinch_dy_;
                }
                draw_unit(unit, x, y);
                // And the frame that says this one is finished, over the token
                // rather than under it, so it reads on a character standing on
                // a lit tile. Framed rather than dimmed: the token art is a
                // fixed CLUT this machine uploads once, and there is no second,
                // greyer copy of it to draw. A frame costs four fills and no
                // palette at all. The character stays where it is, at full
                // strength, with its own colours, and is simply ringed off.
                if (unit.has_acted) draw_wash(x, y, spent_colour);
            }
        }
        // The token in motion, last, and therefore in front of everything.
        //
        // That is not the depth order the list would have given it, and it is
        // the honest trade rather than an oversight: a token part way between
        // two cells belongs to neither, so it has no `depth_key`, and a token
        // sliding behind a raised cell is drawn over it for six frames.
        // The board it lands on is composed normally, so nothing a checkpoint
        // photographs is affected.
        if (moving_ != 0) {
            const sim::UnitSnapshot* unit = unit_of(snapshot, moving_);
            if (unit != nullptr) draw_unit(*unit, move_px_, move_py_);
        }
    }

    void draw_unit(const sim::UnitSnapshot& unit, int x, int y) {
        const int drawing = drawing_index_of(unit);
        if (drawing < 0) return;
        const int cell = character_cell_of[drawing];
        const int clut = character_clut_of[drawing];
        if (cell < 0 || clut < 0) return;
        gpu::draw_cell_scaled(x, y, drawn_tile(), cell, clut);
    }

    void draw_wash(int left, int top, std::uint16_t colour) {
        const int drawn = drawn_tile();
        const int side = drawn - 2 * wash_thickness;
        gpu::fill(left, top, drawn, wash_thickness, colour);
        gpu::fill(
            left, top + drawn - wash_thickness, drawn, wash_thickness, colour
        );
        gpu::fill(
            left, top + wash_thickness, wash_thickness, side, colour
        );
        gpu::fill(
            left + drawn - wash_thickness, top + wash_thickness,
            wash_thickness, side, colour
        );
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
        const int arm = bracket_arm_for(drawn);
        const int inset = emphasised ? view::cursor_emphasis_inset(drawn) : 0;
        const int left = cell_left(cell_x) + inset;
        const int top = cell_top(cell_x, cell_y) + inset;
        const int span = drawn - 2 * inset;
        for (int corner = 0; corner < 4; ++corner) {
            const bool right = (corner & 1) != 0;
            const bool bottom = (corner & 2) != 0;
            const int x = right ? left + span - arm : left;
            const int y = bottom ? top + span - bracket_thickness : top;
            gpu::fill(x, y, arm, bracket_thickness, colour);
            const int vx = right ? left + span - bracket_thickness : left;
            const int vy = bottom ? top + span - arm : top;
            gpu::fill(vx, vy, bracket_thickness, arm, colour);
        }
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
        if (hovered != nullptr) {
            Line status;
            // Who, not what kind. The name the shared client composed, so this
            // bar and the transcript beside it cannot call one character two
            // things. It also lets a board fielding two of a kind tell them
            // apart, which a unit type never could.
            status
                .text(
                    overlay.hovered_name != nullptr ? overlay.hovered_name : ""
                )
                // Second person, as every other line on this console is: the
                // status bar says YOUR TURN and the board menu END YOUR TURN,
                // so a panel saying OURS spoke for a different person than the
                // rest of the screen. THEIRS is unchanged and is still the
                // longer branch, so the row's budget is untouched.
                .text(
                    hovered->side == player_side()
                        ? (hovered->local_player == 2U ? " P2  HP "
                                                       : " P1  HP ")
                        : " ENEMY  HP "
                )
                .decimal(static_cast<std::uint32_t>(hovered->health))
                .text("/")
                .decimal(static_cast<std::uint32_t>(hovered->maximum_health));
            // And whether this one is finished. Said in the bar rather than
            // left to the token, because a character who has already taken
            // their turn is still worth resting the cursor on while the player
            // reads their line to plan the next pick, and this is the row that
            // already answers "who is this".
            if (hovered->has_acted) status.text("  SPENT");
            write_row(row_hovered, status.c_str());
        } else if (overlay.round != nullptr) {
            // Where a board is won by outlasting rounds, the count goes on the
            // hovered row and only when the cursor rests on nobody, so nothing
            // a player was already reading is taken away.
            write_row(row_hovered, overlay.round);
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
            write_row(row_message, overlay.message);
        } else if (overlay.hovered_class != nullptr) {
            write_row(row_message, overlay.hovered_class);
        }
        if (overlay.aiming != nullptr) write_row(row_aiming, overlay.aiming);
        write_row(row_hint, "X ACT  O BACK  /\\ MENU  START WAIT");
    }

    // The sheet is a screen and not a panel, and that is forced rather than
    // chosen. `grandleon::sheet` composes forty columns because that is what a
    // 320-pixel console holds in an eight-pixel font, and this display is forty
    // columns wide. There is no rectangle a sheet could occupy that is not the
    // whole width, and a board ten cells across has nothing left beside it.
    void draw_sheet(const grandleon::sheet::UnitSheet& sheet) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        for (int line = 0; line < sheet.count && sheet_top + line < sheet_footer_row;
             ++line) {
            write_row(sheet_top + line, sheet.line(line));
        }
        write_row(sheet_footer_row, "X OR O  BACK");
    }

    void draw_menu(const turn::Overlay& overlay) {
        const MenuBox box = menu_box(overlay);
        if (box.width <= 0) return;
        gpu::fill(
            box.left * glyph, box.top * glyph, box.width * glyph,
            box.height * glyph, paper_colour
        );
        for (int row = 0; row < overlay.menu_rows; ++row) {
            // The chosen row is named by a caret rather than by a colour: a
            // caret costs no palette entry on any machine, and two consoles
            // that named a selection differently would be two different menus.
            Line line;
            line.text(row == overlay.menu_row ? ">" : " ")
                .text(" ")
                .text(overlay.menu[row].label);
            write_at(box.left, box.top + row, line.c_str());
        }
    }

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    [[nodiscard]] static int text_length(const char* value) noexcept {
        int length = 0;
        while (value != nullptr && value[length] != '\0') ++length;
        return length;
    }

    [[nodiscard]] static int centred_column(const char* value) noexcept {
        const int length = text_length(value);
        return length < screen_cols ? (screen_cols - length) / 2 : 0;
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
            if (i == 0U && token && value == 'A') replacement = "X";
            if (token && value == 'B') replacement = "O";
            if (token && value == 'C') replacement = "/\\";
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

    void write_centred(int row, const char* value, int clut) {
        write_at_with_clut(centred_column(value), row, value, clut);
    }

    void write_controls_centred(int row, const char* value, int clut) {
        char translated[screen_cols + 1]{};
        control_text(value, translated, sizeof translated);
        write_centred(row, translated, clut);
    }

    void draw_title_screen(const turn::ScreenView& view) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        gpu::fill(24, 18, 272, 2, menu_gold_colour);
        gpu::fill(52, 70, 216, 1, menu_muted_colour);
        gpu::fill(52, 198, 216, 1, menu_muted_colour);
        write_scaled_at(88, 30, "GRANDLEON", 16, font_gold_clut);
        if (view.page != nullptr && view.page->count > 2) {
            write_centred(11, view.page->line(2), font_gold_clut);
        }
        write_centred(
            14, "A CAMPAIGN YOUR MEMORY CARD KEEPS", font_muted_clut
        );
        write_controls_centred(26, view.footer, font_clut);
    }

    void draw_slot_screen(const turn::ScreenView& view) {
        gpu::fill(0, 0, gpu::screen_width, gpu::screen_height, paper_colour);
        gpu::fill(24, 14, 272, 2, menu_gold_colour);
        write_scaled_at(106, 22, "GRANDLEON", 12, font_gold_clut);
        if (view.page != nullptr && view.page->count > 0) {
            write_centred(7, view.page->line(0), font_gold_clut);
        }
        write_centred(10, "CHOOSE A SAVE", font_muted_clut);

        for (int choice = 0; choice < view.choices; ++choice) {
            const int page_row = view.first_choice_row + choice;
            if (view.page == nullptr || page_row >= view.page->count) break;
            const int row = 14 + choice * 2;
            const bool selected = page_row == view.caret_row;
            if (selected) {
                gpu::fill(24, row * glyph - 2, 272, 12, menu_gold_colour);
                gpu::fill(26, row * glyph, 268, 8, paper_colour);
                write_at_with_clut(4, row, ">", font_gold_clut);
            }
            write_at_with_clut(
                6, row, view.page->line(page_row),
                selected ? font_gold_clut : font_clut
            );
        }
        write_centred(24, "THE GAME SAVES ITSELF", font_muted_clut);
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
        if (view.screen == turn::Screen::slot) {
            draw_slot_screen(view);
            return;
        }
        draw_scene_backdrop(view.backdrop);
        draw_speaker_face(view);
        gpu::fill(
            0, screen_panel_top * glyph, gpu::screen_width,
            screen_panel_rows * glyph, paper_colour
        );
        if (view.page != nullptr) {
            for (int row = 0; row < view.page->count &&
                              row < turn::page_capacity;
                 ++row) {
                write_at(
                    screen_page_left, screen_page_top + row, view.page->line(row)
                );
            }
        }
        // The caret is a glyph in a column of its own rather than a colour on
        // the row, for the reason the board's action menu gives: a caret costs
        // no palette entry on any machine, and two consoles that named a
        // selection differently would be two different menus.
        if (view.caret_row >= 0 && view.caret_row < turn::page_capacity) {
            write_at(
                screen_caret_column, screen_page_top + view.caret_row, ">"
            );
        }
        if (view.footer != nullptr && view.footer[0] != '\0') {
            gpu::fill(
                0, screen_footer_row * glyph, gpu::screen_width, glyph,
                paper_colour
            );
            char translated[screen_cols + 1]{};
            control_text(view.footer, translated, sizeof translated);
            write_at(screen_page_left, screen_footer_row, translated);
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
        const int cell = character_cell_of[drawing];
        const int clut = character_clut_of[drawing];
        if (cell < 0 || clut < 0) return;
        gpu::draw_cell_scaled(
            screen_face_left, screen_face_top, screen_face_size, cell, clut
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

    // Only the glyphs that are not spaces, because the box beneath them has
    // already been filled with paper and a space glyph is solid paper. That
    // turns a full-screen sheet from twelve hundred textured rectangles into a
    // few hundred, and changes not one pixel.
    void write_at_with_clut(
        int column, int row, const char* value, int clut
    ) {
        if (value == nullptr || clut < 0) return;
        if (row < 0 || row >= screen_rows) return;
        for (int i = 0; value[i] != '\0'; ++i) {
            const int at = column + i;
            if (at >= screen_cols) break;
            char character = value[i];
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - ('a' - 'A'));
            }
            if (character == ' ') continue;
            const int slot = character - view::first_glyph;
            if (slot <= 0 || slot >= view::glyph_count) continue;
            gpu::draw_glyph(at * glyph, row * glyph, slot, clut);
        }
    }

    void write_at(int column, int row, const char* value) {
        write_at_with_clut(column, row, value, font_clut);
    }

    void write_scaled_at(
        int x, int y, const char* value, int size, int clut
    ) {
        if (value == nullptr || clut < 0 || size <= 0) return;
        for (int i = 0; value[i] != '\0'; ++i) {
            char character = value[i];
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - ('a' - 'A'));
            }
            if (character == ' ') continue;
            const int slot = character - view::first_glyph;
            if (slot <= 0 || slot >= view::glyph_count) continue;
            gpu::draw_glyph_scaled(x + i * size, y, size, slot, clut);
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
                against_column = cell_left(unit.position.x) / glyph;
                against_row =
                    cell_top(unit.position.x, unit.position.y) / glyph;
                break;
            }
        }

        // Wrapped here rather than by the client, because how many columns fit
        // is this display's business and the two consoles have different ones.
        char rows[bubble_rows][bubble_columns + 1] = {};
        int row_count = 0;
        int at = 0;
        for (int i = 0; what != nullptr && what[i] != '\0';) {
            // The next word, and whether it still fits on this row.
            int word = 0;
            while (what[i + word] != '\0' && what[i + word] != ' ') ++word;
            if (at > 0 && at + 1 + word > bubble_columns) {
                if (++row_count >= bubble_rows) break;
                at = 0;
            }
            if (at > 0) rows[row_count][at++] = ' ';
            for (int c = 0; c < word && at < bubble_columns; ++c) {
                rows[row_count][at++] = what[i + c];
            }
            i += word;
            while (what[i] == ' ') ++i;
        }
        if (at > 0) ++row_count;
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
        write_at(left, top, who);
        for (int row = 0; row < row_count; ++row) {
            write_at(left, top + 1 + row, rows[row]);
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
        const int start_x = cell_left(from_x);
        const int start_y = cell_top(from_x, from_y);
        const int end_x = cell_left(to_x);
        const int end_y = cell_top(to_x, to_y);
        // A move of no tiles is a gesture of no frames, and its token still has
        // to be somewhere: `draw_list` draws the moving token out of these two
        // and the compose left it out of the list.
        move_px_ = end_x;
        move_py_ = end_y;
        move_world_x_ = to_x * 256 + 128;
        move_world_z_ = to_y * 256 + 128;
        if (to_x < from_x) move_facing_ = -1;
        else if (to_x > from_x) move_facing_ = 1;
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

    [[nodiscard]] int cell_left(int x) const {
        return projection_.cell_left(camera(), x);
    }
    [[nodiscard]] int cell_top(int x, int y) const {
        return projection_.cell_top(camera(), y, elevation_at(x, y));
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
    [[nodiscard]] int kind_of(std::uint64_t identity) const {
        const std::uint8_t kind = shown_->kind_of_terrain(identity);
        return kind < art::terrain_kind_count ? static_cast<int>(kind) : -1;
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

    [[nodiscard]] static MenuBox menu_box(const turn::Overlay& overlay) {
        MenuBox box;
        if (overlay.menu == nullptr || overlay.menu_rows <= 0) return box;
        int width = 0;
        for (int row = 0; row < overlay.menu_rows; ++row) {
            int length = 0;
            const char* label = overlay.menu[row].label;
            while (label != nullptr && label[length] != '\0') ++length;
            if (length > width) width = length;
        }
        box.left = menu_left;
        box.top = menu_top;
        // The caret, the space after it, and a column of paper on the right so
        // the box has an edge on both sides.
        box.width = width + 3;
        box.height = overlay.menu_rows;
        if (box.left + box.width > screen_cols) {
            box.width = screen_cols - box.left;
        }
        return box;
    }

    // The first ink pixel of a glyph, computed out of the font this executable
    // uploaded rather than looked for in the frame. A claim about a pixel the
    // letter itself sets is a claim no blank box can satisfy.
    void claim_glyph(
        const char* label, int column, int row, char value,
        std::uint16_t colour = ink_colour
    ) {
        char character = value;
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - ('a' - 'A'));
        }
        for (int y = 0; y < gpu::glyph_texels; ++y) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                if (!view::glyph_pixel(character, x, y)) continue;
                probe(label, column * glyph + x, row * glyph + y, colour);
                return;
            }
        }
        // A glyph with no ink at all is a space, and solid paper is the whole
        // of what there is to claim about one.
        probe(label, column * glyph, row * glyph, paper_colour);
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
        for (int i = 0; text[i] != '\0' && column + i < screen_cols; ++i) {
            if (text[i] == ' ') continue;
            claim_glyph(label, column + i, row, text[i], colour);
            return;
        }
    }

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
    view::Projection projection_{
        0, 0, native_tile, view::elevation_step_for(native_tile)
    };
    view::DrawList list_{draw_storage, draw_capacity};

    const sim::EncounterSnapshot* held_ = nullptr;
    const turn::Overlay* last_overlay_ = nullptr;

    std::size_t consumed_ = 0;
    bool steered_ = false;
    std::uint16_t pending_buttons_[2]{};
    std::uint8_t pad_priority_{0};

    sim::UnitId moving_ = 0;
    int move_px_ = 0;
    int move_py_ = 0;
    int move_world_x_ = 0;
    int move_world_z_ = 0;
    int move_bob_ = 0;
    int move_facing_ = 1;
    int move_lean_ = 0;
    sim::UnitId flinching_ = 0;
    int flinch_dx_ = 0;
    int flinch_dy_ = 0;
    int flash_x_ = -1;
    int flash_y_ = -1;
    bool flash_lit_ = false;

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
#ifndef GRANDLEON_PSX_AUTOPILOT
    // Presence is learned by a real poll, not by an answer counter left at
    // zero after pad::begin(). Consuming an edge here also prevents a button
    // held during boot from becoming the first board command.
    (void)pad::pressed(pad::Port::two);
    const bool second_controller_connected =
        pad::answers(pad::Port::two) > 0;
    const auto launched_campaign = pr::load_campaign(
        loaded.package, core::stable_content_id_v1(campaign_path)
    );
    expect(
        static_cast<bool>(launched_campaign),
        "the campaign launch policy loads"
    );
    if (launched_campaign) {
        switch (launched_campaign.definition.player_mode) {
            case pr::LocalPlayerMode::one:
                local_players = 1U;
                break;
            case pr::LocalPlayerMode::two:
                expect(
                    second_controller_connected,
                    "the two-player campaign has controller two"
                );
                if (!second_controller_connected) give_up();
                local_players = 2U;
                break;
            case pr::LocalPlayerMode::choice:
                // On PlayStation the physical choice is enough: boot with a
                // second pad connected for co-op, or with one for solo.
                local_players = second_controller_connected ? 2U : 1U;
                break;
        }
    }
#endif

    const auto presentation = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(presentation), "the presentation section reads");

#ifndef GRANDLEON_PSX_3D
    const int theme = presentation.presentation.theme < art::theme_count
                          ? presentation.presentation.theme
                          : 0;
#endif

    // Nothing the art library holds, and nothing the interface draws, may be
    // the backdrop, or a pixel nobody drew would be indistinguishable from a
    // pixel somebody drew.
    {
        bool separated = true;
#ifndef GRANDLEON_PSX_3D
        for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
            for (int variant = 0; variant < art::variant_count; ++variant) {
                const art::Asset asset = art::terrain(theme, kind, variant);
                for (int i = 0; i < art::clut_size; ++i) {
                    if (asset.clut[i] == backdrop) separated = false;
                }
            }
        }
#endif
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

#ifndef GRANDLEON_PSX_3D
    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        terrain_clut_of[kind] = -1;
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell_of[kind][variant] = -1;
        }
    }
#endif
    for (int drawing = 0; drawing < art::character_count; ++drawing) {
        character_cell_of[drawing] = -1;
        character_clut_of[drawing] = -1;
    }

    // Everything the board can draw, resident before the first press. A repaint
    // costs primitives and no transfers, which is what makes a frame's cost
    // independent of what is on it.
#ifndef GRANDLEON_PSX_3D
    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        const art::Asset first = art::terrain(theme, kind, 0);
        terrain_clut_of[kind] = claim_clut(first.clut);
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell_of[kind][variant] =
                claim_cell(art::terrain(theme, kind, variant).texels);
        }
    }
#endif
    for (int drawing = 0; drawing < art::character_count; ++drawing) {
        const art::Asset asset = art::character(drawing);
        character_clut_of[drawing] = claim_clut(asset.clut);
        character_cell_of[drawing] = claim_cell(asset.texels);
    }
    build_font();
    build_font_clut();

    expect(cells_fit, "every cell the board needs fits in the texture pages");
    expect(cluts_fit, "every CLUT the board needs fits in the CLUT band");
    expect(
        font_clut >= 0 && font_gold_clut >= 0 && font_muted_clut >= 0,
        "the interface fonts have CLUTs"
    );

    Line()
        .text("VRAM cells ")
        .decimal(static_cast<std::uint32_t>(cells_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::cell_capacity))
        .text("; cluts ")
        .decimal(static_cast<std::uint32_t>(cluts_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::clut_capacity))
        .text("; font ")
        .decimal(static_cast<std::uint32_t>(gpu::font_halfwords_across * gpu::font_lines))
        .text(" halfwords")
        .flush();

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
        view::slot_menu_rows
    );
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
#ifdef GRANDLEON_PSX_3D
        .text(" budget 33367 target 30fps")
#else
        .text(" budget 16667")
#endif
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
