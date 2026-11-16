// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/campaign_runtime/character_rules.hpp>
#include <grandleon/storage/memory_storage.hpp>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iterator>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace cr = grandleon::campaign_runtime;
namespace camp = grandleon::campaign;
namespace client = grandleon::client;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace core = grandleon::core;
int failures = 0;
void check(bool yes, const char* message) { if (!yes) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

gc::GameSource source() {
    gc::GameSource s;
    s.game_id[0] = 47;
    s.title = "Personal characters";
    s.required_engine = {{0,1,0},{0,1,99}};
    s.weapon_types = {{10,"Sword",{11}}, {11,"Focus"}, {12,"Bow"}};
    s.classes = {{30,"Knight",{8,2,20,0,3},{10}}, {31,"Mage",{8,2,20,0,3},{11}}};
    for (auto& c : s.classes) c.states_allowed_weapon_types = true;
    s.weapons = {{40,"Sword",10,2,1,1}, {41,"Focus",11,2,1,1}, {42,"Loot sword",10,5,1,2}, {43,"Bow",12,5,2,3}};
    s.unit_types = {{60,"Scholar",31,80,{41},{}}, {61,"Enemy",30,81,{40},{}}};
    s.maps = {{70,"Field",4,3,std::vector<std::uint64_t>(12,1)}};
    s.factions = {{80,"Allies"},{81,"Enemies"}};
    s.objectives = {{90,"Win",gc::ObjectiveKind::defeat_all_opponents}};
    s.encounters = {{100,"First",70,{90},{{1000,201,201,60,gc::EncounterSide::first,0,0},{1001,202,202,60,gc::EncounterSide::first,0,1},{1002,301,0,61,gc::EncounterSide::second,3,0}}}};
    s.campaigns = {{110,"Campaign",111,{{111,gc::CampaignNodeKind::encounter,100,{}, {113}, {{112,1,gc::ConditionCombinator::all,{{gc::CampaignPredicateKind::objective_result,90,gc::ObjectiveOutcome::satisfied}}}}},{112,gc::CampaignNodeKind::terminal},{113,gc::CampaignNodeKind::terminal}},{{201,"Lia",60,0},{202,"Mira",60,0}}}};
    auto& hero=s.campaigns[0].roster[0];
    hero.weapon_affinities={10,11}; hero.states_weapon_affinities=true;
    hero.starting_weapons={40}; hero.states_starting_weapons=true;
    hero.defeat_policy=gc::DefeatPolicy::withdraw;
    s.campaigns[0].grants={{0,1,0,42},{0,1,0,43}};
    return s;
}
pf::LoadedPackage package(const gc::GameSource& s) {
    auto compiled=gc::compile(s);
    check(bool(compiled),"source compiles");
    for(const auto& d:compiled.diagnostics) std::cerr<<gc::diagnostic_name(d.code)<<" "<<d.path<<'\n';
    if(!compiled)return {};
    auto loaded=pf::load_mock_package(compiled.package,{{0,1,0},pf::TargetProfile::desktop,0,64,10000});
    check(bool(loaded),"package loads"); return loaded.package;
}
void begin(client::CampaignSession& session, bool expected_resume=false) {
    client::SlotFailure failure;bool refused=false,resumed=false;
    check(session.begin(failure,refused,resumed)==client::CampaignSessionError::none,"session begins");
    check(!refused && resumed==expected_resume,"save resumes without refusal");
}
const sim::UnitDefinition* hero(const client::CampaignSession::PreparedBoard& board) {
    const auto id=board.encounter.binding.battle_of(camp::PersistentEntityId{1}).value;
    for(const auto& unit:board.encounter.encounter.definition.units)if(unit.id==id)return &unit;
    return nullptr;
}
void equipment_and_resume() {
    auto p=package(source());storage::MemorySlotStorage device;client::PackageBoards boards{p};
    client::CampaignSessionOptions options;options.slot="personal";
    client::CampaignSession session{p,110,boards,device,options};begin(session);
    auto board=session.prepare_board();check(board.error==client::CampaignSessionError::none,"personal board prepares");
    check(hero(board)&&hero(board)->weapon_ids==std::vector<sim::ContentId>{40},"Mage role keeps personal sword kit");
    check(bool(sim::create_encounter(board.encounter.encounter.definition)),"personal weapon definitions resolve into a playable board");
    const camp::DefinitionRef loot{p.game_id,core::ContentCategory::weapon,42}, bow{p.game_id,core::ContentCategory::weapon,43}, sword{p.game_id,core::ContentCategory::weapon,40};
    check(session.give_item({1},bow).error==client::ManagementError::disallowed_weapon,"unsupported family refuses usable gift");
    check(session.store().size()==2,"rejected gift leaves unrestricted shared store intact");
    check(bool(session.give_item({1},loot)),"personal sword affinity accepts sword outside class");
    board=session.prepare_board();
    check(hero(board)&&hero(board)->weapon_ids==std::vector<sim::ContentId>({40,42}),"new loot enters usable loadout");
    check(bool(sim::create_encounter(board.encounter.encounter.definition)),"inventory-only weapon and advantage definitions resolve lazily");
    check(bool(session.take_item({1},sword)),"personal starting weapon returns to shared store");
    board=session.prepare_board();check(hero(board)&&hero(board)->weapon_ids==std::vector<sim::ContentId>{42},"removed personal kit does not regenerate");
    options.resume=true;client::CampaignSession resumed{p,110,boards,device,options};begin(resumed,true);
    board=resumed.prepare_board();check(hero(board)&&hero(board)->weapon_ids==std::vector<sim::ContentId>{42},"saved personal inventory reopens unchanged");
    check(bool(resumed.give_item({2},bow)),"absent affinities preserve legacy runtime permission");
    board=resumed.prepare_board();check(bool(sim::create_encounter(board.encounter.encounter.definition)),"legacy inventory weapon also resolves if absent from initial map kits");
    auto empty=source();empty.campaigns[0].roster[0].starting_weapons.clear();empty.campaigns[0].roster[0].weapon_affinities.clear();
    auto ep=package(empty);client::PackageBoards eb{ep};storage::MemorySlotStorage ed;
    options.resume=false;client::CampaignSession es{ep,110,eb,ed,options};begin(es);
    auto unarmed=es.prepare_board();check(hero(unarmed)&&hero(unarmed)->weapon_ids.empty()&&hero(unarmed)->power==0,"explicit empty personal kit is unarmed without role weapon power");
}
void compiler_and_decoder() {
    auto s=source();auto p=package(s);auto read=pr::load_campaign(p,110);
    check(bool(read)&&read.definition.members[0].rules.weapon_affinities==std::vector<std::uint64_t>({10,11}),"multiple affinities round-trip");
    check(read.definition.members[0].rules.starting_weapons==std::vector<std::uint64_t>{40},"personal kit round-trips");
    const auto* section=p.find(pf::SectionType::member_rules);
    check(section&&(section->flags&pf::section_flag_required)!=0,"rule-bearing section is required");
    auto legacy=s;auto& m=legacy.campaigns[0].roster[0];m.weapon_affinities.clear();m.states_weapon_affinities=false;m.starting_weapons.clear();m.states_starting_weapons=false;m.defeat_policy=gc::DefeatPolicy::inherit;
    const auto before=gc::compile(legacy).package;auto old=package(legacy);check(old.find(pf::SectionType::member_rules)==nullptr,"absent rules emit no extension");
    check(before==gc::compile(legacy).package,"legacy package deterministic byte identity");
    auto invalid=s;invalid.campaigns[0].roster[0].weapon_affinities={11};check(!gc::compile(invalid),"personal kit must match personal permission");
    invalid=s;invalid.unit_types[1].starting_weapons={41};check(!gc::compile(invalid),"personal override never bypasses enemy class validation");
    invalid=s;invalid.campaigns[0].roster[0].defeat_policy=static_cast<gc::DefeatPolicy>(255);check(!gc::compile(invalid),"programmatic invalid policy rejected");
    invalid=s;invalid.campaigns[0].roster[0].weapon_affinities={10,10};check(!gc::compile(invalid),"duplicate affinities rejected");
    invalid=s;invalid.campaigns[0].roster[0].starting_weapons={999};check(!gc::compile(invalid),"unknown personal weapon rejected");
    const auto* record=p.find(pf::SectionType::member_rules,110);if(!record)return;
    for (const std::uint32_t flags : {0U, 2U, 3U}) {
        auto bad=p;
        for (auto& entry:bad.sections) if(entry.type==pf::SectionType::member_rules)entry.flags=flags;
        check(!pr::load_campaign(bad,110),"personal rule section must be required without unknown flags");
    }
    {auto bad=p;for(auto& entry:bad.sections)if(entry.type==pf::SectionType::member_rules)entry.records[0].stable_id=999;
        check(!pr::load_campaign(bad,110),"orphan personal rule records cannot be silently ignored");}
    const auto offset=record->payload_offset;
    for(const std::uint8_t flags:{std::uint8_t{0},std::uint8_t{8},std::uint8_t{255}}){auto bad=p;bad.bytes[offset+10]=flags;check(!pr::load_campaign(bad,110),"unknown or empty flags rejected");}
    {auto bad=p;bad.bytes[offset]=255;bad.bytes[offset+1]=255;check(!pr::load_campaign(bad,110),"hostile row count bounded");}
    {auto bad=p;bad.bytes[offset+11]=255;bad.bytes[offset+12]=255;check(!pr::load_campaign(bad,110),"hostile affinity count bounded");}
    {auto bad=p;bad.bytes[offset+2]=0;bad.bytes[offset+3]=0;check(!pr::load_campaign(bad,110),"unknown member rejected");}
    {auto bad=p;bad.bytes[offset+13]=99;check(!pr::load_campaign(bad,110),"unknown affinity reference rejected");}
    {auto bad=p;bad.bytes[offset+21]=10;check(!pr::load_campaign(bad,110),"duplicate affinity payload rejected");}
    {auto bad=p;bad.bytes[offset+record->payload_size-1]=9;check(!pr::load_campaign(bad,110),"invalid serialized defeat enum rejected");}
}
void defeat_commands_and_outcomes() {
    for(const auto policy:{gc::DefeatPolicy::permanent,gc::DefeatPolicy::withdraw,gc::DefeatPolicy::lose_battle}) {
        auto s=source();s.campaigns[0].roster[0].defeat_policy=policy;
        auto p=package(s);storage::MemorySlotStorage device;client::PackageBoards boards{p};client::CampaignSessionOptions options;options.slot="loss";
        client::CampaignSession session{p,110,boards,device,options};begin(session);
        auto board=session.prepare_board();auto definition=board.encounter.encounter.definition;
        auto id=board.encounter.binding.battle_of({1}).value;
        for(auto& unit:definition.units){if(unit.id==id){unit.health=1;unit.position={2,0};}}
        // Enemy attack is legal after the first side waits. A second ally lives.
        auto created=sim::create_encounter(definition);check(bool(created),"loss fixture starts");if(!created)continue;
        client::BattleReport report;
        auto waiting=created.encounter.apply({sim::CommandType::wait,id,{},0});check(bool(waiting),"hero yields a real turn");
        sim::UnitId enemy=0;for(const auto& u:definition.units)if(u.side==sim::Side::second)enemy=u.id;
        auto attack=created.encounter.apply({sim::CommandType::attack,enemy,{},id});check(bool(attack),"enemy actually defeats named hero");
        report.events=attack.events;report.final_snapshot=created.encounter.snapshot();report.objectives=report.final_snapshot.objectives;report.canonical_hash=created.encounter.canonical_hash();report.outcome=report.final_snapshot.outcome;
        if(policy==gc::DefeatPolicy::lose_battle){
            check(report.outcome==sim::Outcome::second_side_won,"required hero loses while another ally survives");
            check(report.objectives[0].state==sim::ObjectiveState::failed,"authored goal fails for authored defeat route");
        }else{
            check(report.outcome==sim::Outcome::ongoing,"ordinary casualty does not end a live battle");
            // Commit a finished report with the same real casualty to inspect
            // persistent consequences independently of the remaining fight.
            report.outcome=sim::Outcome::first_side_won;
            report.objectives[0].state=sim::ObjectiveState::satisfied;
        }
        client::BattleAftermath aftermath;check(session.commit_battle(report,aftermath)==client::CampaignSessionError::none,"casualty commits through shared campaign path");
        const bool permanent=policy==gc::DefeatPolicy::permanent;
        check(session.roster()[0].availability==(permanent?camp::Availability::dead:camp::Availability::available),"per-member permanent versus withdrawal retained");
        check(aftermath.completion.target.stable_id==(policy==gc::DefeatPolicy::lose_battle?113:112),"defeat follows existing authored branch");
        check(session.save()==storage::StorageError::none,"casualty saves without new save format");
        options.resume=true;client::CampaignSession resumed{p,110,boards,device,options};begin(resumed,true);
        check(resumed.roster()[0].availability==session.roster()[0].availability,"casualty policy survives reopen");
    }
    sim::EncounterDefinition def;def.width=3;def.height=1;
    def.units={{1,60,sim::Side::first,{0,0},1,1},{2,61,sim::Side::second,{1,0},20,20},{3,60,sim::Side::first,{2,0},20,1}};
    def.units[0].defeat_ends_battle=true;
    def.objectives={{90,sim::ObjectiveKind::defeat_target,sim::Side::first,1}};
    auto battle=sim::create_encounter(def);check(bool(battle),"conflicting objective fixture is valid");
    if(battle){check(bool(battle.encounter.apply({sim::CommandType::wait,1,{},0})),"conflict fixture yields turn");check(bool(battle.encounter.apply({sim::CommandType::attack,2,{},1})),"conflict attack accepted");check(battle.encounter.snapshot().outcome==sim::Outcome::second_side_won,"required loss takes precedence over an objective satisfied by same defeat");}
}
int main(){compiler_and_decoder();equipment_and_resume();defeat_commands_and_outcomes();return failures?1:0;}
