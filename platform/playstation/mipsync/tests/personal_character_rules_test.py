#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real package -> PS1 export -> generated C++ -> shared character-rule join."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser()
parser.add_argument('--content-compiler', type=Path, default=repo / 'build/grandleon_content_compile')
parser.add_argument('--scene-export', type=Path, default=repo / 'build/grandleon_ps1_mipsync_scene_export')
parser.add_argument('--compiler', default='g++')
args = parser.parse_args()
source = json.loads((repo / 'platform/godot/project/content/templates/starter_campaign.json').read_text())
with tempfile.TemporaryDirectory(prefix='grandleon-ps1-character-rules-') as temporary:
    work = Path(temporary)
    for personal in (False, True):
        project = json.loads(json.dumps(source))
        if not personal:
            project.pop("characterStyleId", None)
            for role in project["unitTypes"]:
                role.pop("characterFigureId", None)
                role.pop("characterStyleId", None)
        if personal:
            project['campaigns'][0]['roster'][1].update(
                weaponAffinityTypeIds=['blade', 'focus'], startingWeaponIds=['traveller_blade'], defeatPolicy='loseBattle')
        filename = work / 'project.json'
        filename.write_text(json.dumps(project))
        subprocess.run([str(args.content_compiler), str(filename), str(work / 'game.gpk')], check=True, capture_output=True)
        for players in (1, 2):
            subprocess.run([str(args.scene_export), str(work / 'game.gpk'), 'first_adventure/first_battle', str(players), str(work / f'{players}.json')], check=True, capture_output=True)
        if not personal:
            for players in (1, 2):
                manifest = json.loads((work / f'{players}.json').read_text())
                assert all(u['style'] == 0 and u['figure'] == 0 for u in manifest['units'])
                assert all(u['style'] == 0 and u['figure'] == 0 for u in manifest['speakers'])
        generated = work / 'generated'
        subprocess.run(['python3', str(repo / 'platform/playstation/mipsync/tools/generate_simulation.py'), str(work / '2.json'), str(generated), '--one-player-manifest', str(work / '1.json')], check=True)
        main = work / 'test.cpp'
        main.write_text(r'''
#include "grandleon_simulation.hpp"
#include <grandleon/campaign_runtime/character_rules.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <cassert>
namespace sim=grandleon::simulation;
namespace camp=grandleon::campaign;
namespace cr=grandleon::campaign_runtime;
namespace pr=grandleon::package_runtime;
namespace core=grandleon::core;
int main() {
 for(unsigned players=1;players<=2;++players) {
  auto definition=grandleon_generated_definition(players);
  auto raw=sim::create_encounter(definition);assert(raw);
  assert(raw.encounter.canonical_hash()==grandleon_generated_canonical_hash(players));
  camp::CampaignState state;
  core::PackageId package{};for(unsigned i=0;i<16;++i)package[i]=grandleon_generated_game_id[i];
  std::vector<camp::CampaignOutcomeOperation> operations;
  for(const auto& member:grandleon_generated_campaign_members()) {
   if(member.join_node_id!=0)continue;
   const camp::PersistentEntityId id{member.persistent_id};
   operations.push_back(camp::recruit_unit(id,{package,core::ContentCategory::unit_type,member.unit_type_id}));
   for(auto item:member.starting_items)operations.push_back(camp::add_item(id,{package,core::ContentCategory::item,item},1));
   if(member.rules.states_starting_weapons)for(auto weapon:member.rules.starting_weapons)
    operations.push_back(camp::add_item(id,{package,core::ContentCategory::weapon,weapon},1));
  }
  assert(camp::apply_outcome(state,camp::make_outcome_batch({{package,core::ContentCategory::campaign,1},0,0},operations)));
  bool changed=false;
  for(auto& unit:definition.units) {
   for(const auto& member:grandleon_generated_campaign_members()) {
    if(member.source_id!=grandleon_generated_unit_source(unit.id))continue;
    const auto* record=camp::find_unit(state,{member.persistent_id});assert(record);
    cr::apply_member_kit(unit,*record,&member.rules);
    cr::apply_member_permissions(unit,&member.rules,definition.weapons);
    unit.endures=grandleon_generated_company_endures();
    if(member.rules.states_starting_weapons) {
     changed=true;assert(unit.weapon_ids==member.rules.starting_weapons);
     assert(unit.defeat_ends_battle);
     assert(member.rules.weapon_affinities.size()==2);
    }
   }
  }
  auto joined=sim::create_encounter(definition);assert(joined);
  assert(changed==PERSONAL);
  if(!changed)assert(joined.encounter.canonical_hash()==raw.encounter.canonical_hash());
  else assert(joined.encounter.canonical_hash()!=raw.encounter.canonical_hash());
 }
 assert(cr::permanent_member_loss(pr::DefeatPolicy::inherit,true));
 assert(!cr::permanent_member_loss(pr::DefeatPolicy::inherit,false));
 assert(cr::permanent_member_loss(pr::DefeatPolicy::permanent,false));
 assert(!cr::permanent_member_loss(pr::DefeatPolicy::withdraw,true));
 assert(!cr::permanent_member_loss(pr::DefeatPolicy::lose_battle,true));
}
'''.replace('PERSONAL', 'true' if personal else 'false'))
        command = [args.compiler, '-std=c++17', '-O0', '-ffunction-sections', '-Wl,--gc-sections', '-I'+str(generated)]
        for module in ('core', 'campaign', 'campaign_runtime', 'package_runtime', 'simulation', 'tactics'):
            command.append('-I'+str(repo / 'engine' / module / 'include'))
        command += [str(main), str(generated / 'simulation_data.cpp')]
        for module, files in {'core':['random'], 'simulation':['encounter'], 'campaign':['state','outcome','identity']}.items():
            command += [str(repo/'engine'/module/'src'/f'{name}.cpp') for name in files]
        command += ['-o', str(work / 'check')]
        subprocess.run(command, check=True)
        subprocess.run([str(work/'check')], check=True)
print('PS1_PERSONAL_CHARACTER_RULES_PASS default_hashes=2 personal_kits=2 policies=5')
