// SPDX-License-Identifier: MIT
#include "GameplaySlice.hpp"

#include <algorithm>
#include <utility>

namespace grandleon::nintendo64::pyrite {

namespace sim = grandleon::simulation;

bool GameplaySlice::open(
    const sim::EncounterDefinition& definition,
    sim::UnitId initial_unit,
    std::uint8_t local_players
) {
    auto created = sim::create_encounter(definition);
    if (!created) return false;
    encounter_.emplace(std::move(created.encounter));
    snapshot_ = encounter_->snapshot();
    const sim::UnitSnapshot* initial = unit(initial_unit);
    if (initial == nullptr || !sim::on_board(*initial)) {
        encounter_.reset();
        snapshot_ = {};
        return false;
    }
    local_players_ = local_players > 1U ? 2U : 1U;
    active_player_ = 0U;
    players_ = {};
    players_[0].cursor = initial->position;
    for (std::uint8_t player = 1U; player < local_players_; ++player) {
        players_[player].cursor = initial->position;
        for (const sim::UnitSnapshot& candidate : snapshot_.units) {
            if (sim::on_board(candidate) &&
                candidate.side == snapshot_.active_side &&
                candidate.local_player == player + 1U) {
                players_[player].cursor = candidate.position;
                break;
            }
        }
    }
    transition_ = {};
    opening_hash_ = encounter_->canonical_hash();
    accepted_commands_ = 0;
    return true;
}

const sim::UnitSnapshot* GameplaySlice::unit_at(sim::Position position) const {
    for (const sim::UnitSnapshot& candidate : snapshot_.units) {
        if (sim::on_board(candidate) && candidate.position == position) {
            return &candidate;
        }
    }
    return nullptr;
}

const sim::UnitSnapshot* GameplaySlice::unit(sim::UnitId id) const {
    for (const sim::UnitSnapshot& candidate : snapshot_.units) {
        if (candidate.id == id) return &candidate;
    }
    return nullptr;
}

bool GameplaySlice::input(Action action, std::uint8_t player) {
    if (player >= local_players_) return false;
    active_player_ = player;
    LocalInput& input = players_[player];
    transition_ = {};
    switch (action) {
        case Action::left:
            if (input.cursor.x > 0) --input.cursor.x;
            return true;
        case Action::right:
            if (input.cursor.x + 1 < static_cast<std::int16_t>(snapshot_.width)) {
                ++input.cursor.x;
            }
            return true;
        case Action::up:
            if (input.cursor.y > 0) --input.cursor.y;
            return true;
        case Action::down:
            if (input.cursor.y + 1 < static_cast<std::int16_t>(snapshot_.height)) {
                ++input.cursor.y;
            }
            return true;
        case Action::cancel:
            input.selected = 0;
            return true;
        case Action::confirm:
            if (input.selected == 0) {
                const sim::UnitSnapshot* candidate = unit_at(input.cursor);
                if (candidate == nullptr ||
                    candidate->side != snapshot_.active_side ||
                    candidate->has_acted ||
                    candidate->local_player != player + 1U) {
                    return false;
                }
                input.selected = candidate->id;
                return true;
            }
            if (const sim::UnitSnapshot* actor = unit(input.selected);
                actor != nullptr && actor->position != input.cursor) {
                return apply({
                    sim::CommandType::move, input.selected, input.cursor,
                    0, 0, 0, 0,
                }, player);
            }
            return false;
        case Action::wait:
            if (input.selected == 0) return false;
            return apply({
                sim::CommandType::wait, input.selected, {}, 0, 0, 0, 0,
            }, player);
    }
    return false;
}

void GameplaySlice::sanitise_local_inputs() {
    for (std::uint8_t player = 0U; player < local_players_; ++player) {
        LocalInput& input = players_[player];
        const sim::UnitSnapshot* selected = unit(input.selected);
        if (selected == nullptr || !sim::on_board(*selected) ||
            selected->side != snapshot_.active_side || selected->has_acted ||
            selected->local_player != player + 1U) {
            input.selected = 0U;
        }
        input.cursor.x = std::clamp<std::int16_t>(
            input.cursor.x, 0, static_cast<std::int16_t>(snapshot_.width - 1U)
        );
        input.cursor.y = std::clamp<std::int16_t>(
            input.cursor.y, 0, static_cast<std::int16_t>(snapshot_.height - 1U)
        );
    }
}

bool GameplaySlice::apply(const sim::Command& command, std::uint8_t player) {
    transition_ = {};
    transition_.attempted = true;
    transition_.command = command;
    transition_.before_hash = encounter_->canonical_hash();
    if (const sim::UnitSnapshot* actor = unit(command.unit_id)) {
        transition_.from = actor->position;
        transition_.to = actor->position;
    }
    const sim::CommandResult result = encounter_->apply(command);
    transition_.error = result.error;
    transition_.accepted = static_cast<bool>(result);
    if (!result) return false;
    transition_.events = result.events;
    snapshot_ = encounter_->snapshot();
    if (const sim::UnitSnapshot* actor = unit(command.unit_id)) {
        transition_.to = actor->position;
    }
    transition_.after_hash = encounter_->canonical_hash();
    ++accepted_commands_;
    if (player < local_players_) players_[player].selected = 0U;
    sanitise_local_inputs();
    return true;
}

}  // namespace grandleon::nintendo64::pyrite
