// SPDX-License-Identifier: MIT
#include <grandleon/client/presenter.hpp>

#include <cstddef>
#include <string>

namespace grandleon::client {

void Roster::rebuild(const simulation::EncounterSnapshot& snapshot) {
    order_.clear();
    order_.reserve(snapshot.units.size());
    for (const simulation::UnitSnapshot& unit : snapshot.units) {
        order_.push_back(unit.id);
    }
}

std::string Roster::label(simulation::UnitId id) const {
    for (std::size_t index = 0; index < order_.size(); ++index) {
        if (order_[index] == id) return std::to_string(index + 1);
    }
    return "?";
}

simulation::UnitId Roster::resolve(const std::string& token) const {
    std::size_t index = 0;
    for (const char character : token) {
        if (character < '0' || character > '9') return 0;
        index = index * 10U + static_cast<std::size_t>(character - '0');
        if (index > order_.size()) return 0;
    }
    if (index == 0 || index > order_.size()) return 0;
    return order_[index - 1];
}

simulation::UnitId Roster::at(std::size_t index) const {
    return index < order_.size() ? order_[index] : 0;
}

}  // namespace grandleon::client
