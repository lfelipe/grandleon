// SPDX-License-Identifier: MIT

#include <grandleon/view/terrain_composition.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace view = grandleon::view;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

struct Mark final {
    int x;
    int y;
    int width;
    int height;
    view::TerrainTone tone;
};

std::vector<Mark> marks(view::TerrainComposition terrain, int size = 32) {
    std::vector<Mark> result;
    view::paint_console_terrain(
        terrain, 100, 50, size,
        [&result](int x, int y, int width, int height,
                  view::TerrainTone tone) {
            result.push_back({x, y, width, height, tone});
        }
    );
    return result;
}

}  // namespace

int main() {
    constexpr auto forest = static_cast<std::uint8_t>(
        view::ConsoleTerrainKind::forest
    );
    constexpr auto grass = static_cast<std::uint8_t>(
        view::ConsoleTerrainKind::grass
    );
    constexpr view::TerrainComposition cluster =
        view::compose_console_terrain(
            forest, 4, 3, forest, forest, forest, forest, 1, 1, 1
        );
    expect(
        cluster.same_edges == 15U && cluster.cluster == 4U,
        "a same-kind four-neighbour cluster has no internal transition"
    );

    constexpr view::TerrainComposition boundary =
        view::compose_console_terrain(
            forest, 4, 3, grass, forest, forest, grass, 2, 2, 1
        );
    expect(
        (boundary.same_edges & view::terrain_north) == 0U &&
            (boundary.same_edges & view::terrain_west) == 0U,
        "different north and west neighbours expose both material seams"
    );
    expect(
        (boundary.lower_edges & view::terrain_south) != 0U &&
            (boundary.lower_edges & view::terrain_east) == 0U,
        "only an edge above a lower neighbour receives an elevation lip"
    );

    const std::vector<Mark> clustered_marks = marks(cluster);
    const std::vector<Mark> boundary_marks = marks(boundary);
    expect(
        clustered_marks.size() >= 4U,
        "a forest cluster receives more than one tree cue"
    );
    bool boundary_has_seam = false;
    for (const Mark& mark : boundary_marks) {
        if (mark.tone == view::TerrainTone::seam) boundary_has_seam = true;
    }
    expect(
        boundary_has_seam,
        "a material boundary adds seams to the terrain-specific cues"
    );
    view::TerrainComposition alternate = cluster;
    alternate.detail = static_cast<std::uint8_t>(cluster.detail ^ 3U);
    const std::vector<Mark> alternate_marks = marks(alternate);
    bool varied = alternate_marks.size() == clustered_marks.size();
    if (varied) {
        varied = false;
        for (std::size_t i = 0; i < clustered_marks.size(); ++i) {
            if (clustered_marks[i].x != alternate_marks[i].x ||
                clustered_marks[i].y != alternate_marks[i].y) {
                varied = true;
            }
        }
    }
    expect(varied, "stable detail variants break up repeated cluster scenery");

    for (int size : {14, 32}) {
        for (std::uint8_t kind = 0; kind < 13U; ++kind) {
            const view::TerrainComposition terrain =
                view::compose_console_terrain(
                    kind, 2, 5, -1, kind, kind, -1, 1, 1, 1
                );
            const std::vector<Mark> painted = marks(terrain, size);
            bool inside = true;
            bool centre_clear = true;
            for (const Mark& mark : painted) {
                inside = inside && mark.x >= 100 && mark.y >= 50 &&
                    mark.x + mark.width <= 100 + size &&
                    mark.y + mark.height <= 50 + size;
                const int centre_x = 100 + size / 2;
                const int centre_y = 50 + size / 2;
                if (centre_x >= mark.x && centre_x < mark.x + mark.width &&
                    centre_y >= mark.y && centre_y < mark.y + mark.height) {
                    centre_clear = false;
                }
            }
            expect(inside, "every tactical cue stays inside its terrain cell");
            expect(
                centre_clear,
                "terrain composition leaves the selection/readback centre clear"
            );
        }
    }

    std::array<view::TerrainColour, 13> accents{};
    bool distinct = true;
    for (std::uint8_t kind = 0; kind < accents.size(); ++kind) {
        accents[kind] = view::terrain_colour(kind, view::TerrainTone::accent);
        for (std::uint8_t earlier = 0; earlier < kind; ++earlier) {
            if (accents[earlier] == accents[kind]) distinct = false;
        }
    }
    expect(distinct, "every terrain kind has its own identifying accent");
    bool bases_distinct = true;
    for (std::uint8_t kind = 0; kind < 13U; ++kind) {
        for (std::uint8_t earlier = 0; earlier < kind; ++earlier) {
            if (view::terrain_base_colour(kind) ==
                view::terrain_base_colour(earlier)) {
                bases_distinct = false;
            }
        }
    }
    expect(bases_distinct, "every terrain kind has its own base hue");
    expect(
        view::console_terrain_tint_level(3, 160) <
                view::console_terrain_tint_level(15, 160) &&
            view::console_terrain_tint_level(15, 160) <
                view::console_terrain_tint_level(31, 160) &&
            view::console_terrain_tint_level(31, 255) <= 31U,
        "runtime palette tinting preserves luminance and five-bit bounds"
    );

    constexpr view::TerrainComposition water =
        view::compose_console_terrain(0, 1, 1, 0, 0, 0, 0, 0, 0, 0);
    constexpr view::TerrainComposition road =
        view::compose_console_terrain(1, 1, 1, 1, 1, 1, 1, 0, 0, 0);
    expect(
        water.battle_variant != road.battle_variant,
        "the battle plate selection responds to terrain identity"
    );

    std::vector<Mark> close_marks;
    view::paint_console_close_terrain(
        boundary, 320, 240,
        [&close_marks](int x, int y, int width, int height,
                       view::TerrainTone tone) {
            close_marks.push_back({x, y, width, height, tone});
        }
    );
    bool close_inside = !close_marks.empty();
    for (const Mark& mark : close_marks) {
        close_inside = close_inside && mark.x >= 0 && mark.y >= 0 &&
            mark.x + mark.width <= 320 && mark.y + mark.height <= 240;
    }
    expect(
        close_inside,
        "terrain-specific close scenery stays inside the framebuffer"
    );

    if (failures != 0) {
        std::cerr << failures << " terrain composition assertion(s) failed\n";
        return 1;
    }
    std::cout << "terrain composition checks passed\n";
    return 0;
}
