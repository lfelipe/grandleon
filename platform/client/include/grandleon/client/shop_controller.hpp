// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/client/campaign_session.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace grandleon::client::shop {

struct PlayerState final {
    std::uint64_t offer{};
    campaign::PersistentEntityId member{};
    ManagementError error{ManagementError::none};
    bool confirming{};
    bool buy_selected{};
    bool ready{};
};

// Presentation state only. Gold, stock, ownership and capacity are checked by
// CampaignSession when it commits the returned buy intent.
class Controller final {
public:
    void reset(const CompanyManagement& company) {
        node_ = company.node;
        players_ = std::clamp<unsigned>(company.local_players, 1U, 2U);
        states_ = {};
        initialized_ = true;
        update(company);
    }
    void update(const CompanyManagement& company) {
        if (!initialized_ || !(node_ == company.node) ||
            players_ != std::clamp<unsigned>(company.local_players, 1U, 2U)) {
            reset(company);
            return;
        }
        for (unsigned player = 0; player < players_; ++player) {
            auto& state = states_[player];
            if (!offer(company, player)) {
                state.offer = company.shop_offers.empty() ? 0U : company.shop_offers.front().offer.id;
                state.confirming = false;
            }
            const auto owned = recipients(company, player);
            if (owned.empty()) {
                state.member = {};
                state.ready = true;
                state.confirming = false;
            } else if (!member(company, player)) {
                state.member = company.roster[owned.front()].member;
                state.ready = false;
                state.confirming = false;
            }
        }
    }
    [[nodiscard]] unsigned players() const noexcept { return players_; }
    [[nodiscard]] const PlayerState& state(unsigned player) const noexcept { return states_[player]; }
    [[nodiscard]] bool all_ready() const noexcept {
        for (unsigned player = 0; player < players_; ++player) if (!states_[player].ready) return false;
        return true;
    }
    [[nodiscard]] const ShopOfferView* offer(const CompanyManagement& company, unsigned player) const {
        if (player >= players_) return nullptr;
        for (const auto& value : company.shop_offers) if (value.offer.id == states_[player].offer) return &value;
        return nullptr;
    }
    [[nodiscard]] const RosterEntry* member(const CompanyManagement& company, unsigned player) const {
        if (player >= players_) return nullptr;
        for (const auto& value : company.roster) {
            if (value.member == states_[player].member && eligible(value, player)) return &value;
        }
        return nullptr;
    }
    void move_offer(const CompanyManagement& company, unsigned player, int delta) {
        if (!can_edit(player) || delta == 0) return;
        auto& state = states_[player];
        if (state.confirming) { state.buy_selected = delta < 0; return; }
        if (company.shop_offers.empty()) return;
        std::size_t index = 0;
        for (std::size_t i = 0; i < company.shop_offers.size(); ++i) if (company.shop_offers[i].offer.id == state.offer) index = i;
        index = delta < 0 ? (index == 0 ? company.shop_offers.size() - 1U : index - 1U)
                          : (index + 1U) % company.shop_offers.size();
        state.offer = company.shop_offers[index].offer.id;
        state.error = ManagementError::none;
    }
    void cycle_member(const CompanyManagement& company, unsigned player, int delta) {
        if (!can_edit(player) || delta == 0) return;
        auto& state = states_[player];
        if (state.confirming) { state.buy_selected = delta < 0; return; }
        const auto owned = recipients(company, player);
        if (owned.empty()) return;
        std::size_t index = 0;
        for (std::size_t i = 0; i < owned.size(); ++i) if (company.roster[owned[i]].member == state.member) index = i;
        index = delta < 0 ? (index == 0 ? owned.size() - 1U : index - 1U) : (index + 1U) % owned.size();
        state.member = company.roster[owned[index]].member;
        state.error = ManagementError::none;
    }
    [[nodiscard]] ManagementIntent confirm(const CompanyManagement& company, unsigned player) {
        ManagementIntent intent;
        if (!can_edit(player) || !offer(company, player) || !member(company, player)) return intent;
        auto& state = states_[player];
        state.error = ManagementError::none;
        if (!state.confirming) { state.confirming = true; state.buy_selected = false; return intent; }
        state.confirming = false;
        if (!state.buy_selected) return intent;
        state.buy_selected = false;
        intent.verb = ManagementVerb::buy;
        intent.member = state.member;
        intent.shop_offer = state.offer;
        intent.shop_player = static_cast<std::uint8_t>(player + 1U);
        return intent;
    }
    void cancel(unsigned player) {
        if (player >= players_) return;
        states_[player].confirming = false;
        states_[player].buy_selected = false;
        states_[player].error = ManagementError::none;
    }
    void toggle_ready(const CompanyManagement& company, unsigned player) {
        if (player >= players_) return;
        cancel(player);
        if (member(company, player)) states_[player].ready = !states_[player].ready;
    }
    void committed(const CompanyManagement& company, unsigned player, const ManagementCommit& result) {
        committed(company, player, result.error, static_cast<bool>(result));
    }
    void committed(const CompanyManagement& company, unsigned player, ManagementError error, bool accepted) {
        if (player >= players_) return;
        auto& state = states_[player];
        state.confirming = false;
        state.buy_selected = false;
        state.error = accepted ? ManagementError::none : error == ManagementError::none
            ? ManagementError::purchase_save_failed : error;
        if (accepted) state.ready = false;
        update(company);
    }

private:
    [[nodiscard]] bool can_edit(unsigned player) const { return player < players_ && !states_[player].ready; }
    [[nodiscard]] static bool eligible(const RosterEntry& value, unsigned player) {
        return value.local_player == player + 1U &&
            (value.availability == campaign::Availability::available || value.availability == campaign::Availability::retired);
    }
    [[nodiscard]] static std::vector<std::size_t> recipients(const CompanyManagement& company, unsigned player) {
        std::vector<std::size_t> result;
        for (std::size_t index = 0; index < company.roster.size(); ++index) if (eligible(company.roster[index], player)) result.push_back(index);
        const auto priority = [&](std::size_t index) {
            const auto id = company.roster[index].member;
            if (std::find(company.fielded.begin(), company.fielded.end(), id) != company.fielded.end()) return 0;
            return std::find(company.placeable.begin(), company.placeable.end(), id) != company.placeable.end() ? 1 : 2;
        };
        std::stable_sort(result.begin(), result.end(), [&](std::size_t a, std::size_t b) { return priority(a) < priority(b); });
        return result;
    }
    campaign::DefinitionRef node_{};
    std::array<PlayerState, 2> states_{};
    unsigned players_{1};
    bool initialized_{};
};
} // namespace grandleon::client::shop
