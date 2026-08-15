// SPDX-License-Identifier: MIT
// Host-side test for the shared board camera. The header is pure arithmetic
// precisely so this suite can pin its edge behaviour without a console, a
// window or a browser.

#include "grandleon/view/board_view.hpp"

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using grandleon::view::Camera;

    // A board that fits never scrolls: identical to the pre-camera renderer.
    {
        Camera camera{0, 0, 16, 11, 10, 8};
        camera.follow(9, 7, 2);
        expect(camera.x == 0 && camera.y == 0,
               "a fitting board pins the camera to the origin");
        expect(camera.visible(0, 0) && camera.visible(9, 7),
               "every cell of a fitting board is visible");
    }

    // A large board scrolls, and clamps at both extremes.
    {
        Camera camera{0, 0, 16, 11, 30, 20};
        camera.follow(29, 19, 2);
        expect(camera.x == 14 && camera.y == 9,
               "following the far corner clamps to the map edge");
        camera.follow(0, 0, 2);
        expect(camera.x == 0 && camera.y == 0,
               "following the origin clamps back");
    }

    // The margin keeps the cursor away from the edge while scrolling.
    {
        Camera camera{0, 0, 16, 11, 30, 20};
        camera.follow(15, 5, 2);
        expect(camera.x == 2,
               "crossing the right margin scrolls just enough");
        expect(camera.visible(15, 5), "the cursor stays visible");
        camera.follow(3, 5, 2);
        expect(camera.x == 1,
               "crossing the left margin scrolls back just enough");
    }

    // Degenerate margins and one-cell viewports stay inside the map.
    {
        Camera camera{0, 0, 1, 1, 5, 5};
        camera.follow(4, 4, 2);
        expect(camera.x == 4 && camera.y == 4,
               "a one-cell viewport tracks the cursor exactly");
        expect(camera.visible(4, 4), "and shows it");
    }

    return failures == 0 ? 0 : 1;
}
