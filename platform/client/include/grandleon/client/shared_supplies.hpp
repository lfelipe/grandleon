// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/client/campaign_session.hpp>

#include <algorithm>

namespace grandleon::client {

inline bool shared_supply(const campaign::DefinitionRef& item) noexcept {
    return item.category == core::ContentCategory::item ||
        item.category == core::ContentCategory::weapon;
}

struct SupplyRow final {
    campaign::DefinitionRef item;
    std::uint32_t stored{};
    std::uint32_t held{};
};

// The same between-battle controls for both consoles. Inventory changes go
// through the campaign; this menu only projects its current stock and kits.
class SharedSupplies final {
public:
    void refresh(const CompanyManagement& company) {
        roster_.clear();
        for (const auto& member : company.roster) {
            if (member.availability == campaign::Availability::available ||
                member.availability == campaign::Availability::retired) roster_.push_back(member);
        }
        stock_ = company.store;
        member_ = std::min(member_, roster_.empty() ? 0U : roster_.size() - 1U);
        populate();
    }

    [[nodiscard]] bool empty() const noexcept {
        const auto has_equipment = [](const auto& stacks) {
            return std::any_of(stacks.begin(), stacks.end(), [](const auto& stack) {
                return shared_supply(stack.item);
            });
        };
        return roster_.empty() || (!has_equipment(stock_) &&
            std::none_of(roster_.begin(), roster_.end(), [&has_equipment](const auto& member) {
                return has_equipment(member.carried);
            }));
    }

    void move_member(int direction) {
        member_ = cycle(member_, roster_.size(), direction);
        row_ = 0;
        populate();
    }

    void move_row(int direction) noexcept { row_ = cycle(row_, rows_.size(), direction); }
    [[nodiscard]] const RosterEntry& member() const noexcept { return roster_[member_]; }
    [[nodiscard]] const std::vector<SupplyRow>& rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t selected() const noexcept { return row_; }

private:
    static std::size_t cycle(std::size_t index, std::size_t size, int direction) noexcept {
        if (size == 0) return 0;
        return (index + (direction < 0 ? size - 1U : 1U)) % size;
    }

    void populate() {
        rows_.clear();
        if (roster_.empty()) return;
        for (const auto& stack : stock_) {
            if (shared_supply(stack.item)) rows_.push_back({stack.item, stack.quantity, 0});
        }
        for (const auto& stack : member().carried) {
            if (!shared_supply(stack.item)) continue;
            auto existing = std::find_if(rows_.begin(), rows_.end(), [&stack](const auto& row) {
                return row.item == stack.item;
            });
            if (existing == rows_.end()) rows_.push_back({stack.item, 0, stack.quantity});
            else existing->held = stack.quantity;
        }
        std::sort(rows_.begin(), rows_.end(), [](const auto& left, const auto& right) {
            if (left.item.category != right.item.category)
                return left.item.category < right.item.category;
            return left.item.stable_id < right.item.stable_id;
        });
        row_ = std::min(row_, rows_.empty() ? 0U : rows_.size() - 1U);
    }

    std::vector<RosterEntry> roster_;
    std::vector<campaign::InventoryStack> stock_;
    std::vector<SupplyRow> rows_;
    std::size_t member_{};
    std::size_t row_{};
};

}  // namespace grandleon::client
