// SPDX-License-Identifier: MIT
// The compact-console language choice and the authored strings it drives.

#include <grandleon/client/console_localization.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/sheet/unit_sheet.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace console = grandleon::client::console_localization;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sheet = grandleon::sheet;
namespace sim = grandleon::simulation;
namespace turn = grandleon::client::turn;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

gc::GameSource localized_source() {
    gc::GameSource value;
    const auto blade = core::stable_content_id_v1("blade");
    const auto knight = core::stable_content_id_v1("knight");
    const auto sword = core::stable_content_id_v1("sword");
    const auto rowan = core::stable_content_id_v1("rowan");
    const auto gate = core::stable_content_id_v1("gate");
    value.game_id[0] = 0x4d;
    value.title = "The Gate";
    value.source_language = "en";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{blade, "Blade"}};
    value.classes = {{knight, "Knight", {8, 4, 2, 0, 3}, {blade}}};
    value.weapons = {{sword, "Sword", blade, 3, 1, 1}};
    value.unit_types = {{rowan, "Rowan", knight, 0, {sword}, {}}};
    value.maps = {{gate, "Gate", 3, 2, std::vector<std::uint64_t>(6, 1)}};
    value.objectives = {{
        core::stable_content_id_v1("hold"), "Hold",
        gc::ObjectiveKind::defeat_all_opponents
    }};
    value.dialogues = {{
        core::stable_content_id_v1("opening"), "At Dawn",
        {{"Rowan", "Hold the gate.", 0}}
    }};
    value.translations = {
        {"es", {
            {"project.title", "The Gate", "La Puerta"},
            {"weapons.sword.name", "Sword", "Espada"},
            {"dialogues.opening.lines.0.speaker", "Rowan", "Capitan"},
            {"dialogues.opening.lines.0.text", "Hold the gate.",
             "Defiende la puerta."}
        }},
        {"pt-BR", {
            {"project.title", "The Gate", "O Portao"},
            {"unit_types.rowan.name", "An old Rowan", "Cavaleiro"},
            {"weapons.sword.name", "Sword", "Espada"},
            {"dialogues.opening.lines.0.speaker", "Rowan", "Comandante"},
            {"dialogues.opening.lines.0.text", "Hold the gate.",
             "Segure o portao."}
        }}
    };
    return value;
}

class QuietSink final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

class Reader final : public turn::TurnClient {
public:
    explicit Reader(turn::ReportSink& sink) : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    void paint_screen(const turn::ScreenView& view) override {
        std::vector<std::string> page;
        if (view.page != nullptr) {
            for (int row = 0; row < view.page->count; ++row) {
                page.emplace_back(view.page->line(row));
            }
        }
        pages.push_back(std::move(page));
    }

    std::uint16_t next_press() override {
        if (at < presses.size()) return presses[at++];
        return turn::pad_a;
    }

    bool saw(std::string_view row) const {
        for (const auto& page : pages) {
            for (const std::string& candidate : page) {
                if (candidate == row) return true;
            }
        }
        return false;
    }

    std::vector<std::uint16_t> presses{
        turn::pad_down, turn::pad_down,
        turn::pad_a, turn::pad_a,
        turn::pad_up, turn::pad_up,
        turn::pad_a, turn::pad_a
    };
    std::size_t at{0};
    std::vector<std::vector<std::string>> pages;
};

}  // namespace

int main() {
    expect(
        std::string_view(console::text("es", console::Message::attack)) ==
            "ATACAR" &&
        std::string_view(console::text("pt-BR", console::Message::two_players)) ==
            "2 JOGADORES",
        "Spanish and Brazilian Portuguese carry compact interface copy"
    );

    const auto compiled = gc::compile(localized_source());
    expect(static_cast<bool>(compiled), "localized console fixture compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 64, 10000}
    );
    expect(static_cast<bool>(loaded), "localized console package loads");
    if (!loaded) return 1;

    QuietSink sink;
    Reader reader(sink);
    reader.set_package(&loaded.package);
    bool holds[1]{false};
    turn::TurnClient::LaunchMenu launch;
    launch.offer = true;
    launch.allow_one = true;
    launch.allow_two = false;
    const auto choice = reader.open_campaign(
        "The Gate", "console-localization", holds, 1, launch
    );
    expect(choice.local_players == 1U, "language selection preserves launch mode");
    expect(reader.language() == "pt-BR", "launch menu selects Brazilian Portuguese");
    expect(reader.saw("O PORTAO"), "selected language updates the authored title");
    expect(reader.saw("1 JOGADOR"), "selected language updates interface copy live");
    expect(reader.saw("IDIOMA  PT-BR"), "launch menu identifies the selected catalog");

    pr::Dialogue dialogue;
    dialogue.id = core::stable_content_id_v1("opening");
    dialogue.name = "At Dawn";
    dialogue.lines.push_back({"Rowan", "Hold the gate.", 0});
    reader.present_dialogue(dialogue);
    expect(reader.saw("COMANDANTE:"), "dialogue speaker uses the selected catalog");
    expect(reader.saw("SEGURE O PORTAO."), "dialogue text uses the selected catalog");

    expect(
        std::string_view(sheet::weapon_name(
            &loaded.package, core::stable_content_id_v1("sword"), "pt-BR"
        ).c_str()) == "ESPADA",
        "content names use the selected authored catalog"
    );
    expect(
        std::string_view(sheet::unit_type_name(
            &loaded.package, core::stable_content_id_v1("rowan"), "pt-BR"
        ).c_str()) == "ROWAN",
        "a stale optional translation falls back to current source copy"
    );

    return failures == 0 ? 0 : 1;
}
