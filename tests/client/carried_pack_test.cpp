// SPDX-License-Identifier: MIT
#include <grandleon/client/turn_client.hpp>
#include <grandleon/simulation/carried_inventory.hpp>
#include <iostream>
#include <vector>
#include <stdexcept>
namespace client=grandleon::client;
namespace turn=grandleon::client::turn;
namespace sim=grandleon::simulation;
namespace core=grandleon::core;
int failures{};
void check(bool ok,const char* text){if(!ok){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
struct Silence:turn::ReportSink{void line(const char*)override{}};
struct Step{std::uint8_t player{};std::uint16_t buttons{};turn::PackAction action{};sim::ContentId content{};bool move{};};
class Pilot final:public turn::TurnClient{
public:
 explicit Pilot(Silence& sink):TurnClient(sink){}
 std::uint16_t next_press() override { return turn::pad_end_of_script; }
 bool checked_exchange_default{}, checked_drop_default{};
 std::vector<Step> script;std::size_t at{};turn::Overlay shown{};
 std::vector<turn::MenuChoice> rows;
 void paint(const sim::EncounterSnapshot&,const turn::Overlay& o)override{
  check(o.menu_rows<=turn::menu_capacity,"pack lists fit the console menu capacity");
  shown=o;rows.clear();if(o.menu)rows.assign(o.menu,o.menu+o.menu_rows);
  for(const auto& row:rows){
   if(row.pack==turn::PackAction::exchange&&!checked_exchange_default){check(rows[o.menu_row].pack==turn::PackAction::back,"full pack defaults to Leave");checked_exchange_default=true;}
   if(row.pack==turn::PackAction::drop&&!checked_drop_default){check(rows[o.menu_row].pack==turn::PackAction::back,"drop confirmation defaults to Back");checked_drop_default=true;}
  }
 }
 turn::ControllerPress next_controller_press()override{
  if(at==script.size())return {0,turn::pad_end_of_script};
  const auto step=script[at];
  if(shown.active_player!=step.player)return {step.player,0};
  if(step.buttons){++at;return {step.player,step.buttons};}
  for(std::size_t i=0;i<rows.size();++i){const auto& row=rows[i];
   if((step.move?row.move:row.pack==step.action)&&(!step.content||row.content==step.content)){
    if(shown.menu_row!=static_cast<int>(i))return {step.player,turn::pad_down};
    ++at;return {step.player,turn::pad_a};
   }
  }
  throw std::runtime_error("Expected real menu action is absent");
 }
 void press(std::uint16_t buttons,std::uint8_t owner=0){script.push_back({owner,buttons,{},{},false});}
 void choose(turn::PackAction action,sim::ContentId content=0,std::uint8_t owner=0){script.push_back({owner,0,action,content,false});}
 void move(std::uint8_t owner=0){script.push_back({owner,0,{},{},true});}
 client::Intent intent(sim::Encounter& battle){client::Roster roster;roster.rebuild(battle.snapshot());draw(battle.snapshot(),roster);return next_intent(battle.snapshot(),roster);}
 void begin(sim::Encounter& battle){client::Roster roster;roster.rebuild(battle.snapshot());battle_begins(battle.snapshot(),roster,sim::Side::first,{});battle_definitions(battle.weapons(),battle.abilities(),battle.items(),battle.objectives());set_viewport(10,8);}
};
sim::EncounterDefinition fixture(bool carried=true){
 sim::EncounterDefinition d;d.width=4;d.height=4;d.terrain.assign(16,sim::Terrain::open);d.turn_order=sim::TurnOrder::side_blocks;
 d.inventory_mode=carried?sim::InventoryMode::carried:sim::InventoryMode::legacy;
 sim::WeaponDefinition sword;sword.id=10;sword.weapon_type=100;sword.power=1;
 auto bow=sword;bow.id=20;bow.weapon_type=200;d.weapons={sword,bow};
 sim::WeaponTypeDefinition blades;blades.id=100;auto bows=blades;bows.id=200;d.weapon_types={blades,bows};
 sim::ItemDefinition potion;potion.id=30;d.items={potion};
 sim::UnitDefinition hero;hero.id=1;hero.unit_type_id=101;hero.health=20;hero.strength=30;hero.movement=3;hero.action_points=1;hero.weapon_ids={10};hero.item_ids={30};hero.item_counts={1};
 hero.states_weapon_affinities=true;hero.weapon_affinities={100};
 if(carried){hero.states_carry_capacity=true;hero.carry_capacity=3;hero.carried_inventory={{core::ContentCategory::weapon,10,2},{core::ContentCategory::item,30,1}};}
 auto ally=hero;ally.id=2;ally.position={2,0};ally.local_player=2;ally.weapon_ids.clear();ally.item_ids.clear();ally.item_counts.clear();ally.carried_inventory.clear();ally.weapon_affinities={200};
 auto enemy=hero;enemy.id=3;enemy.side=sim::Side::second;enemy.position={0,1};enemy.health=1;enemy.weapon_ids={20};enemy.weapon_affinities={200};enemy.item_ids.clear();enemy.item_counts.clear();enemy.carried_inventory.clear();
 if(carried){enemy.carried_inventory={{core::ContentCategory::weapon,20,2}};enemy.drop_inventory_on_defeat=true;enemy.drop_item_id=30;enemy.drop_chance=100;}
 d.units={hero,ally,enemy};return d;
}
const sim::UnitSnapshot& unit(const sim::EncounterSnapshot& s,unsigned id){for(const auto& u:s.units)if(u.id==id)return u;throw std::runtime_error("Missing unit");}
void win(sim::Encounter& battle){sim::Command attack;attack.type=sim::CommandType::attack;attack.unit_id=1;attack.target_id=3;check(bool(battle.apply(attack)),"actual attack wins encounter");check(battle.snapshot().collecting_loot,"final enemy equipment opens real collection phase");}
sim::Command command(const client::Intent& i){sim::Command c;c.unit_id=i.unit_id;c.destination=i.destination;c.ground_item_id=i.ground_item_id;c.carried_category=i.carried_category;c.carried_content_id=i.carried_content_id;
 switch(i.kind){case client::IntentKind::move_to:c.type=sim::CommandType::move;break;case client::IntentKind::pick_up:c.type=sim::CommandType::pick_up;break;case client::IntentKind::drop_carried:c.type=sim::CommandType::drop_carried;break;case client::IntentKind::exchange_carried:c.type=sim::CommandType::exchange_carried;break;case client::IntentKind::finish_collection:c.type=sim::CommandType::finish_collection;break;default:throw std::runtime_error("Unexpected UI intent");}return c;}
void collected_equipment_uses_real_commands(){
 auto made=sim::create_encounter(fixture());check(bool(made),"carried fixture creates");if(!made)return;auto& battle=made.encounter;win(battle);
 Silence sink;Pilot p(sink);p.begin(battle);p.press(turn::pad_a);p.move();p.press(turn::pad_down);p.press(turn::pad_a);
 auto i=p.intent(battle);check(i.kind==client::IntentKind::move_to&&i.unit_id==1,"spent victor remains selectable and walks during collection");check(bool(battle.apply(command(i))),"collection movement obeys actual terrain and occupancy");
 const auto before=battle.snapshot();auto pile=before.ground_items.front().id;
 for(const auto& g:before.ground_items)if(g.stack.category==core::ContentCategory::weapon)pile=g.id;
 p.choose(turn::PackAction::open);p.choose(turn::PackAction::ground);p.choose(turn::PackAction::loot,pile);p.choose(turn::PackAction::back);p.choose(turn::PackAction::loot,pile);p.choose(turn::PackAction::exchange);p.choose(turn::PackAction::inspect,10);
 i=p.intent(battle);check(i.kind==client::IntentKind::exchange_carried&&i.carried_category==core::ContentCategory::weapon&&i.carried_content_id==10&&i.ground_item_id==pile,"full combined bag offers Leave/Exchange and chooses one typed copy");
 check(bool(battle.apply(command(i))),"UI exchange applies through real simulation");
 auto state=battle.snapshot();check(sim::carried_count(unit(state,1).carried_inventory)==3,"exchange preserves capacity counted per copy");check(unit(state,1).weapon_ids==std::vector<sim::ContentId>{10},"incompatible bow is owned without becoming usable");
 bool old=false;for(const auto& g:state.ground_items)if(g.stack.content_id==10){old=g.position==sim::Position{0,1}&&g.stack.quantity==1;}
 check(old,"discarded older copy remains at collector's position");
 for(const auto& g:state.ground_items)if(g.id==pile)check(g.stack.quantity==1,"pickup exchanged exactly one of the two ground copies");
 p.choose(turn::PackAction::open);p.choose(turn::PackAction::inventory);p.choose(turn::PackAction::inspect,20);p.choose(turn::PackAction::drop);
 i=p.intent(battle);check(i.kind==client::IntentKind::drop_carried&&i.carried_content_id==20,"personal pack can drop incompatible owned equipment");check(bool(battle.apply(command(i))),"drop is an ordinary authoritative command");
 p.choose(turn::PackAction::open);p.choose(turn::PackAction::ground);p.choose(turn::PackAction::loot,pile);p.choose(turn::PackAction::pickup);
 i=p.intent(battle);check(i.kind==client::IntentKind::pick_up,"free slot offers ordinary pickup");check(bool(battle.apply(command(i))),"ordinary pickup succeeds");
 state=battle.snapshot();check(state.collecting_loot&&state.outcome==sim::Outcome::first_side_won,"UI transactions preserve victorious collection");check(state.round==before.round&&unit(state,1).action_points==unit(before,1).action_points,"collection does not consume turns or AP");
 Pilot finish(sink);finish.begin(battle);finish.press(turn::pad_start);finish.choose(turn::PackAction::finish);
 i=finish.intent(battle);check(i.kind==client::IntentKind::finish_collection&&i.unit_id==0,"Continue explicitly finishes collection without an actor");check(bool(battle.apply(command(i)))&&!battle.snapshot().collecting_loot,"Continue completes actual collection phase");
}
void ownership_and_legacy(){
 auto made=sim::create_encounter(fixture());if(!made){check(false,"ownership fixture creates");return;}auto& battle=made.encounter;win(battle);Silence sink;Pilot p(sink);p.begin(battle);
 p.press(turn::pad_a);p.choose(turn::PackAction::open);
 p.press(turn::pad_a,1);p.choose(turn::PackAction::open,0,1);p.choose(turn::PackAction::inventory,0,1);
 p.choose(turn::PackAction::inventory);p.choose(turn::PackAction::inspect,10);p.choose(turn::PackAction::drop);
 auto i=p.intent(battle);check(i.kind==client::IntentKind::drop_carried&&i.unit_id==1,"controller switching restores the original owner's pack and typed selection");check(bool(battle.apply(command(i))),"owner's drop applies");check(unit(battle.snapshot(),2).carried_inventory.empty(),"other controller's pack remains unchanged");
 auto legacy=sim::create_encounter(fixture(false));check(bool(legacy),"legacy fixture creates");if(!legacy)return;Pilot old(sink);old.begin(legacy.encounter);old.press(turn::pad_a);auto q=old.intent(legacy.encounter);check(q.kind==client::IntentKind::quit,"legacy input loop remains live");for(const auto& row:old.rows)check(row.pack==turn::PackAction::none,"legacy unit menus expose no personal pack affordance");
}

void paging_and_stale_piles(){
 auto d=fixture();d.units[0].carry_capacity=16;
 for(unsigned id=40;id<48;++id){sim::ItemDefinition item;item.id=id;d.items.push_back(item);d.units[0].carried_inventory.push_back({core::ContentCategory::item,id,1});}
 auto made=sim::create_encounter(d);check(bool(made),"long physical pack creates");if(!made)return;
 Silence sink;Pilot p(sink);p.begin(made.encounter);p.press(turn::pad_a);p.choose(turn::PackAction::open);p.choose(turn::PackAction::inventory);p.choose(turn::PackAction::next);p.choose(turn::PackAction::inspect,47);p.choose(turn::PackAction::drop);
 auto intent=p.intent(made.encounter);check(intent.kind==client::IntentKind::drop_carried&&intent.carried_content_id==47,"paging reaches equipment beyond the first six stack rows");check(bool(made.encounter.apply(command(intent))),"later-page drop applies to the selected copy");
 auto loot=sim::create_encounter(fixture());if(!loot){check(false,"stale pile fixture creates");return;}win(loot.encounter);
 sim::Command move;move.type=sim::CommandType::move;move.unit_id=1;move.destination={0,1};check(bool(loot.encounter.apply(move)),"stale fixture walks onto actual loot");
 std::uint64_t identity{};for(const auto& g:loot.encounter.snapshot().ground_items)if(g.stack.category==core::ContentCategory::item)identity=g.id;
 Pilot stale(sink);stale.begin(loot.encounter);stale.press(turn::pad_a);stale.choose(turn::PackAction::open);stale.choose(turn::PackAction::ground);stale.choose(turn::PackAction::loot,identity);
 check(stale.intent(loot.encounter).kind==client::IntentKind::quit,"script can stop while full-pack choice remains open");
 sim::Command exchange;exchange.type=sim::CommandType::exchange_carried;exchange.unit_id=1;exchange.ground_item_id=identity;exchange.carried_category=core::ContentCategory::weapon;exchange.carried_content_id=10;
 check(bool(loot.encounter.apply(exchange)),"intervening authoritative exchange retires the viewed pile");
 const auto hash=loot.encounter.canonical_hash();stale.press(turn::pad_a);check(stale.intent(loot.encounter).kind==client::IntentKind::quit,"stale choice becomes Leave instead of picking a replacement pile");check(hash==loot.encounter.canonical_hash(),"stale UI navigation cannot mutate equipment");
}
void postwalk_pack_stays_available(){
 auto d=fixture();d.units[0].action_points=2;d.units[2].position={3,3};
 auto made=sim::create_encounter(d);check(bool(made),"postwalk equipment fixture creates");if(!made)return;
 Silence sink;Pilot p(sink);p.begin(made.encounter);p.press(turn::pad_a);p.move();p.press(turn::pad_down);p.press(turn::pad_a);
 auto intent=p.intent(made.encounter);check(intent.kind==client::IntentKind::move_to,"ordinary battle issues real movement");check(bool(made.encounter.apply(command(intent))),"ordinary movement applies");
 check(!unit(made.encounter.snapshot(),1).has_acted,"remaining AP leaves equipment actions usable");
 p.choose(turn::PackAction::open);p.choose(turn::PackAction::inventory);p.choose(turn::PackAction::inspect,10);p.choose(turn::PackAction::drop);
 intent=p.intent(made.encounter);check(intent.kind==client::IntentKind::drop_carried,"postwalk Pack is not swallowed by combat-only auto-wait");check(bool(made.encounter.apply(command(intent))),"postwalk owned-equipment command applies normally");
}

void unowned_selection_is_refused(){
 auto made=sim::create_encounter(fixture());if(!made)return;win(made.encounter);Silence sink;Pilot p(sink);p.begin(made.encounter);
 p.press(turn::pad_a,1);p.press(turn::pad_b,1);p.press(turn::pad_b,1);p.press(turn::pad_left,1);p.press(turn::pad_left,1);p.press(turn::pad_a,1);
 const auto hash=made.encounter.canonical_hash();check(p.intent(made.encounter).kind==client::IntentKind::quit,"other controller's unit cannot produce an equipment command");check(p.shown.selected==0,"unowned character stays unselected in collection");check(hash==made.encounter.canonical_hash(),"unowned selection leaves both packs unchanged");
}

int main(){try{collected_equipment_uses_real_commands();ownership_and_legacy();paging_and_stale_piles();postwalk_pack_stays_available();unowned_selection_is_refused();}catch(const std::exception& e){check(false,e.what());}return failures?1:0;}
