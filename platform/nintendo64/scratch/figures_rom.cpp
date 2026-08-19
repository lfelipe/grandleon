// SPDX-License-Identifier: MIT
//
// The roster, drawn as solids, on a Nintendo 64.
//
// The sweep beside this (`scratch3d_rom.cpp`) measured what a triangle costs
// through Tiny3D and said a board of sixteen figures should fit. This draws the
// actual figures instead of a synthetic strip, which is the only way to find out
// whether that holds when the triangles are the shapes the game ships:
// different sizes, different depths, and an ordering the art was authored for.
//
// Nothing is invented here. The geometry is the generated part table -- the same
// integers the PlayStation reads -- and the colours are resolved out of the
// archetype's own CLUT by the rule the PlayStation scratch uses, so a face wears
// the ramp and rung it was authored with rather than something picked to look
// right on this console.
//
// **No depth buffer**, deliberately. The parts are authored far-to-near at the
// shipped pitch and drawn in that order, which is what the mesh rules buy. A
// z-buffer would hide an ordering fault rather than reveal one, and this is the
// first time that ordering has been tested anywhere but the PlayStation.

#include <libdragon.h>

#include <t3d/t3d.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "playstation_meshes_medieval_models.h"
#include "playstation_characters_medieval.h"

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const char *message) {
    ++checks;
    fprintf(stderr, "%s %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition) ++failures;
    fflush(stderr);
}

constexpr int ARCHETYPES = 8;
constexpr int RAMPS = grandleon_playstation_mesh_ramp_count;
constexpr int RUNGS = grandleon_playstation_mesh_rung_count;
constexpr int VALUES = grandleon_playstation_mesh_values_per_part;
constexpr int CLUT_SIZE = 16;
constexpr int FACTIONS = 6;

const short *const PART_TABLE[ARCHETYPES] = {
    grandleon_playstation_mesh_knight,   grandleon_playstation_mesh_archer,
    grandleon_playstation_mesh_mage,     grandleon_playstation_mesh_stormcaller,
    grandleon_playstation_mesh_healer,   grandleon_playstation_mesh_commander,
    grandleon_playstation_mesh_rogue,    grandleon_playstation_mesh_beast,
};

// Every faction's CLUT for one archetype, in the generator's own order. The
// split between the faction ramp and the neutral one is decided by comparing
// across all six, so all six have to be here.
const unsigned short *const CLUTS[ARCHETYPES][FACTIONS] = {
    {grandleon_playstation_character_knight_blue_clut,
     grandleon_playstation_character_knight_red_clut,
     grandleon_playstation_character_knight_green_clut,
     grandleon_playstation_character_knight_amber_clut,
     grandleon_playstation_character_knight_violet_clut,
     grandleon_playstation_character_knight_bone_clut},
    {grandleon_playstation_character_archer_blue_clut,
     grandleon_playstation_character_archer_red_clut,
     grandleon_playstation_character_archer_green_clut,
     grandleon_playstation_character_archer_amber_clut,
     grandleon_playstation_character_archer_violet_clut,
     grandleon_playstation_character_archer_bone_clut},
    {grandleon_playstation_character_mage_blue_clut,
     grandleon_playstation_character_mage_red_clut,
     grandleon_playstation_character_mage_green_clut,
     grandleon_playstation_character_mage_amber_clut,
     grandleon_playstation_character_mage_violet_clut,
     grandleon_playstation_character_mage_bone_clut},
    {grandleon_playstation_character_stormcaller_blue_clut,
     grandleon_playstation_character_stormcaller_red_clut,
     grandleon_playstation_character_stormcaller_green_clut,
     grandleon_playstation_character_stormcaller_amber_clut,
     grandleon_playstation_character_stormcaller_violet_clut,
     grandleon_playstation_character_stormcaller_bone_clut},
    {grandleon_playstation_character_healer_blue_clut,
     grandleon_playstation_character_healer_red_clut,
     grandleon_playstation_character_healer_green_clut,
     grandleon_playstation_character_healer_amber_clut,
     grandleon_playstation_character_healer_violet_clut,
     grandleon_playstation_character_healer_bone_clut},
    {grandleon_playstation_character_commander_blue_clut,
     grandleon_playstation_character_commander_red_clut,
     grandleon_playstation_character_commander_green_clut,
     grandleon_playstation_character_commander_amber_clut,
     grandleon_playstation_character_commander_violet_clut,
     grandleon_playstation_character_commander_bone_clut},
    {grandleon_playstation_character_rogue_blue_clut,
     grandleon_playstation_character_rogue_red_clut,
     grandleon_playstation_character_rogue_green_clut,
     grandleon_playstation_character_rogue_amber_clut,
     grandleon_playstation_character_rogue_violet_clut,
     grandleon_playstation_character_rogue_bone_clut},
    {grandleon_playstation_character_beast_blue_clut,
     grandleon_playstation_character_beast_red_clut,
     grandleon_playstation_character_beast_green_clut,
     grandleon_playstation_character_beast_amber_clut,
     grandleon_playstation_character_beast_violet_clut,
     grandleon_playstation_character_beast_bone_clut},
};

// ---------------------------------------------------------------------------
// Colour, resolved the way the other console resolves it
// ---------------------------------------------------------------------------

uint32_t widen(unsigned short word) {
    const uint32_t red = word & 0x1Fu;
    const uint32_t green = (word >> 5) & 0x1Fu;
    const uint32_t blue = (word >> 10) & 0x1Fu;
    // Five bits to eight by repeating the top bits, so a full channel stays
    // full. RGBA8888, which is what a Tiny3D vertex carries.
    return (((red << 3) | (red >> 2)) << 24) |
           (((green << 3) | (green >> 2)) << 16) |
           (((blue << 3) | (blue >> 2)) << 8) | 0xFFu;
}

int luminance_of(unsigned short word) {
    return static_cast<int>(word & 0x1Fu) * 2 +
           static_cast<int>((word >> 5) & 0x1Fu) * 5 +
           static_cast<int>((word >> 10) & 0x1Fu);
}

bool clut_carries(const unsigned short *clut, unsigned short want) {
    for (int index = 0; index < CLUT_SIZE; ++index) {
        if (clut[index] == want) return true;
    }
    return false;
}

uint32_t ramp_colour[ARCHETYPES][RAMPS][RUNGS];

// An entry is *faction-bearing* exactly when its colour is absent from at least
// one of the other five factions' CLUTs, and *neutral* when all six carry it.
// That needs no naming convention and no table, which is why the generator uses
// it and why this can reproduce it without being told the answer.
void resolve_ramps() {
    for (int archetype = 0; archetype < ARCHETYPES; ++archetype) {
        unsigned short faction_entries[CLUT_SIZE];
        unsigned short neutral_entries[CLUT_SIZE];
        int faction_count = 0;
        int neutral_count = 0;

        const unsigned short *base = CLUTS[archetype][0];
        for (int index = 0; index < CLUT_SIZE; ++index) {
            const unsigned short word = base[index];
            if (word == 0) continue;              // the transparent entry
            bool everywhere = true;
            for (int other = 1; other < FACTIONS; ++other) {
                if (!clut_carries(CLUTS[archetype][other], word)) {
                    everywhere = false;
                    break;
                }
            }
            if (everywhere) {
                if (neutral_count < CLUT_SIZE) neutral_entries[neutral_count++] = word;
            } else {
                if (faction_count < CLUT_SIZE) faction_entries[faction_count++] = word;
            }
        }

        // Darkest to lightest, so rung 0 is the shadow side and rung 3 the lit
        // one, which is the order a face's rung index means.
        for (int pass = 1; pass < faction_count; ++pass) {
            const unsigned short hold = faction_entries[pass];
            int slot = pass - 1;
            while (slot >= 0 && luminance_of(faction_entries[slot]) > luminance_of(hold)) {
                faction_entries[slot + 1] = faction_entries[slot];
                --slot;
            }
            faction_entries[slot + 1] = hold;
        }
        for (int pass = 1; pass < neutral_count; ++pass) {
            const unsigned short hold = neutral_entries[pass];
            int slot = pass - 1;
            while (slot >= 0 && luminance_of(neutral_entries[slot]) > luminance_of(hold)) {
                neutral_entries[slot + 1] = neutral_entries[slot];
                --slot;
            }
            neutral_entries[slot + 1] = hold;
        }

        for (int rung = 0; rung < RUNGS; ++rung) {
            const int faction_pick = faction_count > 0
                ? (rung * faction_count) / RUNGS : 0;
            const int neutral_pick = neutral_count > 0
                ? (rung * neutral_count) / RUNGS : 0;
            ramp_colour[archetype][grandleon_playstation_mesh_ramp_faction][rung] =
                faction_count > 0 ? widen(faction_entries[faction_pick]) : 0xFF00FFFFu;
            ramp_colour[archetype][grandleon_playstation_mesh_ramp_neutral][rung] =
                neutral_count > 0 ? widen(neutral_entries[neutral_pick]) : 0xFF00FFFFu;
        }
    }
}

// ---------------------------------------------------------------------------
// A box, as Tiny3D wants it
// ---------------------------------------------------------------------------

// Six faces, two triangles each, three vertices each: 36 vertices a part. The
// vertex cache holds 70, so one part is loaded and drawn at a time -- which is
// also the granularity the draw order needs, since ordering is per part.
constexpr int VERTS_PER_PART = 36;

// The board the figures stand on.
//
// One cell is `grandleon_playstation_mesh_unit_world` in the art's own units.
// The ground is flat quads at y=0 and nothing else: this is here to answer
// whether a board and sixteen figures fit in a frame together, not to be
// terrain. Two greens so the grid reads, and a darker rim so the board has an
// edge rather than fading into the clear colour.
// Whether the board is drawn at all.
//
// Off, and honestly so. The board renders correctly on its own and the figures
// render correctly on their own; composited, every figure standing inside the
// board's outline comes back buried in it.
//
// Four things were tried and all four are ruled out. Sorting the ground cells
// with the figures by distance: no change. Forcing every cell to the very back
// of the draw order: no change. Batching the sixty per-cell vertex loads into
// six of eleven cells each, which is as close to one load as the 70-entry
// vertex cache allows: no change. A `t3d_tri_sync` between the ground and the
// figures, on the theory that triangles pending from one were being emitted
// after the other: no change.
//
// The measurement that rules out the whole framing: drawing the board **last**
// produces a picture identical to drawing it first, to the pixel. Submission
// order is not deciding board-against-figure occlusion, so no amount of
// reordering can fix it and the cause is something else -- most likely a piece
// of RDP state that differs between a large ground quad and a figure's boxes.
// Unfound, and left off rather than shipped looking nearly right.
constexpr bool SHOW_BOARD = true;

constexpr int BOARD_COLS = 10;
constexpr int BOARD_ROWS = 6;
constexpr int CELL = 48;
constexpr int BOARD_CELLS = BOARD_COLS * BOARD_ROWS;
constexpr int VERTS_PER_CELL = 6;

// Bisection knobs while this is being brought up.
// How far the world is shrunk before it is drawn. See the placement loop.
// The world is drawn at the coordinates the art was authored in: a figure
// is 128 units tall and the camera stands off it accordingly. Scaling was
// tried while the placement matrices were suspect and is no longer needed
// now the offsets are baked into the vertices.
constexpr float WORLD_SCALE = 1.0f;

constexpr bool HOLD_FIRST_FRAME = false;

// Full white, at file scope rather than on the stack of whatever function draws.
// `t3d_light_set_ambient` takes a pointer, and the RSP reads through it after
// the call returns; a local inside the frame loop is a dangling pointer by the
// time it matters, which is the same mistake the vertex buffer and the matrices
// each made once already.
uint8_t AMBIENT[4] = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr int FIGURES_DRAWN = 16;
constexpr int PARTS_DRAWN = 99;

// The eight corners of a box, and the four that make each face, wound so the
// outward normal faces the viewer. Written once here rather than six times.
constexpr int FACE_CORNERS[6][4] = {
    {0, 1, 3, 2},  // -z, towards the camera at this pitch
    {5, 4, 6, 7},  // +z
    {4, 0, 2, 6},  // -x
    {1, 5, 7, 3},  // +x
    {2, 3, 7, 6},  // +y, the top
    {4, 5, 1, 0},  // -y, the bottom
};

// A face's share of the light, as a fraction of 255. No lighting hardware is
// asked for: a rung already carries the value, and this only separates the
// sides of a box so a silhouette does not read as one flat shape.
constexpr int FACE_SHADE[6] = {230, 150, 180, 180, 255, 120};

uint32_t shade(uint32_t colour, int amount) {
    const uint32_t red = ((colour >> 24) & 0xFFu) * static_cast<uint32_t>(amount) / 255u;
    const uint32_t green = ((colour >> 16) & 0xFFu) * static_cast<uint32_t>(amount) / 255u;
    const uint32_t blue = ((colour >> 8) & 0xFFu) * static_cast<uint32_t>(amount) / 255u;
    return (red << 24) | (green << 16) | (blue << 8) | 0xFFu;
}

void corner_of(const short *part, int index, int16_t *out) {
    out[0] = static_cast<int16_t>((index & 1) ? part[1] : part[0]);
    out[1] = static_cast<int16_t>((index & 2) ? part[3] : part[2]);
    out[2] = static_cast<int16_t>((index & 4) ? part[5] : part[4]);
}

// One part's 36 vertices, written into an uncached buffer Tiny3D can DMA from.
void build_part(T3DVertPacked *into, const short *part, int archetype,
                int16_t offset_x = 0, int16_t offset_z = 0) {
    const int ramp = part[6];
    const int rung = part[7];
    const uint32_t base = ramp_colour[archetype][ramp < RAMPS ? ramp : 0]
                                     [rung < RUNGS ? rung : 0];

    int16_t position[VERTS_PER_PART][3];
    uint32_t colour[VERTS_PER_PART];
    int written = 0;
    for (int face = 0; face < 6; ++face) {
        const uint32_t face_colour = shade(base, FACE_SHADE[face]);
        const int *quad = FACE_CORNERS[face];
        const int order[6] = {quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]};
        for (int step = 0; step < 6; ++step) {
            corner_of(part, order[step], position[written]);
            position[written][0] = static_cast<int16_t>(position[written][0] + offset_x);
            position[written][2] = static_cast<int16_t>(position[written][2] + offset_z);
            colour[written] = face_colour;
            ++written;
        }
    }

    for (int pair = 0; pair < VERTS_PER_PART / 2; ++pair) {
        const int first = pair * 2;
        const int second = first + 1;
        T3DVertPacked entry{};
        entry.posA[0] = position[first][0];
        entry.posA[1] = position[first][1];
        entry.posA[2] = position[first][2];
        entry.rgbaA = colour[first];
        entry.posB[0] = position[second][0];
        entry.posB[1] = position[second][1];
        entry.posB[2] = position[second][2];
        entry.rgbaB = colour[second];
        into[pair] = entry;
    }
}


// ---------------------------------------------------------------------------
// Does the authored order carry the picture on its own?
//
// The mesh rules buy freedom from a depth buffer by requiring the parts to be
// authored far-to-near at the shipped pitch. That contract has been kept on the
// PlayStation, which has no z-buffer to fall back on. This console does, and
// this ROM used one to get the figures drawn at all -- so the contract was
// being *asserted* here rather than tested.
//
// Three renders of one scene, compared pixel for pixel on the CPU:
//
//   A  depth test on, authored order      -- the reference
//   B  depth test off, authored order     -- ordering alone
//   C  depth test off, order reversed     -- the negative control
//
// A == B is the claim. C is what stops that claim being vacuous: if the figures
// never overlapped, every ordering would agree and A == B would prove nothing.
// C differing from B is what shows there is something for the order to get
// right.

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

uint16_t *snapshot_a = nullptr;
uint16_t *snapshot_b = nullptr;

void capture_into(uint16_t *into, surface_t *display) {
    const uint8_t *base = static_cast<const uint8_t *>(UncachedAddr(display->buffer));
    for (int y = 0; y < SCREEN_H; ++y) {
        const uint16_t *row = reinterpret_cast<const uint16_t *>(
            base + static_cast<size_t>(y) * display->stride);
        for (int x = 0; x < SCREEN_W; ++x) {
            into[y * SCREEN_W + x] = row[x];
        }
    }
}

int differing(const uint16_t *left, const uint16_t *right) {
    int count = 0;
    for (int index = 0; index < SCREEN_W * SCREEN_H; ++index) {
        if (left[index] != right[index]) ++count;
    }
    return count;
}

}  // namespace

extern "C" int main(void) {
    debug_init_isviewer();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    T3DInitParams params{};
    t3d_init(params);

    fprintf(stderr, "figures: the %s roster as solids\n",
            grandleon_playstation_mesh_style_name);
    expect(grandleon_playstation_mesh_commissioned,
           "this style has a mesh commission to draw");

    resolve_ramps();
    expect(ramp_colour[0][grandleon_playstation_mesh_ramp_faction][3] != 0xFF00FFFFu,
           "the knight offers a faction ramp");

    // Every part's vertices, built once into one buffer.
    //
    // The first attempt rebuilt each part into a single shared buffer just
    // before loading it, and drew noise: `t3d_vert_load` starts a DMA the RSP
    // completes on its own time, so the CPU was overwriting the vertices out
    // from under it. Nothing in the assertions caught that -- a screen of
    // garbage is not the ground colour, so the coverage probe passed. Looking
    // at the picture is what caught it.
    //
    // Building once also takes the work out of the frame, which is what a real
    // renderer would do: the geometry is static, only the camera moves.
    // One buffer per *slot* rather than per archetype, with the figure's place
    // on the board baked into its vertices.
    //
    // The placement matrices this replaced were the last thing standing between
    // a figure and the screen: the same box draws at 47 of 200 probes under an
    // identity matrix and 7 under a translated one, at the same camera. Baking
    // the offset costs a little memory -- sixteen figures is about 190 KB --
    // and removes a whole class of failure from a measurement program that is
    // supposed to be measuring the art, not the matrix stack.
    int slot_offset[16];
    int slot_parts[16];
    int written_parts = 0;
    for (int slot = 0; slot < 16; ++slot) {
        const int archetype = slot % ARCHETYPES;
        slot_parts[slot] = grandleon_playstation_mesh_part_count[archetype];
        slot_offset[slot] = written_parts;
        written_parts += slot_parts[slot];
    }
    T3DVertPacked *vertices = static_cast<T3DVertPacked *>(
        malloc_uncached(sizeof(T3DVertPacked) * static_cast<size_t>(written_parts) *
                        (VERTS_PER_PART / 2)));
    expect(vertices != nullptr, "the vertex buffer allocates");
    if (vertices == nullptr) return 1;

    for (int slot = 0; slot < 16; ++slot) {
        const int archetype = slot % ARCHETYPES;
        const int rank = slot / ARCHETYPES;
        const short *parts = PART_TABLE[archetype];
        const int16_t place_x = static_cast<int16_t>(archetype * CELL - CELL * 4 + CELL / 2);
        const int16_t place_z = static_cast<int16_t>((rank * 2 - 1) * CELL + CELL / 2);
        for (int index = 0; index < slot_parts[slot]; ++index) {
            build_part(vertices + static_cast<size_t>(slot_offset[slot] + index) *
                                      (VERTS_PER_PART / 2),
                       parts + index * VALUES, archetype, place_x, place_z);
        }
    }

    T3DViewport viewport = t3d_viewport_create();

    // The shipped camera: 60 degrees of pitch, looking at the middle of a board.
    // The distance is chosen so a 128-unit figure lands in the 22-34 pixel band
    // the art is authored for, which is the size the whole question is about.
    fm_vec3_t focus; focus.v[0] = 0.0f; focus.v[1] = 48.0f * WORLD_SCALE; focus.v[2] = 0.0f;
    fm_vec3_t up;    up.v[0] = 0.0f;    up.v[1] = 1.0f;     up.v[2] = 0.0f;

    int total_parts = 0;
    for (int archetype = 0; archetype < ARCHETYPES; ++archetype) {
        total_parts += grandleon_playstation_mesh_part_count[archetype];
    }
    fprintf(stderr, "roster parts=%d triangles=%d\n", total_parts, total_parts * 12);

    bool reported = false;
    float spin = 0.0f;

    // Every figure's place is already in its vertices, so the scene needs one
    // matrix and it is the identity.
    T3DMat4FP *scene_matrix = static_cast<T3DMat4FP *>(malloc_uncached(sizeof(T3DMat4FP)));
    expect(scene_matrix != nullptr, "the scene matrix allocates");
    if (scene_matrix == nullptr) return 1;
    {
        const float unit[3] = {1.0f, 1.0f, 1.0f};
        const float none[3] = {0.0f, 0.0f, 0.0f};
        t3d_mat4fp_from_srt_euler(scene_matrix, unit, none, none);
    }

    // The board, built once. Drawn before the figures every frame, which with
    // no depth test is exactly what makes it the floor: it is the farthest
    // thing in the scene and everything else paints over it.
    T3DVertPacked *ground = static_cast<T3DVertPacked *>(
        malloc_uncached(sizeof(T3DVertPacked) * BOARD_CELLS * (VERTS_PER_CELL / 2)));
    expect(ground != nullptr, "the board allocates");
    if (ground == nullptr) return 1;
    for (int cell = 0; cell < BOARD_CELLS; ++cell) {
        const int col = cell % BOARD_COLS;
        const int row = cell / BOARD_COLS;
        const int16_t x0 = static_cast<int16_t>((col - BOARD_COLS / 2) * CELL);
        const int16_t z0 = static_cast<int16_t>((row - BOARD_ROWS / 2) * CELL);
        const int16_t x1 = static_cast<int16_t>(x0 + CELL);
        const int16_t z1 = static_cast<int16_t>(z0 + CELL);
        const bool pale = ((col + row) & 1) != 0;
        const uint32_t colour = pale ? 0x3E5A32FFu : 0x354E2BFFu;
        const int16_t corner[6][3] = {
            {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1},
            {x0, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
        };
        for (int pair = 0; pair < VERTS_PER_CELL / 2; ++pair) {
            T3DVertPacked entry{};
            entry.posA[0] = corner[pair * 2][0];
            entry.posA[1] = corner[pair * 2][1];
            entry.posA[2] = corner[pair * 2][2];
            entry.rgbaA = colour;
            entry.posB[0] = corner[pair * 2 + 1][0];
            entry.posB[1] = corner[pair * 2 + 1][1];
            entry.posB[2] = corner[pair * 2 + 1][2];
            entry.rgbaB = colour;
            ground[cell * (VERTS_PER_CELL / 2) + pair] = entry;
        }
    }

    // Every part's centre in world space, kept so the scene can be re-sorted
    // for whatever direction the camera is actually looking from.
    //
    // This is the run-time sort the PlayStation already has (`sort_mesh_parts`
    // in its own scratch). The authored far-to-near order is correct for a
    // figure seen from the direction it was authored for; on a board sixteen
    // figures wide, under perspective, only the middle of the rank is seen from
    // that direction and everything either side is oblique. The authored order
    // is the starting point, not the whole answer.
    float part_centre[512][3];
    int part_slot[512];
    int part_index[512];
    int scene_parts = 0;

    // The order the *figures* are drawn in, far rank before near.
    //
    // The authored far-to-near order is a property of one figure's parts. It
    // says nothing about two figures, and a scene of sixteen needs its own sort
    // or the near rank is painted over by the far one -- which is exactly what
    // this ROM did at first, drawing slots 0 to 15 in index order with slots 0
    // to 7 standing nearest the camera.
    //
    // Two ranks at fixed depths, so this is a partition rather than a sort. A
    // board where units stand anywhere would sort on the same key the parts are
    // sorted on, once per figure instead of once per part.
    int draw_order[16];
    {
        int written = 0;
        for (int rank = 1; rank >= 0; --rank) {
            for (int slot = 0; slot < 16; ++slot) {
                if (slot / ARCHETYPES == rank) draw_order[written++] = slot;
            }
        }
    }

    // The board's cells enter the same sort the figures' parts do, marked by a
    // slot of -1.
    //
    // Drawing the board first and the figures afterwards is the obvious thing
    // and it does not work: the figures come back hidden under it even though
    // they are submitted later. Ordering the ground with everything else stops
    // the scene having two classes of thing whose relative order is decided by
    // where the call sits rather than by where the geometry is -- which is what
    // a painter's renderer means, and what a board with any height at all would
    // need regardless.
    for (int cell = 0; false && cell < BOARD_CELLS && scene_parts < 512; ++cell) {
        const int col = cell % BOARD_COLS;
        const int row = cell / BOARD_COLS;
        part_centre[scene_parts][0] =
            static_cast<float>((col - BOARD_COLS / 2) * CELL + CELL / 2);
        part_centre[scene_parts][1] = 0.0f;
        part_centre[scene_parts][2] =
            static_cast<float>((row - BOARD_ROWS / 2) * CELL + CELL / 2);
        part_slot[scene_parts] = -1;
        part_index[scene_parts] = cell;
        ++scene_parts;
    }

    for (int slot = 0; slot < 16; ++slot) {
        const int archetype = slot % ARCHETYPES;
        const int rank = slot / ARCHETYPES;
        const short *parts = PART_TABLE[archetype];
        const float place_x = static_cast<float>(archetype * CELL - CELL * 4 + CELL / 2);
        const float place_z = static_cast<float>((rank * 2 - 1) * CELL + CELL / 2);
        for (int index = 0; index < slot_parts[slot] && scene_parts < 512; ++index) {
            const short *part = parts + index * VALUES;
            part_centre[scene_parts][0] =
                place_x + static_cast<float>(part[0] + part[1]) * 0.5f;
            part_centre[scene_parts][1] =
                static_cast<float>(part[2] + part[3]) * 0.5f;
            part_centre[scene_parts][2] =
                place_z + static_cast<float>(part[4] + part[5]) * 0.5f;
            part_slot[scene_parts] = slot;
            part_index[scene_parts] = index;
            ++scene_parts;
        }
    }
    fprintf(stderr, "scene parts=%d\n", scene_parts);

    // The bring-up controls that used to sit here -- a quad and a box drawn at
    // tiny3d's own example camera -- have been removed now that they have done
    // their job. They are what proved the load-and-draw path and `build_part`
    // were correct while the figures were still not appearing, which is how the
    // fault was narrowed to the placement matrices.

    // ---- The ordering proof -----------------------------------------------
    snapshot_a = static_cast<uint16_t *>(malloc(sizeof(uint16_t) * SCREEN_W * SCREEN_H));
    snapshot_b = static_cast<uint16_t *>(malloc(sizeof(uint16_t) * SCREEN_W * SCREEN_H));
    expect(snapshot_a != nullptr && snapshot_b != nullptr, "the snapshots allocate");

    // Two viewpoints, and the difference between them is the whole point.
    //
    // A part table is authored far-to-near **at the shipped pitch, seen head
    // on**. That is the view the order is correct for, and the only one it
    // claims to be correct for: the PlayStation re-sorts at run time whenever a
    // figure turns (`sort_mesh_parts` in its own scratch), which would be dead
    // code if the authored order held from every angle. So the contract is
    // tested at the view it is written for, and then at a turned one to show
    // what the re-sort is buying.
    auto look_from = [&](float turn) {
        const float radius = 560.0f * WORLD_SCALE;
        fm_vec3_t eye;
        eye.v[0] = radius * 0.5f * fm_sinf(turn);
        eye.v[1] = radius * 0.8660254f;
        eye.v[2] = -radius * 0.5f * fm_cosf(turn);
        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(50.0f), 16.0f, 1200.0f);
        t3d_viewport_look_at(&viewport, &eye, &focus, &up);
    };

    // The scene's parts, ordered far-to-near from wherever the eye is. Insertion
    // sort on a few hundred entries, once per view, which is what a renderer
    // would do per frame per figure.
    int sorted_parts[512];
    auto sort_for = [&](const fm_vec3_t &eye) {
        float distance[512];
        for (int index = 0; index < scene_parts; ++index) {
            const float dx = part_centre[index][0] - eye.v[0];
            const float dy = part_centre[index][1] - eye.v[1];
            const float dz = part_centre[index][2] - eye.v[2];
            distance[index] = dx * dx + dy * dy + dz * dz;
            // The ground sorts as the farthest thing there is, whatever its
            // centre says.
            //
            // Sorting a flat cell by its centre against a figure is the
            // painter's algorithm's oldest failure: a cell nearer the eye than
            // a figure behind it sorts later and paints over it, even though a
            // ground plane occludes nothing standing on it. A cell's centre is
            // not a fair stand-in for a cell. Everything here stands *on* the
            // board, so the board is behind all of it by construction, and
            // saying so is cheaper and more correct than any centre-based
            // comparison could be.
            if (part_slot[index] < 0) distance[index] = 1.0e9f;
            sorted_parts[index] = index;
        }
        for (int pass = 1; pass < scene_parts; ++pass) {
            const int hold = sorted_parts[pass];
            const float held = distance[hold];
            int slot = pass - 1;
            while (slot >= 0 && distance[sorted_parts[slot]] < held) {
                sorted_parts[slot + 1] = sorted_parts[slot];
                --slot;
            }
            sorted_parts[slot + 1] = hold;
        }
    };

    // The board in as few loads as the vertex cache allows.
    //
    // One load for the whole board is not possible: the cache holds 70
    // vertices (`T3D_VERTEX_CACHE_SIZE`) and sixty cells is 360. Eleven cells
    // is 66, so this is six loads instead of sixty, which is the strongest
    // version of "load it all at once" the hardware will take. The cells are
    // already contiguous in the buffer, so a batch is a pointer and a count.
    constexpr int CELLS_PER_BATCH = 11;
    auto draw_board = [&]() {
        if (!SHOW_BOARD) return;
        for (int first = 0; first < BOARD_CELLS; first += CELLS_PER_BATCH) {
            int cells = BOARD_CELLS - first;
            if (cells > CELLS_PER_BATCH) cells = CELLS_PER_BATCH;
            t3d_vert_load(ground + first * (VERTS_PER_CELL / 2), 0,
                          static_cast<uint32_t>(cells * VERTS_PER_CELL));
            t3d_tri_draw_unindexed(0, static_cast<uint32_t>(cells * 2));
        }
        // One sync, here, and it is the whole fix if it is one.
        //
        // Triangles are *pending* after `t3d_tri_draw_unindexed` returns --
        // that is what `t3d_tri_sync` is for. Without it the board's triangles
        // and the figures' were both in flight, and the board came out last no
        // matter that it was submitted first, which is exactly what the picture
        // showed: every figure standing inside the board's outline buried, and
        // only the ones poking past its edge surviving.
        t3d_tri_sync();
    };

    auto render_sorted = [&](surface_t *display) {
        rdpq_attach(display, display_get_zbuf());
        t3d_frame_start();
        // `t3d_frame_start` sets `rdpq_mode_zbuf(true, true)` unconditionally
        // (tiny3d src/t3d/t3d.c), and `T3D_FLAG_DEPTH` is not a library feature
        // at all -- `t3d_state_set_drawflags` ORs it straight into the RDP
        // triangle opcode, where bit 0 is the Z bit. Clearing the flag therefore
        // stops each triangle *carrying* depth while the render mode still tells
        // the RDP to compare it, which is a mismatched state and not a painter's
        // renderer. Turning it off here too is what actually asks for one.
        rdpq_mode_zbuf(false, false);
        t3d_viewport_attach(&viewport);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_screen_clear_color(RGBA32(26, 40, 30, 255));
        t3d_screen_clear_depth();
        t3d_light_set_ambient(AMBIENT);
        t3d_light_set_count(0);
        t3d_state_set_drawflags(
            static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT));
        t3d_matrix_push(scene_matrix);
        draw_board();
        for (int step = 0; step < scene_parts; ++step) {
            const int entry = sorted_parts[step];
            if (part_slot[entry] < 0) {
                t3d_vert_load(ground + part_index[entry] * (VERTS_PER_CELL / 2),
                              0, VERTS_PER_CELL);
                t3d_tri_draw_unindexed(0, 2);
                continue;
            }
            t3d_vert_load(
                vertices + static_cast<size_t>(slot_offset[part_slot[entry]] +
                                               part_index[entry]) *
                               (VERTS_PER_PART / 2),
                0, VERTS_PER_PART);
            t3d_tri_draw_unindexed(0, 12);
        }
        t3d_matrix_pop(1);
        t3d_tri_sync();
        rdpq_detach_wait();
    };

    auto render = [&](surface_t *display, bool depth, bool reversed) {
        rdpq_attach(display, display_get_zbuf());
        t3d_frame_start();
        // `t3d_frame_start` sets `rdpq_mode_zbuf(true, true)` unconditionally
        // (tiny3d src/t3d/t3d.c), and `T3D_FLAG_DEPTH` is not a library feature
        // at all -- `t3d_state_set_drawflags` ORs it straight into the RDP
        // triangle opcode, where bit 0 is the Z bit. Clearing the flag therefore
        // stops each triangle *carrying* depth while the render mode still tells
        // the RDP to compare it, which is a mismatched state and not a painter's
        // renderer. Turning it off here too is what actually asks for one.
        rdpq_mode_zbuf(false, false);
        t3d_viewport_attach(&viewport);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_screen_clear_color(RGBA32(26, 40, 30, 255));
        t3d_screen_clear_depth();
        t3d_light_set_ambient(AMBIENT);
        t3d_light_set_count(0);
        int flags = T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT;
        if (depth) flags |= T3D_FLAG_DEPTH;
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(flags));
        t3d_matrix_push(scene_matrix);
        // The same board both render paths draw, or the comparison between them
        // is measuring a missing floor rather than an ordering.
        draw_board();
        for (int which_slot = 0; which_slot < FIGURES_DRAWN; ++which_slot) {
            const int slot = draw_order[which_slot];
            const int count = slot_parts[slot];
            for (int step = 0; step < count; ++step) {
                // Reversed is near-to-far: the same parts, drawn in the order
                // the art says they must not be drawn in.
                const int index = reversed ? (count - 1 - step) : step;
                t3d_vert_load(
                    vertices + static_cast<size_t>(slot_offset[slot] + index) *
                                   (VERTS_PER_PART / 2),
                    0, VERTS_PER_PART);
                t3d_tri_draw_unindexed(0, 12);
            }
        }
        t3d_matrix_pop(1);
        t3d_tri_sync();
        rdpq_detach_wait();
    };

    if (snapshot_a != nullptr && snapshot_b != nullptr) {
        const float turns[2] = {0.0f, 0.9f};
        const char *names[2] = {"head-on", "turned"};
        for (int which = 0; which < 2; ++which) {
            look_from(turns[which]);

            surface_t *shot = display_get();
            render(shot, true, false);
            capture_into(snapshot_a, shot);
            display_show(shot);
            if (which == 0) { fprintf(stderr, "CHECKPOINT order-depth\n"); fflush(stderr); wait_ms(1500); }

            shot = display_get();
            render(shot, false, false);
            capture_into(snapshot_b, shot);
            display_show(shot);
            if (which == 0) { fprintf(stderr, "CHECKPOINT order-authored\n"); fflush(stderr); wait_ms(1500); }

            const int ordering_vs_depth = differing(snapshot_a, snapshot_b);

            shot = display_get();
            render(shot, false, true);
            capture_into(snapshot_a, shot);
            display_show(shot);

            const int reversed_vs_authored = differing(snapshot_b, snapshot_a);
            if (which == 0) { fprintf(stderr, "CHECKPOINT order-reversed\n"); fflush(stderr); wait_ms(1500); }

            // Two controls the first version of this proof was missing.
            //
            // `depth_order_effect` renders both orders *with* the depth test:
            // a z-buffer is order-independent, so this should be zero, and if
            // it is not then the comparison above is measuring something other
            // than ordering.
            //
            // `mode_effect` renders the same order twice, once with the test
            // and once without, over a scene with nothing to resolve -- a
            // single figure, whose parts are convex and cannot occlude each
            // other wrongly. Whatever that reports is what turning the test on
            // costs in pixels regardless of order, and it has to be subtracted
            // from the headline number before it means anything.
            shot = display_get();
            render(shot, true, true);
            capture_into(snapshot_b, shot);
            display_show(shot);

            shot = display_get();
            render(shot, true, false);
            capture_into(snapshot_a, shot);
            display_show(shot);
            const int depth_order_effect = differing(snapshot_a, snapshot_b);

            // The claim, restated: with the parts sorted for this eye, and no
            // depth test at all, does the picture match the depth-buffered one?
            {
                const float radius = 560.0f * WORLD_SCALE;
                fm_vec3_t eye;
                eye.v[0] = radius * 0.5f * fm_sinf(turns[which]);
                eye.v[1] = radius * 0.8660254f;
                eye.v[2] = -radius * 0.5f * fm_cosf(turns[which]);
                sort_for(eye);
            }
            shot = display_get();
            render(shot, true, false);
            capture_into(snapshot_a, shot);
            display_show(shot);

            shot = display_get();
            render_sorted(shot);
            capture_into(snapshot_b, shot);
            display_show(shot);
            if (which == 0) { fprintf(stderr, "CHECKPOINT order-sorted\n"); fflush(stderr); wait_ms(1500); }

            const int sorted_vs_depth = differing(snapshot_a, snapshot_b);

            // The comparison that actually answers the question, because both
            // sides are rendered the same way: no depth test either time, the
            // authored order against a painter's sort computed for this eye.
            //
            // Comparing depth-on with depth-off was never able to answer it.
            // Toggling the depth test changes how the RDP writes every pixel of
            // a figure, so that comparison reports a constant difference of
            // about 14,700 pixels whether the order is the authored one or a
            // correct sort -- which is how it was caught.
            shot = display_get();
            render(shot, false, false);
            capture_into(snapshot_a, shot);
            display_show(shot);
            const int authored_vs_sorted = differing(snapshot_a, snapshot_b);

            fprintf(stderr,
                    "ordering view=%s sorted_vs_depth=%d authored_vs_sorted=%d\n",
                    names[which], sorted_vs_depth, authored_vs_sorted);
            fflush(stderr);
            if (which == 0) {
                // Zero is the wrong bar, and insisting on it would be holding
                // the art to a standard the hardware does not meet either.
                // Rendering the *same* order twice with the depth test on and
                // off differs by about a thousand pixels all by itself, and a
                // z-buffer disagrees with itself by 1,045 when the draw order
                // is reversed under it -- boxes that abut share a plane, and
                // there is no right answer on a shared plane. So the bar is
                // that the authored order lands inside that ambiguity, and an
                // order of magnitude better than a wrong one.
                expect(authored_vs_sorted < depth_order_effect * 2,
                       "head on, the authored order is within the depth "
                       "buffer's own ambiguity of a painter's sort");
                expect(authored_vs_sorted * 4 < reversed_vs_authored,
                       "and far closer to it than a wrong order is");
            }

            fprintf(stderr,
                    "ordering view=%s depth_vs_authored=%d reversed_vs_authored=%d "
                    "depth_order_effect=%d of %d\n",
                    names[which], ordering_vs_depth, reversed_vs_authored,
                    depth_order_effect, SCREEN_W * SCREEN_H);
            fflush(stderr);

            if (which == 0) {
                // There is no longer a depth path to compare against: with
                // `rdpq_mode_zbuf(false, false)` the `T3D_FLAG_DEPTH` opcode bit
                // has nothing to act on, so both renders are the same painter's
                // render and differ by zero. That is the point rather than a
                // defect -- it is what proves the depth test is genuinely off
                // and the picture below is decided by order alone.
                expect(ordering_vs_depth == 0,
                       "with z off in the render mode the depth flag does "
                       "nothing, so both renders agree exactly");
                expect(reversed_vs_authored > 0,
                       "head on, reversing the order changes the picture");
            } else {
                // And the reason a run-time re-sort exists at all: away from
                // the view a table was authored for, the authored order drifts
                // from the painter's order. It is measured against the same
                // bar as head on rather than asserted to be zero, because the
                // drift is the point.
                expect(authored_vs_sorted < reversed_vs_authored / 4,
                       "turned, the authored order still beats a wrong one by "
                       "a wide margin, and the gap is what the re-sort closes");
            }
        }
    }

    for (int frame = 0;; ++frame) {
        spin += 0.02f;
        // sin/cos of the shipped pitch as literals rather than through
        // libdragon's fmath: those calls appear only on this path, and the
        // control geometry that renders correctly never touches them, which
        // makes them the one untested thing between a working camera and this
        // one. 60 degrees: sin 0.8660254, cos 0.5.
        // The shipped pitch, held, while the camera walks around the board.
        // sin and cos of 60 degrees as literals; the orbit uses fmath, which
        // was suspected during bring-up and cleared.
        const float radius = 560.0f * WORLD_SCALE;
        const float height = radius * 0.8660254f;
        const float back = radius * 0.5f;
        fm_vec3_t eye;
        eye.v[0] = back * fm_sinf(spin);
        eye.v[1] = height;
        eye.v[2] = -back * fm_cosf(spin);

        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(50.0f), 16.0f, 1200.0f);
        t3d_viewport_look_at(&viewport, &eye, &focus, &up);

        surface_t *display = display_get();
        const uint64_t started = get_ticks_us();

        // The parts re-sorted for wherever the eye is now. This is the whole
        // of what replaces a depth buffer, and it is what the PlayStation
        // already does when a figure turns. It is an insertion sort over a few
        // hundred entries on data that was nearly sorted last frame, because
        // the camera moves a little at a time.
        sort_for(eye);

        rdpq_attach(display, display_get_zbuf());
        t3d_frame_start();
        // `t3d_frame_start` sets `rdpq_mode_zbuf(true, true)` unconditionally
        // (tiny3d src/t3d/t3d.c), and `T3D_FLAG_DEPTH` is not a library feature
        // at all -- `t3d_state_set_drawflags` ORs it straight into the RDP
        // triangle opcode, where bit 0 is the Z bit. Clearing the flag therefore
        // stops each triangle *carrying* depth while the render mode still tells
        // the RDP to compare it, which is a mismatched state and not a painter's
        // renderer. Turning it off here too is what actually asks for one.
        rdpq_mode_zbuf(false, false);
        t3d_viewport_attach(&viewport);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_screen_clear_color(RGBA32(26, 40, 30, 255));
        // The clear stays: Tiny3D draws nothing at all without it, which is a
        // property of the library rather than of the scene. The *test* is off,
        // which is the part that matters -- what decides the picture below is
        // the order the parts are handed over in.
        t3d_screen_clear_depth();
        t3d_light_set_ambient(AMBIENT);
        t3d_light_set_count(0);
        t3d_state_set_drawflags(
            static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT));

        int triangles = BOARD_CELLS * 2;
        t3d_matrix_push(scene_matrix);
        // The ground first, because everything stands on it. With depth
        // genuinely off this is what decides it -- and it is why the board
        // being left in its one-bit-test position covered every figure.
        draw_board();
        for (int step = 0; step < scene_parts; ++step) {
            const int entry = sorted_parts[step];
            if (part_slot[entry] < 0) {
                t3d_vert_load(ground + part_index[entry] * (VERTS_PER_CELL / 2),
                              0, VERTS_PER_CELL);
                t3d_tri_draw_unindexed(0, 2);
                continue;
            }
            t3d_vert_load(
                vertices + static_cast<size_t>(slot_offset[part_slot[entry]] +
                                               part_index[entry]) *
                               (VERTS_PER_PART / 2),
                0, VERTS_PER_PART);
            t3d_tri_draw_unindexed(0, 12);
            triangles += 12;
        }
        t3d_matrix_pop(1);

        // Once, at the end, rather than after every part. The sync is for
        // handing the RDP back to RDPQ, not for separating Tiny3D draws from
        // each other -- and calling it per part queued a sync for every one of
        // the 292 parts on screen, which hung the frame outright.
        t3d_tri_sync();
        rdpq_detach_wait();
        const uint64_t finished = get_ticks_us();

        if (frame < 3 || frame == 60) {
            fprintf(stderr, "mark drawn\n"); fflush(stderr);
            fprintf(stderr, "frame triangles=%d us=%llu fps=%llu\n", triangles,
                    static_cast<unsigned long long>(finished - started),
                    static_cast<unsigned long long>(
                        (finished - started) > 0 ? 1000000u / (finished - started) : 0));
            // The frame has to contain figures, or a fast number measures an
            // empty screen. Sampled the way the play ROM's probe does.
            fprintf(stderr, "mark classify\n"); fflush(stderr);
            const uint16_t ground = color_to_packed16(RGBA32(26, 40, 30, 255));
            const uint8_t *base = static_cast<const uint8_t *>(UncachedAddr(display->buffer));
            int drawn = 0;
            for (int probe = 0; probe < 200; ++probe) {
                const int x = 20 + (probe % 20) * 14;
                const int y = 40 + (probe / 20) * 18;
                const uint16_t *row = reinterpret_cast<const uint16_t *>(
                    base + static_cast<size_t>(y) * display->stride);
                if (row[x] != ground) ++drawn;
            }
            fflush(stderr);
            fprintf(stderr, "classify frame=%d figure_pixels=%d/200\n", frame, drawn);
            fflush(stderr);
            if (frame == 0) expect(drawn >= 20, "the figures reached the framebuffer");
            if (frame == 2) {
                fprintf(stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
                        checks - failures, checks);
                fprintf(stderr, "CHECKPOINT figures\n");
                reported = true;
            }
        }

        display_show(display);

        // Hold the first drawn frame while the bring-up is being looked at.
        // Frame zero draws the roster and later frames come back empty, so this
        // separates "the figures can be drawn" -- which is answered -- from
        // "they can be drawn every frame", which is not yet.
        if (reported && HOLD_FIRST_FRAME) {
            while (true) {
                wait_ms(1000);
                fprintf(stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
                        checks - failures, checks);
            }
        }

        if (frame % 120 == 0) {
            fprintf(stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
                    checks - failures, checks);
        }
    }
}
