// SPDX-License-Identifier: MIT
#include <grandleon/view/console_ui.hpp>
#include <grandleon/view/conversation_menu.hpp>

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
    std::vector<Primitive> chip_primitives;
    const view::ConsolePanel chip = view::console_panel(
        4, 0, 118, 0, 8, 16, 0
    );
    view::paint_console_panel(
        chip,
        [&chip_primitives](view::UiRect rectangle, view::UiTone tone) {
            chip_primitives.push_back({rectangle, tone});
        }
    );
    expect(
        chip.outer.height == 20 && chip.header.height == 16 &&
            !chip.rows.valid() && !chip.footer.valid(),
        "a zero-row status chip stays compact and has no phantom content"
    );
    expect(
        chip_primitives.size() == 4U &&
            chip_primitives.front().tone == view::UiTone::shadow &&
            chip_primitives.back().tone == view::UiTone::header,
        "a status chip costs only shadow, frame, surface and header"
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

    const grandleon::core::MapConversation offer{0, "", {
        {1, "Primeira linha.\nSegunda linha.\nTerceira linha.\nVocê pode ajudar?", {
            {"Quero ajudar na aldeia.\nA estrada fica alagada.\n"
             "Vamos pelo pomar.\nPodemos seguir agora.", 2, 1},
            {"Preciso voltar.", 0, 2},
        }},
        {2, "Onde vamos nos encontrar?", {
            {"Na ponte.", 0, 3},
            {"No moinho.", 0, 4},
        }},
    }};
    view::ConversationMenu conversation;
    conversation.start(offer);
    expect(conversation.more() && !conversation.choices_visible(),
        "a long question must show its final page before offering replies");
    expect(conversation.confirm() == 0 && conversation.choices_visible() &&
           conversation.line(0) == "Você pode ajudar?",
        "paging keeps all text including accented letters");
    conversation.move(-1);
    expect(conversation.selected() == 1, "reply navigation wraps");
    conversation.move(1);
    expect(conversation.selected() == 0 && conversation.label(0).find("...") != std::string::npos,
        "a long reply has a compact preview");
    expect(conversation.confirm() == 0 && conversation.more() && !conversation.choices_visible(),
        "selecting the long reply opens its first page");
    expect(conversation.confirm() == 0 && conversation.line(0) == "Podemos seguir agora.",
        "a long reply is readable in full before it is chosen");
    expect(conversation.confirm() == 0 && conversation.question_id() == 2,
        "a follow-up question does not submit a Talk command");
    conversation.move(1);
    expect(conversation.confirm() == 4, "only the selected final reply submits its authored ID");

    return failures == 0 ? 0 : 1;
}
