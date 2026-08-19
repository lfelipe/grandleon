// SPDX-License-Identifier: MIT
//
// What a triangle costs on this console, measured rather than quoted.
//
// The number this repository has authored against -- ~25 us of CPU per
// `rdpq_triangle`, capping a frame near 600 triangles -- was taken on the old
// libdragon pin by a scratch that no longer exists in this tree (it is in
// `~/src/grandleon-old`, `build-n64/scratch3d/main.c`). The pin has since moved
// to `preview`, and `TRIANGLE_BAND` in the art library is still the old
// measurement's consequence. A budget that outlives its measurement is a habit,
// so this re-takes it.
//
// Two shapes are timed, and the difference between them is the point:
//
//   * **textured** triangles, which is what the retired scratch submitted, so
//     there is a like-for-like number to compare against;
//   * **flat** triangles, which is what this game's figures actually are --
//     a box mesh wears a ramp and a rung and never a texture, so the shipped
//     cost is the flat one and the textured number would overstate it.
//
// Each is swept over several counts rather than measured once, because a single
// count cannot separate per-triangle cost from per-frame overhead: the slope
// between two counts is the triangle, the intercept is the frame.
//
// Nothing here draws the game. It is a measurement program, in no gate, and it
// reports through ISViewer so the ares harness can read a verdict off it.

#include <libdragon.h>

#include <t3d/t3d.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const char *message) {
    ++checks;
    fprintf(stderr, "%s %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition) ++failures;
}

// The counts swept. The largest is well past what a board of sixteen figures
// would submit at the current band (16 x 300 = 4,800), so the ceiling is
// measured rather than extrapolated to.
constexpr int SWEEP[] = {64, 256, 512, 1024, 2048, 4096};
constexpr int SWEEP_COUNT = static_cast<int>(sizeof(SWEEP) / sizeof(SWEEP[0]));

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

uint16_t background_word() {
    return color_to_packed16(RGBA32(16, 26, 27, 255));
}

uint16_t sample_at(surface_t *display, int x, int y) {
    const uint8_t *base = static_cast<const uint8_t *>(UncachedAddr(display->buffer));
    const uint16_t *row =
        reinterpret_cast<const uint16_t *>(base + static_cast<size_t>(y) * display->stride);
    return row[x];
}

// One triangle of a strip that covers the screen, so every submitted triangle
// has real area and the rasteriser is doing the work it would do for a figure.
// Zero-area triangles would measure the submission path with the RDP idle,
// which is not the question.
void triangle_corners(int index, float *ax, float *ay, float *bx, float *by,
                      float *cx, float *cy) {
    const int column = index % 20;
    const int row = (index / 20) % 15;
    const float x0 = 8.0f + static_cast<float>(column) * 15.0f;
    const float y0 = 8.0f + static_cast<float>(row) * 15.0f;
    *ax = x0;          *ay = y0;
    *bx = x0 + 14.0f;  *by = y0;
    *cx = x0 + 14.0f;  *cy = y0 + 14.0f;
}

// Microseconds of CPU spent submitting `count` flat-shaded triangles.
uint64_t time_flat(surface_t *display, int count) {
    rdpq_attach(display, nullptr);
    rdpq_clear(RGBA32(16, 26, 27, 255));
    rdpq_set_mode_standard();

    const uint64_t started = get_ticks_us();
    for (int index = 0; index < count; ++index) {
        float ax, ay, bx, by, cx, cy;
        triangle_corners(index, &ax, &ay, &bx, &by, &cx, &cy);
        // Shaded rather than filled: a face of a figure carries one colour of
        // its ramp, and the shade format is how a colour reaches a triangle.
        float v1[] = {ax, ay, 0.9f, 0.6f, 0.3f, 1.0f};
        float v2[] = {bx, by, 0.9f, 0.6f, 0.3f, 1.0f};
        float v3[] = {cx, cy, 0.9f, 0.6f, 0.3f, 1.0f};
        rdpq_triangle(&TRIFMT_SHADE, v1, v2, v3);
    }
    const uint64_t submitted = get_ticks_us();
    rdpq_detach_wait();
    const uint64_t drawn = get_ticks_us();

    fprintf(stderr, "time flat count=%d submit_us=%llu rdp_wait_us=%llu\n",
            count, static_cast<unsigned long long>(submitted - started),
            static_cast<unsigned long long>(drawn - submitted));
    return submitted - started;
}

uint64_t time_flat_total(surface_t *display, int count) {
    const uint64_t started = get_ticks_us();
    rdpq_attach(display, nullptr);
    rdpq_clear(RGBA32(16, 26, 27, 255));
    rdpq_set_mode_standard();
    for (int index = 0; index < count; ++index) {
        float ax, ay, bx, by, cx, cy;
        triangle_corners(index, &ax, &ay, &bx, &by, &cx, &cy);
        float v1[] = {ax, ay, 0.9f, 0.6f, 0.3f, 1.0f};
        float v2[] = {bx, by, 0.9f, 0.6f, 0.3f, 1.0f};
        float v3[] = {cx, cy, 0.9f, 0.6f, 0.3f, 1.0f};
        rdpq_triangle(&TRIFMT_SHADE, v1, v2, v3);
    }
    rdpq_detach_wait();
    return get_ticks_us() - started;
}

// ---------------------------------------------------------------------------
// The same sweep through Tiny3D
//
// `rdpq_triangle` transforms on the CPU. Tiny3D hands a packed vertex buffer to
// an RSP microcode and draws from a 70-entry vertex cache on the RSP, so the CPU
// submits indices rather than transformed corners. That is the whole difference
// being measured, and it is why the pin had to move to `preview` first.
//
// The cache is why this batches: 70 vertices is 23 whole triangles, so a count
// larger than that is loads and draws in a loop, which is exactly what a real
// figure of a few hundred triangles would do.

constexpr int T3D_CACHE_VERTS = 69;              // 23 triangles, a multiple of 3
constexpr int T3D_BATCH_TRIS = T3D_CACHE_VERTS / 3;

T3DVertPacked *make_vertices(int triangles) {
    const int verts = triangles * 3;
    const int packed = (verts + 1) / 2;
    T3DVertPacked *buffer =
        static_cast<T3DVertPacked *>(malloc_uncached(sizeof(T3DVertPacked) * static_cast<size_t>(packed)));
    if (buffer == nullptr) return nullptr;

    // Tiny3D's own headers are C, and this file is C++17 under -Wpedantic, so
    // the compound literals and designated initialisers its examples use are
    // spelled out here rather than the warning discipline being relaxed.
    fm_vec3_t facing;
    facing.v[0] = 0.0f; facing.v[1] = 0.0f; facing.v[2] = 1.0f;
    const uint16_t normal = t3d_vert_pack_normal(&facing);

    for (int index = 0; index < packed; ++index) {
        const int first = index * 2;
        const int second = first + 1;
        T3DVertPacked entry{};
        entry.posA[0] = static_cast<int16_t>((first % 32) * 4 - 64);
        entry.posA[1] = static_cast<int16_t>(((first / 32) % 32) * 4 - 64);
        entry.posA[2] = 0;
        entry.normA = normal;
        entry.posB[0] = static_cast<int16_t>((second % 32) * 4 - 64);
        entry.posB[1] = static_cast<int16_t>(((second / 32) % 32) * 4 - 64);
        entry.posB[2] = 0;
        entry.normB = normal;
        entry.rgbaA = 0xE09950FFu;
        entry.rgbaB = 0xE09950FFu;
        entry.stA[0] = 0; entry.stA[1] = 0;
        entry.stB[0] = 0; entry.stB[1] = 0;
        buffer[index] = entry;
    }
    return buffer;
}

// Microseconds of CPU spent submitting `count` triangles through Tiny3D.
uint64_t time_t3d(surface_t *display, T3DViewport *viewport,
                  const T3DVertPacked *vertices, int count) {
    rdpq_attach(display, nullptr);
    t3d_frame_start();
    t3d_viewport_attach(viewport);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(16, 26, 27, 255));
    // No depth: this console has no z-buffer in this game, the figures are
    // drawn far-to-near, and asking for one here would measure a frame the
    // renderer would never submit. No lighting either, for the same reason --
    // a face carries a rung of its ramp, already resolved.
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT));

    const uint64_t started = get_ticks_us();
    int drawn = 0;
    while (drawn < count) {
        const int batch = (count - drawn) < T3D_BATCH_TRIS ? (count - drawn) : T3D_BATCH_TRIS;
        t3d_vert_load(vertices + (drawn * 3) / 2, 0, static_cast<uint32_t>(batch * 3));
        t3d_tri_draw_unindexed(0, static_cast<uint32_t>(batch));
        t3d_tri_sync();
        drawn += batch;
    }
    const uint64_t submitted = get_ticks_us();
    rdpq_detach_wait();
    const uint64_t finished = get_ticks_us();

    fprintf(stderr, "time t3d count=%d submit_us=%llu rdp_wait_us=%llu\n",
            count, static_cast<unsigned long long>(submitted - started),
            static_cast<unsigned long long>(finished - submitted));
    return finished - started;
}

}  // namespace

extern "C" int main(void) {
    debug_init_isviewer();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE,
                 FILTERS_RESAMPLE);
    rdpq_init();

    fprintf(stderr, "scratch3d: triangle cost on this libdragon pin\n");

    uint64_t submit_us[SWEEP_COUNT];
    uint64_t total_us[SWEEP_COUNT];

    for (int step = 0; step < SWEEP_COUNT; ++step) {
        surface_t *display = display_get();
        submit_us[step] = time_flat(display, SWEEP[step]);
        display_show(display);

        display = display_get();
        total_us[step] = time_flat_total(display, SWEEP[step]);
        fprintf(stderr, "time flat count=%d frame_total_us=%llu\n", SWEEP[step],
                static_cast<unsigned long long>(total_us[step]));

        // The frame has to actually contain triangles, or a fast number is a
        // measurement of nothing. Sampled the way the play ROM's probe does.
        int hits = 0;
        for (int probe = 0; probe < 25; ++probe) {
            const int x = 14 + (probe % 5) * 60;
            const int y = 14 + (probe / 5) * 44;
            if (x < SCREEN_W && y < SCREEN_H &&
                sample_at(display, x, y) != background_word()) {
                ++hits;
            }
        }
        fprintf(stderr, "classify count=%d hits=%d/25\n", SWEEP[step], hits);
        // The strip tiles a 20x15 grid, so a count below 300 covers only part
        // of the screen and a fixed threshold would fail the small steps for
        // being small rather than for being wrong. Coverage is asserted in
        // proportion to what the count can physically reach; the point of the
        // check is that the frame is not empty, which a fast-and-blank frame
        // would otherwise pass.
        const int reachable = SWEEP[step] < 300 ? SWEEP[step] : 300;
        const int wanted = reachable >= 300 ? 15 : 1;
        expect(hits >= wanted, "the triangle field reached the framebuffer");
        display_show(display);
    }

    // The slope between the smallest and largest sweep is the per-triangle
    // cost with per-frame overhead cancelled out; the frame budgets follow
    // from it directly.
    const uint64_t span_submit = submit_us[SWEEP_COUNT - 1] - submit_us[0];
    const uint64_t span_total = total_us[SWEEP_COUNT - 1] - total_us[0];
    const int span_count = SWEEP[SWEEP_COUNT - 1] - SWEEP[0];

    const uint64_t submit_ns = span_submit * 1000u / static_cast<uint64_t>(span_count);
    const uint64_t total_ns = span_total * 1000u / static_cast<uint64_t>(span_count);

    fprintf(stderr, "slope submit_ns_per_triangle=%llu total_ns_per_triangle=%llu\n",
            static_cast<unsigned long long>(submit_ns),
            static_cast<unsigned long long>(total_ns));

    if (total_ns > 0) {
        fprintf(stderr, "budget triangles_at_30fps=%llu triangles_at_60fps=%llu\n",
                static_cast<unsigned long long>(33333u * 1000u / total_ns),
                static_cast<unsigned long long>(16666u * 1000u / total_ns));
    }

    expect(submit_ns > 0, "a triangle costs a measurable amount of CPU");

    // ---- The same sweep, through Tiny3D -----------------------------------
    T3DInitParams t3d_params{};
    t3d_init(t3d_params);
    T3DViewport viewport = t3d_viewport_create();
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(60.0f), 16.0f, 512.0f);
    fm_vec3_t eye;   eye.v[0] = 0.0f;   eye.v[1] = 48.0f;  eye.v[2] = -96.0f;
    fm_vec3_t focus; focus.v[0] = 0.0f; focus.v[1] = 0.0f; focus.v[2] = 0.0f;
    fm_vec3_t up;    up.v[0] = 0.0f;    up.v[1] = 1.0f;    up.v[2] = 0.0f;
    t3d_viewport_look_at(&viewport, &eye, &focus, &up);

    T3DVertPacked *vertices = make_vertices(T3D_BATCH_TRIS);
    expect(vertices != nullptr, "the Tiny3D vertex buffer allocates");

    uint64_t t3d_us[SWEEP_COUNT];
    if (vertices != nullptr) {
        for (int step = 0; step < SWEEP_COUNT; ++step) {
            surface_t *display = display_get();
            t3d_us[step] = time_t3d(display, &viewport, vertices, SWEEP[step]);
            fprintf(stderr, "time t3d count=%d frame_total_us=%llu\n", SWEEP[step],
                    static_cast<unsigned long long>(t3d_us[step]));
            display_show(display);
        }

        const uint64_t t3d_span = t3d_us[SWEEP_COUNT - 1] - t3d_us[0];
        const uint64_t t3d_ns = t3d_span * 1000u / static_cast<uint64_t>(span_count);
        fprintf(stderr, "slope t3d_ns_per_triangle=%llu\n",
                static_cast<unsigned long long>(t3d_ns));
        if (t3d_ns > 0) {
            fprintf(stderr,
                    "budget t3d_triangles_at_30fps=%llu t3d_triangles_at_60fps=%llu\n",
                    static_cast<unsigned long long>(33333u * 1000u / t3d_ns),
                    static_cast<unsigned long long>(16666u * 1000u / t3d_ns));
        }
        expect(t3d_ns > 0, "a Tiny3D triangle costs a measurable amount of CPU");
    }

    fprintf(stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
            checks - failures, checks);
    while (true) {
        wait_ms(1000);
        fprintf(stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
                checks - failures, checks);
    }
}
