// SPDX-License-Identifier: MIT
#include <grandleon/client/turn_client.hpp>
#include <grandleon/client/console_localization.hpp>
#include <iostream>
#include <string>
#include <vector>

namespace client = grandleon::client;
namespace turn = client::turn;
namespace campaign = grandleon::campaign;
namespace core = grandleon::core;
int failures{};
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Sink final : turn::ReportSink { void line(const char*) override {} };
struct Pilot final : turn::TurnClient {
    explicit Pilot(Sink& sink) : TurnClient(sink) {}
    std::vector<std::uint16_t> keys;
    std::size_t next{};
    bool saw_company{}, saw_confirmation{}, saw_ineligible{}, saw_offboard{}, saw_capacity{}, saw_refusal{};
    void paint(const grandleon::simulation::EncounterSnapshot&, const turn::Overlay&) override {}
    void paint_screen(const turn::ScreenView& view) override {
        if (view.screen == turn::Screen::company) saw_company = true;
        for (int row = 0; row < view.page->count; ++row) {
            const std::string text(view.page->line(row));
            if (text.find("DEAD HERO") != std::string::npos || text.find("UNRECRUITED") != std::string::npos) saw_ineligible = true;
            if (text.find("NOT IN BATTLE") != std::string::npos) saw_offboard = true;
            if (text.find("2/5") != std::string::npos) saw_capacity = true;
            if (text.find("NOT ENOUGH GOLD") != std::string::npos) saw_refusal = true;
            if (text == "BUY ONE" && !saw_confirmation) {
                saw_confirmation = true;
                check(std::string(view.page->line(view.caret_row)) == "BACK", "purchase defaults to Back");
            }
        }
    }
    std::uint16_t next_press() override {
        return next < keys.size() ? keys[next++] : turn::pad_end_of_script;
    }
    void presses(std::initializer_list<std::uint16_t> sequence) { keys.insert(keys.end(), sequence); }
};
client::CompanyManagement company() {
    client::CompanyManagement value;
    value.node.stable_id = 42;
    value.inventory_mode = grandleon::simulation::InventoryMode::carried;
    value.gold = 20;
    client::ShopOfferView stock;
    stock.offer = {9, core::ContentCategory::weapon, 100, 5, 2};
    stock.remaining = 2; stock.limited = true;
    value.shop_offers = {stock};
    for (unsigned id = 1; id <= 5; ++id) {
        client::RosterEntry hero;
        hero.member.value = id;
        hero.name = id == 1 ? "Rowan" : id == 2 ? "Akari" : id == 3 ? "Reserve" : id == 4 ? "Dead Hero" : "Unrecruited";
        hero.availability = id == 4 ? campaign::Availability::dead : id == 5 ? campaign::Availability::unrecruited : campaign::Availability::available;
        hero.states_carry_capacity = true; hero.carry_capacity = 5;
        hero.carried = {{{{}, core::ContentCategory::weapon, 88}, 2}};
        value.roster.push_back(hero);
    }
    value.fielded = {{1}, {2}};
    value.placeable = value.fielded;
    return value;
}
int main() {
    Sink sink;
    auto camp = company();
    check(!client::company_can_start_directly(camp), "authored shop prevents automatic skip");
    Pilot skip(sink);
    skip.presses({turn::pad_a});
    check(skip.next_management_intent(camp).verb == client::ManagementVerb::proceed,
        "default Continue leaves the optional shop without a purchase");
    Pilot p(sink);
    // Continue is initially selected. Choose the offer, Akari, then explicitly Buy.
    p.presses({turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_a});
    const auto purchase = p.next_management_intent(camp);
    check(purchase.verb == client::ManagementVerb::buy && purchase.member.value == 2 && purchase.shop_offer == 9,
        "host can buy one typed offer directly for the other army's explicit hero");
    check(p.saw_confirmation && p.saw_capacity && p.saw_offboard && !p.saw_ineligible,
        "recipient list shows capacity/reserves while excluding dead and unrecruited heroes");
    client::ManagementCommit refused;
    refused.error = client::ManagementError::insufficient_gold;
    p.management_committed(refused);
    p.presses({turn::pad_start});
    check(p.next_management_intent(camp).verb == client::ManagementVerb::proceed && p.saw_refusal,
        "purchase refusal remains visible and Continue enters the board");
    auto refused_formation = company();
    refused_formation.refused = grandleon::campaign_runtime::RosterError::over_deployment_capacity;
    Pilot repair(sink);
    repair.presses({turn::pad_start});
    check(repair.next_management_intent(refused_formation).verb == client::ManagementVerb::proceed && repair.saw_company,
        "an authored formation refusal leaves the existing roster-repair screen reachable");
    Pilot cancel(sink);
    cancel.presses({turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_a, turn::pad_a, turn::pad_b, turn::pad_start});
    check(cancel.next_management_intent(camp).verb == client::ManagementVerb::proceed,
        "confirming default Back never creates a purchase");
    auto many = company();
    for (unsigned id = 10; id <= 15; ++id) { auto offer = many.shop_offers.front(); offer.offer.id = id; many.shop_offers.push_back(offer); }
    Pilot pages(sink);
    pages.presses({turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_up, turn::pad_a,
        turn::pad_up, turn::pad_up, turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_a});
    const auto later = pages.next_management_intent(many);
    if (later.verb != client::ManagementVerb::buy || later.shop_offer != 15 || later.member.value != 1) std::cerr << "later verb=" << unsigned(later.verb) << " offer=" << later.shop_offer << " member=" << later.member.value << "\n";
    check(later.verb == client::ManagementVerb::buy && later.shop_offer == 15 && later.member.value == 1,
        "paging reaches later finite offers and returns exact identity");
    auto large_party = company();
    for (unsigned id = 6; id <= 11; ++id) {
        auto reserve = large_party.roster.front();
        reserve.member.value = id;
        reserve.name = "Reserve " + std::to_string(id);
        large_party.roster.push_back(reserve);
    }
    Pilot party_pages(sink);
    party_pages.presses({turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_a,
        turn::pad_up, turn::pad_up, turn::pad_a, turn::pad_up, turn::pad_a});
    const auto remote_recipient = party_pages.next_management_intent(large_party);
    check(remote_recipient.verb == client::ManagementVerb::buy && remote_recipient.member.value == 11,
        "recipient paging can reach living reserves beyond the first six heroes");
    return failures ? 1 : 0;
}
