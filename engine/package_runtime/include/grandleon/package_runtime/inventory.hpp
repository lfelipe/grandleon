// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/package_format/package.hpp>
#include <grandleon/core/currency.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace grandleon::package_runtime {
struct UnitInventoryRules final {
    std::uint16_t carry_capacity{};
    bool states_carry_capacity{false};
    std::uint16_t drop_gold{};
    std::uint64_t drop_weapon_id{};
    std::uint8_t drop_weapon_chance{};
    std::vector<std::uint64_t> weapon_affinities;
    bool states_weapon_affinities{false};
};
namespace inventory_detail {
class Reader final {
    const std::uint8_t* bytes_;
    std::size_t cursor_, end_;
public:
    Reader(const package_format::LoadedPackage& p,const package_format::RecordView& r)
        : bytes_(p.byte_data()),cursor_(r.payload_offset),end_(cursor_+r.payload_size) {}
    bool number(unsigned width,std::uint64_t& result) {
        if(end_-cursor_<width) return false;
        result=0;
        for(unsigned i=0;i<width;++i) result|=std::uint64_t(bytes_[cursor_++])<<(i*8U);
        return true;
    }
    bool done() const{return cursor_==end_;}
};
inline bool read_rules(const package_format::LoadedPackage& package,const package_format::RecordView& record,UnitInventoryRules& out) {
    Reader reader(package,record);std::uint64_t flags{},capacity{},gold{},chance{},weapon{},count{};
    if(!reader.number(1,flags) || (flags&~3U) || !reader.number(2,capacity) ||
       !reader.number(2,gold) || !reader.number(1,chance) || !reader.number(8,weapon) ||
       !reader.number(2,count) || count>65535U || gold>32767U || capacity>64U || chance>100U ||
       ((weapon==0)!=(chance==0)) || (!(flags&1U) && capacity) || (!(flags&2U) && count)) return false;
    out.states_carry_capacity=(flags&1U)!=0;out.carry_capacity=static_cast<std::uint16_t>(capacity);
    out.states_weapon_affinities=(flags&2U)!=0;out.drop_gold=static_cast<std::uint16_t>(gold);
    out.drop_weapon_id=weapon;out.drop_weapon_chance=static_cast<std::uint8_t>(chance);
    if(weapon && !package.find(package_format::SectionType::weapons,weapon)) return false;
    if(gold && !package.find(package_format::SectionType::currencies,core::gold_currency_id)) return false;
    for(std::uint64_t i=0;i<count;++i){
        std::uint64_t id{};
        if(!reader.number(8,id) || !id || !package.find(package_format::SectionType::weapon_types,id) ||
           std::find(out.weapon_affinities.begin(),out.weapon_affinities.end(),id)!=out.weapon_affinities.end()) return false;
        out.weapon_affinities.push_back(id);
    }
    return reader.done() && (flags || gold || weapon);
}
}
// Validate the complete sparse extensions, not only the currently selected role.
inline bool validate_inventory_sections(const package_format::LoadedPackage& package) {
    using package_format::SectionType;
    if(const auto* section=package.find(SectionType::unit_loot)) {
        if(section->flags!=package_format::section_flag_required || section->records.empty()) return false;
        for(const auto& record:section->records){
            UnitInventoryRules rules;
            if(!package.find(SectionType::unit_types,record.stable_id) || !inventory_detail::read_rules(package,record,rules)) return false;
        }
    }
    for(const auto type:{SectionType::campaign_inventory,SectionType::encounter_inventory}) {
        if(const auto* section=package.find(type)){
            if(section->flags!=package_format::section_flag_required || section->records.empty()) return false;
            for(const auto& record:section->records){
                inventory_detail::Reader reader(package,record);std::uint64_t mode{};
                const auto parent=type==SectionType::campaign_inventory?SectionType::campaigns:SectionType::encounters;
                if(!package.find(parent,record.stable_id) || !reader.number(1,mode) || mode!=1 || !reader.done()) return false;
            }
        }
    }
    return true;
}
inline UnitInventoryRules unit_inventory_rules(const package_format::LoadedPackage& package,std::uint64_t unit_type) {
    UnitInventoryRules out;
    if(const auto* record=package.find(package_format::SectionType::unit_loot,unit_type))
        (void)inventory_detail::read_rules(package,*record,out);
    return out;
}
inline simulation::InventoryMode inventory_mode(const package_format::LoadedPackage& package,package_format::SectionType section,std::uint64_t id) {
    return package.find(section,id)?simulation::InventoryMode::carried:simulation::InventoryMode::legacy;
}
}
