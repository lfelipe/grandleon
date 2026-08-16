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

    // -----------------------------------------------------------------------
    // Fitting a board to a screen
    // -----------------------------------------------------------------------

    using grandleon::view::BoardFit;
    using grandleon::view::fit_board;

    // The Nintendo 64's own numbers, and the reason this function exists in
    // this shape: it has to reproduce the arithmetic that renderer already
    // spelled inline, or adopting it would move a shipped cartridge's picture.
    // 300 by 200 pixels of a 320x240 frame, a cell never larger than 26, never
    // smaller than 14, and 18 when the board scrolls.
    const grandleon::view::FitRule cartridge{300, 200, 26, 14, 18};

    {
        // A board that fits is drawn whole, at the tighter axis's cell.
        // 300/10 is 30 and 200/8 is 25, so height decides.
        const BoardFit fit = fit_board(cartridge, 10, 8);
        expect(fit.tile == 25, "the tighter axis chooses the cell");
        expect(fit.view_w == 10 && fit.view_h == 8,
               "and the whole board is the window");
        expect(!fit.scrolling, "a board that fits does not scroll");
    }
    {
        // A tiny board is capped rather than drawn at 150 pixels a cell.
        const BoardFit fit = fit_board(cartridge, 2, 2);
        expect(fit.tile == 26, "a small board is capped at the largest cell");
        expect(!fit.scrolling, "and still does not scroll");
    }
    {
        // The exact edge of fitting, in both directions. 300/21 is 14 and
        // 200/14 is 14; one cell more on either axis drops below the floor.
        expect(fit_board(cartridge, 21, 14).tile == 14,
               "21x14 is the largest board the cartridge draws whole");
        expect(!fit_board(cartridge, 21, 14).scrolling,
               "and it does not scroll");
        expect(fit_board(cartridge, 22, 14).scrolling,
               "one column more scrolls");
        expect(fit_board(cartridge, 21, 15).scrolling,
               "one row more scrolls");
    }
    {
        // A scrolling board takes the scrolling cell and the window it buys:
        // 300/18 is 16 across, 200/18 is 11 down.
        const BoardFit fit = fit_board(cartridge, 30, 20);
        expect(fit.tile == 18, "a scrolling board draws at the scrolling cell");
        expect(fit.view_w == 16 && fit.view_h == 11,
               "and sees what that cell buys");
        expect(fit.scrolling, "and says that it scrolls");
    }
    {
        // A board past the floor on one axis only still shows all of the
        // other, rather than being cropped to the window on both.
        const BoardFit fit = fit_board(cartridge, 40, 3);
        expect(fit.view_h == 3,
               "a short board is never windowed shorter than it is");
        expect(fit.view_w == 16, "while its long axis is");
        expect(fit.scrolling, "and it scrolls");
    }

    // The PlayStation's numbers: a whole 320 across, 208 once the message bar
    // has taken four eight-pixel rows, a cell never larger than the 32 texels
    // the art is drawn at, and never smaller than half of that.
    const grandleon::view::FitRule disc{320, 208, 32, 16, 16};

    {
        // The window the fixed-cell renderer had, reached by the rule rather
        // than declared: any board of ten by six or less is drawn at 32.
        const BoardFit fit = fit_board(disc, 10, 6);
        expect(fit.tile == 32, "a ten by six board keeps the native cell");
        expect(!fit.scrolling, "and does not scroll");
        expect(fit_board(disc, 6, 4).tile == 32,
               "and so does a smaller one, capped");
    }
    {
        // Every board the Tarnholt Line ships is larger than that window and
        // every one of them fits once the cell may shrink.
        expect(!fit_board(disc, 10, 8).scrolling, "fordlight crossing fits");
        expect(fit_board(disc, 10, 8).tile == 26, "at 26 pixels a cell");
        expect(!fit_board(disc, 12, 9).scrolling, "ashen watch fits");
        expect(!fit_board(disc, 13, 9).scrolling, "the coldgate fits");
        expect(fit_board(disc, 13, 9).tile == 23, "at 23 pixels a cell");
    }
    {
        // The floor, and what it buys when a board is past it.
        expect(fit_board(disc, 20, 13).tile == 16,
               "20x13 is the largest board the disc draws whole");
        expect(!fit_board(disc, 20, 13).scrolling, "and it does not scroll");
        const BoardFit fit = fit_board(disc, 40, 40);
        expect(fit.tile == 16 && fit.view_w == 20 && fit.view_h == 13,
               "a board past the floor scrolls in a 20x13 window");
    }

    // A board of no size is treated as one cell rather than dividing by zero,
    // which on a console is a trap the machine does not return from.
    {
        const BoardFit fit = fit_board(cartridge, 0, 0);
        expect(fit.tile == 26 && fit.view_w == 1 && fit.view_h == 1,
               "a board of no size is one capped cell");
    }

    return failures == 0 ? 0 : 1;
}
