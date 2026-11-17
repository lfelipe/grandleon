// SPDX-License-Identifier: MIT
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/campaign_runtime/shop.hpp>
#include <grandleon/storage/memory_storage.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
namespace gc = grandleon::game_content;
namespace cl = grandleon::client;
namespace ca = grandleon::campaign;
namespace pr = grandleon::package_runtime;
namespace pf = grandleon::package_format;
namespace sim = grandleon::simulation;
namespace st = grandleon::storage;
namespace core = grandleon::core;
int failures{};
void check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
struct Device final : st::SlotStorage {
    st::MemorySlotStorage memory;
    bool fail_next{};
    st::StorageError write(std::string_view slot, const std::vector<std::uint8_t>& bytes) override {
        if (fail_next) { fail_next=false; return st::StorageError::io_failure; }
        return memory.write(slot,bytes);
    }
    st::StorageRead read(std::string_view slot) const override { return memory.read(slot); }
    bool contains(std::string_view slot) const override { return memory.contains(slot); }
    st::StorageError erase(std::string_view slot) override { return memory.erase(slot); }
    std::vector<std::string> slots() const override { return memory.slots(); }
    st::StorageBudget budget() const noexcept override { return memory.budget(); }
};
void begin(cl::CampaignSession& session, bool expected_resume=false) {
    cl::SlotFailure failure; bool refused=false,resumed=false;
    check(session.begin(failure,refused,resumed)==cl::CampaignSessionError::none &&
        !refused && resumed==expected_resume,"campaign begins/resumes normally");
}
int main() {
    std::ifstream input(std::string(GRANDLEON_SOURCE_DIR)+"/tests/fixtures/campaign_projects/shop_campaign.json");
    const std::string json{std::istreambuf_iterator<char>(input),{}};
    auto parsed=gc::parse_source_project_json(json);
    check(bool(parsed),"authored shop source parses"); if(!parsed)return 1;
    auto& source=parsed.source;
    const auto engine=core::engine_version();
    const pf::Version version{engine.major,engine.minor,engine.patch};source.required_engine={version,version};
    auto compiled=gc::compile(source);check(bool(compiled),"shop compiles");if(!compiled)return 1;
    auto loaded=pf::load_mock_package(compiled.package,{version,pf::TargetProfile::desktop,0,256,100000});
    check(bool(loaded),"required shop package loads");if(!loaded)return 1;
    auto& package=loaded.package;
    const auto campaign_id=source.campaigns.front().id;
    const auto decoded=pr::load_campaign(package,campaign_id);
    check(bool(decoded),"shop metadata decodes");if(!decoded)return 1;
    const auto tonic_id=core::stable_content_id_v1("shop_tonic");
    const auto focus_id=core::stable_content_id_v1("shop_focus");
    const ca::DefinitionRef tonic{package.game_id,core::ContentCategory::item,core::stable_content_id_v1("tonic")};
    const ca::DefinitionRef focus{package.game_id,core::ContentCategory::weapon,core::stable_content_id_v1("apprentice_focus")};
    for (unsigned players : {1U,2U}) {
        Device device;cl::PackageBoards boards{package,static_cast<std::uint8_t>(players)};
        cl::CampaignSessionOptions options;options.slot="shop";options.local_players=static_cast<std::uint8_t>(players);
        cl::CampaignSession session{package,campaign_id,boards,device,options};begin(session);
        check(session.management().shop_offers.empty(),"no shop does not add an intermission");
        const auto untouched=session.save_bytes();
        check(session.buy_shop_offer({1},tonic_id).error==cl::ManagementError::unknown_offer &&
            session.save_bytes()==untouched,"offer from another node cannot be bought early");
        auto board=session.prepare_board();check(board.error==cl::CampaignSessionError::none,"real first battle prepares");
        auto battle=sim::create_encounter(board.encounter.encounter.definition);check(bool(battle),"real battle creates");if(!battle)return 1;
        const auto actor=board.encounter.binding.battle_of({1}).value;
        sim::UnitId target{};for(const auto& unit:battle.encounter.snapshot().units)if(unit.side==sim::Side::second)target=unit.id;
        sim::Command attack;attack.type=sim::CommandType::attack;attack.unit_id=actor;attack.target_id=target;
        const auto hit=battle.encounter.apply(attack);check(bool(hit),"ordinary final attack accepted");
        const auto snapshot=battle.encounter.snapshot();check(snapshot.outcome==sim::Outcome::first_side_won && !snapshot.collecting_loot,"gold-only enemy victory completes normally");
        cl::BattleReport report;report.outcome=snapshot.outcome;report.canonical_hash=battle.encounter.canonical_hash();report.final_snapshot=snapshot;report.objectives=snapshot.objectives;report.events=hit.events;
        cl::BattleAftermath aftermath;check(session.commit_battle(report,aftermath)==cl::CampaignSessionError::none,"real battle gold settles");
        auto company=session.management();check(company.gold==11 && company.shop_offers.size()==2,"next battle has earned gold and authored shop");
        check(!cl::company_can_start_directly(company),"authored shop is the only new preparation stop");
        const auto before=session.save_bytes();const auto disk_before=device.read("shop").bytes;
        device.fail_next=true;
        auto failed=session.buy_shop_offer({1},focus_id);
        check(failed.error==cl::ManagementError::purchase_save_failed && !failed.saved && session.save_bytes()==before && device.read("shop").bytes==disk_before,"failed save preserves wallet, stock, owner and exact disk bytes");
        check(bool(session.buy_shop_offer({1},focus_id)),"hero can own incompatible weapon from shop");
        check(session.management().gold==8,"one purchase pays exact authored price");
        const auto full=session.save_bytes();
        check(session.buy_shop_offer({1},tonic_id).error==cl::ManagementError::inventory_full && session.save_bytes()==full,"full bag refuses without spending gold or stock");
        check(session.buy_shop_offer({999},tonic_id).error==cl::ManagementError::unknown_member,"unknown buyer refused");
        check(bool(session.buy_shop_offer({2},tonic_id)) && bool(session.buy_shop_offer({2},tonic_id)),"two finite copies go directly to chosen second hero");
        check(session.management().gold==4 && session.management().shop_offers.front().remaining==0,"wallet and finite stock update together");
        const auto sold=session.save_bytes();
        check(session.buy_shop_offer({2},tonic_id).error==cl::ManagementError::sold_out && session.save_bytes()==sold,"sold-out offer cannot spend again");
        options.resume=true;cl::CampaignSession resumed{package,campaign_id,boards,device,options};begin(resumed,true);
        check(resumed.management().gold==4 && resumed.management().shop_offers.front().remaining==0,"resume does not refill shop or refund gold");
        const auto saved=ca::load_campaign(resumed.save_bytes(), {});
        check(bool(saved),"purchased save decodes");
        check(ca::item_quantity(saved.save.state,{1},focus)==1 && ca::item_quantity(saved.save.state,{2},tonic)==2,"both recipients keep actual purchased copies after reload");
        const pr::ShopOffer expensive{focus_id,core::ContentCategory::weapon,focus.stable_id,5,0};
        const auto prepare = [&](const ca::CampaignState& state, const pr::ShopOffer& offer,
                                 bool stated=false, std::uint16_t capacity=0) {
            return grandleon::campaign_runtime::prepare_shop_purchase(state,package.game_id,
                campaign_id,saved.save.state.progress.active_node.stable_id,offer,{1},stated,capacity);
        };
        check(prepare(saved.save.state,expensive).error==grandleon::campaign_runtime::ShopError::insufficient_gold,
              "an unaffordable price cannot borrow or underflow gold");
        auto cheap=expensive;cheap.price=1;
        check(prepare(saved.save.state,cheap,true,0).error==grandleon::campaign_runtime::ShopError::inventory_full,
              "explicit zero capacity cannot accept a purchase");
        auto deceased=saved.save.state;
        for(auto& unit:deceased.units)if(unit.id==ca::PersistentEntityId{1})unit.availability=ca::Availability::dead;
        check(prepare(deceased,cheap).error==grandleon::campaign_runtime::ShopError::invalid_member,
              "dead characters cannot receive purchased equipment");
        auto unlimited=saved.save.state;
        for(auto& unit:unlimited.units)if(unit.id==ca::PersistentEntityId{1})
            for(auto& stack:unit.carried)if(stack.item==focus)stack.quantity=65535U;
        check(prepare(unlimited,cheap).error==grandleon::campaign_runtime::ShopError::inventory_full,
              "omitted capacity cannot overflow a battle stack into an unplayable kit");
        auto next=resumed.prepare_board();check(next.error==cl::CampaignSessionError::none,"purchased inventory starts next battle");
        const auto hero=next.encounter.binding.battle_of({1}).value;
        for(const auto& unit:next.encounter.encounter.definition.units)if(unit.id==hero)
            check(std::find(unit.weapon_ids.begin(),unit.weapon_ids.end(),focus.stable_id)==unit.weapon_ids.end(),"possession does not grant incompatible affinity");
        check(resumed.buy_shop_offer({1},focus_id).error==cl::ManagementError::not_managing,"no shopping once battle is prepared");
    }
    return failures?1:0;
}
