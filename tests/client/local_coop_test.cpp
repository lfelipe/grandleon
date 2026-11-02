// SPDX-License-Identifier: MIT
// Two local controllers preparing one deterministic side's commands.

#include <grandleon/client/turn_client.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace client = grandleon::client;
namespace sim = grandleon::simulation;
namespace turn = grandleon::client::turn;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

class Transcript final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

struct Painted final {
    std::int16_t x{};
    std::int16_t y{};
    sim::UnitId selected{};
    bool second{};
    std::int16_t second_x{};
    std::int16_t second_y{};
    sim::UnitId second_selected{};
    std::uint8_t player{};
    bool menu{};
    int menu_row{};
};

class TwoPads final : public turn::TurnClient {
public:
    TwoPads(turn::ReportSink& sink, std::vector<turn::ControllerPress> script)
        : TurnClient(sink), script_(std::move(script)) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay& overlay) override {
        painted.push_back({
            overlay.cursor_x,
            overlay.cursor_y,
            overlay.selected,
            overlay.second_cursor,
            overlay.second_cursor_x,
            overlay.second_cursor_y,
            overlay.second_selected,
            overlay.active_player,
            overlay.menu != nullptr,
            overlay.menu_row,
        });
    }

    std::uint16_t next_press() override { return turn::pad_end_of_script; }

    turn::ControllerPress next_controller_press() override {
        if (at_ >= script_.size()) {
            return {0, turn::pad_end_of_script};
        }
        return script_[at_++];
    }

    std::vector<Painted> painted;

private:
    std::vector<turn::ControllerPress> script_;
    std::size_t at_{0};
};

sim::EncounterDefinition board() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 5;
    definition.turn_order = sim::TurnOrder::side_blocks;

    sim::UnitDefinition first;
    first.id = 1;
    first.unit_type_id = 101;
    first.side = sim::Side::first;
    first.position = {0, 0};
    first.health = 10;
    first.strength = 2;
    first.movement = 3;
    first.action_points = 2;

    sim::UnitDefinition second = first;
    second.id = 2;
    second.unit_type_id = 102;
    second.position = {0, 2};

    sim::UnitDefinition opponent = first;
    opponent.id = 3;
    opponent.unit_type_id = 201;
    opponent.side = sim::Side::second;
    opponent.position = {4, 4};

    definition.units = {first, second, opponent};
    return definition;
}

void two_cursors_move_without_erasing_each_other() {
    auto created = sim::create_encounter(board());
    expect(static_cast<bool>(created), "the co-op board is valid");
    if (!created) return;

    Transcript sink;
    TwoPads pads(
        sink,
        {
            {0, turn::pad_right},
            {1, turn::pad_down},
            {0, turn::pad_right},
            {1, turn::pad_right},
            {0, turn::pad_end_of_script},
        }
    );
    client::Roster roster;
    roster.rebuild(created.encounter.snapshot());
    pads.set_viewport(5, 5);
    pads.battle_begins(
        created.encounter.snapshot(), roster, sim::Side::first, {}
    );
    pads.battle_definitions({}, {}, {}, created.encounter.objectives());
    const client::Intent result =
        pads.next_intent(created.encounter.snapshot(), roster);
    expect(result.kind == client::IntentKind::quit, "the script finishes cleanly");

    bool saw_player_two = false;
    bool saw_player_one_again = false;
    bool saw_both_final_positions = false;
    for (const Painted& frame : pads.painted) {
        if (frame.player == 1 && frame.x == 0 && frame.y == 3 && frame.second &&
            frame.second_x == 1 && frame.second_y == 0) {
            saw_player_two = true;
        }
        if (frame.player == 0 && frame.x == 2 && frame.y == 0 && frame.second &&
            frame.second_x == 0 && frame.second_y == 3) {
            saw_player_one_again = true;
        }
        if (frame.player == 1 && frame.x == 1 && frame.y == 3 && frame.second &&
            frame.second_x == 2 && frame.second_y == 0) {
            saw_both_final_positions = true;
        }
    }
    expect(saw_player_two, "player two moves while player one's cursor stays put");
    expect(saw_player_one_again, "player one resumes from its own cursor");
    expect(saw_both_final_positions, "both independent cursor moves remain visible");
}

void a_menu_waits_for_its_player() {
    auto created = sim::create_encounter(board());
    expect(static_cast<bool>(created), "the menu board is valid");
    if (!created) return;

    Transcript sink;
    TwoPads pads(
        sink,
        {
            {0, turn::pad_a},
            {1, turn::pad_right},
            {0, turn::pad_down},
            {0, turn::pad_end_of_script},
        }
    );
    client::Roster roster;
    roster.rebuild(created.encounter.snapshot());
    pads.set_viewport(5, 5);
    pads.battle_begins(
        created.encounter.snapshot(), roster, sim::Side::first, {}
    );
    pads.battle_definitions({}, {}, {}, created.encounter.objectives());
    (void)pads.next_intent(created.encounter.snapshot(), roster);

    bool player_two_moved = false;
    bool menu_resumed = false;
    for (const Painted& frame : pads.painted) {
        if (frame.player == 1 && frame.x == 1 && frame.y == 2 && !frame.menu &&
            frame.second_selected == 1) {
            player_two_moved = true;
        }
        if (frame.player == 0 && frame.selected == 1 && frame.menu &&
            frame.menu_row == 1 && frame.second_x == 1 && frame.second_y == 2) {
            menu_resumed = true;
        }
    }
    expect(player_two_moved, "player two can move while player one's menu is parked");
    expect(menu_resumed, "player one's selection and menu resume at their own row");
}

}  // namespace

int main() {
    two_cursors_move_without_erasing_each_other();
    a_menu_waits_for_its_player();
    if (failures == 0) {
        std::cout << "local co-op: two controllers keep independent board state\n";
    }
    return failures == 0 ? 0 : 1;
}
