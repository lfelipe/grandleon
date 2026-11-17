// SPDX-License-Identifier: MIT
#include <grandleon/client/turn_client.hpp>
#include <grandleon/client/shop_controller.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/storage/memory_storage.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace client=grandleon::client;
namespace turn=client::turn;
namespace campaign=grandleon::campaign;
namespace core=grandleon::core;
namespace sim=grandleon::simulation;
int failures{};
void check(bool ok,const char* text){if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
struct Sink:turn::ReportSink{void line(const char*)override{}};
struct Pilot:turn::TurnClient{
 explicit Pilot(Sink& s):TurnClient(s){}
 std::vector<turn::ControllerPress> keys;std::size_t at{};bool saw_both{},saw_back{},saw_error{},saw_ready{};
 void paint(const sim::EncounterSnapshot&,const turn::Overlay&)override{}
 std::uint16_t next_press()override{return turn::pad_end_of_script;}
 turn::ControllerPress next_controller_press()override{return at<keys.size()?keys[at++]:turn::ControllerPress{0,turn::pad_end_of_script};}
 void paint_screen(const turn::ScreenView& v)override{
  std::string all;for(int i=0;i<v.page->count;++i){all+=v.page->line(i);all+='\n';}
  saw_both|=all.find("P1 ROWAN")!=std::string::npos&&all.find("P2 AKARI")!=std::string::npos;
  saw_back|=all.find("[BACK]")!=std::string::npos;saw_ready|=all.find("READY  START EDIT")!=std::string::npos;
  saw_error|=all.find("NOT ENOUGH GOLD")!=std::string::npos;
  check(all.find("DEAD HERO")==std::string::npos,"dead heroes never offered");
 }
 void press(unsigned p,std::uint16_t b){keys.push_back({static_cast<std::uint8_t>(p),b});}
};
client::CompanyManagement fixture(){
 client::CompanyManagement c;c.node.stable_id=42;c.local_players=2;c.gold=8;c.inventory_mode=sim::InventoryMode::carried;
 c.shop_offers={{{9,core::ContentCategory::weapon,100,5,1},1,true},{{10,core::ContentCategory::item,30,6,0},0,false}};
 for(unsigned i=1;i<=4;++i){client::RosterEntry h;h.member.value=i;h.name=i==1?"Rowan":i==2?"Akari":i==3?"Reserve":"Dead Hero";h.local_player=i==2?2:1;h.availability=i==4?campaign::Availability::dead:campaign::Availability::available;h.states_carry_capacity=true;h.carry_capacity=3;h.carried={{{{},core::ContentCategory::weapon,88},2}};c.roster.push_back(h);}
 c.fielded={{1},{2}};c.placeable=c.fielded;return c;
}
void independent_ui(){
 auto c=fixture();client::shop::Controller ui;ui.reset(c);
 check(ui.state(0).member.value==1&&ui.state(1).member.value==2,"both seats begin with their own heroes");
 ui.cycle_member(c,0,1);ui.cycle_member(c,1,1);
 check(ui.state(0).member.value==3&&ui.state(1).member.value==2,"recipient cycling never crosses armies");
 ui.move_offer(c,0,1);check(ui.state(0).offer==10&&ui.state(1).offer==9,"catalog cursors independent");
 check(ui.confirm(c,1).verb==client::ManagementVerb::none&&!ui.state(1).buy_selected,"confirmation defaults to Back");
 check(ui.confirm(c,1).verb==client::ManagementVerb::none,"default confirmation cannot spend");
 ui.toggle_ready(c,0);check(!ui.all_ready(),"one ready seat cannot begin battle");
 (void)ui.confirm(c,1);ui.cycle_member(c,1,-1);auto buy=ui.confirm(c,1);
 check(buy.verb==client::ManagementVerb::buy&&buy.member.value==2&&buy.shop_player==2,"P2 independently emits owned purchase");
 client::ManagementCommit failed;failed.error=client::ManagementError::sold_out;ui.committed(c,1,failed);
 check(ui.state(0).ready&&ui.state(0).offer==10&&ui.state(1).error==client::ManagementError::sold_out,"other seat selection/readiness survives refusal");
 ui.toggle_ready(c,1);check(ui.all_ready()&&ui.state(1).error==client::ManagementError::none,"both readiness states permit battle and clear stale refusal");ui.toggle_ready(c,0);check(!ui.all_ready(),"Ready can be withdrawn");
 auto empty=c;empty.roster.erase(empty.roster.begin()+1);ui.reset(empty);check(ui.state(1).ready&&!ui.state(0).ready,"seat without owned eligible heroes auto-readies");
 Sink sink;Pilot p(sink);p.press(1,turn::pad_a);p.press(0,turn::pad_down);p.press(1,turn::pad_left);p.press(1,turn::pad_a);
 const auto intent=p.next_management_intent(c);
 check(intent.verb==client::ManagementVerb::buy&&intent.shop_player==2&&intent.member.value==2&&intent.shop_offer==9,"real TurnClient accepts interleaved P1/P2 shop input");
 check(p.saw_both&&p.saw_back,"both player cards and safe confirmation visible together");
 client::ManagementCommit refused;refused.error=client::ManagementError::insufficient_gold;p.management_committed(refused);
 p.press(0,turn::pad_start);p.press(1,turn::pad_start);
 check(p.next_management_intent(c).verb==client::ManagementVerb::proceed&&p.saw_error&&p.saw_ready,"both ready after visible buyer-specific error proceeds");
 Pilot held(sink);held.press(0,turn::pad_start);check(held.next_management_intent(c).verb==client::ManagementVerb::quit,"one ready controller alone cannot proceed");
 auto many=c;for(unsigned id=11;id<20;++id){auto offer=c.shop_offers.front();offer.offer.id=id;many.shop_offers.push_back(offer);}ui.reset(many);for(unsigned i=0;i<10;++i)ui.move_offer(many,1,1);check(ui.state(1).offer==19&&ui.state(0).offer==9,"long catalog browsing preserves each cursor");
}
void real_session_contention(){
 namespace gc=grandleon::game_content;namespace pf=grandleon::package_format;namespace st=grandleon::storage;
 std::ifstream file(std::string(GRANDLEON_SOURCE_DIR)+"/tests/fixtures/campaign_projects/shop_campaign.json");std::string text{std::istreambuf_iterator<char>(file),{}};
 auto source=gc::parse_source_project_json(text);check(bool(source),"real shop source parses");if(!source)return;
 auto compiled=gc::compile(source.source);check(bool(compiled),"real shop source compiles");if(!compiled)return;
 auto loaded=pf::load_mock_package(compiled.package,{{0,1,0},pf::TargetProfile::desktop,0,256,100000});check(bool(loaded),"real package loads");if(!loaded)return;
 st::MemorySlotStorage disk;client::PackageBoards boards(loaded.package,2);client::CampaignSessionOptions options;options.slot="coop-shop-ui";options.local_players=2;
 client::CampaignSession session(loaded.package,source.source.campaigns.front().id,boards,disk,options);client::SlotFailure failure;bool refused{},resumed{};
 check(session.begin(failure,refused,resumed)==client::CampaignSessionError::none,"real co-op session starts");
 auto prepared=session.prepare_board();check(prepared.error==client::CampaignSessionError::none,"normal first board prepares");auto battle=sim::create_encounter(prepared.encounter.encounter.definition);if(!battle){check(false,"battle creates");return;}
 sim::UnitId target{};for(const auto& u:battle.encounter.snapshot().units)if(u.side==sim::Side::second)target=u.id;
 sim::Command attack;attack.type=sim::CommandType::attack;attack.unit_id=prepared.encounter.binding.battle_of({1}).value;attack.target_id=target;
 const auto hit=battle.encounter.apply(attack);check(bool(hit),"ordinary first battle earns funds");
 const auto snapshot=battle.encounter.snapshot();client::BattleReport report;report.outcome=snapshot.outcome;report.canonical_hash=battle.encounter.canonical_hash();report.final_snapshot=snapshot;report.objectives=snapshot.objectives;report.events=hit.events;client::BattleAftermath aftermath;
 check(session.commit_battle(report,aftermath)==client::CampaignSessionError::none,"earned gold commits normally");
 auto c=session.management();client::shop::Controller ui;ui.reset(c);
 const auto earned_save=session.save_bytes();
 auto choose=[&](unsigned p){(void)ui.confirm(c,p);ui.cycle_member(c,p,-1);return ui.confirm(c,p);};
 auto first=choose(0);auto result=session.buy_shop_offer(first.member,first.shop_offer,first.shop_player);check(bool(result),"P1 buys first finite copy");c=session.management();ui.committed(c,0,result);
 const auto full_save=session.save_bytes();auto full=choose(0);result=session.buy_shop_offer(full.member,full.shop_offer,full.shop_player);check(result.error==client::ManagementError::inventory_full&&session.save_bytes()==full_save,"full own bag refuses before another player consumes last stock");ui.committed(c,0,result);
 // Both confirmations exist against the same one remaining copy. P2 wins;
 // P1's still-open confirmation must survive the refreshed company view.
 (void)ui.confirm(c,0);ui.cycle_member(c,0,-1);
 (void)ui.confirm(c,1);ui.cycle_member(c,1,-1);
 auto second=ui.confirm(c,1);result=session.buy_shop_offer(second.member,second.shop_offer,second.shop_player);check(bool(result),"P2 buys last finite copy");c=session.management();ui.committed(c,1,result);
 check(ui.state(0).confirming&&ui.state(0).buy_selected,"other pending confirmation survives shared-stock refresh");
 const auto saved=session.save_bytes();auto retry=ui.confirm(c,0);result=session.buy_shop_offer(retry.member,retry.shop_offer,retry.shop_player);check(result.error==client::ManagementError::sold_out&&session.save_bytes()==saved,"last-copy contention cannot overspend or duplicate finite stock");
 check(session.buy_shop_offer({1},first.shop_offer,2).error==client::ManagementError::not_owner&&session.save_bytes()==saved,"authoritative core refuses cross-army purchase");
 // Reload a real pre-purchase save, not manufactured gold, for the funds case.
 st::MemorySlotStorage funds_disk;check(funds_disk.write("funds",earned_save)==st::StorageError::none,"earned shop save copies normally");
 auto funds_options=options;funds_options.slot="funds";funds_options.resume=true;
 client::CampaignSession funds(loaded.package,source.source.campaigns.front().id,boards,funds_disk,funds_options);
 check(funds.begin(failure,refused,resumed)==client::CampaignSessionError::none&&resumed,"funds case resumes real earned save");
 const auto focus=core::stable_content_id_v1("shop_focus");
 check(bool(funds.buy_shop_offer({1},focus,1))&&bool(funds.buy_shop_offer({2},focus,2))&&bool(funds.buy_shop_offer({2},focus,2)),"both owners spend actual shared gold on unlimited stock");
 const auto low=funds.save_bytes();check(funds.management().gold==2,"three real purchases leave two shared gold");
 check(funds.buy_shop_offer({2},focus,2).error==client::ManagementError::insufficient_gold&&funds.save_bytes()==low,"insufficient shared funds cannot alter save or stock");

 check(c.gold==7,"shared gold charged exactly twice");
}
int main(){independent_ui();real_session_contention();return failures?1:0;}
