// SPDX-License-Identifier: MIT
// Small, allocation-free UI geometry shared by the compact consoles.

#pragma once

#include <cstdint>

namespace grandleon::view {

enum class ConsolePlatform : std::uint8_t {
    nintendo64 = 0,
    playstation,
};

enum class UiTone : std::uint8_t {
    shadow = 0,
    frame,
    surface,
    header,
    focus,
    accent,
    meter_track,
    meter_fill,
};

struct UiRect final {
    int x{};
    int y{};
    int width{};
    int height{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return width > 0 && height > 0;
    }

    [[nodiscard]] constexpr bool contains(int px, int py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

struct ConsolePanel final {
    UiRect outer{};
    UiRect inner{};
    UiRect header{};
    UiRect rows{};
    UiRect footer{};
    int row_height{};
    int row_count{};

    [[nodiscard]] constexpr UiRect row(int index) const noexcept {
        return index >= 0 && index < row_count
            ? UiRect{
                  rows.x, rows.y + index * row_height,
                  rows.width, row_height,
              }
            : UiRect{};
    }
};

// Full-page chrome for story, save, help and campaign-information screens.
// Unlike `ConsolePanel`, its body is one rectangle rather than a row list.
// The two shapes deliberately share the same tones and border arithmetic, so
// moving between a battle menu and a campaign page never looks like moving
// into a second interface.
struct ConsolePage final {
    UiRect outer{};
    UiRect inner{};
    UiRect header{};
    UiRect body{};
    UiRect footer{};
};

[[nodiscard]] constexpr ConsolePage console_page(
    int left, int top, int width, int height,
    int header_height, int footer_height, int border = 2
) noexcept {
    if (width <= border * 2 || height <= border * 2 ||
        header_height < 0 || footer_height < 0 || border < 0) {
        return {};
    }
    const int inner_width = width - border * 2;
    const int inner_height = height - border * 2;
    if (header_height + footer_height > inner_height) return {};
    const UiRect inner{
        left + border, top + border, inner_width, inner_height,
    };
    const int body_height = inner_height - header_height - footer_height;
    return {
        {left, top, width, height},
        inner,
        {inner.x, inner.y, inner.width, header_height},
        {inner.x, inner.y + header_height, inner.width, body_height},
        {
            inner.x, inner.y + header_height + body_height,
            inner.width, footer_height,
        },
    };
}

// `left` and `top` name the outside of the frame. The returned rectangles are
// exact pixels, so both consoles can use their native flat-rectangle command.
[[nodiscard]] constexpr ConsolePanel console_panel(
    int left, int top, int width, int row_count, int row_height,
    int header_height, int footer_height, int border = 2
) noexcept {
    if (width <= border * 2 || row_count < 0 || row_height <= 0 ||
        header_height < 0 || footer_height < 0 || border < 0) {
        return {};
    }
    const int content_height = row_count * row_height;
    const int height = border * 2 + header_height + content_height +
        footer_height;
    const UiRect inner{
        left + border, top + border, width - border * 2,
        height - border * 2,
    };
    return {
        {left, top, width, height},
        inner,
        {inner.x, inner.y, inner.width, header_height},
        {inner.x, inner.y + header_height, inner.width, content_height},
        {
            inner.x, inner.y + header_height + content_height,
            inner.width, footer_height,
        },
        row_height,
        row_count,
    };
}

struct ButtonLegend final {
    const char* confirm;
    const char* back;
    const char* menu;
    const char* wait;
};

[[nodiscard]] constexpr ButtonLegend button_legend(
    ConsolePlatform platform
) noexcept {
    return platform == ConsolePlatform::playstation
        ? ButtonLegend{"X", "O", "^", "START"}
        : ButtonLegend{"A", "B", "Z", "START"};
}

[[nodiscard]] constexpr int meter_fill_pixels(
    int pixels, int value, int maximum
) noexcept {
    if (pixels <= 0 || value <= 0 || maximum <= 0) return 0;
    if (value >= maximum) return pixels;
    return (pixels * value + maximum / 2) / maximum;
}

template<typename Paint>
inline void paint_console_panel(const ConsolePanel& panel, Paint paint) {
    if (!panel.outer.valid()) return;
    paint(
        {panel.outer.x + 2, panel.outer.y + 2,
         panel.outer.width, panel.outer.height},
        UiTone::shadow
    );
    paint(panel.outer, UiTone::frame);
    paint(panel.inner, UiTone::surface);
    if (panel.header.valid()) paint(panel.header, UiTone::header);
    if (panel.header.valid() && panel.rows.valid()) {
        paint(
            {panel.rows.x, panel.rows.y, panel.rows.width, 1},
            UiTone::accent
        );
    }
    if (panel.footer.valid()) {
        paint(panel.footer, UiTone::surface);
        paint(
            {panel.footer.x, panel.footer.y, panel.footer.width, 1},
            UiTone::accent
        );
    }
}

template<typename Paint>
inline void paint_console_page(const ConsolePage& page, Paint paint) {
    if (!page.outer.valid()) return;
    paint(
        {page.outer.x + 2, page.outer.y + 2,
         page.outer.width, page.outer.height},
        UiTone::shadow
    );
    paint(page.outer, UiTone::frame);
    paint(page.inner, UiTone::surface);
    if (page.header.valid()) paint(page.header, UiTone::header);
    if (page.header.valid() && page.body.valid()) {
        paint(
            {page.body.x, page.body.y, page.body.width, 1},
            UiTone::accent
        );
    }
    if (page.footer.valid()) {
        paint(page.footer, UiTone::surface);
        paint(
            {page.footer.x, page.footer.y, page.footer.width, 1},
            UiTone::accent
        );
    }
}

template<typename Paint>
inline void paint_focus(UiRect selected, Paint paint) {
    if (!selected.valid()) return;
    paint(selected, UiTone::focus);
    paint({selected.x, selected.y, 3, selected.height}, UiTone::accent);
    paint(
        {selected.x + selected.width - 1, selected.y, 1, selected.height},
        UiTone::accent
    );
}

template<typename Paint>
inline void paint_focus_row(
    const ConsolePanel& panel, int row, Paint paint
) {
    paint_focus(panel.row(row), paint);
}

template<typename Paint>
inline void paint_meter(
    UiRect track, int value, int maximum, Paint paint
) {
    if (!track.valid()) return;
    paint(track, UiTone::meter_track);
    const int fill = meter_fill_pixels(track.width, value, maximum);
    if (fill > 0) {
        paint({track.x, track.y, fill, track.height}, UiTone::meter_fill);
    }
}

}  // namespace grandleon::view
