// SPDX-License-Identifier: MIT
// Pixel-metric UI geometry for Grandleon's 320x240 PlayStation renderer.

#pragma once

#include "grandleon/view/console_ui.hpp"

namespace grandleon::playstation::ui_layout {

constexpr int screen_width = 320;
constexpr int board_bottom = 208;
constexpr int text_height = 8;

[[nodiscard]] constexpr bool overlaps(
    view::UiRect first, view::UiRect second
) noexcept {
    if (!first.valid() || !second.valid()) return false;
    return first.x < second.x + second.width &&
        second.x < first.x + first.width &&
        first.y < second.y + second.height &&
        second.y < first.y + first.height;
}

struct Menu final {
    view::ConsolePanel panel{};
    int heading_x{};
    int heading_y{};
    int row_text_x{};
    int row_text_y{};
    int footer_text_x{};
    int footer_text_y{};
};

[[nodiscard]] constexpr Menu menu_at(
    int widest_label, int rows, int left, int top
) noexcept {
    int width = widest_label + 36;
    if (width < 144) width = 144;
    if (width > 272) width = 272;
    constexpr int row_height = 10;
    constexpr int header_height = 16;
    constexpr int footer_height = 16;
    const view::ConsolePanel panel = view::console_panel(
        left, top, width, rows, row_height, header_height, footer_height
    );
    return {
        panel,
        panel.header.x + 8, panel.header.y + 4,
        panel.rows.x + 8, panel.rows.y + 1,
        panel.footer.x + 8, panel.footer.y + 4,
    };
}

[[nodiscard]] constexpr Menu menu(
    int widest_label, int rows, int cursor_centre
) noexcept {
    const Menu sized = menu_at(widest_label, rows, 0, 0);
    int top = board_bottom - sized.panel.outer.height - 6;
    if (top < 6) top = 6;
    const int left = cursor_centre < screen_width / 2
        ? screen_width - 16 - sized.panel.outer.width : 16;
    return menu_at(widest_label, rows, left, top);
}

struct Sheet final {
    view::ConsolePage page{};
    view::ConsolePanel status{};
    view::ConsolePanel loadout{};
    view::UiRect footer_text{};
};

[[nodiscard]] constexpr Sheet sheet(int loadout_rows) noexcept {
    const view::ConsolePage page = view::console_page(2, 2, 316, 236, 28, 18);
    const view::ConsolePanel status = view::console_panel(
        8, 36, 304, 4, 13, 14, 0
    );
    const view::ConsolePanel loadout = view::console_panel(
        8, 112, 304, loadout_rows, 11, 14, 0
    );
    return {page, status, loadout, {16, page.footer.y + 5, 288, text_height}};
}

[[nodiscard]] constexpr bool sheet_regions_are_separate(
    int loadout_rows
) noexcept {
    const Sheet layout = sheet(loadout_rows);
    return !overlaps(layout.status.outer, layout.loadout.outer) &&
        !overlaps(layout.loadout.outer, layout.page.footer) &&
        !overlaps(layout.status.header, layout.status.rows) &&
        !overlaps(layout.loadout.header, layout.loadout.rows);
}

static_assert(sheet_regions_are_separate(6),
              "sheet text regions must not share pixels");

}  // namespace grandleon::playstation::ui_layout
