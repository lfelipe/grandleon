// SPDX-License-Identifier: MIT
#include "GameplaySlice.hpp"

#include <utility>

namespace grandleon::nintendo64::pyrite {

namespace sim = grandleon::simulation;

bool GameplaySlice::open(
    const sim::EncounterDefinition& definition,
    sim::UnitId initial_unit
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
    cursor_ = initial->position;
    selected_ = 0;
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

bool GameplaySlice::input(Action action) {
    transition_ = {};
    switch (action) {
        case Action::left:
            if (cursor_.x > 0) --cursor_.x;
            return true;
        case Action::right:
            if (cursor_.x + 1 < static_cast<std::int16_t>(snapshot_.width)) {
                ++cursor_.x;
            }
            return true;
        case Action::up:
            if (cursor_.y > 0) --cursor_.y;
            return true;
        case Action::down:
            if (cursor_.y + 1 < static_cast<std::int16_t>(snapshot_.height)) {
                ++cursor_.y;
            }
            return true;
        case Action::cancel:
            selected_ = 0;
            return true;
        case Action::confirm:
            if (selected_ == 0) {
                const sim::UnitSnapshot* candidate = unit_at(cursor_);
                if (candidate == nullptr ||
                    candidate->side != snapshot_.active_side ||
                    candidate->has_acted) {
                    return false;
                }
                selected_ = candidate->id;
                return true;
            }
            if (const sim::UnitSnapshot* actor = unit(selected_);
                actor != nullptr && actor->position != cursor_) {
                return apply({
                    sim::CommandType::move, selected_, cursor_, 0, 0, 0, 0,
                });
            }
            return false;
        case Action::wait:
            if (selected_ == 0) return false;
            return apply({sim::CommandType::wait, selected_, {}, 0, 0, 0, 0});
    }
    return false;
}

bool GameplaySlice::apply(const sim::Command& command) {
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
    selected_ = 0;
    return true;
}

}  // namespace grandleon::nintendo64::pyrite
