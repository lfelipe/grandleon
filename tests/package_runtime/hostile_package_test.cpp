// SPDX-License-Identifier: MIT
// What every reader in this repository does when it is handed a package
// somebody meant it harm with.
//
// Two mechanisms, and between them they hold two properties that are otherwise
// held only by convention.
//
// The first is the allocator below. It counts every byte the program asks for
// and remembers the high-water mark, so a decode can be asked how much memory
// it wanted rather than only whether it returned the right answer. The
// property that buys is the module's stated discipline: **a count read out of
// a file is measured against the bytes actually present before anything is
// reserved**. Three decoders said so in comments and seven did not, and the
// only way to notice the eighth is to measure. The bound below is what a whole
// hostile-input sweep is allowed to allocate, expressed against the size of
// the input, so a decoder that believes a number instead of checking it fails
// here whatever section it belongs to.
//
// The same allocator poisons memory as it is freed. That is the second
// property: a decoded definition may borrow the package's bytes, and a
// `LoadedPackage` may be copied and may have its bytes appended to. The
// browser's campaign entry points do the second on every attached record. A
// borrow that is a captured pointer is wrong after either, and reading a
// poisoned byte is a wrong answer every time rather than an occasional one.
//
// The sweep itself is the second mechanism: one valid package covering all
// seventeen section types, and every two- and four-byte field in it in turn
// replaced by the largest value it can hold, with the checksums repaired so
// the damaged file still authenticates and reaches the decoders rather than
// being turned away at the envelope. Every reader is run over every variant.

#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/package_runtime/progression.hpp>
#include <grandleon/package_runtime/starting_kit.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;

namespace {

struct AllocationCounter final {
    std::size_t live{};
    std::size_t peak{};
};

AllocationCounter& counter() noexcept {
    static AllocationCounter value;
    return value;
}

// Sixteen bytes rather than `sizeof(std::size_t)`, so that what is handed back
// keeps the alignment `std::malloc` promised.
constexpr std::size_t allocation_header = 16;

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

void* operator new(std::size_t size) {
    void* const block = std::malloc(size + allocation_header);
    if (block == nullptr) throw std::bad_alloc();
    *static_cast<std::size_t*>(block) = size;
    AllocationCounter& counted = counter();
    counted.live += size;
    if (counted.live > counted.peak) counted.peak = counted.live;
    return static_cast<std::uint8_t*>(block) + allocation_header;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* pointer) noexcept {
    if (pointer == nullptr) return;
    void* const block =
        static_cast<std::uint8_t*>(pointer) - allocation_header;
    const std::size_t size = *static_cast<std::size_t*>(block);
    counter().live -= size;
    // Freed memory is filled rather than left as it was, so that a decoded
    // definition still pointing into it answers something recognisably wrong
    // instead of something that happens to be right.
    std::memset(pointer, 0xDD, size);
    std::free(block);
}

void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }

void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

namespace {

// The high-water mark of everything allocated inside one scope, above whatever
// was already live when it opened.
class Watermark final {
public:
    Watermark() noexcept : base_(counter().live) { counter().peak = base_; }

    [[nodiscard]] std::size_t peak() const noexcept {
        return counter().peak > base_ ? counter().peak - base_ : 0;
    }

private:
    std::size_t base_;
};

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return value;
}

void write_u32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

// The envelope's own FNV-1a, spelled here rather than exported from the format
// module: this test's job is to forge a package that authenticates, and a
// forger that borrows the victim's stamp proves less than one that carves it.
std::uint32_t fnv1a(const std::vector<std::uint8_t>& bytes,
                    std::size_t offset,
                    std::size_t size,
                    bool skip_envelope_field) noexcept {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = offset; index < offset + size; ++index) {
        const std::uint8_t value =
            skip_envelope_field && index >= 68U && index < 72U
                ? 0U
                : bytes[index];
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

// Every checksum in the file recomputed over whatever the bytes now say, so a
// mutation reaches the decoders instead of being refused at the envelope. A
// directory entry whose offset and size no longer name a region inside the
// file is left alone: the loader will refuse that entry on its own terms, which
// is one of the answers this sweep is looking for.
void repair(std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 72) return;
    const std::uint32_t section_count = read_u32(bytes, 60);
    const std::uint32_t directory_offset = read_u32(bytes, 64);
    const std::size_t directory_end =
        static_cast<std::size_t>(directory_offset) +
        static_cast<std::size_t>(section_count) * 32U;
    if (section_count > 1024U || directory_offset > bytes.size() ||
        directory_end > bytes.size()) {
        return;
    }
    for (std::uint32_t index = 0; index < section_count; ++index) {
        const std::size_t entry =
            static_cast<std::size_t>(directory_offset) +
            static_cast<std::size_t>(index) * 32U;
        const std::uint32_t offset = read_u32(bytes, entry + 16);
        const std::uint32_t size = read_u32(bytes, entry + 20);
        if (offset > bytes.size() || size > bytes.size() - offset) continue;
        write_u32(bytes, entry + 28, fnv1a(bytes, offset, size, false));
    }
    write_u32(bytes, 68, fnv1a(bytes, 0, directory_end, true));
}

// A project that authors something in every one of the seventeen sections the
// format defines, so the sweep below has a field of every kind to lie about.
gc::GameSource complete_source() {
    gc::GameSource value;
    value.game_id[0] = 0x47U;
    value.title = "Hostile input";
    value.content_revision = 3;
    value.theme = 1;
    value.character_loss = gc::CharacterLoss::recoverable;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {
        {30, "Knight", {6, 4, 1, 2, 3}, {10}},
        {31, "Archer", {5, 3, 1, 1, 3}, {10}},
    };
    value.weapons = {{40, "Sword", 10, 3, 1, 1}, {41, "Bow", 10, 2, 2, 3, 90}};
    value.items = {{50, "Tonic", 20, 5, gc::ItemKind::restore, 4}};
    value.abilities = {
        {55, "Ember", gc::AbilityKind::damage, gc::DamageType::magical,
         gc::AreaShape::cross, 4, 1, 2, 1, 80}
    };
    gc::UnitType knight{60, "Knight", 30, 80, {40}, {50}, {55}};
    knight.experience_per_level = 40;
    knight.growth.chance = {95, 80, 65, 50, 35, 20};
    knight.drop_item = 50;
    knight.drop_chance = 40;
    // One character drawn in a style and with a body of its own, so the
    // presentation section carries its two optional records as well as its
    // first three.
    knight.character_style = 1;
    knight.character_figure = 1;
    gc::UnitType archer{61, "Archer", 31, 81, {41}, {}};
    value.unit_types = {knight, archer};
    value.maps = {{70, "Field", 4, 3, std::vector<std::uint64_t>(12, 1)}};
    value.factions = {{80, "Blue"}, {81, "Red"}};
    value.objectives = {
        {90, "Defeat the opponent", gc::ObjectiveKind::defeat_all_opponents}
    };
    gc::Dialogue opening;
    opening.id = 95;
    opening.name = "Opening";
    opening.backdrop = 2;
    opening.cast = {{"Kestrel", 60}};
    opening.lines = {{"Kestrel", "The ford is held.", 1}};
    value.dialogues = {opening};
    gc::Placement blue;
    blue.id = 1000;
    blue.source_key_id = 2000;
    blue.member_id = 2000;
    blue.unit_type_id = 60;
    blue.side = gc::EncounterSide::first;
    blue.x = 0;
    blue.y = 1;
    // A second of the player's own, so the region below can carry a capacity
    // that actually refuses somebody.
    gc::Placement escort;
    escort.id = 1003;
    escort.source_key_id = 2003;
    escort.member_id = 2003;
    escort.unit_type_id = 61;
    escort.side = gc::EncounterSide::first;
    escort.x = 0;
    escort.y = 2;
    gc::Placement red;
    red.id = 1001;
    red.source_key_id = 2001;
    red.unit_type_id = 61;
    red.side = gc::EncounterSide::second;
    red.x = 2;
    red.y = 1;
    red.behavior = gc::UnitBehavior::patrol;
    red.patrol = {{2, 0}, {2, 2}};
    // A character who can be talked to and a character who arrives later, so
    // the talks and arrivals sections exist at all.
    red.talk_flag_id = 3000;
    gc::Placement wave;
    wave.id = 1002;
    wave.source_key_id = 2002;
    wave.unit_type_id = 61;
    wave.side = gc::EncounterSide::second;
    wave.x = 3;
    wave.y = 2;
    wave.arrival_round = 3;
    gc::Encounter clash;
    clash.id = 100;
    clash.name = "First clash";
    clash.map_id = 70;
    clash.objective_ids = {90};
    clash.placements = {blue, escort, red, wave};
    clash.turn_order = gc::TurnOrder::initiative;
    clash.deployment.id = 4000;
    clash.deployment.tiles = {{0, 0}, {0, 1}, {0, 2}};
    clash.deployment.capacity = 1;
    value.encounters = {clash};
    gc::CampaignMember kestrel;
    kestrel.id = 2000;
    kestrel.name = "Kestrel";
    kestrel.unit_type_id = 60;
    kestrel.states_specificity = true;
    kestrel.specificity.stat_deltas[0] = 2;
    kestrel.specificity.stated[0] = true;
    kestrel.specificity.reach_bonus = 1;
    gc::Campaign campaign;
    campaign.id = 110;
    campaign.name = "Muster";
    campaign.entry_node_id = 111;
    campaign.nodes = {
        {111, gc::CampaignNodeKind::story, 0, {95}, {112}, {}},
        {112, gc::CampaignNodeKind::encounter, 100, {}, {113}, {}},
        {113, gc::CampaignNodeKind::terminal, 0, {}, {}, {}},
    };
    gc::CampaignMember rook;
    rook.id = 2003;
    rook.name = "Rook";
    rook.unit_type_id = 61;
    campaign.roster = {kestrel, rook};
    campaign.grants = {{50, 2, 0}};
    value.campaigns = {campaign};
    return value;
}

std::vector<std::uint8_t> fixture_bytes() {
    const gc::CompileResult compiled = gc::compile(complete_source());
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    expect(static_cast<bool>(compiled), "the hostile-input fixture compiles");
    return compiled.package;
}

pf::LoadOptions options() {
    pf::LoadOptions value;
    value.engine_version = {0, 1, 0};
    value.target = pf::TargetProfile::desktop;
    return value;
}

// Every decoder this repository has, over whatever loaded. What they answer is
// not the point: a damaged package is entitled to be refused by all of them.
// What matters is that asking costs bounded memory and reads nothing it should
// not.
void read_everything(const pf::LoadedPackage& package) {
    volatile std::size_t sink = 0;
    sink += pr::project_title(package).size();
    sink += pr::load_presentation(package).presentation.factions.size();
    if (const pf::SectionView* section =
            package.find(pf::SectionType::dialogue)) {
        for (const pf::RecordView& record : section->records) {
            sink += pr::load_dialogue(package, record.stable_id)
                        .dialogue.lines.size();
        }
    }
    if (const pf::SectionView* section =
            package.find(pf::SectionType::encounters)) {
        for (const pf::RecordView& record : section->records) {
            sink += pr::load_encounter(package, record.stable_id)
                        .definition.units.size();
        }
    }
    if (const pf::SectionView* section =
            package.find(pf::SectionType::campaigns)) {
        for (const pf::RecordView& record : section->records) {
            const pr::CampaignLoadResult loaded =
                pr::load_campaign(package, record.stable_id);
            sink += loaded.definition.nodes.size();
            for (const pr::CampaignMember& member : loaded.definition.members) {
                sink += member.name_in(package).size();
            }
        }
    }
    if (const pf::SectionView* section =
            package.find(pf::SectionType::unit_types)) {
        for (const pf::RecordView& record : section->records) {
            sink += pr::load_unit_progression(package, record.stable_id)
                        .progression.experience_per_level;
            sink += pr::load_unit_starting_items(package, record.stable_id)
                        .items.size();
            sink += pr::content_name(
                        package, pf::SectionType::unit_types, record.stable_id
                    )
                        .size();
        }
    }
    (void)sink;
}

// What one load and one full read of a package of this size is allowed to
// want.
//
// The shape is what matters: a fixed cost, plus a multiple of the bytes the
// caller actually handed over. Nothing a decoder allocates is entitled to be
// larger than the input that justified it, and the multiple is generous
// because the decoded forms genuinely are several times their encoded size: a
// twelve-byte record becomes a sixteen-byte view plus a set node, and a
// twenty-three-byte campaign node becomes an eighty-eight-byte structure.
//
// Measured on this file's fixture, which is 2,012 bytes: a valid load and a full
// read of every reader peaks at 4,832 bytes, and so does the worst of the
// 2,759 damaged variants that reach the decoders. That is 2.4 times the input
// in both cases. The bound is eight times the input over a four-kilobyte
// floor, so it has room for a section growing a richer decoded form without
// having room for a decoder that believes a number. A count believed rather
// than checked is not near this line: it asks for tens of kilobytes from a
// hundred and twenty bytes, and megabytes from a few hundred.
std::size_t allowance(std::size_t input_size) noexcept {
    return 4'096U + 8U * input_size;
}

void the_fixture_covers_every_section() {
    const std::vector<std::uint8_t> bytes = fixture_bytes();
    const pf::LoadResult loaded = pf::load_mock_package(bytes, options());
    expect(static_cast<bool>(loaded), "the hostile-input fixture loads");
    expect(
        loaded.package.sections.size() == 17U,
        "the hostile-input fixture authors all seventeen sections"
    );
    expect(
        pr::project_title(loaded.package) == "Hostile input",
        "and names itself"
    );
    const pr::CampaignLoadResult campaign =
        pr::load_campaign(loaded.package, 110);
    expect(static_cast<bool>(campaign), "its campaign decodes");
    expect(
        campaign.definition.members.size() == 2U &&
            campaign.definition.members.front().name_in(loaded.package) ==
                "Kestrel",
        "and holds the company the author wrote"
    );
    const pr::EncounterLoadResult encounter =
        pr::load_encounter(loaded.package, 100);
    expect(static_cast<bool>(encounter), "its board decodes");
    expect(
        encounter.definition.units.size() == 4U &&
            encounter.definition.deployment_tiles.size() == 3U,
        "with the placements and the region the author wrote"
    );
    expect(
        static_cast<bool>(pr::load_dialogue(loaded.package, 95)),
        "and its scene decodes"
    );
}

// The sweep.
//
// Every offset in the file, twice: once with the sixteen bits there set to the
// largest count a sixteen-bit field can state, and once with the thirty-two
// bits there set to the largest a thirty-two-bit field can. Every count in
// this format is one of those two widths, so this reaches all of them without
// having to know which bytes are counts. It also reaches the ones a section
// added later will put somewhere this test has never heard of.
void no_lie_about_a_count_outruns_the_bytes_behind_it() {
    const std::vector<std::uint8_t> original = fixture_bytes();
    const std::size_t limit = allowance(original.size());
    std::size_t worst = 0;
    std::size_t loaded_count = 0;
    for (std::size_t offset = 0; offset + 2 <= original.size(); ++offset) {
        for (int width = 0; width < 2; ++width) {
            const std::size_t span = width == 0 ? 2U : 4U;
            if (offset + span > original.size()) continue;
            std::vector<std::uint8_t> damaged = original;
            for (std::size_t index = 0; index < span; ++index) {
                damaged[offset + index] = 0xffU;
            }
            repair(damaged);

            const Watermark owning;
            {
                const pf::LoadResult result =
                    pf::load_mock_package(damaged, options());
                if (result) {
                    ++loaded_count;
                    read_everything(result.package);
                }
            }
            if (owning.peak() > worst) worst = owning.peak();

            const Watermark borrowing;
            {
                const pf::LoadResult result = pf::load_mock_package_in_place(
                    {damaged.data(), damaged.size()}, options()
                );
                if (result) read_everything(result.package);
            }
            if (borrowing.peak() > worst) worst = borrowing.peak();

            if (worst > limit) {
                std::cerr << "FAIL: a " << span << "-byte count at offset "
                          << offset << " of a " << original.size()
                          << "-byte package asked for " << worst
                          << " bytes, past the " << limit
                          << " a package that size is allowed\n";
                ++failures;
                return;
            }
        }
    }
    expect(loaded_count > 0, "some damaged variants reach the decoders");
    std::cerr << "note: " << original.size() << "-byte fixture, "
              << loaded_count << " damaged variants loaded, worst peak "
              << worst << " bytes against an allowance of " << limit << '\n';
}

// The same measurement over the package as it is, so the bound above is known
// to be a bound and not merely a large number.
void a_valid_package_costs_what_it_is_worth() {
    const std::vector<std::uint8_t> bytes = fixture_bytes();
    const Watermark watermark;
    {
        const pf::LoadResult loaded = pf::load_mock_package(bytes, options());
        expect(static_cast<bool>(loaded), "the fixture loads");
        read_everything(loaded.package);
    }
    const std::size_t peak = watermark.peak();
    std::cerr << "note: a valid load and full read of " << bytes.size()
              << " bytes peaks at " << peak << '\n';
    expect(
        peak <= allowance(bytes.size()),
        "a valid package costs less than the allowance a hostile one is held to"
    );
}

// A package whose bytes stop part-way through, at every length. Nothing here
// may read past the buffer and nothing may reserve for a count the remaining
// bytes could not hold. A truncated package is the cheapest way to state a
// count nothing backs.
void every_truncation_is_refused_within_its_allowance() {
    const std::vector<std::uint8_t> original = fixture_bytes();
    const std::size_t limit = allowance(original.size());
    for (std::size_t length = 0; length < original.size(); ++length) {
        std::vector<std::uint8_t> cut(
            original.begin(),
            original.begin() + static_cast<std::ptrdiff_t>(length)
        );
        // The total-size field is what the loader measures a package's length
        // against, so a cut file still claiming its original length is turned
        // away before anything else is looked at. Rewriting it to the length
        // the file now has, and repairing the checksums over that, is what
        // gets a truncation past the envelope and into the decoders.
        if (cut.size() >= 16) {
            write_u32(cut, 12, static_cast<std::uint32_t>(length));
        }
        repair(cut);
        const Watermark watermark;
        {
            const pf::LoadResult result = pf::load_mock_package(cut, options());
            if (result) read_everything(result.package);
        }
        if (watermark.peak() > limit) {
            std::cerr << "FAIL: a package cut to " << length
                      << " bytes asked for " << watermark.peak()
                      << " bytes, past the " << limit << " allowed\n";
            ++failures;
            return;
        }
    }
}

// A cursor is public API over any definition, including one nothing validated.
// A target its node list does not hold must be refused rather than walked to.
void a_missing_target_does_not_move_the_cursor_past_its_nodes() {
    pr::CampaignDefinition definition;
    definition.entry_node_id = 1;
    pr::CampaignNode first;
    first.id = 1;
    first.kind = pr::CampaignNodeKind::encounter;
    first.unconditional_target_id = 999;
    first.has_unconditional_target = true;
    pr::CampaignNode second;
    second.id = 2;
    second.kind = pr::CampaignNodeKind::terminal;
    definition.nodes = {first, second};

    pr::CampaignCursor cursor(definition);
    expect(cursor.current().id == 1U, "the cursor starts at the entry node");
    expect(
        cursor.advance_after(
            grandleon::simulation::Outcome::first_side_won
        ) == pr::CampaignError::missing_reference,
        "advancing to a node the definition does not hold is refused"
    );
    expect(
        cursor.current().id == 1U,
        "and leaves the cursor on the node it was standing on"
    );

    // The same question asked of a branch rather than of the unconditional
    // edge, because both funnel into one search.
    pr::CampaignDefinition branched = definition;
    branched.nodes.front().has_unconditional_target = false;
    branched.nodes.front().unconditional_target_id = 0;
    pr::CampaignBranch branch;
    branch.target_id = 998;
    branch.combinator = pr::ConditionCombinator::all;
    pr::CampaignPredicate predicate;
    predicate.kind = pr::CampaignPredicateKind::objective_result;
    predicate.subject = 90;
    predicate.result = pr::ObjectiveOutcome::satisfied;
    branch.predicates = {predicate};
    branched.nodes.front().branches = {branch};
    pr::CampaignCursor branching(branched);
    const std::vector<grandleon::simulation::ObjectiveResult> satisfied{
        {90, grandleon::simulation::ObjectiveState::satisfied}
    };
    expect(
        branching.advance_after(
            grandleon::simulation::Outcome::first_side_won, satisfied
        ) == pr::CampaignError::missing_reference,
        "a branch naming a node the definition does not hold is refused too"
    );
    expect(
        branching.current().id == 1U,
        "and also leaves the cursor where it stood"
    );
}

// A decoded campaign borrows the package's bytes for its members' names, and
// the browser's campaign entry points append to those bytes after the decode.
// The name has to still be the author's name afterwards.
void a_members_name_survives_the_packages_bytes_growing() {
    const std::vector<std::uint8_t> bytes = fixture_bytes();
    pf::LoadResult loaded = pf::load_mock_package(bytes, options());
    expect(static_cast<bool>(loaded), "the fixture loads");
    const pr::CampaignLoadResult campaign =
        pr::load_campaign(loaded.package, 110);
    expect(static_cast<bool>(campaign), "its campaign decodes");
    expect(
        campaign.definition.members.size() == 2U, "and holds the two members"
    );
    if (campaign.definition.members.empty()) return;
    const pr::CampaignMember& member = campaign.definition.members.front();

    // Exactly what `gl_campaign_add_dialogue` does: append a record's bytes to
    // the package a definition has already been decoded from. Enough of them
    // to guarantee the buffer moves.
    loaded.package.bytes.resize(loaded.package.bytes.size() + 1024U * 1024U);
    expect(
        member.name_in(loaded.package) == "Kestrel",
        "a member's name is still the author's after the package's bytes grow"
    );

    // And across a copy of the package, which is the other way the bytes end
    // up somewhere else.
    const pf::LoadedPackage copied = loaded.package;
    expect(
        member.name_in(copied) == "Kestrel",
        "and is still the author's read out of a copy of the package"
    );
}

}  // namespace

int main() {
    the_fixture_covers_every_section();
    a_valid_package_costs_what_it_is_worth();
    no_lie_about_a_count_outruns_the_bytes_behind_it();
    every_truncation_is_refused_within_its_allowance();
    a_missing_target_does_not_move_the_cursor_past_its_nodes();
    a_members_name_survives_the_packages_bytes_growing();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "package_runtime hostile-input checks passed\n";
    return 0;
}
