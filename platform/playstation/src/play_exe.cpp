// SPDX-License-Identifier: MIT
// The PlayStation play executable: a board, drawn.
//
// It opens a package compiled on the host, creates the encounter the package
// names, asks `grandleon::view` where every cell and every unit goes, and puts
// that on the screen through the GPU. Then it says, over the BIOS teletype,
// where every cell is and what colour it should be, and reads the framebuffer
// back through the GPU to say what colour is actually there.
// `platform/playstation/harness` decides whether those agree.
//
// What it is not: there is no input, no turn, no campaign and no game. This
// wave carries the entry gate as far as "it renders", and a playable console is
// downstream of that.
//
// ---------------------------------------------------------------------------
// How the picture is made, and how little of it is this machine's business
//
// Almost none of this executable's length goes on hardware arithmetic. There is
// no palette bank to allocate a board out of, no deciding which cells can live
// on a plane and which have to become sprites because their lift is not a
// multiple of eight, and no sprite link list to walk backwards so the picture
// comes out forwards. The reasons are worth naming because they are what a
// renderer on this console is buying:
//
//   * **A primitive is positioned in pixels.** There is no cell grid and no
//     8-pixel rounding, so the projection's origin is used exactly as
//     `grandleon::view` computes it. Nowhere does the renderer choose rather
//     than compute.
//
//   * **Every draw names its own CLUT.** A textured-rectangle packet carries a
//     CLUT address, so each asset keeps the sixteen-colour subset the art
//     library already chose for it. There are no banks, no four-on-screen
//     limit, and nothing for a board to fail to fit into.
//
//   * **The order of the draws is the order of the picture.** The GPU has no
//     depth buffer and composites nothing: it paints primitives as they arrive.
//     `view::DrawList::sort` already puts them back to front, so walking the
//     sorted list *is* the occlusion rule, with no priority bits and no link
//     order to reverse.
//
// What is genuinely this machine's is where things go in VRAM, and
// `psx_gpu.h` holds all of it.
//
// The band below a raised cell is backdrop, and that is not a bug: it is what
// every other client draws. The draw order is elevation-ascending, so the level
// cell in front of a raised one is drawn first and its rectangle begins at the
// raised cell's *unlifted* bottom edge; the `lift` rows between are painted by
// nobody.
//
// The lift is not clamped away. 240 lines hold seven 32-pixel rows with sixteen
// to spare, and the deepest lift the projection offers is twelve, so the
// headroom a raised board asks for is reserved in full and a first-row mountain
// is drawn whole. The Nintendo 64 has to clamp its reservation to the slack the
// frame has; this machine has the lines and does not.
//
// ---------------------------------------------------------------------------
// Where the content comes from
//
// A package compiled on the host, embedded as bytes. The JSON parser cannot be
// linked here at all: it reports a malformed document by throwing, and the
// pinned toolchain's exception-handling archives are compiled -mabicalls
// against glibc. Unlike the Nintendo 64, this executable therefore never sees
// a source project. README.md has the detail.
//
// It needs no companion table beside those bytes, and that is worth recording
// rather than taking for granted. Everything drawn comes out of the package:
// the board, the units and their sides, the faction colours, the theme, and
// (carried by the presentation section) which art-library terrain kind a
// cell's identity draws as and which archetype a unit type wears. The
// encounter is named by its *authored path* through
// `core::stable_content_id_v1`, which is how the conformance executable
// already names the demo campaign, so there is no generated table of ids
// either. The one build-time choice left is the character style, and a project
// that names none takes the art library's first.

#include "grandleon/view/board_view.hpp"

#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "psx_art.h"
#include "psx_gpu.h"
#include "psx_runtime.h"
// The art library's terrain registry, for the elevation each kind stands at.
// Generated beside the kind's colours and marks, and the authority every client
// reads: nothing in a package carries elevation, because elevation is drawing.
#include "themes.h"

#include "generated/board_package.h"

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace view = grandleon::view;

namespace psx = grandleon::playstation;
namespace art = grandleon::playstation::art;
namespace gpu = grandleon::playstation::gpu;

namespace {

using psx::Line;

// Which of the project's encounters this executable draws, by authored path.
// Selected at build time rather than at run time because one executable carries
// one board and nothing here can ask for another; the two builds differ in this
// string and in nothing else.
#ifndef GRANDLEON_PLAYSTATION_ENCOUNTER
#define GRANDLEON_PLAYSTATION_ENCOUNTER "tarnholt_line/fordlight_battle"
#endif
constexpr const char* encounter_path = GRANDLEON_PLAYSTATION_ENCOUNTER;

// The backdrop, and why it is this colour. Nothing the art library can draw is
// (31, 0, 31), because the master palette's 124 entries stay 124 distinct
// colours at five bits per channel and none of them is that one. A probe that
// comes back as the backdrop is therefore a cell that was never drawn, rather
// than a cell drawn in a colour that happens to match. The executable checks
// that separation against the art rather than asserting it from this comment.
constexpr std::uint16_t backdrop = 0x7C1F;

// The host callback slot the harness installs itself on. Any non-zero number
// would do; this one is written down in exactly two places, here and in
// `harness/playstation_probe.lua`, and they have to agree.
constexpr int harness_slot = 13;

int checks = 0;
int failures = 0;
int probes = 0;

void expect(bool condition, const char* name) {
    ++checks;
    if (!condition) ++failures;
    Line().text("CHECK ").text(name).text(condition ? " PASS" : " FAIL").flush();
}

[[noreturn]] void give_up() {
    Line().text("RESULT FAIL 0/1").flush();
    // Under -testmode a non-zero write to the control port ends the emulator
    // process, so a failure here is a failed run rather than a hang. Nugget's
    // own abort() cannot be used for this: it pauses the machine first and
    // waits for a debugger that is not there. See README.md.
    *reinterpret_cast<volatile std::int16_t*>(0x1f802082) = 1;
    for (;;) {
    }
}

// The board's cell size, which is the art library's and not this renderer's.
constexpr int tile = art::cell_size;

// The console's deterministic interior-variant choice, and the one place this
// renderer deliberately does not use `view::autotile_variant`. The mask table
// indexes a 47-variant sheet, and embedding one per terrain kind per theme is
// 47 cells where four will do. So the four interior variants are resident and
// the choice among them is this, exactly as on the Nintendo 64 and for the
// same reason.
[[nodiscard]] constexpr int terrain_variant(int x, int y) noexcept {
    return (x + y * 3) % art::variant_count;
}

// The art library's terrain elevation table, keyed by the same kind index the
// package's terrain join carries.
[[nodiscard]] int elevation_of_kind(int kind) noexcept {
    if (kind < 0 || kind >= grandleon_terrain_kind_count) return 0;
    return grandleon_terrain_elevation[kind];
}

// The terrain kind a cell identity draws as, out of the package's own join.
// Negative when the package cannot say, which the check below turns into a
// failure rather than into a hole in the board.
[[nodiscard]] int kind_of(
    const pr::Presentation& shown, std::uint64_t identity
) noexcept {
    const std::uint8_t kind = shown.kind_of_terrain(identity);
    return kind < art::terrain_kind_count ? static_cast<int>(kind) : -1;
}

// The archetype a unit type wears, out of the same join. A package that cannot
// say draws the roster's first, which is what every client drew before any of
// this was carried.
[[nodiscard]] int archetype_of(
    const pr::Presentation& shown, std::uint64_t unit_type
) noexcept {
    const std::uint8_t archetype = shown.archetype_of_unit_type(unit_type);
    return archetype < art::archetype_count ? static_cast<int>(archetype) : 0;
}

// The faction colour a unit draws in: the package's if it names one, otherwise
// the side it fights on, which is the convention every other client uses.
[[nodiscard]] int colour_of(
    const pr::Presentation& shown, const sim::UnitSnapshot& unit
) noexcept {
    const std::uint8_t colour = shown.colour_of_unit_type(unit.unit_type_id);
    if (colour == pr::colour_unresolved || colour >= art::faction_colour_count) {
        return unit.side == sim::Side::first ? 0 : 1;
    }
    return static_cast<int>(colour);
}

// Storage for the draw list, static and caller-owned so that composing a board
// allocates nothing. A 32-pixel cell puts at most 10x7 cells on a 320x240
// display, so seventy is the whole visible ground and the rest is headroom for
// units and for the overflow flag to mean something rather than to be reached.
constexpr int draw_capacity = 128;
view::DrawItem draw_storage[draw_capacity];

// ---------------------------------------------------------------------------
// VRAM residency
//
// A cell is uploaded once however many times the board draws it, and so is a
// CLUT. Both are keyed by what the art library calls the thing rather than by
// a pointer, because two assets can legitimately share a CLUT: the four
// interior variants of a terrain kind do, since the profile chose one palette
// over the whole sheet.
// ---------------------------------------------------------------------------

int terrain_cell_of[art::terrain_kind_count][art::variant_count];
int terrain_clut_of[art::terrain_kind_count];
int character_cell_of[art::archetype_count][art::faction_colour_count];
int character_clut_of[art::archetype_count][art::faction_colour_count];

int cells_used = 0;
int cluts_used = 0;
bool cells_fit = true;
bool cluts_fit = true;

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

// ---------------------------------------------------------------------------
// The report
//
// A probe line is a claim about one pixel: where it is on the 320x240 display,
// and what colour the executable believes it drew there, computed from the art
// library and from `grandleon::view` and from nothing the GPU has touched.
//
// A readback line is the answer to a different question: what the GPU actually
// stored at that address, fetched back out of VRAM through GP0(0xC0). The two
// are produced by different machinery and the harness requires them to agree,
// which is what makes a correct claim about a wrongly-placed rectangle a
// failure instead of a pass.
//
// Five-bit levels rather than eight-bit bytes: five bits is what this machine
// stores, and widening them would be a claim about a digital-to-analogue
// converter rather than about the picture.
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

// The opaque texel of a cell nearest its centre, searched outward in rings so
// that a silhouette with a transparent middle still yields a texel that is
// unambiguously part of the figure rather than of the ground behind it.
// Returns false when the whole cell is transparent, which is a broken asset and
// is reported as such.
//
// "Opaque" is a property of the *colour* here, not of the index: the GPU skips
// a texel whose CLUT word is zero, and unlike every other console this
// repository targets, index 0 is not reserved: the art library's forty opaque
// terrain sheets use it for ground.
[[nodiscard]] bool opaque_near_centre(
    const art::Asset& asset, int& out_x, int& out_y
) {
    const int centre = tile / 2;
    for (int radius = 0; radius < tile; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                // Only the ring, so the search really is outward.
                if (radius > 0 && dx > -radius && dx < radius && dy > -radius &&
                    dy < radius) {
                    continue;
                }
                const int x = centre + dx;
                const int y = centre + dy;
                if (x < 0 || y < 0 || x >= tile || y >= tile) continue;
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

}  // namespace

int main() {
    Line().text("GRANDLEON PLAYSTATION RENDER").flush();

    static_assert(
        grandleon_terrain_kind_count == art::terrain_kind_count,
        "the art library's terrain registry and its PlayStation header disagree"
    );
    // There is no style to check an index against any more: the build read
    // this executable's project, included that style's header, and
    // `art::character` has no style dimension left. What remains true is that
    // the style the art library named is one of the library's own, and
    // `psx_art.h` asserts that where the name arrives.

    // -----------------------------------------------------------------------
    // Content
    // -----------------------------------------------------------------------
    //
    // The package is in the executable's own .rodata and the loader wants a
    // vector, so opening it costs two copies of it in RAM at once: one to hand
    // over, one that `LoadedPackage` keeps. This machine has 1.8 MiB to spend
    // and needs no immediately-invoked block to survive it, but the input copy
    // is scoped away all the same, because the peak this reports should be the
    // peak a port would pay rather than one this file chose not to avoid.
    // `DESIGN.md` §3.4 names loading-by-copy as a defect of the format; this is
    // what it costs where there is room for it.
    const auto loaded = [] {
        const std::vector<std::uint8_t> package_bytes(
            board_package_bytes, board_package_bytes + board_package_size
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

    const std::uint64_t encounter_id = core::stable_content_id_v1(encounter_path);
    const auto encounter_load = pr::load_encounter(loaded.package, encounter_id);
    expect(static_cast<bool>(encounter_load), "the encounter loads");
    if (!encounter_load) give_up();

    auto created = sim::create_encounter(encounter_load.definition);
    expect(static_cast<bool>(created), "the encounter is legal");
    if (!created) give_up();

    const sim::EncounterSnapshot snapshot = created.encounter.snapshot();
    const std::vector<std::uint64_t>& terrain = encounter_load.terrain;
    const std::size_t cells =
        static_cast<std::size_t>(snapshot.width) * snapshot.height;
    expect(terrain.size() == cells, "the terrain covers the board");
    // And the run stops on it, because everything below indexes the terrain by
    // cell. `read_map` refuses a map whose cell count is not its width times
    // its height, so a package that opens cannot reach this, but a check that
    // reports a short board and then reads off the end of it is a check that
    // has been overtaken by the failure it named.
    if (terrain.size() != cells) give_up();

    Line()
        .text("BOARD ")
        .text(encounter_path)
        .text(" ")
        .decimal(static_cast<std::uint32_t>(snapshot.width))
        .text("x")
        .decimal(static_cast<std::uint32_t>(snapshot.height))
        .text(" units ")
        .decimal(static_cast<std::uint32_t>(snapshot.units.size()))
        .flush();

    // This machine renders and does not steer, so a board that opens in the
    // deployment phase is drawn exactly as the content authored it, which is
    // the board every non-interactive surface opens on. The line exists so a
    // zoned board is identified rather than silently indistinct; no shipped
    // PlayStation encounter authors a region, so it never prints.
    if (snapshot.deploying) {
        Line()
            .text("DEPLOY zone ")
            .decimal(
                static_cast<std::uint32_t>(snapshot.deployment_tiles.size())
            )
            .flush();
    }

    // Every cell's identity has to resolve, or the board has a hole in it that
    // no probe would notice.
    {
        bool resolved = true;
        for (std::size_t i = 0; i < terrain.size(); ++i) {
            if (kind_of(presentation.presentation, terrain[i]) < 0) {
                resolved = false;
            }
        }
        expect(resolved, "every terrain identity resolves to a kind");
    }

    const int theme = presentation.presentation.theme < art::theme_count
                          ? presentation.presentation.theme
                          : 0;

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    const int max_cols = gpu::screen_width / tile;
    const int max_rows = gpu::screen_height / tile;
    view::Camera camera{};
    camera.map_w = snapshot.width;
    camera.map_h = snapshot.height;
    camera.view_w = snapshot.width < max_cols ? snapshot.width : max_cols;
    camera.view_h = snapshot.height < max_rows ? snapshot.height : max_rows;
    camera.clamp();

    int highest = 0;
    for (std::size_t i = 0; i < terrain.size(); ++i) {
        const int elevation =
            elevation_of_kind(kind_of(presentation.presentation, terrain[i]));
        if (elevation > highest) highest = elevation;
    }
    const int step = view::elevation_step_for(tile);
    int reserved = view::headroom(highest, step, tile);
    int slack = gpu::screen_height - tile * camera.view_h;
    if (slack < 0) slack = 0;
    const bool headroom_clamped = reserved > slack;
    if (headroom_clamped) reserved = slack;
    // Seven 32-pixel rows in 240 lines leave sixteen, and the deepest lift the
    // projection offers is twelve, so this never fires on either of this
    // project's boards. It is here because a taller cell or a shorter display
    // would make it fire, and a first-row cell losing its lift silently is the
    // kind of thing that should be an error rather than a shrug.
    expect(!headroom_clamped, "the board's tallest lift fits above the frame");

    // No rounding. A renderer that draws through a tile plane has to snap the
    // origin to that plane's 8-pixel grid; a primitive here is positioned per
    // pixel, so the projection's own origin is used.
    const int origin_x = (gpu::screen_width - tile * camera.view_w) / 2;
    const int origin_y =
        reserved + (gpu::screen_height - reserved - tile * camera.view_h) / 2;
    const view::Projection projection{origin_x, origin_y, tile, step};

    Line()
        .text("LAYOUT tile ")
        .decimal(static_cast<std::uint32_t>(tile))
        .text(" origin ")
        .decimal(static_cast<std::uint32_t>(origin_x))
        .text(",")
        .decimal(static_cast<std::uint32_t>(origin_y))
        .text(" view ")
        .decimal(static_cast<std::uint32_t>(camera.view_w))
        .text("x")
        .decimal(static_cast<std::uint32_t>(camera.view_h))
        .text(" step ")
        .decimal(static_cast<std::uint32_t>(step))
        .text(" headroom ")
        .decimal(static_cast<std::uint32_t>(reserved))
        .text(" maxlift ")
        .decimal(static_cast<std::uint32_t>(view::max_lift_for(tile)))
        .flush();

    // -----------------------------------------------------------------------
    // The draw list
    // -----------------------------------------------------------------------
    view::DrawList draw_list{draw_storage, draw_capacity};
    draw_list.clear();
    for (int y = camera.y; y < camera.y + camera.view_h; ++y) {
        for (int x = camera.x; x < camera.x + camera.view_w; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * snapshot.width + x;
            const int kind = kind_of(presentation.presentation, terrain[index]);
            if (kind < 0) continue;
            draw_list.add(
                view::terrain_item(
                    camera, projection, x, y, elevation_of_kind(kind), kind,
                    terrain_variant(x, y), static_cast<std::uint32_t>(kind)
                ),
                kind
            );
        }
    }
    for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
        const sim::UnitSnapshot& unit = snapshot.units[i];
        // Only what is standing there is drawn, and the probe's occupancy
        // census below has to say the same word or it asserts about a tile
        // nobody covered.
        if (!sim::on_board(unit)) continue;
        if (!camera.visible(unit.position.x, unit.position.y)) continue;
        const std::size_t index =
            static_cast<std::size_t>(unit.position.y) * snapshot.width +
            unit.position.x;
        const int kind = kind_of(presentation.presentation, terrain[index]);
        draw_list.add(
            view::billboard_item(
                camera, projection, view::Layer::unit, unit.position.x,
                unit.position.y, elevation_of_kind(kind), 0,
                static_cast<std::uint32_t>(i)
            )
        );
    }
    expect(!draw_list.overflowed(), "the draw list holds the whole board");
    draw_list.sort();

    // -----------------------------------------------------------------------
    // The picture
    // -----------------------------------------------------------------------
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

    // Nothing the art library holds may collide with the backdrop, or a cell
    // that was never drawn would be indistinguishable from one that was.
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
        expect(separated, "no colour the board can draw is the backdrop");
    }

    // Walking the sorted list forwards is the whole of the occlusion rule: the
    // GPU paints in command order and owns no depth buffer, and the list is
    // already back to front.
    for (int i = 0; i < draw_list.size(); ++i) {
        const view::DrawItem& item = draw_list[i];
        if (item.layer == view::Layer::terrain) {
            const int kind = static_cast<int>(item.subject);
            const int variant = item.variant;
            const art::Asset asset = art::terrain(theme, kind, variant);
            if (terrain_clut_of[kind] < 0) {
                terrain_clut_of[kind] = claim_clut(asset.clut);
            }
            if (terrain_cell_of[kind][variant] < 0) {
                terrain_cell_of[kind][variant] = claim_cell(asset.texels);
            }
            if (terrain_clut_of[kind] < 0 || terrain_cell_of[kind][variant] < 0) {
                continue;
            }
            gpu::draw_cell(
                item.x, item.y, terrain_cell_of[kind][variant],
                terrain_clut_of[kind]
            );
        } else if (item.layer == view::Layer::unit) {
            const sim::UnitSnapshot& unit = snapshot.units[item.subject];
            const int archetype =
                archetype_of(presentation.presentation, unit.unit_type_id);
            const int colour = colour_of(presentation.presentation, unit);
            const art::Asset asset = art::character(archetype, colour);
            if (character_clut_of[archetype][colour] < 0) {
                character_clut_of[archetype][colour] = claim_clut(asset.clut);
            }
            if (character_cell_of[archetype][colour] < 0) {
                character_cell_of[archetype][colour] = claim_cell(asset.texels);
            }
            if (character_clut_of[archetype][colour] < 0 ||
                character_cell_of[archetype][colour] < 0) {
                continue;
            }
            gpu::draw_cell(
                item.x, item.y, character_cell_of[archetype][colour],
                character_clut_of[archetype][colour]
            );
        }
    }

    expect(cells_fit, "every cell the board needs fits in the texture pages");
    expect(cluts_fit, "every CLUT the board needs fits in the CLUT band");
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
    Line()
        .text("VRAM cells ")
        .decimal(static_cast<std::uint32_t>(cells_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::cell_capacity))
        .text("; cluts ")
        .decimal(static_cast<std::uint32_t>(cluts_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::clut_capacity))
        .flush();

    // -----------------------------------------------------------------------
    // The claims
    //
    // One pixel per cell. Where a unit stands, the claim is about the unit,
    // because the unit is drawn in front of the ground and its centre is what
    // is there; everywhere else the claim is about the ground.
    //
    // The centre is a safe place to ask about, and that is a property of the
    // projection rather than a hope: a raised cell is drawn at most
    // `3*tile/8` higher than the cell behind, which is an eighth of a tile
    // short of that cell's centre row. `board_view.hpp` proves it, and every
    // centre-sampling probe in this repository relies on it.
    // -----------------------------------------------------------------------
    for (int i = 0; i < draw_list.size(); ++i) {
        const view::DrawItem& item = draw_list[i];
        Line label;
        if (item.layer == view::Layer::unit) {
            const sim::UnitSnapshot& unit = snapshot.units[item.subject];
            const art::Asset asset = art::character(
                archetype_of(presentation.presentation, unit.unit_type_id),
                colour_of(presentation.presentation, unit)
            );
            int px = 0;
            int py = 0;
            if (!opaque_near_centre(asset, px, py)) {
                expect(false, "a unit's art has an opaque texel");
                continue;
            }
            label.text("u")
                .decimal(static_cast<std::uint32_t>(item.cell_x))
                .text("_")
                .decimal(static_cast<std::uint32_t>(item.cell_y));
            probe(
                label.c_str(), item.x + px, item.y + py,
                art::colour_at(asset, px, py)
            );
        } else if (item.layer == view::Layer::terrain) {
            bool occupied = false;
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (sim::on_board(unit) && unit.position.x == item.cell_x &&
                    unit.position.y == item.cell_y) {
                    occupied = true;
                }
            }
            if (occupied) continue;
            const art::Asset asset =
                art::terrain(theme, static_cast<int>(item.subject), item.variant);
            const int centre = tile / 2;
            if (art::colour_at(asset, centre, centre) == 0) {
                // Terrain is opaque by construction; a transparent centre would
                // mean the backdrop shows through the ground.
                expect(false, "a terrain cell's centre is opaque");
                continue;
            }
            label.text("t")
                .decimal(static_cast<std::uint32_t>(item.cell_x))
                .text("_")
                .decimal(static_cast<std::uint32_t>(item.cell_y));
            probe(
                label.c_str(),
                projection.cell_centre_x(camera, item.cell_x),
                projection.cell_centre_y(camera, item.cell_y, item.elevation),
                art::colour_at(asset, centre, centre)
            );
        }
    }

    Line().text("PROBE-COUNT ").decimal(static_cast<std::uint32_t>(probes)).flush();

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
        .text("RESULT ")
        .text(failures == 0 ? "PASS " : "FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/")
        .decimal(static_cast<std::uint32_t>(checks))
        .flush();

    // Hand the machine to the host, here, with the picture finished and the
    // report written. `platform/playstation/harness` puts its capture on this
    // slot, so the frame it measures is the frame this executable says it drew
    // and not a frame from some other instant: no polling, no race, and no
    // agreement needed about how many vertical retraces to wait.
    //
    // With no observer attached this is a store to a port nothing reads.
    psx::signal_host(harness_slot);

    // Returning from main reaches Nugget's cxxglue, which calls pcsx_exit with
    // this value, which under -testmode is the emulator's own exit code.
    return failures == 0 ? 0 : 1;
}
