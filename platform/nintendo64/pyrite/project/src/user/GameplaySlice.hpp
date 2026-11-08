// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/simulation/encounter.hpp>

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <vector>

namespace grandleon::nintendo64::pyrite {

// The smallest useful input boundary for the Pyrite migration.  It owns a real
// portable Encounter and translates board gestures into the same commands the
// shipping presenters send.  Rendering can be replaced without restating a
// movement or turn rule here.
class GameplaySlice final {
public:
    enum class Action : std::uint8_t {
        left,
        right,
        up,
        down,
        confirm,
        cancel,
        wait,
    };

    struct Transition final {
        bool attempted{};
        bool accepted{};
        simulation::Command command{};
        simulation::CommandError error{simulation::CommandError::none};
        simulation::Position from{};
        simulation::Position to{};
        std::vector<simulation::Event> events;
        std::uint64_t before_hash{};
        std::uint64_t after_hash{};
    };

    [[nodiscard]] bool open(
        const simulation::EncounterDefinition& definition,
        simulation::UnitId initial_unit,
        std::uint8_t local_players = 1U
    );

    // Handles one edge-triggered controller gesture. Selection is local input
    // state; accepted move/wait gestures go through Encounter::apply.
    [[nodiscard]] bool input(Action action, std::uint8_t player = 0U);

    // Opposing policy produces ordinary commands, then enters through this
    // same rule boundary. Exposed only for the deterministic migration replay.
    [[nodiscard]] bool apply(
        const simulation::Command& command,
        std::uint8_t player = 0U
    );

    [[nodiscard]] const simulation::EncounterSnapshot& snapshot() const {
        return snapshot_;
    }
    [[nodiscard]] simulation::Encounter& encounter() { return *encounter_; }
    [[nodiscard]] const simulation::Encounter& encounter() const {
        return *encounter_;
    }
    [[nodiscard]] simulation::Position cursor(std::uint8_t player = 0U) const {
        return players_[player < local_players_ ? player : 0U].cursor;
    }
    [[nodiscard]] simulation::UnitId selected(std::uint8_t player = 0U) const {
        return players_[player < local_players_ ? player : 0U].selected;
    }
    [[nodiscard]] std::uint8_t active_player() const { return active_player_; }
    [[nodiscard]] std::uint8_t local_players() const { return local_players_; }
    [[nodiscard]] const Transition& transition() const { return transition_; }
    [[nodiscard]] std::uint64_t opening_hash() const { return opening_hash_; }
    [[nodiscard]] std::size_t accepted_commands() const {
        return accepted_commands_;
    }

private:
    struct LocalInput final {
        simulation::Position cursor{};
        simulation::UnitId selected{};
    };

    [[nodiscard]] const simulation::UnitSnapshot* unit_at(
        simulation::Position position
    ) const;
    [[nodiscard]] const simulation::UnitSnapshot* unit(
        simulation::UnitId id
    ) const;

    std::optional<simulation::Encounter> encounter_;
    simulation::EncounterSnapshot snapshot_{};
    void sanitise_local_inputs();

    std::array<LocalInput, 2> players_{};
    std::uint8_t local_players_{1U};
    std::uint8_t active_player_{};
    Transition transition_{};
    std::uint64_t opening_hash_{};
    std::size_t accepted_commands_{};
};

}  // namespace grandleon::nintendo64::pyrite
