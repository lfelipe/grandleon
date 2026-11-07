// SPDX-License-Identifier: MIT
#include <grandleon/view/console_ui.hpp>

#include <cstddef>
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

struct Primitive final {
    view::UiRect rectangle;
    view::UiTone tone;
};

}  // namespace

int main() {
    const view::ConsolePanel panel = view::console_panel(
        10, 20, 120, 5, 10, 12, 12
    );
    expect(
        panel.outer.width == 120 && panel.outer.height == 78,
        "panel height is exactly border, header, rows and footer"
    );
    expect(
        panel.row(0).y == 34 && panel.row(4).y == 74 &&
            !panel.row(5).valid(),
        "focus rows stay inside the panel content"
    );

    std::vector<Primitive> primitives;
    view::paint_console_panel(
        panel,
        [&primitives](view::UiRect rectangle, view::UiTone tone) {
            primitives.push_back({rectangle, tone});
        }
    );
    expect(
        primitives.size() == 7U,
        "a complete panel has a fixed seven-primitive budget"
    );
    view::paint_focus_row(
        panel, 2,
        [&primitives](view::UiRect rectangle, view::UiTone tone) {
            primitives.push_back({rectangle, tone});
        }
    );
    expect(
        primitives.size() == 10U,
        "a focus frame adds exactly three flat primitives"
    );

    const view::ConsolePage page = view::console_page(
        12, 8, 296, 224, 18, 18
    );
    expect(
        page.body.x == 14 && page.body.y == 28 &&
            page.body.width == 292 && page.body.height == 184 &&
            page.footer.y == 212,
        "page chrome divides one bounded frame into header, body and footer"
    );
    std::vector<Primitive> page_primitives;
    view::paint_console_page(
        page,
        [&page_primitives](view::UiRect rectangle, view::UiTone tone) {
            page_primitives.push_back({rectangle, tone});
        }
    );
    expect(
        page_primitives.size() == 7U,
        "a complete page uses the same fixed seven-primitive budget"
    );
    expect(
        !view::console_page(0, 0, 20, 20, 12, 12).outer.valid(),
        "page chrome refuses header and footer geometry that cannot fit"
    );

    expect(
        view::meter_fill_pixels(50, 0, 20) == 0 &&
            view::meter_fill_pixels(50, 10, 20) == 25 &&
            view::meter_fill_pixels(50, 25, 20) == 50,
        "meter fill clamps empty, proportional and over-full values"
    );
    std::vector<Primitive> meter;
    view::paint_meter(
        {4, 8, 50, 3}, 10, 20,
        [&meter](view::UiRect rectangle, view::UiTone tone) {
            meter.push_back({rectangle, tone});
        }
    );
    expect(
        meter.size() == 2U && meter[1].rectangle.width == 25,
        "a non-empty meter costs one track and one proportional fill"
    );

    const view::ButtonLegend n64 = view::button_legend(
        view::ConsolePlatform::nintendo64
    );
    const view::ButtonLegend ps1 = view::button_legend(
        view::ConsolePlatform::playstation
    );
    expect(
        std::string_view{n64.confirm} == "A" &&
            std::string_view{n64.back} == "B" &&
            std::string_view{n64.menu} == "Z",
        "Nintendo 64 legends use its controller labels"
    );
    expect(
        std::string_view{ps1.confirm} == "X" &&
            std::string_view{ps1.back} == "O" &&
            std::string_view{ps1.menu} == "^",
        "PlayStation legends use cross, circle and triangle glyphs"
    );

    return failures == 0 ? 0 : 1;
}
