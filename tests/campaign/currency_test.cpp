// SPDX-License-Identifier: MIT
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/core/currency.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>

namespace camp = grandleon::campaign;
namespace core = grandleon::core;
int failures{};
void check(bool value, const char* label) {
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
int main(int argc, char** argv) {
    core::PackageId package{}; package[0] = 91;
    const camp::DefinitionRef gold{package, core::ContentCategory::currency, core::gold_currency_id};
    const camp::DefinitionRef item{package, core::ContentCategory::item, core::gold_currency_id};
    const camp::DefinitionRef weapon{package, core::ContentCategory::weapon, core::gold_currency_id};
    const auto batch = [&](std::uint64_t sequence, std::vector<camp::CampaignOutcomeOperation> ops) {
        return camp::make_outcome_batch({{package,core::ContentCategory::campaign,1},0,sequence},std::move(ops));
    };
    camp::CampaignState state;
    const auto reward = batch(0,{camp::recruit_unit({1},{package,core::ContentCategory::unit_type,1}),
        camp::add_item({},gold,20),camp::add_item({},item,2),camp::add_item({},weapon,1)});
    check(bool(camp::apply_outcome(state,reward)),"typed money enters shared wallet");
    check(state.store.size()==3,"money, item and weapon sharing a name keep separate identities");
    check(camp::item_quantity(state,{},gold)==20,"wallet retains quantity");
    check(camp::apply_outcome(state,reward).already_applied,"retry never duplicates money");
    const auto before = camp::canonical_hash(state);
    const auto refused = camp::apply_outcome(state,batch(1,{camp::consume_item({},gold,1),camp::add_item({1},gold,1)}));
    check(refused.error==camp::OutcomeError::invalid_candidate && refused.state_error==camp::StateError::carried_currency,
          "currency cannot become character equipment");
    check(camp::canonical_hash(state)==before,"refused transfer is atomic");
    const auto overflow = camp::apply_outcome(state,batch(2,{camp::add_item({},gold,std::numeric_limits<std::uint32_t>::max())}));
    check(overflow.error==camp::OutcomeError::quantity_overflow,"wallet overflow refuses instead of wrapping");
    check(camp::canonical_hash(state)==before,"overflow leaves wallet and receipt history unchanged");
    const auto bytes = camp::save_campaign(camp::make_campaign_save(state,{}));
    auto decoded = camp::decode_save_envelope(bytes,{});
    check(bool(decoded),"currency save envelope decodes");
    const auto marker = static_cast<std::uint32_t>(camp::SaveSectionType::currency_inventory);
    const auto position = std::find_if(decoded.decoded.sections.begin(),decoded.decoded.sections.end(),
        [marker](const auto& section){return section.type==marker;});
    check(position!=decoded.decoded.sections.end() && position->required(),"currency save requires feature support");
    const auto loaded = camp::load_campaign(bytes,{});
    check(bool(loaded) && camp::canonical_hash(loaded.save.state)==before,"wallet and typed inventories survive reload");
    auto missing = decoded.decoded;
    missing.sections.erase(std::remove_if(missing.sections.begin(),missing.sections.end(),
        [marker](const auto& section){return section.type==marker;}),missing.sections.end());
    const auto markerless = camp::interpret_save(std::move(missing),{});
    check(markerless.error==camp::SaveError::missing_required_section && markerless.section==marker,
          "opaque currency tags without compatibility marker are refused");
    if (position!=decoded.decoded.sections.end()) {
        position->flags=0;
        check(camp::interpret_save(decoded.decoded,{}).error==camp::SaveError::invalid_section,
              "currency marker cannot be marked optional for older readers");
    }
    camp::CampaignState plain;
    auto plain_decoded=camp::decode_save_envelope(camp::save_campaign(camp::make_campaign_save(plain,{})),{});
    check(bool(plain_decoded) && std::none_of(plain_decoded.decoded.sections.begin(),plain_decoded.decoded.sections.end(),
          [marker](const auto& section){return section.type==marker;}),"ordinary saves omit currency capability");
    check(bool(camp::load_campaign(plain_decoded.decoded.bytes,{})),"ordinary saves remain readable");
    if(argc==2){std::ofstream f(argv[1],std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    std::cout << "Campaign currency " << (failures?"FAIL":"PASS") << '\n';
    return failures?1:0;
}
