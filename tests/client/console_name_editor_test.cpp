// SPDX-License-Identifier: MIT
#include <grandleon/client/console_name_editor.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace client = grandleon::client;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

}  // namespace

int main() {
    client::ConsoleNameEditor authored{"Aren", "Aren"};
    expect(
        authored.name() == "AREN" && authored.current_is_editable(),
        "a source name is folded into the shared console alphabet"
    );
    expect(
        authored.save() == client::ConsoleNameAnswer::use_default,
        "saving the visible authored default keeps it localizable"
    );

    for (int i = 0; i < 4; ++i) authored.erase();
    expect(
        authored.save() == client::ConsoleNameAnswer::editing &&
            authored.invalid(),
        "an empty name stays in the editor with a visible refusal"
    );
    expect(
        authored.activate() == client::ConsoleNameAnswer::editing &&
            authored.name() == "A",
        "the selected letter enters the bounded name"
    );
    expect(
        authored.save() == client::ConsoleNameAnswer::save,
        "a non-default printable name is ready for the rename intent"
    );

    client::ConsoleNameEditor bounded{"", "Aren"};
    for (int i = 0; i < 30; ++i) (void)bounded.activate();
    expect(
        bounded.name().size() == client::console_player_name_glyphs,
        "the editor cannot create a name wider than either console HUD"
    );

    client::ConsoleNameEditor defaults{"Luzia", "Aren"};
    for (int row = 0; row < 4; ++row) defaults.move(0, 1);
    for (int column = 0; column < 3; ++column) defaults.move(1, 0);
    expect(
        client::ConsoleNameEditor::key(
            defaults.row(), defaults.column()
        ).kind == client::ConsoleNameKeyKind::use_default &&
            defaults.activate() == client::ConsoleNameAnswer::use_default,
        "the grid exposes an explicit reset to the authored default"
    );

    const std::string utf8 = "Lu\xC3\xADza";
    client::ConsoleNameEditor external{utf8, "Aren"};
    expect(
        !external.current_is_editable() && external.name().empty(),
        "a name another platform authored outside the font is not rewritten"
    );

    if (failures != 0) return 1;
    std::cout << "compact-console name editor passed\n";
    return 0;
}
