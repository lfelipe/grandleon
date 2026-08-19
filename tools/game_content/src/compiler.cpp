// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>

#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace grandleon::game_content {
namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void put_i16(Bytes& out, std::int16_t value) {
    put_u16(out, static_cast<std::uint16_t>(value));
}

void put_u32(Bytes& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void put_u64(Bytes& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void put_string(Bytes& out, const std::string& value) {
    put_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void put_stats(Bytes& out, Stats stats) {
    put_i16(out, stats.health);
    put_i16(out, stats.strength);
    put_i16(out, stats.defense);
    put_i16(out, stats.resistance);
    out.push_back(stats.movement);
    out.push_back(stats.action_points);
    out.push_back(stats.speed);
}

void put_ids(Bytes& out, const std::vector<StableId>& ids) {
    put_u16(out, static_cast<std::uint16_t>(ids.size()));
    for (StableId id : ids) {
        put_u64(out, id);
    }
}

template <typename Definition>
bool validate_identity(
    const std::vector<Definition>& definitions,
    std::string_view category,
    std::vector<Diagnostic>& diagnostics
) {
    std::set<StableId> ids;
    bool valid = true;
    for (const Definition& definition : definitions) {
        const std::string path =
            std::string(category) + "[" + std::to_string(definition.id) + "]";
        if (definition.id == 0) {
            diagnostics.push_back(
                {DiagnosticCode::missing_id, path + ".id", 0}
            );
            valid = false;
        } else if (!ids.insert(definition.id).second) {
            diagnostics.push_back(
                {DiagnosticCode::duplicate_id, path + ".id", definition.id}
            );
            valid = false;
        }
        if (definition.name.empty()) {
            diagnostics.push_back(
                {DiagnosticCode::missing_name, path + ".name", definition.id}
            );
            valid = false;
        } else if (
            definition.name.size() >
            std::numeric_limits<std::uint16_t>::max()
        ) {
            diagnostics.push_back(
                {DiagnosticCode::name_too_long, path + ".name", definition.id}
            );
            valid = false;
        }
    }
    return valid;
}

// Every string this compiler writes carries a `uint16` length, and every list
// it writes carries a `uint16` or `uint8` count. Neither width fails when it is
// exceeded: it wraps, and the record that comes back declares a length nothing
// wrote, leaving the bytes past it to be read as whatever the next field
// happens to be. `validate_identity` bounds a definition's own name; these two
// bound everything else, before a byte is encoded, so the answer is a
// diagnostic naming the field rather than a package that decodes into garbage.
void validate_text(
    const std::string& value,
    std::string path,
    StableId id,
    std::vector<Diagnostic>& diagnostics
) {
    if (value.size() <= std::numeric_limits<std::uint16_t>::max()) return;
    diagnostics.push_back({DiagnosticCode::name_too_long, std::move(path), id});
}

void validate_count(
    std::size_t count,
    std::size_t limit,
    std::string path,
    StableId id,
    std::vector<Diagnostic>& diagnostics
) {
    if (count <= limit) return;
    diagnostics.push_back(
        {DiagnosticCode::invalid_range, std::move(path), id}
    );
}

// The two widths a count is written in. Named so a call site says which field
// of the record it is bounding rather than repeating a number.
inline constexpr std::size_t count_limit_u16 =
    std::numeric_limits<std::uint16_t>::max();
inline constexpr std::size_t count_limit_u8 =
    std::numeric_limits<std::uint8_t>::max();

template <typename Definition>
std::set<StableId> id_set(const std::vector<Definition>& definitions) {
    std::set<StableId> result;
    for (const Definition& definition : definitions) {
        result.insert(definition.id);
    }
    return result;
}

void validate_reference(
    StableId value,
    const std::set<StableId>& allowed,
    std::string path,
    std::vector<Diagnostic>& diagnostics
) {
    if (value == 0 || allowed.find(value) == allowed.end()) {
        diagnostics.push_back(
            {DiagnosticCode::missing_reference, std::move(path), value}
        );
    }
}

void validate_references(
    const std::vector<StableId>& values,
    const std::set<StableId>& allowed,
    const std::string& path,
    std::vector<Diagnostic>& diagnostics
) {
    std::set<StableId> seen;
    for (StableId value : values) {
        validate_reference(value, allowed, path, diagnostics);
        if (!seen.insert(value).second) {
            diagnostics.push_back(
                {DiagnosticCode::duplicate_reference, path, value}
            );
        }
    }
    if (values.size() > std::numeric_limits<std::uint16_t>::max()) {
        diagnostics.push_back(
            {
                DiagnosticCode::invalid_range,
                path,
                static_cast<StableId>(values.size())
            }
        );
    }
}

template <typename Definition, typename Encoder>
package_format::SectionSource make_section(
    package_format::SectionType type,
    const std::vector<Definition>& definitions,
    Encoder encode
) {
    std::vector<const Definition*> ordered;
    ordered.reserve(definitions.size());
    for (const Definition& definition : definitions) {
        ordered.push_back(&definition);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const Definition* lhs, const Definition* rhs) {
            return lhs->id < rhs->id;
        }
    );

    package_format::SectionSource section;
    section.type = type;
    section.records.reserve(ordered.size());
    for (const Definition* definition : ordered) {
        section.records.push_back({definition->id, encode(*definition)});
    }
    return section;
}

// The same question the engine's `can_enter` answers, asked here so a project
// is refused with a path an author can read rather than by a runtime that can
// only say "invalid unit". The compiler keeps its own copy of the vocabulary
// for the same reason every other encoded enumeration here does: it writes the
// wire format, and the wire format is the contract.
bool crossing_admits(std::uint8_t passability, std::uint8_t crossings) noexcept {
    if (passability == passability_open) return true;
    if ((crossings & crossing_every) != 0U) return true;
    if (passability == passability_water) {
        return (crossings & crossing_water) != 0U;
    }
    if (passability == passability_heights) {
        return (crossings & crossing_heights) != 0U;
    }
    return false;
}

// The range one delta-able stat may hold on a class.
//
// These are the bounds that stat's own class field admits and nothing else,
// which is one rule rather than eleven and cannot drift from the stat line
// because it *is* the stat line's bounds: an author may make a character
// anything they could have made a class, and nothing they could not.
//
// `movement`'s floor is one rather than the source schema's zero, because a
// class with movement zero is already refused as `invalid_stat`. The engine can
// hold a unit that cannot move; an author cannot write one, and a delta must
// not become the way around that.
//
// The four the damage arithmetic reads stop at `simulation::maximum_stat`, and
// they ask the engine for it rather than writing the number out: it is the same
// bound `create_encounter` and the package loader hold a unit to, and a delta
// this let through would land a character on a board that refuses to open.
// `skill`, `luck` and `evasion` are percentage points on a hit chance the rules
// clamp before reading, so the engine asks only that they not be negative and
// what an `int16` holds is genuinely their bound.
struct SpecificStatBounds final {
    std::int32_t minimum{};
    std::int32_t maximum{};
};

[[nodiscard]] SpecificStatBounds bounds_of(SpecificStat stat) noexcept {
    constexpr std::int32_t damage_stat = simulation::maximum_stat;
    constexpr std::int32_t chance = 32767;
    switch (stat) {
        case SpecificStat::health: return {1, 32767};
        case SpecificStat::movement: return {1, 255};
        case SpecificStat::action_points: return {1, 255};
        case SpecificStat::speed: return {1, 255};
        case SpecificStat::skill:
        case SpecificStat::luck:
        case SpecificStat::evasion: return {0, chance};
        case SpecificStat::strength:
        case SpecificStat::defense:
        case SpecificStat::resistance:
        case SpecificStat::magic: return {0, damage_stat};
    }
    return {0, 0};
}

// What that stat holds on the class the member's unit type names, which is the
// number a delta is added to.
[[nodiscard]] std::int32_t base_of(
    const Stats& stats,
    SpecificStat stat
) noexcept {
    switch (stat) {
        case SpecificStat::health: return stats.health;
        case SpecificStat::strength: return stats.strength;
        case SpecificStat::defense: return stats.defense;
        case SpecificStat::resistance: return stats.resistance;
        case SpecificStat::movement: return stats.movement;
        case SpecificStat::action_points: return stats.action_points;
        case SpecificStat::skill: return stats.skill;
        case SpecificStat::luck: return stats.luck;
        case SpecificStat::evasion: return stats.evasion;
        case SpecificStat::magic: return stats.magic;
        case SpecificStat::speed: return stats.speed;
    }
    return 0;
}

// Whether what an author wrote about one character beyond their class can be
// believed.
//
// Three refusals, and the order they are decided in is the point of the
// function. A stated object holding nothing, and a delta of zero, are refused
// without looking at any class at all. They are wrong on their own terms, and
// they are the same kind of wrong wherever the member's references point. The
// bounds are decided last and only when the class is in hand, because a delta
// whose base cannot be determined is not a delta problem: a member whose unit
// type or whose unit type's class does not resolve is already
// `missing_reference`, and reporting a bad number on top of a bad reference
// would tell an author to fix the wrong thing.
//
// Nothing is clamped. If this returns without a diagnostic, the number the
// author wrote is the number that reaches the board.
void validate_specificity(
    const CampaignMember& member,
    const std::map<StableId, const UnitType*>& unit_types_by_id,
    const std::map<StableId, const UnitClass*>& classes_by_id,
    const std::string& member_path,
    std::vector<Diagnostic>& diagnostics
) {
    if (!member.states_specificity) return;
    const std::string path = member_path + ".specificity";
    // Stated and saying nothing. This is a claim that a character is specific
    // without saying how, and it is distinct from omitting the object, which is
    // a character who is exactly their class and is what most members are.
    //
    // Emptiness is judged on what the author *stated* rather than on what the
    // numbers came to, so that a specificity whose only content is a zero delta
    // is answered by the diagnostic about that zero rather than by this one.
    // The author who wrote it made a mistake about a stat, not about whether to
    // write a specificity at all, and both analyzers say the same.
    bool stated_anything = member.specificity.reach_bonus != 0U;
    for (const bool stated : member.specificity.stated) {
        stated_anything = stated_anything || stated;
    }
    if (!stated_anything) {
        diagnostics.push_back(
            {DiagnosticCode::invalid_specificity, path, member.id}
        );
        return;
    }

    const UnitType* type = nullptr;
    const auto found_type = unit_types_by_id.find(member.unit_type_id);
    if (found_type != unit_types_by_id.end()) type = found_type->second;
    const UnitClass* unit_class = nullptr;
    if (type != nullptr) {
        const auto found_class = classes_by_id.find(type->class_id);
        if (found_class != classes_by_id.end()) unit_class = found_class->second;
    }

    for (std::size_t index = 0; index < specific_stat_count; ++index) {
        const auto stat = static_cast<SpecificStat>(index);
        const std::int16_t delta = member.specificity.stat_deltas[index];
        const std::string stat_path =
            path + ".stats." + std::string(specific_stat_name(stat));
        // A delta of zero is an author saying nothing with a number, and
        // omitting the stat is how nothing is said. Refused rather than
        // dropped, because two members with the same effective line would
        // otherwise be able to encode differently.
        //
        // Only reachable for a stat the author actually wrote: an omitted stat
        // never reaches the record, so a zero here is a stated zero.
        if (delta == 0) {
            if (!member.specificity.stated[index]) continue;
            diagnostics.push_back(
                {DiagnosticCode::invalid_specificity, stat_path, member.id}
            );
            continue;
        }
        if (unit_class == nullptr) continue;
        const SpecificStatBounds limits = bounds_of(stat);
        const std::int32_t landed =
            base_of(unit_class->base_stats, stat) + delta;
        if (landed < limits.minimum || landed > limits.maximum) {
            diagnostics.push_back(
                {DiagnosticCode::invalid_specificity, stat_path, member.id}
            );
        }
    }
}

}  // namespace

// The art library's faction colour menu, in its own order
// (tools/placeholder_art/placeholder_art/characters.py). Colour is
// presentation data: it travels in the package's presentation section, no rule
// reads it, and it never enters canonical state.
std::uint8_t faction_colour_index(std::string_view name) noexcept {
    constexpr std::string_view menu[faction_colour_count] = {
        "blue", "red", "green", "violet", "amber", "bone",
    };
    for (std::uint8_t index = 0; index < faction_colour_count; ++index) {
        if (menu[index] == name) return index;
    }
    return faction_colour_unchosen;
}

std::uint8_t resolved_faction_colour(
    const Faction& faction,
    std::size_t position
) noexcept {
    if (faction.colour != faction_colour_unchosen) return faction.colour;
    return static_cast<std::uint8_t>(position % faction_colour_count);
}

// The art library's terrain registry, in its own keyword match order
// (tools/placeholder_art/placeholder_art/terrain.py, mirrored into the
// generated tools/placeholder_art/assets/themes.h that the console reads). The
// order settles ambiguity between words rather than between layers: "mountain
// road" is a road, and grass is last because it is also open ground's name.
std::uint8_t terrain_kind_index(std::string_view name) noexcept {
    struct Kind final {
        std::string_view keywords[2];
    };
    constexpr Kind kinds[terrain_kind_count] = {
        {{"water", "river"}},   {{"bridge", "road"}},
        {{"forest", "wood"}},   {{"mountain", "rock"}},
        {{"sand", "desert"}},   {{"snow", "ice"}},
        {{"swamp", "marsh"}},   {{"hill", "highland"}},
        {{"ruin", "rubble"}},   {{"grass", "plain"}},
        // Appended after grass, which is why adding them cannot change how a
        // name authored before they existed resolves: grass is the fallback
        // for open ground and is still reached first.
        {{"farm", "field"}},    {{"bamboo", "thicket"}},
        {{"pave", "cobble"}},
    };
    std::string lowered(name);
    for (char& character : lowered) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    for (std::uint8_t index = 0; index < terrain_kind_count; ++index) {
        for (const std::string_view keyword : kinds[index].keywords) {
            if (lowered.find(keyword) != std::string::npos) return index;
        }
    }
    return terrain_kind_unknown;
}

// The gameplay half of the terrain vocabulary, keyed on the same kind index
// the art library uses. Water is water; a mountain is a climb. Everything else
// is open ground: whether a cell takes somebody and what it charges them are
// two questions, and ground that should be slow rather than shut answers the
// first with "anyone" and the second in `terrain_movement_cost`.
std::uint8_t terrain_passability(std::uint8_t kind) noexcept {
    switch (kind) {
        case 0U: return passability_water;    // water, river
        case 3U: return passability_heights;  // mountain, rock
        default: return passability_open;
    }
}

// The price half, keyed on the same kind index again.
//
// Two prices, not a scale. Ground is either ordinary or it is heavy going, and
// a third band would be a number an author has to learn rather than a fact they
// can see: a marsh reading "three" against a forest's "two" is a distinction
// nobody can read off a board.
//
// What is heavy going is what a character has to get *through* rather than walk
// *on*: undergrowth in a forest or a bamboo thicket, water underfoot in a
// marsh, rubble in a ruin, a slope in a hill, and loose footing in sand. Sand
// is the one kind here named for what is under the boot rather than for what
// stands on it, and it is slow in every game in this genre that has bothered to
// have it.
//
// Snow charges nothing, and it is the one entry worth arguing about. In a
// winter setting snow is not a feature of the ground, it is the ground: it is
// what grass is in a temperate one, the fill a whole board is painted in before
// anything is placed on it. Pricing it makes a winter map uniformly slower than
// a summer one, which is a change to the movement allowance wearing a terrain
// costume. A price that lands on every cell of a board tells a player nothing
// about any cell of it. Deep snow that is meant to bog a character down is a
// marsh with a white sprite, which the marsh entry already prices.
//
// Water and a mountainside charge one, to the few who may enter them at all.
// Being able to swim or climb is the whole of what those cells ask; asking a
// premium on top would price a character's own element against them.
std::uint8_t terrain_movement_cost(std::uint8_t kind) noexcept {
    switch (kind) {
        case 2U:   // forest, wood
        case 4U:   // sand, desert
        case 6U:   // swamp, marsh
        case 7U:   // hill, highland
        case 8U:   // ruin, rubble
        case 11U:  // bamboo, thicket
            return 2U;
        default:
            return movement_cost_step;
    }
}

std::uint8_t crossing_bit(std::string_view name) noexcept {
    if (name == "water") return crossing_water;
    if (name == "heights") return crossing_heights;
    return crossing_none;
}

// The art library's biome and season menu, in its own order
// (tools/placeholder_art/placeholder_art/themes.py). Like faction colour, a
// theme is source data rather than package data: no rule reads it.
std::uint8_t theme_index(std::string_view name) noexcept {
    constexpr std::string_view menu[theme_count] = {
        "temperate", "autumn", "winter", "ashland",
    };
    for (std::uint8_t index = 0; index < theme_count; ++index) {
        if (menu[index] == name) return index;
    }
    return theme_count;
}

// The art library's character style menu, in its own order
// (tools/placeholder_art/placeholder_art/styles.py). Presentation only, like
// the theme menu above: a style says which drawing of an archetype is used and
// nothing else, so no rule reads it and no canonical hash can move with it.
std::uint8_t character_style_index(std::string_view name) noexcept {
    constexpr std::string_view menu[character_style_count] = {
        "medieval",
        "scifi",
        "mythical",
        "nature",
        "sengoku",
        "undead",
        "pirates",
    };
    for (std::uint8_t index = 0; index < character_style_count; ++index) {
        if (menu[index] == name) return index;
    }
    return character_style_count;
}

// The art library's figure menu, in its own order
// (tools/placeholder_art/placeholder_art/figures.py). Presentation only, like
// the style menu above: a figure says at what build a role is drawn and
// nothing else, so no rule reads it and no canonical hash can move with it.
std::uint8_t character_figure_index(std::string_view name) noexcept {
    constexpr std::string_view menu[character_figure_count] = {
        "first",
        "second",
    };
    for (std::uint8_t index = 0; index < character_figure_count; ++index) {
        if (menu[index] == name) return index;
    }
    return character_figure_count;
}

std::uint8_t character_geometry_index(std::string_view name) noexcept {
    constexpr std::string_view menu[character_geometry_count] = {
        "sprites", "models"
    };
    for (std::uint8_t index = 0; index < character_geometry_count; ++index) {
        if (menu[index] == name) { return index; }
    }
    return character_geometry_count;
}

// The art library's scene backdrop menu, in its own order
// (tools/placeholder_art/placeholder_art/backdrops.py). Presentation only,
// like the two menus above: a backdrop says what a conversation between maps
// is drawn against and nothing else.
std::uint8_t backdrop_index(std::string_view name) noexcept {
    constexpr std::string_view menu[backdrop_count] = {
        "throne_hall",
        "night_camp",
        "deep_wood",
        "mountain_dusk",
        "open_sea",
        "star_field",
        "crypt",
    };
    for (std::uint8_t index = 0; index < backdrop_count; ++index) {
        if (menu[index] == name) return index;
    }
    return backdrop_count;
}

// The art library's character archetype roster, in its own order
// (tools/placeholder_art/placeholder_art/characters.py, mirrored into the
// generated tools/placeholder_art/assets/styles.h). The order settles which
// archetype a name spelling two of them wears, and it is the order every
// client searched in before this function existed.
std::uint8_t archetype_index(std::string_view name) noexcept {
    constexpr std::string_view roster[archetype_count] = {
        "knight", "archer", "mage", "stormcaller",
        "healer", "commander", "rogue", "beast",
    };
    std::string lowered(name);
    for (char& character : lowered) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    for (std::uint8_t index = 0; index < archetype_count; ++index) {
        if (lowered.find(roster[index]) != std::string::npos) return index;
    }
    return archetype_unnamed;
}

std::uint8_t resolved_archetype(
    const GameSource& source,
    const UnitType& unit_type
) noexcept {
    for (const UnitClass& unit_class : source.classes) {
        if (unit_class.id != unit_type.class_id) continue;
        const std::uint8_t named = archetype_index(unit_class.name);
        if (named != archetype_unnamed) return named;
        break;
    }
    const std::uint8_t named = archetype_index(unit_type.name);
    return named == archetype_unnamed ? archetype_default : named;
}

//: The style a unit type is drawn in: its own if it names one, and otherwise
//: the game's. This is the whole of the resolution: a project style is a
//: default, never a gate, so any style the menu holds is legal on any
//: character.
std::uint8_t resolved_character_style(
    const GameSource& source,
    const UnitType& unit_type
) noexcept {
    return unit_type.character_style == character_style_unnamed
        ? source.character_style
        : unit_type.character_style;
}

//: The body a unit type is drawn with: its own if it names one, and otherwise
//: the game's. The same resolution as the style beside it, one axis over.
std::uint8_t resolved_character_figure(
    const GameSource& source,
    const UnitType& unit_type
) noexcept {
    return unit_type.character_figure == character_figure_unnamed
        ? source.character_figure
        : unit_type.character_figure;
}

//: Whether any character in this game names a style of its own. The one thing
//: the presentation section's style record is conditioned on: a game where
//: nobody does compiles to the bytes it always did, down to the record not
//: being there.
bool draws_any_style_override(const GameSource& source) noexcept {
    for (const UnitType& unit_type : source.unit_types) {
        if (unit_type.character_style != character_style_unnamed) return true;
    }
    return false;
}

//: The same question about the figure axis, asked separately because the two
//: records are separate. A game that dresses one character in another setting
//: pays for the style join and not for the figure join, and the other way
//: round.
bool draws_any_figure_override(const GameSource& source) noexcept {
    for (const UnitType& unit_type : source.unit_types) {
        if (unit_type.character_figure != character_figure_unnamed) return true;
    }
    return false;
}

std::string_view diagnostic_name(DiagnosticCode code) noexcept {
    switch (code) {
        case DiagnosticCode::missing_id: return "missing_id";
        case DiagnosticCode::duplicate_id: return "duplicate_id";
        case DiagnosticCode::missing_name: return "missing_name";
        case DiagnosticCode::name_too_long: return "name_too_long";
        case DiagnosticCode::missing_reference: return "missing_reference";
        case DiagnosticCode::duplicate_reference: return "duplicate_reference";
        case DiagnosticCode::invalid_stat: return "invalid_stat";
        case DiagnosticCode::invalid_range: return "invalid_range";
        case DiagnosticCode::disallowed_weapon: return "disallowed_weapon";
        case DiagnosticCode::invalid_map: return "invalid_map";
        case DiagnosticCode::invalid_placement: return "invalid_placement";
        case DiagnosticCode::incomplete_pair: return "incomplete_pair";
        case DiagnosticCode::empty_roster: return "empty_roster";
        case DiagnosticCode::invalid_member: return "invalid_member";
        case DiagnosticCode::invalid_deployment: return "invalid_deployment";
        case DiagnosticCode::invalid_grant: return "invalid_grant";
        case DiagnosticCode::invalid_objective: return "invalid_objective";
        case DiagnosticCode::invalid_arrival: return "invalid_arrival";
        case DiagnosticCode::invalid_specificity:
            return "invalid_specificity";
        case DiagnosticCode::invalid_transition: return "invalid_transition";
        case DiagnosticCode::undecided_encounter:
            return "undecided_encounter";
    }
    return "unknown";
}

std::string_view specific_stat_name(SpecificStat stat) noexcept {
    switch (stat) {
        case SpecificStat::health: return "health";
        case SpecificStat::strength: return "strength";
        case SpecificStat::defense: return "defense";
        case SpecificStat::resistance: return "resistance";
        case SpecificStat::movement: return "movement";
        case SpecificStat::action_points: return "actionPoints";
        case SpecificStat::skill: return "skill";
        case SpecificStat::luck: return "luck";
        case SpecificStat::evasion: return "evasion";
        case SpecificStat::magic: return "magic";
        case SpecificStat::speed: return "speed";
    }
    return "unknown";
}

CompileResult compile(const GameSource& source) {
    CompileResult result;
    if (source.title.empty()) {
        result.diagnostics.push_back(
            {DiagnosticCode::missing_name, "manifest.title", 0}
        );
    } else if (
        source.title.size() > std::numeric_limits<std::uint16_t>::max()
    ) {
        result.diagnostics.push_back(
            {DiagnosticCode::name_too_long, "manifest.title", 0}
        );
    }
    validate_identity(source.weapon_types, "weapon_types", result.diagnostics);
    validate_identity(source.item_types, "item_types", result.diagnostics);
    validate_identity(source.classes, "classes", result.diagnostics);
    validate_identity(source.weapons, "weapons", result.diagnostics);
    validate_identity(source.items, "items", result.diagnostics);
    validate_identity(source.unit_types, "unit_types", result.diagnostics);
    validate_identity(source.maps, "maps", result.diagnostics);
    validate_identity(source.factions, "factions", result.diagnostics);
    // The presentation record lists every faction behind a `uint16` count, so
    // the project's faction list is bounded by that width and not by the
    // section's own record count.
    validate_count(
        source.factions.size(), count_limit_u16, "factions", 0,
        result.diagnostics
    );
    validate_identity(source.abilities, "abilities", result.diagnostics);
    validate_identity(source.objectives, "objectives", result.diagnostics);
    // The count and the kind are one authored fact. Checked here as well as at
    // the source, because this compiler is reachable from a native model that
    // never passed through the JSON reader, and the engine can only refuse the
    // whole encounter.
    for (const Objective& objective : source.objectives) {
        const bool counts = objective.kind == ObjectiveKind::survive_rounds;
        if (counts == (objective.rounds != 0)) continue;
        result.diagnostics.push_back(
            {DiagnosticCode::invalid_objective, "objectives", objective.id}
        );
    }
    validate_identity(source.dialogues, "dialogues", result.diagnostics);
    validate_identity(source.encounters, "encounters", result.diagnostics);
    validate_identity(source.campaigns, "campaigns", result.diagnostics);

    const auto weapon_type_ids = id_set(source.weapon_types);
    const auto item_type_ids = id_set(source.item_types);
    const auto class_ids = id_set(source.classes);
    const auto weapon_ids = id_set(source.weapons);
    const auto item_ids = id_set(source.items);
    const auto unit_type_ids = id_set(source.unit_types);
    const auto map_ids = id_set(source.maps);
    const auto ability_ids = id_set(source.abilities);
    const auto objective_ids = id_set(source.objectives);
    const auto dialogue_ids = id_set(source.dialogues);
    const auto encounter_ids = id_set(source.encounters);
    const auto faction_ids = id_set(source.factions);
    std::map<StableId, const UnitClass*> classes_by_id;
    std::map<StableId, StableId> weapon_type_by_weapon_id;
    for (const UnitClass& definition : source.classes) {
        classes_by_id.emplace(definition.id, &definition);
    }
    for (const Weapon& definition : source.weapons) {
        weapon_type_by_weapon_id.emplace(definition.id, definition.type_id);
    }

    for (const UnitClass& definition : source.classes) {
        validate_references(
            definition.allowed_weapon_types,
            weapon_type_ids,
            "classes[" + std::to_string(definition.id) +
                "].allowed_weapon_types",
            result.diagnostics
        );
        if (definition.base_stats.health <= 0 ||
            definition.base_stats.strength < 0 ||
            definition.base_stats.defense < 0 ||
            definition.base_stats.resistance < 0 ||
            definition.base_stats.skill < 0 ||
            definition.base_stats.luck < 0 ||
            definition.base_stats.evasion < 0 ||
            definition.base_stats.magic < 0 ||
            definition.base_stats.movement == 0) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_stat,
                    "classes[" + std::to_string(definition.id) +
                        "].base_stats",
                    definition.id
                }
            );
        }
    }
    for (const Weapon& definition : source.weapons) {
        // A type, if this weapon claims one. Zero is legacy unclassified
        // content rather than a broken reference, on the same terms a unit
        // type's faction is optional below.
        if (definition.type_id != 0) {
            validate_reference(
                definition.type_id,
                weapon_type_ids,
                "weapons[" + std::to_string(definition.id) + "].type_id",
                result.diagnostics
            );
        }
        if (definition.minimum_range == 0 ||
            definition.maximum_range < definition.minimum_range) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_range,
                    "weapons[" + std::to_string(definition.id) + "].range",
                    definition.id
                }
            );
        }
        // Accuracy is a whole percentage and nothing else. A hundred and ten
        // is not a very accurate weapon, it is an author who meant something
        // the rules cannot express, and the roll would silently round it to
        // certainty, so it is refused here where an author can still see it.
        if (definition.accuracy > 100U) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_range,
                    "weapons[" + std::to_string(definition.id) + "].accuracy",
                    definition.id
                }
            );
        }
    }
    for (const Ability& definition : source.abilities) {
        if (definition.accuracy > 100U) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_range,
                    "abilities[" + std::to_string(definition.id) +
                        "].accuracy",
                    definition.id
                }
            );
        }
    }
    for (const Item& definition : source.items) {
        // A type, if this item claims one, on the weapon type's exact terms.
        if (definition.type_id != 0) {
            validate_reference(
                definition.type_id,
                item_type_ids,
                "items[" + std::to_string(definition.id) + "].type_id",
                result.diagnostics
            );
        }
        // Every field is either read or refused. A restoring item that gives
        // back nothing is an item authored to be wasted, and power on an item
        // that restores nothing is a number the rules would never read. The
        // ability record still carries that second shape unrefused (a
        // restoring ability may name an `accuracy` no cast ever rolls), and
        // this is the field pair where it is refused rather than repeated.
        const bool restores = definition.kind == ItemKind::restore;
        if ((restores && definition.power <= 0) ||
            (!restores && definition.power != 0)) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_range,
                    "items[" + std::to_string(definition.id) + "].power",
                    definition.id
                }
            );
        }
    }
    std::map<StableId, const UnitType*> unit_types_by_id;
    for (const UnitType& definition : source.unit_types) {
        unit_types_by_id.emplace(definition.id, &definition);
        const std::string path =
            "unit_types[" + std::to_string(definition.id) + "]";
        validate_reference(
            definition.class_id,
            class_ids,
            path + ".class_id",
            result.diagnostics
        );
        if (definition.faction_id != 0) {
            validate_reference(
                definition.faction_id, faction_ids, path + ".faction_id",
                result.diagnostics
            );
        }
        validate_references(
            definition.starting_weapons,
            weapon_ids,
            path + ".starting_weapons",
            result.diagnostics
        );
        validate_references(
            definition.starting_items,
            item_ids,
            path + ".starting_items",
            result.diagnostics
        );
        validate_references(
            definition.abilities,
            ability_ids,
            path + ".abilities",
            result.diagnostics
        );
        // A growth chance is a whole percentage and nothing else, on exactly
        // the terms a weapon's accuracy is: the authored number is the rolled
        // number, so one hundred and ten is not enthusiasm, it is a rule the
        // engine cannot express, and it is refused where the author can see it.
        for (std::size_t index = 0; index < growable_stat_count; ++index) {
            if (definition.growth.chance[index] > 100U) {
                result.diagnostics.push_back(
                    {
                        DiagnosticCode::invalid_range,
                        path + ".growth_rates",
                        definition.id
                    }
                );
                break;
            }
        }
        // What this one leaves behind. The identity must resolve, exactly as
        // every other reference must: an author naming a tonic that does not
        // exist has written a drop nothing can ever be, and the compiler is
        // where that is cheapest to hear. The *encounter* deliberately does not
        // resolve it: a drop is recorded rather than handed over, so no board
        // has to register an item nobody on it carries. That is precisely why
        // the check has to happen here instead.
        if (definition.drop_item != 0) {
            validate_reference(
                definition.drop_item,
                item_ids,
                path + ".drop_item",
                result.diagnostics
            );
        }
        // And it is authored as a pair or not at all, in both directions.
        // Honouring one half would leave an author wondering why the picket
        // never drops anything; refusing says which half is missing. A chance
        // of zero never reaches here (the source reader bounds it at one)
        // because "leaves nothing" is already spelled by authoring neither.
        if ((definition.drop_item == 0) != (definition.drop_chance == 0)) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::incomplete_pair,
                    path + (definition.drop_item == 0 ? ".drop_chance"
                                                      : ".drop_item"),
                    definition.id
                }
            );
        }
        if (definition.drop_chance > 100U) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_range,
                    path + ".drop_chance",
                    definition.id
                }
            );
        }
        // Zero experience per level is a level threshold no amount of
        // experience reaches, and division by it has no answer at all.
        if (definition.experience_per_level == 0U) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_range,
                    path + ".experience_per_level",
                    definition.id
                }
            );
        }
        const auto found_class = classes_by_id.find(definition.class_id);
        // What the class permits, asked only of a class that states an
        // allowance. One that states none has unrestricted access, and reading
        // its silence as "permits nothing" would refuse every legacy project in
        // existence while claiming the author had written something they had
        // not. A class that states an *empty* allowance did write something:
        // "no weapon type". It is held to it.
        if (found_class != classes_by_id.end() &&
            found_class->second->states_allowed_weapon_types) {
            const UnitClass& unit_class = *found_class->second;
            const std::set<StableId> allowed_weapon_types(
                unit_class.allowed_weapon_types.begin(),
                unit_class.allowed_weapon_types.end()
            );
            for (StableId weapon_id : definition.starting_weapons) {
                const auto found_weapon =
                    weapon_type_by_weapon_id.find(weapon_id);
                if (found_weapon != weapon_type_by_weapon_id.end() &&
                    allowed_weapon_types.find(found_weapon->second) ==
                        allowed_weapon_types.end()) {
                    result.diagnostics.push_back(
                        {
                            DiagnosticCode::disallowed_weapon,
                            path + ".starting_weapons",
                            weapon_id
                        }
                    );
                }
            }
        }
    }
    std::map<StableId, const Map*> maps_by_id;
    // The terrain join the package carries: one entry per distinct terrain
    // identity in the whole project, so a client that holds only identities
    // can still say what each one draws as. Keyed rather than per-cell because
    // a cell's identity and its kind are resolved from the same authored name,
    // which makes the kind a property of the identity.
    std::map<StableId, std::uint8_t> terrain_kinds;
    for (const Map& definition : source.maps) {
        maps_by_id.emplace(definition.id, &definition);
        const std::uint64_t cell_count =
            static_cast<std::uint64_t>(definition.width) * definition.height;
        if (definition.width == 0 || definition.height == 0 ||
            definition.terrain.size() != cell_count) {
            result.diagnostics.push_back(
                {
                    DiagnosticCode::invalid_map,
                    "maps[" + std::to_string(definition.id) + "].terrain",
                    definition.id
                }
            );
        }
        for (std::size_t cell = 0; cell < definition.terrain.size(); ++cell) {
            // A source built in memory rather than parsed may carry no kinds
            // at all; such a project simply has no join to write.
            if (cell >= definition.terrain_kinds.size()) break;
            const std::uint8_t kind = definition.terrain_kinds[cell];
            const auto placed = terrain_kinds.emplace(
                definition.terrain[cell], kind
            );
            if (!placed.second && placed.first->second != kind) {
                // One identity drawn two ways would make the join a lie, and
                // keeping the first would make the board wrong on whichever
                // map lost. Parsing cannot produce it, because identity and
                // kind come from the same string, so it is checked rather than
                // assumed.
                result.diagnostics.push_back(
                    {
                        DiagnosticCode::invalid_map,
                        "maps[" + std::to_string(definition.id) +
                            "].terrain[" + std::to_string(cell) + "]",
                        definition.id
                    }
                );
            }
        }
    }
    std::map<StableId, const Objective*> objectives_by_id;
    for (const Objective& definition : source.objectives) {
        objectives_by_id.emplace(definition.id, &definition);
    }
    for (const Encounter& definition : source.encounters) {
        const std::string path =
            "encounters[" + std::to_string(definition.id) + "]";
        validate_reference(
            definition.map_id, map_ids, path + ".map_id", result.diagnostics
        );
        validate_references(
            definition.objective_ids, objective_ids,
            path + ".objective_ids", result.diagnostics
        );
        // A board has to say how it is won or lost. The runtime reads the
        // objective count before anything else and refuses a payload that
        // declares none, so emitting one would be emitting a Stage that cannot
        // be opened at all -- and the author would not find out until a console
        // reached it. `undecided_encounter` says why.
        if (definition.objective_ids.empty()) {
            result.diagnostics.push_back(
                {DiagnosticCode::undecided_encounter,
                 path + ".objective_ids",
                 definition.id}
            );
        }
        // Who this board's objectives are about. `encounter_loader` resolves an
        // objective's target against the placements of the encounter that names
        // it and refuses the whole encounter when it cannot, so a target
        // nothing places is not a battle played badly. It is a battle no
        // client can load, and the author would hear about it first from a
        // device. This compiler is the only component holding the objective
        // registry and the placements at once, which is why the check is here.
        //
        // The key matched is the placement's `source_key_id`, which is exactly
        // what the runtime matches. For a placement that fields a roster member
        // that key is the *member's*, not the tile's: the character is who the
        // objective is about, and they are the same character on every board
        // that places them.
        std::set<StableId> placement_keys;
        for (const Placement& placement : definition.placements) {
            placement_keys.insert(placement.source_key_id);
        }
        for (StableId objective_id : definition.objective_ids) {
            const auto found_objective = objectives_by_id.find(objective_id);
            if (found_objective == objectives_by_id.end()) continue;
            const ObjectiveKind kind = found_objective->second->kind;
            if (kind != ObjectiveKind::defeat_target &&
                kind != ObjectiveKind::protect_target) {
                continue;
            }
            validate_reference(
                found_objective->second->target_placement_id, placement_keys,
                path + ".objective_ids.target_placement_id",
                result.diagnostics
            );
        }
        // What the encounter record's own widths admit: the arrangement, each
        // patrol route inside it, and the deployment region.
        validate_count(
            definition.placements.size(), count_limit_u16, path + ".placements",
            definition.id, result.diagnostics
        );
        validate_count(
            definition.deployment.tiles.size(), count_limit_u16,
            path + ".deployment.tiles", definition.id, result.diagnostics
        );
        std::set<StableId> placement_ids;
        std::set<std::pair<std::int16_t, std::int16_t>> placement_tiles;
        const auto found_map = maps_by_id.find(definition.map_id);
        for (const Placement& placement : definition.placements) {
            validate_count(
                placement.patrol.size(), count_limit_u16,
                path + ".placements.patrol", placement.id, result.diagnostics
            );
            validate_reference(
                placement.unit_type_id, unit_type_ids,
                path + ".placements.unit_type_id", result.diagnostics
            );
            // A placement's name is written as a length-prefixed string like
            // every other, so it is bounded like every other: before a byte is
            // encoded, rather than by a `uint16` that wrapped.
            validate_text(
                placement.name, path + ".placements.name", placement.id,
                result.diagnostics
            );
            const bool on_board =
                found_map != maps_by_id.end() && placement.x >= 0 &&
                placement.y >= 0 &&
                placement.x < static_cast<std::int16_t>(
                                  found_map->second->width
                              ) &&
                placement.y < static_cast<std::int16_t>(
                                  found_map->second->height
                              );
            // Only the opening arrangement is judged against itself. A
            // character who comes in on the sixth round does not share the
            // board with the characters standing on it at the opening, and
            // where its tile is held when its round comes it takes the nearest
            // one it could stand on instead. So two waves may be authored onto
            // one tile, and a wave onto a tile somebody starts on.
            const bool arrives = placement.arrival_round != 0;
            const bool tile_taken =
                !arrives &&
                !placement_tiles.emplace(placement.x, placement.y).second;
            if (placement.id == 0 ||
                !placement_ids.insert(placement.id).second || tile_taken ||
                (found_map != maps_by_id.end() && !on_board)) {
                result.diagnostics.push_back(
                    {
                        DiagnosticCode::invalid_placement,
                        path + ".placements",
                        placement.id
                    }
                );
                continue;
            }
            // A roster member cannot be a wave. `campaign_runtime` joins a
            // member to a placement to field them, and counts who takes the
            // field against the deployment capacity; somebody who is not on the
            // board at the opening is neither fielded nor withheld, and
            // answering that is a campaign design question rather than a rule.
            // Refused here so an author is told rather than surprised.
            // An arrival nothing could honour, and a roster member who cannot
            // be one. Both are said at the placement's own path.
            const bool half_stated =
                (placement.arrival_every == 0) !=
                (placement.arrival_times == 0);
            if ((arrives &&
                 (placement.member_id != 0 || placement.arrival_round < 2 ||
                  placement.arrival_times > 64)) ||
                (!arrives && (placement.arrival_every != 0 ||
                              placement.arrival_times != 0)) ||
                half_stated) {
                result.diagnostics.push_back(
                    {
                        DiagnosticCode::invalid_arrival,
                        path + ".placements",
                        placement.id
                    }
                );
                continue;
            }
            // A character may not begin standing where it could never walk.
            // Caught here, with the placement's own path, because the engine
            // can only refuse the whole encounter. It is asked of an arriving
            // character too: the tile it is authored onto is where it will try
            // to come in, and a tile it could never stand on is content the
            // author meant something else by.
            if (!on_board) continue;
            const Map& map = *found_map->second;
            const std::size_t cell =
                static_cast<std::size_t>(placement.y) * map.width +
                static_cast<std::size_t>(placement.x);
            if (cell >= map.terrain_kinds.size()) continue;
            std::uint8_t crossings = crossing_none;
            const auto found_unit_type =
                unit_types_by_id.find(placement.unit_type_id);
            if (found_unit_type != unit_types_by_id.end()) {
                const auto found_placed_class =
                    classes_by_id.find(found_unit_type->second->class_id);
                if (found_placed_class != classes_by_id.end()) {
                    crossings = found_placed_class->second->crossings;
                }
            }
            if (!crossing_admits(
                    terrain_passability(map.terrain_kinds[cell]), crossings
                )) {
                result.diagnostics.push_back(
                    {
                        DiagnosticCode::invalid_placement,
                        path + ".placements",
                        placement.id
                    }
                );
            }
        }
        if (definition.placements.empty()) {
            result.diagnostics.push_back(
                {DiagnosticCode::invalid_placement, path + ".placements", 0}
            );
        }
        // The deployment region, checked here rather than at the map because
        // the region belongs to the battle: two encounters on one map may state
        // different regions or none. An encounter that states none has nothing
        // to check and no phase, which is every encounter written before this.
        const DeploymentZone& zone = definition.deployment;
        if (!zone.tiles.empty() || zone.id != 0 || zone.capacity != 0) {
            const std::string zone_path = path + ".deployment";
            auto refuse = [&result, &zone_path, &zone]() {
                result.diagnostics.push_back(
                    {DiagnosticCode::invalid_deployment, zone_path, zone.id}
                );
            };
            std::set<std::pair<std::int16_t, std::int16_t>> zone_tiles;
            // A deployment says at least one of the two things it can say. One
            // that says neither is an author who wrote the object and then
            // wrote nothing in it, and reading it as "no phase and no cap"
            // would silently drop the field they typed.
            bool sound = zone.id != 0 &&
                (!zone.tiles.empty() || zone.capacity != 0);
            // A cap no company could ever exceed refuses nothing, ever. The
            // board has as many first-side placements as it has, and a cap at
            // or above that number is a knob that cannot turn, which is an
            // author's mistake and not a permissive setting.
            if (zone.capacity != 0) {
                std::size_t first_side_placements = 0;
                for (const Placement& placement : definition.placements) {
                    if (placement.side == EncounterSide::first) {
                        ++first_side_placements;
                    }
                }
                if (static_cast<std::size_t>(zone.capacity) >=
                    first_side_placements) {
                    sound = false;
                }
            }
            for (const PatrolPoint& tile : zone.tiles) {
                if (!zone_tiles.emplace(tile.x, tile.y).second) {
                    sound = false;
                    continue;
                }
                if (found_map == maps_by_id.end()) continue;
                const Map& map = *found_map->second;
                if (tile.x < 0 || tile.y < 0 ||
                    tile.x >= static_cast<std::int16_t>(map.width) ||
                    tile.y >= static_cast<std::int16_t>(map.height)) {
                    sound = false;
                }
            }
            // Everybody the region arranges must be able to stand on every one
            // of its tiles, because the phase offers each of them every tile
            // and `Encounter::apply` deliberately keeps no second copy of the
            // terrain rule. A region straddling a river is therefore refused
            // here, where the author is, rather than becoming a tile that is
            // lit for the flier and refused for the walker.
            std::uint8_t arranged_crossings = crossing_every;
            bool anybody_inside = false;
            for (const Placement& placement : definition.placements) {
                if (placement.side != EncounterSide::first) continue;
                if (zone_tiles.count({placement.x, placement.y}) == 0) continue;
                anybody_inside = true;
                std::uint8_t crossings = crossing_none;
                const auto found_unit_type =
                    unit_types_by_id.find(placement.unit_type_id);
                if (found_unit_type != unit_types_by_id.end()) {
                    const auto found_placed_class =
                        classes_by_id.find(found_unit_type->second->class_id);
                    if (found_placed_class != classes_by_id.end()) {
                        crossings = found_placed_class->second->crossings;
                    }
                }
                arranged_crossings =
                    static_cast<std::uint8_t>(arranged_crossings & crossings);
            }
            if (found_map != maps_by_id.end() && anybody_inside) {
                const Map& map = *found_map->second;
                for (const auto& tile : zone_tiles) {
                    if (tile.first < 0 || tile.second < 0 ||
                        tile.first >= static_cast<std::int16_t>(map.width) ||
                        tile.second >= static_cast<std::int16_t>(map.height)) {
                        continue;
                    }
                    const std::size_t cell =
                        static_cast<std::size_t>(tile.second) * map.width +
                        static_cast<std::size_t>(tile.first);
                    if (cell < map.terrain_kinds.size() &&
                        !crossing_admits(
                            terrain_passability(map.terrain_kinds[cell]),
                            arranged_crossings
                        )) {
                        sound = false;
                    }
                }
            }
            // A region no first-side placement stands inside is a region
            // nobody can be arranged in: the phase would open with nothing to
            // do and the author would never learn why. A deployment that
            // states only a capacity states no region, so there is nobody to
            // be inside one and nothing here to answer.
            if (!sound || (!zone.tiles.empty() && !anybody_inside)) refuse();
        }
    }
    // Who a scene says its speakers are. The join from a speaker string to a
    // cast entry was made by the source reader, which is the component holding
    // both lists; what is left here is the one check that needs the rest of
    // the project: that the unit type a scene casts is a unit type this
    // project has. Presentation only, and checked all the same: a portrait
    // resolved through a unit type nothing declares would draw a client's
    // fallback and never say why.
    for (const Dialogue& definition : source.dialogues) {
        const std::string path =
            "dialogues[" + std::to_string(definition.id) + "]";
        for (std::size_t index = 0; index < definition.cast.size(); ++index) {
            validate_reference(
                definition.cast[index].unit_type_id, unit_type_ids,
                path + ".cast[" + std::to_string(index) + "].unit_type_id",
                result.diagnostics
            );
        }
        // What the record's own widths admit. A line's words are the one text
        // in the whole package that never passes through `validate_identity`,
        // and a cast is counted in a single byte because a line names its
        // speaker in one.
        validate_count(
            definition.lines.size(), count_limit_u16, path + ".lines",
            definition.id, result.diagnostics
        );
        validate_count(
            definition.cast.size(), count_limit_u8, path + ".cast",
            definition.id, result.diagnostics
        );
        for (std::size_t index = 0; index < definition.lines.size(); ++index) {
            const std::string line_path =
                path + ".lines[" + std::to_string(index) + "]";
            validate_text(
                definition.lines[index].speaker, line_path + ".speaker",
                definition.id, result.diagnostics
            );
            validate_text(
                definition.lines[index].text, line_path + ".text",
                definition.id, result.diagnostics
            );
        }
    }
    std::map<StableId, const Encounter*> encounters_by_id;
    for (const Encounter& definition : source.encounters) {
        encounters_by_id.emplace(definition.id, &definition);
    }
    for (const Campaign& definition : source.campaigns) {
        const std::string path =
            "campaigns[" + std::to_string(definition.id) + "]";
        // What the campaign record's own widths admit: the flow, the company,
        // and what the company is granted. The specificity tail is counted out
        // of the roster, so bounding the roster bounds it too.
        validate_count(
            definition.nodes.size(), count_limit_u16, path + ".nodes",
            definition.id, result.diagnostics
        );
        validate_count(
            definition.roster.size(), count_limit_u16, path + ".roster",
            definition.id, result.diagnostics
        );
        validate_count(
            definition.grants.size(), count_limit_u16, path + ".grants",
            definition.id, result.diagnostics
        );
        // Who this campaign can ever hold, and what each of them is. Built
        // before the nodes are walked because a node's placements are checked
        // against it, and a node's recruits are part of it.
        std::map<StableId, const CampaignMember*> members_by_id;
        std::size_t founding_members = 0;
        for (const CampaignMember& member : definition.roster) {
            const std::string member_path = path + ".roster";
            if (member.id == 0 ||
                !members_by_id.emplace(member.id, &member).second) {
                result.diagnostics.push_back(
                    {DiagnosticCode::duplicate_id, member_path, member.id}
                );
                continue;
            }
            if (member.name.empty()) {
                result.diagnostics.push_back(
                    {DiagnosticCode::missing_name, member_path, member.id}
                );
            }
            // A member's name is written into the campaign record directly
            // rather than through `validate_identity`, so its width is bounded
            // here or nowhere.
            validate_text(
                member.name, member_path + ".name", member.id,
                result.diagnostics
            );
            validate_reference(
                member.unit_type_id, unit_type_ids,
                member_path + ".unit_type_id", result.diagnostics
            );
            validate_specificity(
                member, unit_types_by_id, classes_by_id, member_path,
                result.diagnostics
            );
            if (member.join_node_id == 0) ++founding_members;
        }
        // A campaign nobody can be founded from: no roster at all, or a roster
        // whose every member joins later. Both leave a company somebody would
        // otherwise have to invent, and inventing one is exactly what this
        // compiler declines to do.
        if (founding_members == 0) {
            result.diagnostics.push_back(
                {DiagnosticCode::empty_roster, path + ".roster", definition.id}
            );
        }
        std::set<StableId> node_ids;
        for (const CampaignNode& node : definition.nodes) {
            if (node.id == 0 || !node_ids.insert(node.id).second) {
                result.diagnostics.push_back(
                    {DiagnosticCode::duplicate_id, path + ".nodes", node.id}
                );
            }
            if (node.kind == CampaignNodeKind::encounter) {
                validate_reference(
                    node.encounter_id, encounter_ids,
                    path + ".nodes.encounter_id", result.diagnostics
                );
            }
        }
        validate_reference(
            definition.entry_node_id, node_ids, path + ".entry_node_id",
            result.diagnostics
        );
        // A member who joins along the way joins at a node this campaign
        // holds. Checked once, here, rather than once per node.
        for (const CampaignMember& member : definition.roster) {
            if (member.join_node_id == 0) continue;
            validate_reference(
                member.join_node_id, node_ids, path + ".roster.join_node_id",
                result.diagnostics
            );
        }
        // What this campaign puts in its store by authoring. Each list (the
        // founding stock and each node's grants) is its own namespace, so an
        // item granted at the abbey and again at the ford is two grants and not
        // a duplicate, while one list naming an item twice is an author
        // answering "how many" twice with two different numbers.
        std::set<std::pair<StableId, StableId>> granted;
        for (const CampaignItemGrant& grant : definition.grants) {
            const std::string grant_path =
                grant.join_node_id == 0 ? path + ".starting_store"
                                        : path + ".nodes.grants";
            if (grant.item_id == 0 || grant.quantity == 0 ||
                !granted.emplace(grant.join_node_id, grant.item_id).second) {
                result.diagnostics.push_back(
                    {DiagnosticCode::invalid_grant, grant_path, grant.item_id}
                );
                continue;
            }
            validate_reference(
                grant.item_id, item_ids, grant_path + ".item_id",
                result.diagnostics
            );
            if (grant.join_node_id != 0) {
                validate_reference(
                    grant.join_node_id, node_ids,
                    grant_path + ".join_node_id", result.diagnostics
                );
            }
        }
        for (const CampaignNode& node : definition.nodes) {
            validate_references(
                node.unconditional_targets, node_ids,
                path + ".nodes.targets", result.diagnostics
            );
            // The scenes this node presents when it is entered. Checked on
            // exactly the terms every other reference is, and for the reason
            // the dialogue cast's unit types are: a scene the project does not
            // hold is not a scene played badly, it is a scene that never plays
            // at all, and a campaign that silently skips a cutscene tells its
            // author nothing.
            validate_references(
                node.dialogue_ids, dialogue_ids, path + ".nodes.dialogue_ids",
                result.diagnostics
            );
            validate_count(
                node.conditional_targets.size(), count_limit_u16,
                path + ".nodes.transitions", node.id, result.diagnostics
            );
            // At most one conditionless fallback, which is what the runtime
            // will accept: `campaign::load_campaign` refuses a second with
            // `unsupported_flow`, because more than one would make the taken
            // edge depend on authoring order.
            if (node.unconditional_targets.size() > 1) {
                result.diagnostics.push_back(
                    {
                        DiagnosticCode::invalid_transition,
                        path + ".nodes.targets",
                        node.id
                    }
                );
            }
            // And no two conditional transitions may share a priority. The
            // lowest matching value wins independently of array order, and two
            // transitions holding one value make that untrue: the runtime
            // stable-sorts, so the winner becomes whichever was written first.
            // Nothing downstream can detect this, which is why it is refused
            // here.
            std::set<std::uint16_t> priorities;
            for (const CampaignConditionalTarget& branch :
                 node.conditional_targets) {
                if (!priorities.insert(branch.priority).second) {
                    result.diagnostics.push_back(
                        {
                            DiagnosticCode::invalid_transition,
                            path + ".nodes.transitions.priority",
                            node.id
                        }
                    );
                }
            }
            for (const CampaignConditionalTarget& branch :
                 node.conditional_targets) {
                validate_reference(
                    branch.target_id, node_ids,
                    path + ".nodes.transitions.target_id", result.diagnostics
                );
                validate_count(
                    branch.predicates.size(), count_limit_u16,
                    path + ".nodes.transitions.when", node.id,
                    result.diagnostics
                );
                for (const CampaignPredicate& predicate : branch.predicates) {
                    // A world flag names no record, so there is nothing to
                    // resolve it against: the key *is* the identity. Only an
                    // objective predicate has a reference to check.
                    if (predicate.kind !=
                        CampaignPredicateKind::objective_result) {
                        continue;
                    }
                    validate_reference(
                        predicate.subject, objective_ids,
                        path + ".nodes.transitions.when.objective_id",
                        result.diagnostics
                    );
                }
            }
            if (node.kind != CampaignNodeKind::encounter) continue;
            const auto found = encounters_by_id.find(node.encounter_id);
            if (found == encounters_by_id.end()) continue;
            // Who stands on this board, in both directions: every first-side
            // placement fields exactly one member the campaign holds, no
            // second-side placement fields any, and no member is asked to
            // stand in two places at once.
            std::set<StableId> fielded;
            for (const Placement& placement : found->second->placements) {
                const std::string placement_path = path + ".nodes.placements";
                const auto member = members_by_id.find(placement.member_id);
                if (placement.side == EncounterSide::second) {
                    if (placement.member_id != 0) {
                        result.diagnostics.push_back(
                            {
                                DiagnosticCode::invalid_member, placement_path,
                                placement.member_id
                            }
                        );
                    }
                    continue;
                }
                if (placement.member_id == 0 ||
                    member == members_by_id.end() ||
                    !fielded.insert(placement.member_id).second ||
                    member->second->unit_type_id != placement.unit_type_id) {
                    result.diagnostics.push_back(
                        {
                            DiagnosticCode::invalid_member, placement_path,
                            placement.id
                        }
                    );
                }
            }
        }
    }

    if (!result.diagnostics.empty()) {
        return result;
    }

    package_format::PackageSource package;
    package.game_id = source.game_id;
    package.content_revision = source.content_revision;
    package.required_engine = source.required_engine;
    package.target = source.target;
    package.required_features = source.required_features;
    package.sections = {
        package_format::SectionSource{
            package_format::SectionType::manifest,
            1,
            0,
            package_format::section_flag_required,
            {
                package_format::RecordSource{
                    package_format::manifest_title_record_id,
                    [&source] {
                        Bytes bytes;
                        put_string(bytes, source.title);
                        return bytes;
                    }()
                }
            }
        },
        // Presentation: the choices an author made about how the game looks
        // and what its content looks like, in one optional section so that a
        // reader which does not know it skips it and a package written before
        // it existed still loads. Everything here is written resolved (a
        // faction's colour, a cell's terrain kind, a unit type's archetype),
        // so no presenter re-derives a rule. Nothing here is read by a rule.
        //
        // Three records, each addressed by an identity the format owns. They
        // are independent: a reader that knows only the first finds it by its
        // identity and is unaffected by the two beside it.
        package_format::SectionSource{
            package_format::SectionType::presentation,
            1,
            0,
            0,
            {
                package_format::RecordSource{
                    package_format::presentation_record_id,
                    [&source] {
                        Bytes bytes;
                        bytes.push_back(source.theme);
                        put_u16(
                            bytes,
                            static_cast<std::uint16_t>(source.factions.size())
                        );
                        for (std::size_t index = 0;
                             index < source.factions.size(); ++index) {
                            const Faction& faction = source.factions[index];
                            put_u64(bytes, faction.id);
                            bytes.push_back(
                                resolved_faction_colour(faction, index)
                            );
                        }
                        return bytes;
                    }()
                },
                // Terrain identity to art-library terrain kind. A cell's
                // identity is a hash of the authored name, so without this a
                // client can tell two cells apart and can draw neither.
                package_format::RecordSource{
                    package_format::presentation_terrain_record_id,
                    [&terrain_kinds] {
                        Bytes bytes;
                        put_u32(
                            bytes,
                            static_cast<std::uint32_t>(terrain_kinds.size())
                        );
                        // `std::map` walks ascending, which is the order the
                        // reader requires and the order a client searches.
                        for (const auto& entry : terrain_kinds) {
                            put_u64(bytes, entry.first);
                            bytes.push_back(entry.second);
                        }
                        return bytes;
                    }()
                },
                // Unit type identity to art-library archetype. The keyword
                // convention runs over class and unit type names, which a
                // package does carry, but running it is authoring semantics
                // and belongs here rather than in every renderer.
                package_format::RecordSource{
                    package_format::presentation_archetype_record_id,
                    [&source] {
                        std::map<StableId, std::uint8_t> archetypes;
                        for (const UnitType& unit_type : source.unit_types) {
                            archetypes.emplace(
                                unit_type.id,
                                resolved_archetype(source, unit_type)
                            );
                        }
                        Bytes bytes;
                        put_u32(
                            bytes,
                            static_cast<std::uint32_t>(archetypes.size())
                        );
                        for (const auto& entry : archetypes) {
                            put_u64(bytes, entry.first);
                            bytes.push_back(entry.second);
                        }
                        return bytes;
                    }()
                }
            }
        },
        make_section(
            package_format::SectionType::weapon_types,
            source.weapon_types,
            [](const WeaponType& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::item_types,
            source.item_types,
            [](const ItemType& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::classes,
            source.classes,
            [](const UnitClass& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_stats(bytes, value.base_stats);
                bytes.push_back(value.acts_after_attacking ? 1U : 0U);
                put_ids(bytes, value.allowed_weapon_types);
                // Appended after the weapon types a class may hold, so a
                // package written before classes could cross anything still
                // reads: a record that ends here is a walker.
                bytes.push_back(value.crossings);
                // And appended again, after the crossings byte, for the same
                // reason: a record that ends here is a class from a package
                // written before a blow could be dodged, and all four of these
                // are zero for it. Written in `GrowableStat` order so the one
                // list this repository keeps of them is the one list.
                put_i16(bytes, value.base_stats.skill);
                put_i16(bytes, value.base_stats.luck);
                put_i16(bytes, value.base_stats.evasion);
                put_i16(bytes, value.base_stats.magic);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::weapons,
            source.weapons,
            [](const Weapon& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u64(bytes, value.type_id);
                put_i16(bytes, value.power);
                bytes.push_back(value.minimum_range);
                bytes.push_back(value.maximum_range);
                // Appended after the band, so a package written before an
                // attack could miss still reads: a record that ends here is a
                // weapon that always lands, which is exactly what it was.
                bytes.push_back(value.accuracy);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::items,
            source.items,
            [](const Item& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u64(bytes, value.type_id);
                put_u16(bytes, value.stack_limit);
                // What spending it does, appended after the stack limit, so a
                // package written before an item could be spent still reads: a
                // record that ends here is an item nothing can do anything
                // with, which is exactly what it was.
                bytes.push_back(static_cast<std::uint8_t>(value.kind));
                put_i16(bytes, value.power);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::unit_types,
            source.unit_types,
            [](const UnitType& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u64(bytes, value.class_id);
                put_u64(bytes, value.faction_id);
                put_ids(bytes, value.starting_weapons);
                put_ids(bytes, value.starting_items);
                put_ids(bytes, value.abilities);
                // Appended last, for the reason a weapon's accuracy was: a
                // reader that stops after the ability list still reads every
                // field it knew about, so a build older than growth decodes a
                // package written with it and sees the unit type it always saw.
                put_u16(bytes, value.experience_award);
                put_u16(bytes, value.experience_per_level);
                for (std::size_t index = 0; index < growable_stat_count;
                     ++index) {
                    bytes.push_back(value.growth.chance[index]);
                }
                // And the drop after that, for the same reason again: a record
                // that ends before these nine bytes is a unit type from a
                // package written before a defeated character left anything
                // behind, and zero is exactly what "leaves nothing" means. It
                // is written whether or not anything was authored, because the
                // convention this section already keeps is that a tail is a
                // fixed shape once it exists: a reader discriminates on the
                // block's length, not on a flag.
                put_u64(bytes, value.drop_item);
                bytes.push_back(value.drop_chance);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::maps,
            source.maps,
            [](const Map& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u16(bytes, value.width);
                put_u16(bytes, value.height);
                put_u32(bytes, static_cast<std::uint32_t>(value.terrain.size()));
                for (StableId terrain : value.terrain) put_u64(bytes, terrain);
                // What each cell asks of whoever stands in it and what it
                // charges them, one byte per cell each, appended after the
                // identities in that order. A map built in memory with no kinds
                // resolved carries neither, and reads as open ground where
                // every step costs one, the same answer a package written
                // before either field gives.
                //
                // The two blocks are written together or not at all, so a
                // reader tells the three shapes apart by what is left: nothing,
                // one byte per cell, or two. A package carrying only the first
                // is one written before ground had a price, and it still means
                // what it meant.
                if (value.terrain_kinds.size() == value.terrain.size()) {
                    for (std::uint8_t kind : value.terrain_kinds) {
                        bytes.push_back(terrain_passability(kind));
                    }
                    for (std::uint8_t kind : value.terrain_kinds) {
                        bytes.push_back(terrain_movement_cost(kind));
                    }
                }
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::factions,
            source.factions,
            [](const Faction& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::objectives,
            source.objectives,
            [](const Objective& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                bytes.push_back(static_cast<std::uint8_t>(value.kind));
                bytes.push_back(static_cast<std::uint8_t>(value.side));
                put_u64(bytes, value.target_placement_id);
                // The count, as a tail written only for the kind that can read
                // one. Every objective of every other kind therefore writes the
                // bytes it always wrote, and a reader that stops where it
                // always stopped reads exactly the objective it always did.
                if (value.kind == ObjectiveKind::survive_rounds) {
                    put_u16(bytes, value.rounds);
                }
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::abilities,
            source.abilities,
            [](const Ability& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                bytes.push_back(static_cast<std::uint8_t>(value.kind));
                bytes.push_back(static_cast<std::uint8_t>(value.damage_type));
                bytes.push_back(static_cast<std::uint8_t>(value.area));
                put_i16(bytes, value.power);
                bytes.push_back(value.minimum_range);
                bytes.push_back(value.maximum_range);
                bytes.push_back(value.radius);
                // Appended last, for the reason a weapon's accuracy is: an
                // ability record that ends before it always lands.
                bytes.push_back(value.accuracy);
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::dialogue,
            source.dialogues,
            [](const Dialogue& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u16(bytes, static_cast<std::uint16_t>(value.lines.size()));
                for (const DialogueLine& line : value.lines) {
                    put_string(bytes, line.speaker);
                    put_string(bytes, line.text);
                }
                // Two optional tails, and the shape of them is what keeps
                // every older package byte-identical.
                //
                // A scene that casts nobody writes what it wrote before a
                // cast existed: the backdrop byte, and only when it names a
                // backdrop. That is nothing at all for a scene naming
                // neither, which is every scene in every package written
                // before either field did.
                //
                // A scene that casts somebody writes the longer tail: the
                // backdrop byte unconditionally, zero for a scene naming none
                // and legal only here, then the cast, then one
                // byte per line saying which entry speaks it. The reader
                // consumes a record to its exact end and tells the three
                // cases apart by how much is left: nothing, exactly one byte,
                // or more than one. No flag, no version, no section.
                if (value.cast.empty()) {
                    if (value.backdrop != 0) bytes.push_back(value.backdrop);
                    return bytes;
                }
                bytes.push_back(value.backdrop);
                bytes.push_back(
                    static_cast<std::uint8_t>(value.cast.size())
                );
                for (const DialogueCastEntry& entry : value.cast) {
                    put_u64(bytes, entry.unit_type_id);
                }
                for (const DialogueLine& line : value.lines) {
                    bytes.push_back(line.cast_entry);
                }
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::encounters,
            source.encounters,
            [](const Encounter& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u64(bytes, value.map_id);
                bytes.push_back(static_cast<std::uint8_t>(value.turn_order));
                put_ids(bytes, value.objective_ids);
                put_u16(
                    bytes, static_cast<std::uint16_t>(value.placements.size())
                );
                for (const Placement& placement : value.placements) {
                    put_u64(bytes, placement.id);
                    put_u64(bytes, placement.source_key_id);
                    put_u64(bytes, placement.unit_type_id);
                    bytes.push_back(
                        static_cast<std::uint8_t>(placement.side)
                    );
                    put_i16(bytes, placement.x);
                    put_i16(bytes, placement.y);
                    bytes.push_back(
                        static_cast<std::uint8_t>(placement.behavior)
                    );
                    put_u16(
                        bytes,
                        static_cast<std::uint16_t>(placement.patrol.size())
                    );
                    for (const PatrolPoint& point : placement.patrol) {
                        put_i16(bytes, point.x);
                        put_i16(bytes, point.y);
                    }
                }
                // The deployment region, as a tail: absent from every record
                // written before troops could be arranged, and absent from
                // every encounter that authors none, so a reader that stops
                // after the placements reads exactly the encounter it always
                // did. Written only when there is a region, which is what
                // makes "no region" and "a package from before regions" the
                // same bytes and therefore the same meaning.
                //
                // The capacity is a further tail inside it, written only when
                // there is one, so an encounter that states a region and no cap
                // writes exactly the bytes it wrote before caps existed. A
                // deployment that states only a cap therefore writes a tile
                // count of zero, which is malformed on its own and is why the
                // reader decides after it has looked for the capacity.
                if (!value.deployment.tiles.empty() ||
                    value.deployment.capacity != 0) {
                    put_u64(bytes, value.deployment.id);
                    put_u16(
                        bytes,
                        static_cast<std::uint16_t>(
                            value.deployment.tiles.size()
                        )
                    );
                    for (const PatrolPoint& tile : value.deployment.tiles) {
                        put_i16(bytes, tile.x);
                        put_i16(bytes, tile.y);
                    }
                    if (value.deployment.capacity != 0) {
                        put_u16(bytes, value.deployment.capacity);
                    }
                }
                return bytes;
            }
        ),
        make_section(
            package_format::SectionType::campaigns,
            source.campaigns,
            [&source](const Campaign& value) {
                Bytes bytes;
                put_string(bytes, value.name);
                put_u64(bytes, value.entry_node_id);
                put_u16(bytes, static_cast<std::uint16_t>(value.nodes.size()));
                for (const CampaignNode& node : value.nodes) {
                    put_u64(bytes, node.id);
                    bytes.push_back(static_cast<std::uint8_t>(node.kind));
                    put_u64(bytes, node.encounter_id);
                    put_ids(bytes, node.dialogue_ids);
                    put_ids(bytes, node.unconditional_targets);
                    put_u16(
                        bytes,
                        static_cast<std::uint16_t>(
                            node.conditional_targets.size()
                        )
                    );
                    for (const CampaignConditionalTarget& branch :
                         node.conditional_targets) {
                        put_u64(bytes, branch.target_id);
                        put_u16(bytes, branch.priority);
                        bytes.push_back(
                            static_cast<std::uint8_t>(branch.combinator)
                        );
                        put_u16(
                            bytes,
                            static_cast<std::uint16_t>(
                                branch.predicates.size()
                            )
                        );
                        for (const CampaignPredicate& predicate :
                             branch.predicates) {
                            // The subject, then one byte that is either the
                            // objective's outcome or a kind above the two codes
                            // an outcome can take. A campaign whose every
                            // predicate asks about an objective therefore
                            // spends no byte on a tag at all.
                            put_u64(bytes, predicate.subject);
                            if (predicate.kind ==
                                CampaignPredicateKind::world_flag_equals) {
                                bytes.push_back(static_cast<std::uint8_t>(
                                    CampaignPredicateKind::world_flag_equals
                                ));
                                // The tail, written only for the new kind, so
                                // it costs nothing to a package that does not
                                // use it.
                                bytes.push_back(predicate.value_type);
                                put_u64(
                                    bytes,
                                    static_cast<std::uint64_t>(predicate.value)
                                );
                                continue;
                            }
                            bytes.push_back(
                                static_cast<std::uint8_t>(predicate.result)
                            );
                        }
                    }
                }
                // The company, after the flow because it was learned after the
                // flow: a reader that stops at the last node reads exactly the
                // campaign it always did, and one that carries on reads who
                // plays it. Founding members carry a join node of zero.
                put_u16(
                    bytes, static_cast<std::uint16_t>(value.roster.size())
                );
                for (const CampaignMember& member : value.roster) {
                    put_u64(bytes, member.id);
                    put_string(bytes, member.name);
                    put_u64(bytes, member.unit_type_id);
                    put_u64(bytes, member.join_node_id);
                }
                // What the campaign puts in its store by authoring, after the
                // company for the same reason the company came after the flow.
                // Written only when there is any, so a campaign that grants
                // nothing writes exactly the bytes it wrote before grants
                // existed. A grant carrying a join node of zero is the founding
                // stock, on the same convention a founding member carries.
                //
                // Written also when nobody grants anything but somebody is
                // written to be more than their class, because the tail after
                // this one is positional and needs this one's place held. A
                // count of zero reads back as no grants, which is the same
                // campaign either way; what it must not do is let the reader
                // mistake a specificity count for a grant count.
                std::size_t specific_members = 0;
                for (const CampaignMember& member : value.roster) {
                    if (!member.specificity.empty()) ++specific_members;
                }
                // Whether the project said anything at all about what a fall
                // costs. The two settings share one answer because they share
                // one tail: writing either means writing both bytes, and a
                // project that states neither must write neither, which is the
                // whole of the byte-identity claim below.
                //
                // Note what is *not* asked here. A project stating
                // `characterLoss: "permanent"` in so many words states the
                // value an absent rule already means, so it writes no tail and
                // compiles to the bytes it always did, the same courtesy the
                // turn order extends to a board that authors alternating.
                const bool states_a_loss_rule =
                    source.character_loss != CharacterLoss::permanent ||
                    source.invulnerable_for_testing;
                if (!value.grants.empty() || specific_members != 0 ||
                    states_a_loss_rule) {
                    put_u16(
                        bytes, static_cast<std::uint16_t>(value.grants.size())
                    );
                    for (const CampaignItemGrant& grant : value.grants) {
                        put_u64(bytes, grant.join_node_id);
                        put_u64(bytes, grant.item_id);
                        put_u32(bytes, grant.quantity);
                    }
                }
                // What the author made of individual characters, after the
                // store for the same reason the store came after the company.
                // Written only when somebody is written to be more than their
                // class, so a campaign whose members are all exactly their unit
                // types writes not one byte of this and is the campaign it
                // always was, which is what every golden depends on.
                //
                // Sparse: only the stats an author named are written, in
                // `SpecificStat` order. A stated zero never reaches here,
                // because the compiler refused it, so every delta written is a
                // delta that changes something and the decoder can read a
                // second entry for one stat as the malformed record it is.
                //
                // The founding roster and every node's recruits are one loop,
                // because they are one table: a recruit is a member of the
                // company from the moment they join.
                //
                // Written also when nobody is written to be anything but their
                // class and the project stated a loss rule, for the reason the
                // store's count is written above when nobody grants anything:
                // the tail after this one is positional and needs this one's
                // place held. A count of zero reads back as nobody authored,
                // which is the same campaign either way.
                if (specific_members != 0 || states_a_loss_rule) {
                    put_u16(
                        bytes, static_cast<std::uint16_t>(specific_members)
                    );
                    for (const CampaignMember& member : value.roster) {
                        const CampaignMemberSpecificity& specificity =
                            member.specificity;
                        if (specificity.empty()) continue;
                        put_u64(bytes, member.id);
                        std::uint8_t written = 0;
                        for (std::size_t index = 0;
                             index < specific_stat_count; ++index) {
                            if (specificity.stat_deltas[index] != 0) ++written;
                        }
                        bytes.push_back(written);
                        for (std::size_t index = 0;
                             index < specific_stat_count; ++index) {
                            const std::int16_t delta =
                                specificity.stat_deltas[index];
                            if (delta == 0) continue;
                            bytes.push_back(static_cast<std::uint8_t>(index));
                            put_i16(bytes, delta);
                        }
                        bytes.push_back(specificity.reach_bonus);
                    }
                }
                // What a fall costs the company, and whether the company can
                // fall at all. Two bytes, after the characters for the same
                // reason the characters came after the store.
                //
                // These are the project's words rather than the campaign's,
                // and they are written on every campaign a project holds. That
                // is not a duplication to be tidied away: a package is read one
                // campaign at a time by runtimes that never see a project, so
                // the rule has to be where the company is. The same reasoning
                // puts each encounter's resolved turn order on the encounter.
                //
                // Written only when the project stated something, so a project
                // that states neither setting produces the campaign records it
                // always produced, down to the last byte, and every golden and
                // every console expectation built on them still holds.
                if (states_a_loss_rule) {
                    bytes.push_back(
                        static_cast<std::uint8_t>(source.character_loss)
                    );
                    bytes.push_back(source.invulnerable_for_testing ? 1U : 0U);
                }
                return bytes;
            }
        ),
        // Who can be talked to, per encounter. Built by hand rather than
        // through `make_section` because the record is not one definition but
        // the talkable subset of one encounter's placements.
        //
        // **A record exists only for an encounter somebody is talkable on, and
        // the section only when at least one does.** The empty-section pruning
        // below then removes it entirely, so a project where nobody authors a
        // `talk` produces a package with no directory entry for this at all,
        // which is the byte-identity claim, made total rather than argued.
        //
        // Required, deliberately. A runtime that skipped this section would
        // place the false enemy as an ordinary enemy, offer no talk, and play a
        // different game whose campaign flag never rises. A silent divergence
        // is refused rather than dropped, so a runtime that cannot read this
        // section declines the package with `unsupported_required_section`.
        [&source]() {
            package_format::SectionSource section;
            section.type = package_format::SectionType::talks;
            std::vector<const Encounter*> ordered;
            for (const Encounter& encounter : source.encounters) {
                bool any = false;
                for (const Placement& placement : encounter.placements) {
                    any = any || placement.talk_flag_id != 0;
                }
                if (any) ordered.push_back(&encounter);
            }
            std::sort(
                ordered.begin(),
                ordered.end(),
                [](const Encounter* lhs, const Encounter* rhs) {
                    return lhs->id < rhs->id;
                }
            );
            for (const Encounter* encounter : ordered) {
                Bytes bytes;
                std::uint16_t count = 0;
                for (const Placement& placement : encounter->placements) {
                    if (placement.talk_flag_id != 0) ++count;
                }
                put_u16(bytes, count);
                for (const Placement& placement : encounter->placements) {
                    if (placement.talk_flag_id == 0) continue;
                    put_u64(bytes, placement.id);
                    put_u64(bytes, placement.talk_flag_id);
                }
                section.records.push_back({encounter->id, std::move(bytes)});
            }
            return section;
        }(),
        // What is said during a battle, per encounter. Built by hand and pruned
        // for the talks section's reasons, and optional where that one is
        // required: a runtime that skips this plays the same battle without the
        // words, which is a quieter game rather than a different one.
        //
        // The placement a moment is about is written as the author's own
        // placement identity, not as a unit identifier: the latter is derived
        // per encounter and is a different number every time the character
        // appears, which is the same reason a talk's flag is authored.
        [&source]() {
            package_format::SectionSource section;
            section.type = package_format::SectionType::moments;
            // Optional, and said here because the default is required: a
            // runtime that cannot read this plays the same battle in silence.
            section.flags = 0;
            std::vector<const Encounter*> ordered;
            for (const Encounter& encounter : source.encounters) {
                if (!encounter.moments.empty()) ordered.push_back(&encounter);
            }
            std::sort(
                ordered.begin(),
                ordered.end(),
                [](const Encounter* lhs, const Encounter* rhs) {
                    return lhs->id < rhs->id;
                }
            );
            for (const Encounter* encounter : ordered) {
                Bytes bytes;
                put_u16(
                    bytes, static_cast<std::uint16_t>(encounter->moments.size())
                );
                for (const Moment& moment : encounter->moments) {
                    put_u64(bytes, moment.id);
                    bytes.push_back(static_cast<std::uint8_t>(moment.trigger));
                    put_u64(bytes, moment.placement_id);
                    put_u64(bytes, moment.dialogue_id);
                }
                section.records.push_back({encounter->id, std::move(bytes)});
            }
            return section;
        }(),
        // When a placement comes in, per encounter. Built by hand for the
        // reason the talks section is, and pruned the same way: a record exists
        // only for an encounter something arrives on, and the section only when
        // at least one does, so a project with no waves produces a package with
        // no directory entry for this at all.
        //
        // Required, deliberately and for the talks section's exact reason. A
        // runtime that skipped it would stand every wave on the board at the
        // opening and play a different battle, and a survive map would be a
        // brawl. A silent divergence is refused rather than dropped.
        [&source]() {
            package_format::SectionSource section;
            section.type = package_format::SectionType::arrivals;
            std::vector<const Encounter*> ordered;
            for (const Encounter& encounter : source.encounters) {
                bool any = false;
                for (const Placement& placement : encounter.placements) {
                    any = any || placement.arrival_round != 0;
                }
                if (any) ordered.push_back(&encounter);
            }
            std::sort(
                ordered.begin(),
                ordered.end(),
                [](const Encounter* lhs, const Encounter* rhs) {
                    return lhs->id < rhs->id;
                }
            );
            for (const Encounter* encounter : ordered) {
                Bytes bytes;
                std::uint16_t count = 0;
                for (const Placement& placement : encounter->placements) {
                    if (placement.arrival_round != 0) ++count;
                }
                put_u16(bytes, count);
                for (const Placement& placement : encounter->placements) {
                    if (placement.arrival_round == 0) continue;
                    put_u64(bytes, placement.id);
                    put_u16(bytes, placement.arrival_round);
                    put_u16(bytes, placement.arrival_every);
                    put_u16(bytes, placement.arrival_times);
                }
                section.records.push_back({encounter->id, std::move(bytes)});
            }
            return section;
        }(),
        // What the author called a placement, one record each, keyed by the
        // placement's own identity, which is already namespaced by its
        // encounter, so a record needs no enclosing list and no encounter
        // beside it. The record is exactly a length-prefixed string in the
        // leading position every named section writes one in, so
        // `package_runtime::content_name` reads it with no new decoder.
        //
        // Pruned the way `talks` is: a record exists only for a placement
        // somebody named, and the section only when at least one does, so a
        // project where nobody names a placement produces a package with no
        // directory entry for this at all.
        //
        // **Optional**, where `talks` and `arrivals` are required, and the
        // difference is what a name is. A runtime that skipped a talk or a wave
        // would play a different battle; a runtime that skips this draws the
        // same battle under a derived name. Nothing here is read by a rule.
        [&source]() {
            package_format::SectionSource section;
            section.type = package_format::SectionType::placement_names;
            section.flags = 0;
            std::vector<const Placement*> named;
            for (const Encounter& encounter : source.encounters) {
                for (const Placement& placement : encounter.placements) {
                    if (!placement.name.empty()) named.push_back(&placement);
                }
            }
            std::sort(
                named.begin(),
                named.end(),
                [](const Placement* lhs, const Placement* rhs) {
                    return lhs->id < rhs->id;
                }
            );
            for (const Placement* placement : named) {
                Bytes bytes;
                put_string(bytes, placement->name);
                section.records.push_back({placement->id, std::move(bytes)});
            }
            return section;
        }()
    };
    // Which of the art library's styles each character is drawn in and which
    // body it is drawn with, appended to the presentation section beside the
    // archetype join they complete: that record says which role a unit type
    // wears, and these say whose hand drew it and at what build.
    //
    // Appended here rather than written inline above, and written at all only
    // when some character names a style of its own, because the absence of the
    // record is itself the statement: *every character follows the game's
    // style*. It is the statement every project written before this field
    // existed makes. A game where nobody overrides emits no record, no
    // directory entry and no byte, so it compiles byte-identical.
    //
    // When it is written it is written whole: every unit type, resolved, in
    // the same shape the archetype record uses. A reader then needs nothing
    // but this record to answer the question, and in particular does not need
    // the project's own style, which no package has ever carried.
    if (draws_any_style_override(source) || draws_any_figure_override(source)) {
        // One join per axis, each written only if its own axis was authored,
        // and each written whole when it is. The shape is the archetype
        // record's: a count, then identities ascending with one resolved byte
        // apiece, so the reader that already walks that record walks these.
        const auto drawing_join = [&source](
            std::uint8_t (*resolve)(const GameSource&, const UnitType&) noexcept
        ) {
            std::map<StableId, std::uint8_t> chosen;
            for (const UnitType& unit_type : source.unit_types) {
                chosen.emplace(unit_type.id, resolve(source, unit_type));
            }
            Bytes bytes;
            put_u32(bytes, static_cast<std::uint32_t>(chosen.size()));
            // `std::map` walks ascending, which is the order the reader
            // requires and the order a client searches.
            for (const auto& entry : chosen) {
                put_u64(bytes, entry.first);
                bytes.push_back(entry.second);
            }
            return bytes;
        };
        for (package_format::SectionSource& section : package.sections) {
            if (section.type != package_format::SectionType::presentation) {
                continue;
            }
            if (draws_any_style_override(source)) {
                section.records.push_back(
                    package_format::RecordSource{
                        package_format::presentation_style_record_id,
                        drawing_join(resolved_character_style)
                    }
                );
            }
            if (draws_any_figure_override(source)) {
                section.records.push_back(
                    package_format::RecordSource{
                        package_format::presentation_figure_record_id,
                        drawing_join(resolved_character_figure)
                    }
                );
            }
            break;
        }
    }
    package.sections.erase(
        std::remove_if(
            package.sections.begin() + 1,
            package.sections.end(),
            [](const package_format::SectionSource& section) {
                return section.records.empty();
            }
        ),
        package.sections.end()
    );
    result.package = package_format::write_mock_package(package);
    return result;
}

}  // namespace grandleon::game_content
