// SPDX-License-Identifier: MIT
// The compact-console representation of exact authored 2x2 landmarks.

#include <grandleon/view/landmark.hpp>

#include <array>
#include <cstddef>
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

struct Rectangle final {
    int x;
    int y;
    int width;
    int height;
    view::LandmarkTone tone;
};

std::vector<Rectangle> rectangles(view::LandmarkCell cell) {
    std::vector<Rectangle> result;
    view::paint_landmark_cell(
        cell, 100, 50, 32,
        [&result](int x, int y, int width, int height,
                  view::LandmarkTone tone) {
            result.push_back({x, y, width, height, tone});
        }
    );
    return result;
}

std::vector<Rectangle> castle_rectangles(view::CastleTile tile) {
    std::vector<Rectangle> result;
    view::paint_castle_tile(
        tile, 100, 50, 32,
        [&result](int x, int y, int width, int height,
                  view::LandmarkTone tone) {
            result.push_back({x, y, width, height, tone});
        }
    );
    return result;
}

}  // namespace

int main() {
    constexpr std::uint64_t grove = 41U;
    constexpr std::uint64_t house = 73U;

    expect(
        view::tactical_landmark(grove, grove, house) ==
            view::TacticalLandmark::ancient_grove,
        "the exact ancient-grove identity selects the grove presentation"
    );
    expect(
        view::tactical_landmark(house, grove, house) ==
            view::TacticalLandmark::village_house,
        "the exact village-house identity selects the house presentation"
    );
    expect(
        view::tactical_landmark(99U, grove, house) ==
            view::TacticalLandmark::none,
        "another mountain identity is not mistaken for a landmark"
    );

    constexpr std::array<view::LandmarkCell, 4> cells{{
        view::landmark_cell(house, 0U, 0U, grove, house),
        view::landmark_cell(house, house, 0U, grove, house),
        view::landmark_cell(house, 0U, house, grove, house),
        view::landmark_cell(house, house, house, grove, house),
    }};
    constexpr std::array<view::LandmarkQuadrant, 4> quadrants{{
        view::LandmarkQuadrant::top_left,
        view::LandmarkQuadrant::top_right,
        view::LandmarkQuadrant::bottom_left,
        view::LandmarkQuadrant::bottom_right,
    }};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        expect(
            cells[i].landmark == view::TacticalLandmark::village_house &&
                cells[i].quadrant == quadrants[i],
            "left and above neighbours select the corresponding 2x2 quarter"
        );
    }

    bool all_inside_cell = true;
    bool every_quarter_draws = true;
    for (const view::TacticalLandmark landmark : {
             view::TacticalLandmark::ancient_grove,
             view::TacticalLandmark::village_house,
         }) {
        for (const view::LandmarkQuadrant quadrant : quadrants) {
            const std::vector<Rectangle> drawn = rectangles({landmark, quadrant});
            if (drawn.empty()) every_quarter_draws = false;
            for (const Rectangle& rectangle : drawn) {
                all_inside_cell = all_inside_cell &&
                    rectangle.x >= 100 && rectangle.y >= 50 &&
                    rectangle.width > 0 && rectangle.height > 0 &&
                    rectangle.x + rectangle.width <= 132 &&
                    rectangle.y + rectangle.height <= 82;
            }
        }
    }
    expect(every_quarter_draws, "every landmark quarter emits visible geometry");
    expect(
        all_inside_cell,
        "landmark geometry stays inside the terrain cell it decorates"
    );

    const std::vector<Rectangle> no_landmark = rectangles({});
    expect(no_landmark.empty(), "ordinary terrain emits no landmark geometry");

    constexpr std::uint64_t wall = 101U;
    constexpr std::uint64_t door = 102U;
    constexpr std::uint64_t floor = 103U;
    constexpr std::uint64_t dais = 104U;
    const std::array<view::CastleTile, 4> castle_tiles{{
        view::CastleTile::wall, view::CastleTile::door,
        view::CastleTile::floor, view::CastleTile::dais,
    }};
    expect(
        view::castle_tile(wall, wall, door, floor, dais) ==
            view::CastleTile::wall &&
        view::castle_tile(door, wall, door, floor, dais) ==
            view::CastleTile::door &&
        view::castle_tile(floor, wall, door, floor, dais) ==
            view::CastleTile::floor &&
        view::castle_tile(dais, wall, door, floor, dais) ==
            view::CastleTile::dais &&
        view::castle_tile(999U, wall, door, floor, dais) ==
            view::CastleTile::none,
        "exact castle identities keep walls, doors, room and dais distinct"
    );
    for (const view::CastleTile tile : castle_tiles) {
        const std::vector<Rectangle> drawn = castle_rectangles(tile);
        expect(!drawn.empty(), "every castle tile emits visible geometry");
        for (const Rectangle& rectangle : drawn) {
            expect(
                rectangle.x >= 100 && rectangle.y >= 50 &&
                    rectangle.width > 0 && rectangle.height > 0 &&
                    rectangle.x + rectangle.width <= 132 &&
                    rectangle.y + rectangle.height <= 82,
                "castle geometry stays inside its tactical cell"
            );
        }
    }
    expect(
        castle_rectangles(view::CastleTile::none).empty(),
        "ordinary terrain emits no castle geometry"
    );

    return failures == 0 ? 0 : 1;
}
