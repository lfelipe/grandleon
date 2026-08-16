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
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/view/glyphs.hpp>
#include <grandleon/view/motion.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "psx_art.h"
#include "psx_gpu.h"
#include "psx_pad.h"
#include "psx_runtime.h"
#include "themes.h"

#ifdef GRANDLEON_PSX_AUTOPILOT
#include "fordlight_pad.h"
#endif

// Which project's bytes this executable carries. Tarnholt's board, unless a
// campaign build says otherwise, because a turn executable's subject is a
// board with two sides, real terrain and mountains on it, and the demo is a
// teaching board.
#ifdef GRANDLEON_PSX_DEMO_CAMPAIGN
#include "generated/demo_package.h"
#else
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
// Two projects, and one of them is only ever a campaign build: the turn
// executable's subject is the Fordlight Crossing, the board
// `fordlight_pad.h`'s script was written against. `campaign_pad.h` carries a
// script for each.
#ifdef GRANDLEON_PSX_DEMO_CAMPAIGN
constexpr const char* campaign_path = "demo_campaign";
constexpr const std::uint8_t* campaign_package_bytes = demo_package_bytes;
constexpr std::size_t campaign_package_size = demo_package_size;
#else
constexpr const char* campaign_path = "tarnholt_line";
constexpr const std::uint8_t* campaign_package_bytes = board_package_bytes;
constexpr std::size_t campaign_package_size = board_package_size;
#endif

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
// What the campaign is kept under on the card, and what the title screen calls
// the project. The slot base is a storage name: lower case, digits, `_` and
// `-`. `view::slot_name_at` makes rows two to four out of it.
#ifdef GRANDLEON_PSX_DEMO_CAMPAIGN
constexpr const char* campaign_slot_base = "demo";
constexpr const char* project_title = "THE BRIDGE AT DAWN";
#else
constexpr const char* campaign_slot_base = "tarnholt";
constexpr const char* project_title = "TARNHOLT";
#endif
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

// The board's cell, and the screen's own numbers.
constexpr int tile = art::cell_size;
constexpr int glyph = gpu::glyph_texels;
constexpr int screen_cols = gpu::screen_width / glyph;
constexpr int screen_rows = gpu::screen_height / glyph;
constexpr int bar_rows = 4;
constexpr int bar_top = gpu::screen_height - bar_rows * glyph;
constexpr int view_cols = gpu::screen_width / tile;
constexpr int view_rows = bar_top / tile;

static_assert(
    view_cols == turn::viewport_cols && view_rows == turn::viewport_rows,
    "the window the bar left is not the window the expectations assume"
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

// The cursor's four corner brackets: how long an arm is and how thick.
constexpr int bracket_arm = tile / 4;
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
int character_cell_of[art::archetype_count][art::faction_colour_count];
int character_clut_of[art::archetype_count][art::faction_colour_count];
int font_clut = -1;

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
void build_font_clut() {
    unsigned short entries[art::clut_size];
    for (int i = 0; i < art::clut_size; ++i) entries[i] = paper_colour;
    entries[1] = ink_colour;
    font_clut = claim_clut(entries);
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

// The opaque texel of a cell nearest its centre, searched outward in rings and
// confined to the middle half so the texel it finds can never be one the
// cursor's brackets or a wash's frame is drawn over. `play_exe.cpp` searches
// the whole cell because it draws neither.
[[nodiscard]] bool opaque_near_centre(
    const art::Asset& asset, int& out_x, int& out_y
) {
    const int centre = tile / 2;
    const int inset = tile / 4;
    for (int radius = 0; radius < inset; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0 && dx > -radius && dx < radius && dy > -radius &&
                    dy < radius) {
                    continue;
                }
                const int x = centre + dx;
                const int y = centre + dy;
                if (x < inset || y < inset || x >= tile - inset ||
                    y >= tile - inset) {
                    continue;
                }
                if (art::colour_at(asset, x, y) != 0) {
                    out_x = x;
                    out_y = y;
                    return true;
                }
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

    [[nodiscard]] bool covers(int column, int row) const noexcept {
        if (width <= 0 || height <= 0) return false;
        return column + tile / glyph > left && column < left + width &&
               row + tile / glyph > top && row < top + height;
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

// Storage for the draw list. Sized from the window rather than rounded up: one
// item per visible cell and one per visible living unit, and ten by six is
// sixty.
constexpr int draw_capacity = view_cols * view_rows + 16;
view::DrawItem draw_storage[draw_capacity];

// ---------------------------------------------------------------------------
// The console's half of the client
// ---------------------------------------------------------------------------
class PlayStationClient final : public turn::TurnClient {
public:
    PlayStationClient(turn::ReportSink& sink, const pr::Presentation& shown) noexcept
        : TurnClient(sink), shown_(&shown) {}

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
        std::uint32_t frame = 0;
        bool emphasised = false;
        for (;;) {
            psx::wait_vblank();
            const std::uint16_t live = pad::pressed();
            if (live != 0) {
#ifdef GRANDLEON_PSX_AUTOPILOT
                // Somebody has picked up a controller during a scripted run.
                // From here the script is over and the board is theirs; a run
                // under the harness never reaches this, because a headless
                // emulator has no pad to press.
                steered_ = true;
#endif
                if (emphasised) draw_held(false);
                return live;
            }
#ifdef GRANDLEON_PSX_AUTOPILOT
            if (!steered_ &&
                frame >= static_cast<std::uint32_t>(script_dwell_frames)) {
                if (emphasised) draw_held(false);
                if (consumed_ >= script_press_count) {
                    return turn::pad_end_of_script;
                }
                return script_presses[consumed_++];
            }
#endif
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
            claim_first_glyph_at(
                "footertext", screen_page_left, screen_footer_row, view.footer
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
        int separation
    ) override {
        play_attack(
            before, struck, striker, toward_x, toward_y, gesture, separation,
            true
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
        play_attack(before, struck, striker, 0, 0, gesture, separation, false);
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
        bool landed
    ) {
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
            draw(before, *last_overlay_, false);
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
                flinch_dx_ = view::flinch_offset(frame, toward_x, tile);
                flinch_dy_ = view::flinch_offset(frame, toward_y, tile);
            }
            flash_lit_ = (frame % 2) == 0;
            psx::wait_vblank();
            draw(before, *last_overlay_, false);
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
                if (box.covers(left / glyph, top / glyph)) continue;
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
            if (!opaque_near_centre(asset, px, py)) continue;
            const int at_x = cell_left(unit.position.x) + px;
            const int at_y = cell_top(unit.position.x, unit.position.y) + py;
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
        layout(snapshot);
        compose(snapshot);
        draw_list(snapshot, overlay);
        draw_cursor(overlay, emphasised);
        draw_bar(snapshot, overlay);
        if (overlay.menu != nullptr) draw_menu(overlay);
    }

    void layout(const sim::EncounterSnapshot& snapshot) {
        board_width_ = snapshot.width;
        int highest = 0;
        for (std::size_t i = 0; i < terrain().size(); ++i) {
            const int elevation = elevation_of_kind(kind_of(terrain()[i]));
            if (elevation > highest) highest = elevation;
        }
        const int step = view::elevation_step_for(tile);
        int reserved = view::headroom(highest, step, tile);
        int slack = bar_top - tile * camera().view_h;
        if (slack < 0) slack = 0;
        if (reserved > slack) reserved = slack;
        // No rounding. A primitive here is positioned per pixel, so the
        // projection's own origin is used, with nothing snapped to a grid.
        const int origin_x = (gpu::screen_width - tile * camera().view_w) / 2;
        const int origin_y =
            reserved + (bar_top - reserved - tile * camera().view_h) / 2;
        projection_ = view::Projection{origin_x, origin_y, tile, step};
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
                    gpu::draw_cell(item.x, item.y, cell, clut);
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
                    gpu::fill(item.x, item.y, tile, tile, flash_colour);
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
        const int archetype = archetype_of(unit.unit_type_id);
        const int colour = colour_of(unit);
        const int cell = character_cell_of[archetype][colour];
        const int clut = character_clut_of[archetype][colour];
        if (cell < 0 || clut < 0) return;
        gpu::draw_cell(x, y, cell, clut);
    }

    void draw_wash(int left, int top, std::uint16_t colour) {
        gpu::fill(left, top, tile, wash_thickness, colour);
        gpu::fill(left, top + tile - wash_thickness, tile, wash_thickness, colour);
        gpu::fill(
            left, top + wash_thickness, wash_thickness,
            tile - 2 * wash_thickness, colour
        );
        gpu::fill(
            left + tile - wash_thickness, top + wash_thickness, wash_thickness,
            tile - 2 * wash_thickness, colour
        );
    }

    // Four corner brackets. At rest they sit on the cell's own corners; the
    // pulse moves them inward by `view::cursor_emphasis_inset`, which is
    // deliberately short of the centre so the pulse can never change what a
    // centre-sampling probe reads.
    void draw_cursor(const turn::Overlay& overlay, bool emphasised) {
        if (!camera().visible(overlay.cursor_x, overlay.cursor_y)) return;
        const int inset = emphasised ? view::cursor_emphasis_inset(tile) : 0;
        const int left = cell_left(overlay.cursor_x) + inset;
        const int top = cell_top(overlay.cursor_x, overlay.cursor_y) + inset;
        const int span = tile - 2 * inset;
        for (int corner = 0; corner < 4; ++corner) {
            const bool right = (corner & 1) != 0;
            const bool bottom = (corner & 2) != 0;
            const int x = right ? left + span - bracket_arm : left;
            const int y = bottom ? top + span - bracket_thickness : top;
            gpu::fill(x, y, bracket_arm, bracket_thickness, cursor_colour);
            const int vx = right ? left + span - bracket_thickness : left;
            const int vy = bottom ? top + span - bracket_arm : top;
            gpu::fill(vx, vy, bracket_thickness, bracket_arm, cursor_colour);
        }
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
                    hovered->side == player_side() ? " YOURS  HP "
                                                   : " THEIRS  HP "
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
    // A campaign screen, drawn: the scene behind it, the panel the words sit
    // on, the words, the caret and the footer. Five `gpu::fill`s and a few
    // hundred glyphs, which is less than the board costs.
    void draw_screen(const turn::ScreenView& view) {
        draw_scene_backdrop(view.backdrop);
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
            write_at(screen_page_left, screen_footer_row, view.footer);
        }
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
    void write_at(int column, int row, const char* value) {
        if (value == nullptr || font_clut < 0) return;
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
            gpu::draw_glyph(at * glyph, row * glyph, slot, font_clut);
        }
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
        for (int frame = 1; frame <= frames; ++frame) {
            move_px_ = view::slide_between(start_x, end_x, frame, frames);
            move_py_ = view::slide_between(start_y, end_y, frame, frames);
            psx::wait_vblank();
            draw(before, *last_overlay_, false);
        }
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
    [[nodiscard]] int colour_of(const sim::UnitSnapshot& unit) const {
        const std::uint8_t colour = shown_->colour_of_unit_type(unit.unit_type_id);
        if (colour == pr::colour_unresolved || colour >= art::faction_colour_count) {
            return unit.side == sim::Side::first ? 0 : 1;
        }
        return static_cast<int>(colour);
    }
    [[nodiscard]] art::Asset unit_asset(const sim::UnitSnapshot& unit) const {
        return art::character(archetype_of(unit.unit_type_id), colour_of(unit));
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
    void claim_glyph(const char* label, int column, int row, char value) {
        char character = value;
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - ('a' - 'A'));
        }
        for (int y = 0; y < gpu::glyph_texels; ++y) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                if (!view::glyph_pixel(character, x, y)) continue;
                probe(label, column * glyph + x, row * glyph + y, ink_colour);
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
        const char* label, int column, int row, const char* text
    ) {
        if (text == nullptr) return;
        for (int i = 0; text[i] != '\0' && column + i < screen_cols; ++i) {
            if (text[i] == ' ') continue;
            claim_glyph(label, column + i, row, text[i]);
            return;
        }
    }

    const pr::Presentation* shown_;
    int board_width_ = 0;
    view::Projection projection_{0, 0, tile, view::elevation_step_for(tile)};
    view::DrawList list_{draw_storage, draw_capacity};

    const sim::EncounterSnapshot* held_ = nullptr;
    const turn::Overlay* last_overlay_ = nullptr;

    std::size_t consumed_ = 0;
    bool steered_ = false;

    sim::UnitId moving_ = 0;
    int move_px_ = 0;
    int move_py_ = 0;
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
        return pf::load_mock_package(package_bytes, options);
    }();
    expect(loaded.error == pf::Error::none, "the compiled package opens");
    if (loaded.error != pf::Error::none) give_up();

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
        for (int a = 0; a < art::archetype_count; ++a) {
            for (int c = 0; c < art::faction_colour_count; ++c) {
                const art::Asset asset = art::character(a, c);
                for (int i = 0; i < art::clut_size; ++i) {
                    if (asset.clut[i] == backdrop) separated = false;
                }
            }
        }
        const std::uint16_t interface_colours[] = {
            ink_colour, paper_colour, cursor_colour,
            move_colour, aim_colour, danger_colour, flash_colour
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
    for (int a = 0; a < art::archetype_count; ++a) {
        for (int c = 0; c < art::faction_colour_count; ++c) {
            character_cell_of[a][c] = -1;
            character_clut_of[a][c] = -1;
        }
    }

    // Everything the board can draw, resident before the first press. A repaint
    // costs primitives and no transfers, which is what makes a frame's cost
    // independent of what is on it.
    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        const art::Asset first = art::terrain(theme, kind, 0);
        terrain_clut_of[kind] = claim_clut(first.clut);
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell_of[kind][variant] =
                claim_cell(art::terrain(theme, kind, variant).texels);
        }
    }
    for (int a = 0; a < art::archetype_count; ++a) {
        for (int c = 0; c < art::faction_colour_count; ++c) {
            const art::Asset asset = art::character(a, c);
            character_clut_of[a][c] = claim_clut(asset.clut);
            character_cell_of[a][c] = claim_cell(asset.texels);
        }
    }
    build_font();
    build_font_clut();

    expect(cells_fit, "every cell the board needs fits in the texture pages");
    expect(cluts_fit, "every CLUT the board needs fits in the CLUT band");
    expect(font_clut >= 0, "the font has a CLUT");

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
        .decimal(static_cast<std::uint32_t>(tile))
        .text(" view ")
        .decimal(static_cast<std::uint32_t>(view_cols))
        .text("x")
        .decimal(static_cast<std::uint32_t>(view_rows))
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
    game.set_viewport(view_cols, view_rows);
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
        project_title, campaign_slot_base, holds, view::slot_menu_rows
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
    const client::SessionError status = client::run_campaign(
        loaded.package, campaign_id, sim::Side::first, game
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
