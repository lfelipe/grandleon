// SPDX-License-Identifier: MIT
#include <grandleon/sheet/unit_sheet.hpp>

#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_runtime/names.hpp>

#include <cstddef>
#include <string_view>

namespace grandleon::sheet {
namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;

namespace {

struct NamedId final {
    std::uint64_t id;
    const char* name;
};

// The shipped projects' display names, in one table rather than one per client.
//
// One table, because a copy per client is how a menu row and a stat sheet come
// to call the same weapon two different things. Content this build has never
// met reads as its category, so an unrecognised weapon is still a row rather
// than a number.
constexpr NamedId unit_type_names[] = {
    {core::stable_content_id_v1("dawn_knight"), "KNIGHT"},
    {core::stable_content_id_v1("dawn_archer"), "ARCHER"},
    {core::stable_content_id_v1("dawn_mage"), "MAGE"},
    {core::stable_content_id_v1("dawn_healer"), "HEALER"},
    // A class, and deliberately not a person. `dawn_commander` is an entirely
    // plausible id for a stranger's project, and a row here that held one of
    // this campaign's character names would label their unit type with it. A
    // person's name comes off the campaign roster, which is where every client
    // already asks for it first, and the package's own word comes before this
    // table anyway.
    {core::stable_content_id_v1("dawn_commander"), "COMMANDER"},
    {core::stable_content_id_v1("ashen_knight"), "KNIGHT"},
    {core::stable_content_id_v1("ashen_archer"), "ARCHER"},
    {core::stable_content_id_v1("ashen_stormcaller"), "STORMCALLER"},
    {core::stable_content_id_v1("ashen_commander"), "COMMANDER"},
    // The demo's two, which a terminal campaign now names out loud between
    // battles as well as on a sheet.
    {core::stable_content_id_v1("dawn_guard_unit"), "DAWN GUARD"},
    {core::stable_content_id_v1("river_watch_unit"), "RIVER WATCH"},
};

constexpr NamedId weapon_names[] = {
    {core::stable_content_id_v1("guard_sword"), "GUARD SWORD"},
    {core::stable_content_id_v1("warden_blade"), "WARDEN BLADE"},
    {core::stable_content_id_v1("long_bow"), "LONG BOW"},
    {core::stable_content_id_v1("ember_staff"), "EMBER STAFF"},
    {core::stable_content_id_v1("storm_sigil"), "STORM SIGIL"},
    {core::stable_content_id_v1("mending_staff"), "MENDING STAFF"},
    {core::stable_content_id_v1("training_sword"), "TRAINING SWORD"},
};

constexpr NamedId ability_names[] = {
    {core::stable_content_id_v1("power_strike"), "POWER STRIKE"},
    {core::stable_content_id_v1("volley"), "VOLLEY"},
    {core::stable_content_id_v1("ember_bolt"), "EMBER BOLT"},
    {core::stable_content_id_v1("cinder_arc"), "CINDER ARC"},
    {core::stable_content_id_v1("chain_storm"), "CHAIN STORM"},
    {core::stable_content_id_v1("mend"), "MEND"},
    {core::stable_content_id_v1("rally"), "RALLY"},
};

constexpr NamedId item_names[] = {
    {core::stable_content_id_v1("field_tonic"), "FIELD TONIC"},
};

template <std::size_t Count>
[[nodiscard]] const char* name_of(
    const NamedId (&table)[Count], std::uint64_t id, const char* fallback
) noexcept {
    for (std::size_t i = 0; i < Count; ++i) {
        if (table[i].id == id) return table[i].name;
    }
    return fallback;
}

// One line, built into storage the caller owns, truncated at the column budget
// rather than allowed to run past it.
//
// Written here rather than reached for from the C library: these lines are
// compared byte for byte between a console and a host, and `snprintf` on two
// toolchains is two implementations of one format.
class Line final {
public:
    explicit Line(char* buffer) noexcept : buffer_(buffer) { buffer_[0] = '\0'; }

    Line& text(const char* value) noexcept {
        while (value != nullptr && *value != '\0') push(*value++);
        return *this;
    }

    Line& number(int value) noexcept {
        if (value < 0) {
            push('-');
            // Negated in unsigned arithmetic. The most negative `int` has no
            // positive counterpart, so `-value` on it is signed overflow:
            // undefined, and on a console a wrong line rather than a caught
            // one. Every other value comes out identical either way.
            return unsigned_number(0U - static_cast<unsigned>(value));
        }
        return unsigned_number(static_cast<unsigned>(value));
    }

    Line& gap() noexcept { return text("  "); }

private:
    void push(char value) noexcept {
        if (length_ >= unit_sheet_columns) return;
        buffer_[length_++] = value;
        buffer_[length_] = '\0';
    }

    Line& unsigned_number(unsigned value) noexcept {
        char digits[12];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && count < 12);
        while (count > 0) push(digits[--count]);
        return *this;
    }

    char* buffer_;
    int length_{0};
};

[[nodiscard]] const sim::WeaponDefinition* weapon_definition(
    const std::vector<sim::WeaponDefinition>& weapons, sim::ContentId id
) noexcept {
    for (const sim::WeaponDefinition& weapon : weapons) {
        if (weapon.id == id) return &weapon;
    }
    return nullptr;
}

[[nodiscard]] const sim::ItemDefinition* item_definition(
    const std::vector<sim::ItemDefinition>& items, sim::ContentId id
) noexcept {
    for (const sim::ItemDefinition& item : items) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

[[nodiscard]] const sim::AbilityDefinition* ability_definition(
    const std::vector<sim::AbilityDefinition>& abilities, sim::ContentId id
) noexcept {
    for (const sim::AbilityDefinition& ability : abilities) {
        if (ability.id == id) return &ability;
    }
    return nullptr;
}

// The reach band, written the way both consoles already write one: a single
// number when the band is one tile wide, and a dash otherwise.
void write_band(Line& line, int minimum, int maximum) noexcept {
    line.text("RNG ").number(minimum);
    if (minimum != maximum) line.text("-").number(maximum);
}

// A weapon's ceiling once the character's own reach bonus is on it, saturating
// at 255 rather than wrapping.
//
// This mirrors `widened_reach` in `engine/simulation/src/encounter.cpp`, which
// is where the rule lives and where the three sites that resolve a band call
// it. It is repeated rather than shared because the engine's copy is a detail
// of the rule and this one is a detail of the sentence. They must still agree
// digit for digit, because a sheet that named a band the engine would not
// honour is the exact disagreement this library exists to prevent.
[[nodiscard]] int widened_reach(std::uint8_t maximum, std::uint8_t bonus) noexcept {
    const int total = static_cast<int>(maximum) + static_cast<int>(bonus);
    return total > 255 ? 255 : total;
}

}  // namespace

const char* unit_type_name(std::uint64_t unit_type_id) noexcept {
    return name_of(unit_type_names, unit_type_id, "UNIT");
}

const char* weapon_name(std::uint64_t weapon_id) noexcept {
    return name_of(weapon_names, weapon_id, "WEAPON");
}

const char* ability_name(std::uint64_t ability_id) noexcept {
    return name_of(ability_names, ability_id, "ABILITY");
}

const char* item_name(std::uint64_t item_id) noexcept {
    return name_of(item_names, item_id, "ITEM");
}

namespace {

// The author's own word, copied into the caller's storage and folded to the
// only character set this font has.
//
// The fold is not a taste. `grandleon::view::glyphs` runs 0x20 to 0x5F, which
// is space through underscore: there is no lower case in it, so "Captain Mirea"
// drawn verbatim through that font is "C        M    " and nothing else. Bytes
// outside ASCII become a dash rather than being dropped, which is the fold the
// Nintendo 64 already applies to a project title, so a name in a language this
// font cannot draw is visibly abbreviated rather than silently shortened.
//
// The name is cut at the capacity rather than allowed to run past it, on the
// same terms as every other line this library builds. `limit` is that capacity
// by default and less when the caller has reserved room after the name, an
// ordinal say, so the cut happens once, here, rather than being undone by a
// suffix written past it.
//
// Returns how many characters landed, so a caller appending to the block knows
// where it ends without measuring.
std::size_t fold_into(
    ContentName& out,
    const char* source,
    std::size_t length,
    std::size_t limit = content_name_capacity
) noexcept {
    if (limit > content_name_capacity) limit = content_name_capacity;
    std::size_t written = 0;
    for (std::size_t i = 0; i < length && written < limit; ++i) {
        const auto byte = static_cast<unsigned char>(source[i]);
        if (byte >= 0x80U) {
            // One dash per lead byte of a multi-byte sequence, so a folded name
            // is one mark per character rather than one per byte.
            if ((byte & 0xC0U) == 0xC0U) out.text[written++] = '-';
            continue;
        }
        char value = static_cast<char>(byte);
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>(value - ('a' - 'A'));
        }
        out.text[written++] = value;
    }
    out.text[written] = '\0';
    return written;
}

std::size_t length_of(const char* text) noexcept {
    std::size_t length = 0;
    while (text != nullptr && text[length] != '\0') ++length;
    return length;
}

ContentName resolved(
    const pf::LoadedPackage* package,
    pf::SectionType section,
    std::uint64_t id,
    const char* fallback
) noexcept {
    ContentName out;
    std::string_view authored;
    if (package != nullptr) authored = pr::content_name(*package, section, id);
    const std::size_t written =
        !authored.empty()
            ? fold_into(out, authored.data(), authored.size())
            : fold_into(out, fallback, length_of(fallback));
    // A name that folded away to nothing, every byte of it outside ASCII and
    // not a lead byte, is no name at all, and the category is a better answer
    // than a blank row.
    if (written == 0U) {
        std::size_t i = 0;
        while (fallback[i] != '\0' && i < content_name_capacity) {
            out.text[i] = fallback[i];
            ++i;
        }
        out.text[i] = '\0';
    }
    return out;
}

}  // namespace

ContentName unit_type_name(
    const pf::LoadedPackage* package, std::uint64_t unit_type_id
) noexcept {
    return resolved(
        package, pf::SectionType::unit_types, unit_type_id,
        unit_type_name(unit_type_id)
    );
}

ContentName weapon_name(
    const pf::LoadedPackage* package, std::uint64_t weapon_id
) noexcept {
    return resolved(
        package, pf::SectionType::weapons, weapon_id, weapon_name(weapon_id)
    );
}

ContentName ability_name(
    const pf::LoadedPackage* package, std::uint64_t ability_id
) noexcept {
    return resolved(
        package, pf::SectionType::abilities, ability_id, ability_name(ability_id)
    );
}

ContentName item_name(
    const pf::LoadedPackage* package, std::uint64_t item_id
) noexcept {
    return resolved(
        package, pf::SectionType::items, item_id, item_name(item_id)
    );
}

namespace {

// How many digits an ordinal takes, so the name in front of it can be cut to
// leave room. Counted rather than assumed: a board may field more than nine of
// one kind, and a two-digit ordinal that shortened nothing would be the digit
// that fell off the end.
std::size_t digits_of(std::size_t value) noexcept {
    std::size_t count = 1;
    while (value >= 10U) {
        value /= 10U;
        ++count;
    }
    return count;
}

}  // namespace

ContentName person_name(const char* name) noexcept {
    ContentName out;
    fold_into(out, name, length_of(name));
    return out;
}

ContentName character_name(
    const pf::LoadedPackage* package,
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit,
    const char* member_name
) noexcept {
    // The campaign's own name, when a member stands in this unit. A person's
    // name, and it outranks everything below because everything below describes
    // a kind of character rather than a character.
    if (member_name != nullptr && member_name[0] != '\0') {
        const ContentName out = person_name(member_name);
        if (out.text[0] != '\0') return out;
    }

    // What the author wrote on this placement. Keyed by the unit's own
    // identity, which is the placement's. See
    // `package_format::SectionType::placement_names` for why that needs no
    // encounter alongside it.
    if (package != nullptr) {
        const std::string_view authored = pr::content_name(
            *package, pf::SectionType::placement_names, unit
        );
        if (!authored.empty()) {
            ContentName out;
            if (fold_into(out, authored.data(), authored.size()) != 0U) {
                return out;
            }
        }
    }

    // Derived. A unit the snapshot does not carry cannot be described at all,
    // having no type to name and no company to count it among, so it gets the
    // one word that is true of it.
    const sim::UnitSnapshot* standing = nullptr;
    for (const sim::UnitSnapshot& candidate : snapshot.units) {
        if (candidate.id == unit) standing = &candidate;
    }
    if (standing == nullptr) {
        ContentName out;
        fold_into(out, "SOMEBODY", 8);
        return out;
    }

    // How many of this kind the board holds, and where this one falls in
    // ascending identity among them. Both in one pass, because the second
    // question is only asked when the first answers more than one.
    std::size_t kindred = 0;
    std::size_t ordinal = 1;
    for (const sim::UnitSnapshot& candidate : snapshot.units) {
        if (candidate.unit_type_id != standing->unit_type_id) continue;
        ++kindred;
        if (candidate.id < unit) ++ordinal;
    }
    const ContentName kind = unit_type_name(package, standing->unit_type_id);
    if (kindred < 2U) return kind;

    // `BANDIT 2`. The kind is folded again rather than copied, because it has
    // to be cut shorter this time: the ordinal and the space in front of it
    // come out of the same capacity, and a suffix written past the cut would
    // be the truncation this library never does.
    const std::size_t width = digits_of(ordinal) + 1U;
    ContentName out;
    const std::size_t base = fold_into(
        out, kind.text, length_of(kind.text),
        width < content_name_capacity ? content_name_capacity - width : 0U
    );
    std::size_t at = base;
    out.text[at++] = ' ';
    for (std::size_t place = digits_of(ordinal); place > 0U; --place) {
        std::size_t divisor = 1;
        for (std::size_t i = 1; i < place; ++i) divisor *= 10U;
        out.text[at++] = static_cast<char>('0' + (ordinal / divisor) % 10U);
    }
    out.text[at] = '\0';
    return out;
}

ContentName class_name(
    const pf::LoadedPackage* package, std::uint64_t unit_type_id
) noexcept {
    ContentName out;
    if (package == nullptr) return out;
    const std::uint64_t class_id =
        pr::unit_type_class(*package, unit_type_id);
    if (class_id == 0U) return out;
    const std::string_view authored =
        pr::content_name(*package, pf::SectionType::classes, class_id);
    if (authored.empty()) return out;
    fold_into(out, authored.data(), authored.size());
    return out;
}

const char* objective_name(sim::ObjectiveKind kind) noexcept {
    switch (kind) {
        case sim::ObjectiveKind::defeat_all_opponents: return "DEFEAT ALL";
        case sim::ObjectiveKind::defeat_target: return "DEFEAT TARGET";
        case sim::ObjectiveKind::protect_target: return "PROTECT TARGET";
        case sim::ObjectiveKind::survive_rounds: return "SURVIVE ROUNDS";
    }
    return "OBJECTIVE";
}

std::uint32_t rounds_to_survive(
    const std::vector<sim::ObjectiveDefinition>& objectives
) noexcept {
    for (const sim::ObjectiveDefinition& objective : objectives) {
        if (objective.kind != sim::ObjectiveKind::survive_rounds) continue;
        return objective.round_count;
    }
    return 0U;
}

void round_line(
    std::uint32_t completed_rounds,
    std::uint32_t total,
    char* out,
    std::size_t capacity
) noexcept {
    if (out == nullptr || capacity == 0U) return;
    // Written a digit at a time rather than through snprintf, because this line
    // is drawn on a machine with no heap and a stack watermark the ROM checks,
    // and because both consoles have to agree with the host character for
    // character about what it says.
    std::size_t cursor = 0U;
    const auto put = [&](const char* text) {
        while (*text != '\0' && cursor + 1U < capacity) out[cursor++] = *text++;
    };
    const auto put_number = [&](std::uint32_t value) {
        char digits[12];
        int length = 0;
        do {
            digits[length++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && length < 11);
        while (length > 0 && cursor + 1U < capacity) {
            out[cursor++] = digits[--length];
        }
    };
    put("ROUND ");
    // The round in progress, which is one past the rounds behind it.
    put_number(completed_rounds + 1U);
    if (total != 0U) {
        put(" OF ");
        put_number(total);
    }
    out[cursor] = '\0';
}

UnitSheet build(
    const sim::EncounterSnapshot& snapshot,
    const sim::UnitSnapshot& unit,
    const char* name,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities,
    const std::vector<sim::ItemDefinition>& items,
    const CampaignContext* campaign,
    const pf::LoadedPackage* package
) {
    UnitSheet sheet;
    const auto next = [&sheet]() -> char* {
        if (sheet.count >= unit_sheet_capacity) return nullptr;
        return sheet.lines[sheet.count++];
    };

    // Who this is, and what it is: the name on its own row and the class on the
    // row under it. Two rows rather than one, because they answer two
    // questions a player is asking at once: *which* of my archers is this, and
    // *what* can an archer do. A single row reading `WREN ASHDOWN ARCHER`
    // makes the second word look like part of the first.
    if (char* row = next()) {
        Line(row).text(
            character_name(package, snapshot, unit.id, name).c_str()
        );
    }
    if (const ContentName kind = class_name(package, unit.unit_type_id);
        kind.text[0] != '\0') {
        if (char* row = next()) Line(row).text(kind.c_str());
    }

    // What the campaign has made of them, when there is a campaign. Directly
    // under the name because a level is part of who somebody is rather than
    // part of what they can do this turn, and absent entirely otherwise: a
    // battle with no campaign behind it has no level to state, and stating one
    // anyway would be the sheet inventing a number.
    if (campaign != nullptr) {
        if (char* row = next()) {
            Line(row)
                .text("LEVEL ")
                .number(static_cast<int>(campaign->level))
                .gap()
                .text("EXP ")
                .number(static_cast<int>(campaign->experience));
        }
    }

    // What it can spend this turn, on the engine's own answer, so a sheet and
    // an accepted command cannot disagree about what a half-spent character has
    // left. Restating the rule here would hold only while one character could
    // ever be part-way through a turn; under `side_blocks` several can be at
    // once, and the snapshot's side-wide count is empty there.
    if (char* row = next()) {
        const int points =
            static_cast<int>(sim::action_points_left(snapshot, unit.id));
        Line line(row);
        line.text("HP ")
            .number(static_cast<int>(unit.health))
            .text("/")
            .number(static_cast<int>(unit.maximum_health))
            .gap()
            .text("AP ")
            .number(points)
            .gap()
            .text("MOV ")
            .number(static_cast<int>(unit.movement))
            .gap()
            .text("SPD ")
            .number(static_cast<int>(unit.speed));
    }

    // What it deals and what it takes. Strength and magic price a swing and a
    // magical cast; defence and resistance answer them.
    if (char* row = next()) {
        Line line(row);
        line.text("STR ")
            .number(static_cast<int>(unit.strength))
            .gap()
            .text("DEF ")
            .number(static_cast<int>(unit.defense))
            .gap()
            .text("RES ")
            .number(static_cast<int>(unit.resistance))
            .gap()
            .text("MAG ")
            .number(static_cast<int>(unit.magic));
    }

    // Whether it lands and whether it is landed on. These three are the whole
    // reason the sheet had to be more than a status line: a hit chance that
    // reads unit state is invisible unless the unit's state is readable.
    if (char* row = next()) {
        Line line(row);
        line.text("SKL ")
            .number(static_cast<int>(unit.skill))
            .gap()
            .text("LCK ")
            .number(static_cast<int>(unit.luck))
            .gap()
            .text("EVA ")
            .number(static_cast<int>(unit.evasion));
    }

    if (char* row = next()) {
        Line(row).text("WEAPONS");
    }
    if (unit.weapon_ids.empty()) {
        if (char* row = next()) Line(row).text("  NONE");
    }
    for (std::size_t i = 0;
         i < unit.weapon_ids.size() &&
         i < static_cast<std::size_t>(unit_sheet_max_weapons);
         ++i) {
        char* row = next();
        if (row == nullptr) break;
        const sim::ContentId id = unit.weapon_ids[i];
        Line line(row);
        line.text("  ").text(weapon_name(package, id).c_str()).gap();
        const sim::WeaponDefinition* definition = weapon_definition(weapons, id);
        if (definition == nullptr) {
            // The registry the encounter was created with does not describe
            // it. Say so rather than borrowing the band of the weapon in hand,
            // which would be a number this character has no claim to.
            line.text("RNG ?");
            continue;
        }
        // The band this character strikes at with this weapon, which is the
        // weapon's band with this character's own reach bonus on its ceiling.
        // The registry record is the authored bow, shared by everyone carrying
        // one; the bonus is the fact about the archer, and a row that printed
        // the record alone would tell a player written to shoot three tiles
        // that she shoots two, on every client that draws a sheet.
        //
        // The floor is the weapon's own, untouched, because the bonus raises a
        // ceiling and never lowers a floor: see `UnitDefinition::reach_bonus`,
        // where the refusal an archer's minimum encodes is the reason.
        write_band(
            line, static_cast<int>(definition->minimum_reach),
            widened_reach(definition->maximum_reach, unit.reach_bonus)
        );
        // The weapon's own accuracy, which is where a hit roll starts. What it
        // becomes is the striker's skill and luck against the target's evasion
        // and luck, and that needs a target the sheet does not have.
        line.gap()
            .text("HIT ")
            .number(static_cast<int>(definition->accuracy))
            .text("%");
    }

    if (char* row = next()) {
        Line(row).text("ABILITIES");
    }
    if (unit.ability_ids.empty()) {
        if (char* row = next()) Line(row).text("  NONE");
    }
    for (std::size_t i = 0;
         i < unit.ability_ids.size() &&
         i < static_cast<std::size_t>(unit_sheet_max_abilities);
         ++i) {
        char* row = next();
        if (row == nullptr) break;
        const sim::ContentId id = unit.ability_ids[i];
        Line line(row);
        line.text("  ").text(ability_name(package, id).c_str()).gap();
        const sim::AbilityDefinition* definition =
            ability_definition(abilities, id);
        if (definition == nullptr) {
            line.text("RNG ?");
            continue;
        }
        // Both ends are the ability's own, and the character's reach bonus is
        // deliberately not on either. An ability's power and shape come from
        // the ability rather than from the caster, and the bonus is about the
        // arm that swings a weapon. So the engine leaves ability reach alone at
        // every site that resolves one, and a sheet that widened this row
        // would be offering a cast the engine would refuse.
        write_band(
            line, static_cast<int>(definition->minimum_reach),
            static_cast<int>(definition->maximum_reach)
        );
    }

    // The pack, listed the way the weapons and the spells are: what is carried,
    // how many are left, and what spending one gives back. The count comes off
    // the snapshot rather than off the definition, so a sheet read after a
    // draught was drunk says so; an item the registry does not describe is
    // still named, and what is missing is what it does.
    if (char* row = next()) {
        Line(row).text("ITEMS");
    }
    if (unit.item_ids.empty()) {
        if (char* row = next()) Line(row).text("  NONE");
    }
    for (std::size_t i = 0;
         i < unit.item_ids.size() &&
         i < static_cast<std::size_t>(unit_sheet_max_items);
         ++i) {
        char* row = next();
        if (row == nullptr) break;
        const sim::ContentId id = unit.item_ids[i];
        Line line(row);
        line.text("  ")
            .text(item_name(package, id).c_str())
            .gap()
            .text("x")
            .number(static_cast<int>(unit.item_counts[i]));
        const sim::ItemDefinition* definition = item_definition(items, id);
        if (definition == nullptr) {
            line.gap().text("?");
            continue;
        }
        if (definition->kind == sim::ItemKind::restore) {
            line.gap().text("HEAL ").number(static_cast<int>(definition->power));
        }
    }

    // One line, and only for somebody a talk could reach. Deliberately not a
    // block of its own with a heading and a NONE row: every other block here
    // describes something the character carries or knows, and being talkable is
    // a single fact about them. A character whose placement authors no record,
    // which is every character in every shipped board, adds no line at all, so
    // no sheet already on screen moves and no console's row budget changes.
    //
    // What the record *is* stays unsaid. It is a campaign identity, the rules
    // never read what it means, and a sheet that printed the number would be
    // showing the player a key rather than a fact about the person.
    if (unit.talk_record_id != 0) {
        if (char* row = next()) {
            Line(row).text(unit.departed ? "HAS WALKED AWAY" : "WILL TALK");
        }
    }

    return sheet;
}

}  // namespace grandleon::sheet
