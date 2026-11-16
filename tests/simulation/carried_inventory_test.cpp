// SPDX-License-Identifier: MIT
#include <grandleon/simulation/carried_inventory.hpp>
#include <iostream>
#include <limits>
namespace sim=grandleon::simulation;
namespace core=grandleon::core;
int failures{};
void check(bool value,const char* message){if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}

sim::EncounterDefinition definition(bool full) {
 sim::EncounterDefinition d;
 d.width=4;d.height=4;d.turn_order=sim::TurnOrder::side_blocks;d.inventory_mode=sim::InventoryMode::carried;
 d.terrain.assign(16,sim::Terrain::open);
 sim::WeaponDefinition sword;sword.id=10;sword.power=1;sword.weapon_type=100;
 sim::WeaponDefinition bow;bow.id=20;bow.power=1;bow.weapon_type=200;
 d.weapons={sword,bow};sim::WeaponTypeDefinition blades;blades.id=100;sim::WeaponTypeDefinition bows;bows.id=200;d.weapon_types={blades,bows};
 sim::ItemDefinition tonic;tonic.id=30;d.items={tonic};
 sim::UnitDefinition first;first.id=1;first.unit_type_id=101;first.health=20;first.movement=2;first.action_points=3;
 first.weapon_ids={10};first.carried_inventory={{core::ContentCategory::weapon,10,1}};
 first.states_carry_capacity=true;first.carry_capacity=2;
 auto second=first;second.id=2;second.position={1,0};second.local_player=2;
 second.weapon_ids.clear();second.carried_inventory.clear();
 second.states_weapon_affinities=true;second.weapon_affinities={200};
 if(full){second.weapon_ids={20};second.item_ids={30};second.carried_inventory={{core::ContentCategory::weapon,20,1},{core::ContentCategory::item,30,1}};}
 auto enemy=first;enemy.id=3;enemy.side=sim::Side::second;enemy.position={3,3};
 d.units={first,second,enemy};return d;
}
void actual_commands(bool full){
 auto made=sim::create_encounter(definition(full));check(bool(made),"physical encounter creates");if(!made)return;
 auto& game=made.encounter;
 sim::Command drop;drop.type=sim::CommandType::drop_carried;drop.unit_id=1;drop.carried_category=core::ContentCategory::weapon;drop.carried_content_id=10;
 check(bool(game.apply(drop)),"normal command drops actual weapon");
 const auto dropped=game.snapshot();check(dropped.ground_items.size()==1,"dropped weapon exists on map");
 const auto id=dropped.ground_items.front().id;
 sim::Command pickup;pickup.type=sim::CommandType::pick_up;pickup.unit_id=2;pickup.ground_item_id=id;
 const auto hash=game.canonical_hash();
 check(game.apply(pickup).error==sim::CommandError::ground_out_of_reach,"normal command refuses distant pickup");
 check(game.canonical_hash()==hash,"refused command keeps AP, ownership and ground unchanged");
 sim::Command move;move.type=sim::CommandType::move;move.unit_id=1;move.destination={0,1};check(bool(game.apply(move)),"owner moves away from dropped weapon");
 move.unit_id=2;move.destination={0,0};check(bool(game.apply(move)),"second player moves onto weapon tile");
 if(full){
  const auto before=game.canonical_hash();check(game.apply(pickup).error==sim::CommandError::inventory_full,"actual pickup sees combined bag capacity");
  check(game.canonical_hash()==before,"full-bag refusal is atomic");
  pickup.type=sim::CommandType::exchange_carried;pickup.carried_category=core::ContentCategory::weapon;pickup.carried_content_id=20;
 }
 const auto result=game.apply(pickup);check(bool(result),"actual pickup or exchange succeeds locally");
 const auto state=game.snapshot();
 const auto actor=std::find_if(state.units.begin(),state.units.end(),[](const auto& unit){return unit.id==2;});
 check(actor!=state.units.end() && actor->local_player==2,"command preserves co-op ownership");
 if(actor!=state.units.end()){
  check(std::any_of(actor->carried_inventory.begin(),actor->carried_inventory.end(),[](const auto& stack){return stack.category==core::ContentCategory::weapon && stack.content_id==10;}),"new weapon is physically owned");
  check(actor->weapon_ids.empty(),"incompatible weapon remains owned without becoming usable");
 }
 check(state.ground_items.size()==(full?1U:0U),"exchange leaves old copy while ordinary pickup removes ground copy");
 if(full) check(state.ground_items.front().stack.content_id==20 && state.ground_items.front().id!=id,"old weapon remains local under a fresh identity");
}

void casualty_equipment_stays_local(bool permanent) {
 auto d=definition(false);
 d.units[0].health=1;d.units[0].drop_inventory_on_defeat=permanent;
 d.units[2].position={0,1};d.units[2].strength=30;
 auto made=sim::create_encounter(d);check(bool(made),"casualty battle creates");if(!made)return;
 sim::Command command;command.type=sim::CommandType::wait;command.unit_id=1;
 check(bool(made.encounter.apply(command)),"first hero finishes turn");
 command.unit_id=2;check(bool(made.encounter.apply(command)),"second hero finishes turn");
 command.type=sim::CommandType::attack;command.unit_id=3;command.target_id=1;
 auto killed=made.encounter.apply(command);check(bool(killed),"enemy defeats hero through ordinary attack");
 const auto state=made.encounter.snapshot();
 check(state.units[0].health==0,"hero was actually defeated");
 check(state.ground_items.size()==(permanent?1U:0U),"only permanent casualty leaves owned equipment on ground");
 check(state.units[0].carried_inventory.empty()==permanent,"recoverable casualty keeps personal equipment");
 if(permanent){
  check(state.ground_items[0].position==sim::Position{0,0} && state.ground_items[0].stack.content_id==10,
        "fallen hero's weapon stays at their own tile");
  check(std::count_if(killed.events.begin(),killed.events.end(),[](const auto& event){return event.type==sim::EventType::inventory_dropped;})==1,
        "casualty emits actual ownership debit for settlement");
 }
}
void last_enemy_loot_can_be_collected() {
 auto d=definition(false);d.units[0].strength=30;d.units[2].health=1;d.units[2].position={0,1};
 d.units[2].drop_item_id=30;d.units[2].drop_chance=100;d.units[2].drop_weapon_id=20;d.units[2].drop_weapon_chance=100;d.units[2].drop_gold=12;
 auto made=sim::create_encounter(d);check(bool(made),"loot battle creates");if(!made)return;
 auto& battle=made.encounter;sim::Command command;command.type=sim::CommandType::attack;command.unit_id=1;command.target_id=3;
 const auto win=battle.apply(command);check(bool(win),"ordinary attack defeats final enemy");
 auto state=battle.snapshot();
 check(state.outcome==sim::Outcome::first_side_won && state.collecting_loot,"victory leaves a collection phase for final enemy loot");
 check(state.ground_items.size()==2 && state.drops.empty(),"weapon and item loot remain local rather than entering legacy shared rewards");
 check(std::none_of(win.events.begin(),win.events.end(),[](const auto& event){return event.type==sim::EventType::encounter_completed;}),
       "stage completion is deferred until player continues");
 check(std::count_if(win.events.begin(),win.events.end(),[](const auto& event){return event.type==sim::EventType::gold_gained && event.amount==12;})==1,
       "gold is a separate guaranteed reward");
 const auto hash=battle.canonical_hash();
 check(battle.apply(command).error==sim::CommandError::wrong_phase && battle.canonical_hash()==hash,"collection cannot restart combat");
 command={};command.type=sim::CommandType::move;command.unit_id=1;command.destination={0,1};
 check(bool(battle.apply(command)),"winner walks onto final enemy loot after victory");
 state=battle.snapshot();const auto id=state.ground_items.front().id;
 command={};command.type=sim::CommandType::pick_up;command.unit_id=1;command.ground_item_id=id;
 check(bool(battle.apply(command)),"winner picks up one copy without starting another turn");
 command.ground_item_id=battle.snapshot().ground_items.front().id;
 check(battle.apply(command).error==sim::CommandError::inventory_full,"combined bag capacity still applies after victory");
 command.type=sim::CommandType::exchange_carried;command.carried_category=core::ContentCategory::weapon;command.carried_content_id=10;
 check(bool(battle.apply(command)),"full victory pack can exchange an owned weapon for remaining loot");
 state=battle.snapshot();check(state.collecting_loot && state.round==0,"collection does not advance combat rounds");
 check(state.ground_items.size()==1 && state.ground_items[0].stack.content_id==10,"exchanged weapon is left locally");
 command={};command.type=sim::CommandType::finish_collection;
 const auto finished=battle.apply(command);check(bool(finished),"Continue finishes collection explicitly");
 check(!battle.snapshot().collecting_loot && finished.events.size()==1 && finished.events[0].type==sim::EventType::encounter_completed,
       "Continue publishes stage completion once");
 check(battle.apply(command).error==sim::CommandError::wrong_phase,"duplicate Continue cannot complete stage twice");
}

int main(){
 casualty_equipment_stays_local(false);casualty_equipment_stays_local(true);last_enemy_loot_can_be_collected();
 actual_commands(false);actual_commands(true);
 const sim::CarriedStack sword{core::ContentCategory::weapon,10,1};
 const sim::CarriedStack bow{core::ContentCategory::weapon,20,1};
 const sim::CarriedStack tonic{core::ContentCategory::item,30,1};
 sim::UnitSnapshot actor;
 actor.position={2,3};actor.states_carry_capacity=true;actor.carry_capacity=2;
 actor.carried_inventory={sword,tonic};
 std::vector<sim::GroundItem> ground{{1,{2,3},bow}};
 std::uint64_t next=2;
 const auto original=actor.carried_inventory;
 check(sim::collect_ground_item(actor,ground,1)==sim::InventoryError::over_capacity,"full pack refuses pickup");
 check(actor.carried_inventory==original && ground.size()==1 && ground[0].stack==bow,"full pack leaves found equipment untouched");
 auto distant=actor;distant.carried_inventory.clear();distant.position={8,3};
 check(sim::collect_ground_item(distant,ground,1)==sim::InventoryError::wrong_position,"distant ally cannot collect equipment");
 check(distant.carried_inventory.empty() && ground.size()==1,"refused distant collection changes nothing");
 check(sim::exchange_ground_item(actor,ground,1,next,sword)==sim::InventoryError::none,"full pack exchanges a chosen owned copy");
 check(actor.carried_inventory==std::vector<sim::CarriedStack>({bow,tonic}),"new weapon belongs only to collecting unit");
 check(ground.size()==1 && ground[0].stack==sword && ground[0].position==actor.position,"discarded weapon remains at collector's tile");
 check(ground[0].id==2 && next==3,"exchange retires found identity and allocates a fresh drop identity");
 check(sim::collect_ground_item(distant,ground,1)==sim::InventoryError::missing_ground_item,"stale pickup cannot take an exchanged item's replacement");
 check(sim::drop_carried_item(actor,ground,next,bow)==sim::InventoryError::none,"unit can deliberately drop an owned copy");
 check(actor.carried_inventory==std::vector<sim::CarriedStack>({tonic}),"drop changes actual possession");
 check(ground.size()==2 && ground[1].stack==bow && ground[1].id==3 && next==4,"deliberate drop stays local with stable new identity");
 distant.position=actor.position;
 check(sim::collect_ground_item(distant,ground,2)==sim::InventoryError::none,"ally must reach equipment before collecting it");
 check(distant.carried_inventory==std::vector<sim::CarriedStack>({sword}),"second unit owns only the copy it collected");
 check(actor.carried_inventory==std::vector<sim::CarriedStack>({tonic}),"second unit collection cannot change first unit pack");
 sim::UnitSnapshot empty;
 empty.position=actor.position;empty.states_carry_capacity=true;empty.carry_capacity=0;
 check(sim::collect_ground_item(empty,ground,3)==sim::InventoryError::over_capacity,"explicit zero capacity accepts no item");
 empty.states_carry_capacity=false;
 check(sim::collect_ground_item(empty,ground,3)==sim::InventoryError::none,"omitted capacity is distinct from zero");
 check(sim::add_carried(empty.carried_inventory,bow,true,2)==sim::InventoryError::none,"duplicate weapon copies remain owned");
 check(sim::carried_count(empty.carried_inventory)==2 && empty.carried_inventory[0].quantity==2,"each weapon copy consumes a slot");
 check(sim::add_carried(empty.carried_inventory,tonic,true,2)==sim::InventoryError::over_capacity,"item and weapon share one combined limit");
 const sim::CarriedStack gold{core::ContentCategory::currency,40,1};
 check(sim::add_carried(empty.carried_inventory,gold,false,0)==sim::InventoryError::invalid_stack,"money is not counted equipment");
 next=std::numeric_limits<std::uint64_t>::max();
 const auto last=empty.carried_inventory;
 check(sim::drop_carried_item(empty,ground,next,bow)==sim::InventoryError::exhausted_identity,"ground identities cannot wrap");
 check(empty.carried_inventory==last && ground.empty(),"identity exhaustion loses no owned copy");
 sim::UnitSnapshot collector;
 collector.position={1,1};collector.states_carry_capacity=true;collector.carry_capacity=1;
 ground={{8,collector.position,{core::ContentCategory::weapon,10,3}}};next=9;
 check(sim::collect_ground_item(collector,ground,8)==sim::InventoryError::none,"one free slot collects one copy from a pile");
 check(collector.carried_inventory==std::vector<sim::CarriedStack>{sword} && ground.size()==1 && ground[0].stack.quantity==2,
       "remaining copies stay on the map under the same identity");
 check(sim::collect_ground_item(collector,ground,8)==sim::InventoryError::over_capacity,"remaining pile cannot exceed the bag limit");
 ground[0].stack={core::ContentCategory::weapon,20,2};
 check(sim::exchange_ground_item(collector,ground,8,next,sword)==sim::InventoryError::none,"full bag exchanges one copy from a larger pile");
 check(collector.carried_inventory==std::vector<sim::CarriedStack>{bow} && ground.size()==2 && ground[0].stack.quantity==1 &&
       ground[1].stack==sword && ground[1].id==9,"exchange preserves incoming remainder and the outgoing copy locally");
 std::cout<<"Carried inventory transactions "<<(failures?"FAIL":"PASS")<<'\n';return failures?1:0;
}
