// SPDX-License-Identifier: MIT
#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/campaign/state.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace campaign = grandleon::campaign;
namespace core = grandleon::core;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

core::PackageId package(std::uint8_t marker) {
    core::PackageId identity{};
    identity[15] = marker;
    return identity;
}

campaign::DefinitionRef reference(
    core::ContentCategory category,
    std::string_view key
) {
    return {package(1), category, core::stable_content_id_v1(key)};
}

const campaign::DefinitionRef lancer =
    reference(core::ContentCategory::unit_type, "lancer");
const campaign::DefinitionRef healer =
    reference(core::ContentCategory::unit_type, "healer");
const campaign::DefinitionRef potion =
    reference(core::ContentCategory::item, "potion");
const campaign::DefinitionRef torch =
    reference(core::ContentCategory::item, "torch");
const campaign::DefinitionRef hold_the_ford =
    reference(core::ContentCategory::objective, "hold_the_ford");
const campaign::DefinitionRef bridge_burned =
    reference(core::ContentCategory::objective, "bridge_burned");
const campaign::DefinitionRef ford_battle =
    reference(core::ContentCategory::encounter, "ford_battle");
const campaign::DefinitionRef ford_held =
    reference(core::ContentCategory::campaign, "ford_held");

constexpr campaign::PersistentEntityId first{1};
constexpr campaign::PersistentEntityId second{2};
constexpr campaign::PersistentEntityId third{3};
constexpr campaign::PersistentEntityId store{};

campaign::OutcomeSource source(std::uint64_t battle_hash, std::uint64_t sequence) {
    return {ford_battle, battle_hash, sequence};
}

void commit(
    campaign::CampaignState& state,
    std::uint64_t battle_hash,
    std::vector<campaign::CampaignOutcomeOperation> operations
) {
    const auto applied = campaign::apply_outcome(
        state, campaign::make_outcome_batch(source(battle_hash, 0U), std::move(operations))
    );
    expect(static_cast<bool>(applied), "the fixture campaign commits");
}

// A campaign with a roster, a dead member, a shared store, objectives, typed
// world values, and an outcome history. Every field the encoder writes is
// non-empty, so a field the encoder forgets is a field a round trip notices.
campaign::CampaignState told_campaign() {
    campaign::CampaignState state;
    commit(
        state,
        0x1111ULL,
        {
            campaign::recruit_unit(first, lancer),
            campaign::recruit_unit(second, healer),
            campaign::recruit_unit(third, lancer),
            campaign::set_availability(first, campaign::Availability::available),
            campaign::set_availability(second, campaign::Availability::available),
            campaign::set_availability(third, campaign::Availability::retired),
            campaign::add_item(first, potion, 3U),
            campaign::add_item(first, torch, 1U),
            campaign::add_item(second, potion, 2U),
            campaign::add_item(store, potion, 9U),
            campaign::add_item(store, torch, 4U),
        }
    );
    commit(
        state,
        0x2222ULL,
        {
            campaign::grant_experience(first, 240U),
            campaign::advance_level(first, 2U),
            // What the two levels gave. Durable in its own right: the points
            // are what the roster adds to the authored unit type when this
            // member next takes the field, so a save that lost them would lose
            // the level rather than only the number beside it.
            campaign::grow_stat(first, campaign::GrowableStat::health, 2U),
            campaign::grow_stat(first, campaign::GrowableStat::action_points, 1U),
            campaign::record_permanent_death(second),
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            ),
            campaign::record_objective(
                bridge_burned, campaign::ObjectiveOutcome::failed
            ),
            campaign::set_world_flag(ford_held, campaign::WorldValue{
                campaign::WorldValueType::boolean, 1
            }),
            campaign::set_world_flag(
                reference(core::ContentCategory::campaign, "coin"),
                campaign::WorldValue{campaign::WorldValueType::integer, -1234}
            ),
        }
    );
    return state;
}

// The graph the route fixtures walk. Two ways out of the ford recombine at the
// muster, which ends: the smallest shape that makes a persisted route worth
// more than a node name, because the node alone cannot say which way it came.
//
//   ford --(1: the ford was held)--> watch --> muster --> ending
//        --(fallback)-------------> retreat --> muster
campaign::DefinitionRef node(std::string_view key) {
    return reference(core::ContentCategory::campaign_node, key);
}

campaign::CampaignGraph tarnholt_graph() {
    campaign::CampaignGraphNode ford;
    ford.node = node("ford");
    campaign::CampaignTransition held;
    held.target = node("watch");
    held.priority = 1U;
    held.predicates.push_back(campaign::objective_result_is(
        hold_the_ford, campaign::ObjectiveOutcome::satisfied
    ));
    ford.transitions.push_back(held);
    ford.has_fallback = true;
    ford.fallback = node("retreat");

    campaign::CampaignGraphNode watch;
    watch.node = node("watch");
    watch.has_fallback = true;
    watch.fallback = node("muster");

    campaign::CampaignGraphNode retreat;
    retreat.node = node("retreat");
    retreat.has_fallback = true;
    retreat.fallback = node("muster");

    campaign::CampaignGraphNode muster;
    muster.node = node("muster");
    muster.has_fallback = true;
    muster.fallback = node("ending");

    campaign::CampaignGraphNode ending;
    ending.node = node("ending");
    ending.terminal = true;

    return campaign::make_campaign_graph(
        reference(core::ContentCategory::campaign, "tarnholt"),
        node("ford"),
        {ford, watch, retreat, muster, ending}
    );
}

// The told campaign, then walked: it entered the graph, held the ford, and
// went by the watch to the muster. A route with a real branch in it, so the
// bytes have to carry which branch was taken and not merely where it ended.
campaign::CampaignState walked_campaign() {
    campaign::CampaignState state = told_campaign();
    const campaign::CampaignGraph graph = tarnholt_graph();
    expect(
        campaign::begin_campaign(state, graph) ==
            campaign::ProgressionError::none,
        "the fixture campaign enters its graph"
    );
    const auto ford_fought = campaign::make_outcome_batch(
        source(0x7777ULL, 0U), {campaign::add_item(store, torch, 1U)}
    );
    expect(
        campaign::complete_node(state, graph, ford_fought).target ==
            node("watch"),
        "and takes the branch its objectives earned"
    );
    const auto watch_kept = campaign::make_outcome_batch(
        source(0x8888ULL, 1U), {campaign::grant_experience(first, 15U)}
    );
    expect(
        campaign::complete_node(state, graph, watch_kept).target ==
            node("muster"),
        "and recombines at the muster"
    );
    return state;
}

std::vector<campaign::SavePackageRequirement> requirements() {
    return {
        {package(2), 7U, 0xfeedfacecafebeefULL},
        {package(1), 3U, 0x0123456789abcdefULL},
    };
}

campaign::CampaignSave told_save() {
    return campaign::make_campaign_save(told_campaign(), requirements());
}

campaign::CampaignSave walked_save() {
    return campaign::make_campaign_save(walked_campaign(), requirements());
}

// ---------------------------------------------------------------------------
// A reader for the envelope, written from the format's own description in
// `save.hpp` rather than from the encoder. If the two ever disagree, this is
// where it shows.
// ---------------------------------------------------------------------------

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return value;
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U)
    );
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

std::uint32_t package_count(const std::vector<std::uint8_t>& bytes) {
    return read_u32(bytes, 28);
}

std::uint32_t section_count(const std::vector<std::uint8_t>& bytes) {
    return read_u32(bytes, 36);
}

std::uint32_t directory_offset(const std::vector<std::uint8_t>& bytes) {
    return read_u32(bytes, 40);
}

// The directory entry for one section type, or the byte count when absent.
std::size_t entry_of(const std::vector<std::uint8_t>& bytes, std::uint32_t type) {
    for (std::uint32_t index = 0; index < section_count(bytes); ++index) {
        const std::size_t entry =
            directory_offset(bytes) + index * campaign::save_directory_entry_size;
        if (read_u32(bytes, entry) == type) {
            return entry;
        }
    }
    return bytes.size();
}

// The checksum over one section's bytes, written into that section's directory
// entry. Separate from the envelope below and from the walk over all of them,
// because a fixture that builds a directory by hand seals the entries whose
// bytes are really there and leaves the lying one alone.
void seal_section(std::vector<std::uint8_t>& bytes, std::size_t entry) {
    const std::uint32_t offset = read_u32(bytes, entry + 12);
    const std::uint32_t size = read_u32(bytes, entry + 16);
    std::uint64_t hash = core::fnv1a64_offset_basis;
    for (std::uint32_t byte = 0; byte < size; ++byte) {
        hash = core::fnv1a64_step(hash, bytes[offset + byte]);
    }
    write_u64(bytes, entry + 24, hash);
}

// The checksum over the header, the package table and the directory, with the
// eight bytes it lives in read as zero.
void seal_envelope(std::vector<std::uint8_t>& bytes) {
    const std::size_t metadata_end =
        directory_offset(bytes) +
        section_count(bytes) * campaign::save_directory_entry_size;
    std::uint64_t hash = core::fnv1a64_offset_basis;
    for (std::size_t index = 0; index < metadata_end; ++index) {
        const bool hole = index >= 48U && index < 56U;
        hash = core::fnv1a64_step(hash, hole ? std::uint8_t{0} : bytes[index]);
    }
    write_u64(bytes, 48, hash);
}

// Recompute both levels of integrity, so that a fixture which patches a field
// tests the field it patched rather than the checksum it invalidated.
void refresh(std::vector<std::uint8_t>& bytes) {
    for (std::uint32_t index = 0; index < section_count(bytes); ++index) {
        seal_section(
            bytes,
            directory_offset(bytes) + index * campaign::save_directory_entry_size
        );
    }
    seal_envelope(bytes);
}

// ---------------------------------------------------------------------------

// The header a reader meets first, read back with nothing but the offsets the
// header comment publishes.
void the_envelope_announces_itself_before_anything_is_interpreted() {
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(told_save());

    expect(bytes.size() > campaign::save_header_size, "a save has a header and a body");
    expect(
        bytes[0] == 'G' && bytes[1] == 'L' && bytes[2] == 'S' && bytes[3] == 'V',
        "which opens with the save magic"
    );
    expect(
        read_u16(bytes, 4) == campaign::save_envelope_major &&
            read_u16(bytes, 6) == campaign::save_envelope_minor,
        "and the envelope version"
    );
    expect(
        read_u32(bytes, 8) == campaign::save_header_size,
        "and the size of the header itself"
    );
    expect(read_u32(bytes, 12) == bytes.size(), "and the total size of the save");
    expect(
        read_u16(bytes, 16) == core::engine_version().major &&
            read_u16(bytes, 18) == core::engine_version().minor &&
            read_u16(bytes, 20) == core::engine_version().patch,
        "and the engine that wrote it"
    );
    expect(
        read_u32(bytes, 24) == campaign::save_rules_contract,
        "and the rules contract it was written against"
    );
    expect(package_count(bytes) == 2U, "and every package its contents refer to");
    expect(
        read_u32(bytes, 32) == campaign::save_header_size,
        "in a table that starts where the header ends"
    );
    expect(section_count(bytes) == 5U, "and the five sections a campaign is cut into");
    expect(
        directory_offset(bytes) ==
            campaign::save_header_size + 2U * campaign::save_package_entry_size,
        "in a directory that starts where the package table ends"
    );

    // The package table is readable before a single campaign byte is, which is
    // the whole reason it is not a section.
    const std::size_t table = campaign::save_header_size;
    expect(
        bytes[table + 15] == 1U && read_u32(bytes, table + 16) == 3U,
        "the first requirement names a package and the revision it wants"
    );
    expect(
        bytes[table + campaign::save_package_entry_size + 15] == 2U,
        "and the table is in ascending package order, not the order it was given"
    );
}

// Five sections, each carrying its own schema version. The point of the
// directory is that one of them can move without the other four.
void every_section_carries_its_own_version() {
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(told_save());

    const std::uint32_t types[] = {1U, 2U, 3U, 4U, 5U};
    std::uint32_t previous_type = 0;
    std::size_t previous_end = directory_offset(bytes) +
                               section_count(bytes) * campaign::save_directory_entry_size;
    for (const std::uint32_t type : types) {
        const std::size_t entry = entry_of(bytes, type);
        expect(entry != bytes.size(), "the directory holds every known section");
        if (entry == bytes.size()) {
            return;
        }
        const campaign::SaveSectionSchema schema =
            campaign::save_section_schema(static_cast<campaign::SaveSectionType>(type));
        expect(
            read_u16(bytes, entry + 4) == schema.major &&
                read_u16(bytes, entry + 6) == schema.minor,
            "each at the schema version this build declares for it"
        );
        expect(
            read_u32(bytes, entry + 8) == campaign::save_section_flag_required,
            "and each marked required, because a campaign is not its roster alone"
        );
        expect(type > previous_type, "and the directory is in ascending type order");
        previous_type = type;
        const std::uint32_t offset = read_u32(bytes, entry + 12);
        expect(
            offset == (previous_end + 3U) / 4U * 4U,
            "with the sections laid out in directory order, four-byte aligned"
        );
        previous_end = offset + read_u32(bytes, entry + 16);
    }
    expect(previous_end == bytes.size(), "and nothing trailing the last of them");
    expect(
        campaign::save_section_name(1U) == "roster" &&
            campaign::save_section_name(9999U) == "unknown",
        "and a name for a diagnostic to print"
    );
}

// The property the whole save format exists for.
void a_campaign_survives_the_round_trip() {
    const campaign::CampaignSave original = told_save();
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(original);

    const campaign::SaveLoadResult loaded =
        campaign::load_campaign(bytes, campaign::SaveLoadOptions{});
    expect(
        static_cast<bool>(loaded),
        std::string_view{campaign::save_error_name(loaded.error)}
    );
    expect(loaded.save == original, "load(save(state)) is the state it was given");
    expect(
        campaign::canonical_hash(loaded.save.state) ==
            campaign::canonical_hash(original.state),
        "and folds to the same campaign"
    );

    // The parts a hash could agree about by accident, checked by name.
    expect(loaded.save.state.units.size() == 3U, "the roster came back whole");
    const campaign::PersistentUnit* survivor =
        campaign::find_unit(loaded.save.state, first);
    expect(survivor != nullptr, "and the survivor with it");
    if (survivor != nullptr) {
        expect(
            survivor->progression.level == 3U && survivor->progression.experience == 240U,
            "with what they became"
        );
        expect(
            survivor->progression.gained ==
                std::array<std::uint16_t, campaign::growable_stat_count>{
                    2U, 0U, 0U, 0U, 0U, 1U
                },
            "and with what those levels actually gave them, stat by stat and "
            "in the order a level-up rolls them"
        );
        expect(survivor->carried.size() == 2U, "and what they carry");
    }
    expect(
        !campaign::is_deployable(loaded.save.state, second),
        "and the dead stayed dead across the boundary"
    );
    expect(
        campaign::item_quantity(loaded.save.state, store, potion) == 11U,
        "and the fallen member's kit is still in the store where it fell"
    );
    expect(
        loaded.save.state.applied_outcomes.size() == 2U,
        "and the outcome history came back, so a retry is still a retry"
    );
    expect(loaded.save.packages == original.packages, "and so did the package table");
}

// The route comes back exactly, which is what resuming a branched, recombined
// campaign needs: the node alone would not say which way it was reached.
void a_branched_route_survives_the_round_trip() {
    const campaign::CampaignSave original = walked_save();
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(original);

    const campaign::SaveLoadResult loaded =
        campaign::load_campaign(bytes, campaign::SaveLoadOptions{});
    expect(
        static_cast<bool>(loaded),
        std::string_view{campaign::save_error_name(loaded.error)}
    );
    expect(loaded.save == original, "a walked campaign round trips whole");
    expect(
        campaign::save_campaign(loaded.save) == bytes,
        "and re-encodes to the bytes it came from"
    );

    const campaign::CampaignProgress& progress = loaded.save.state.progress;
    expect(progress.active, "the loaded campaign is standing in its graph");
    expect(
        progress.campaign ==
            reference(core::ContentCategory::campaign, "tarnholt"),
        "in the campaign it belongs to"
    );
    expect(progress.active_node == node("muster"), "at the node it reached");
    expect(
        progress.history.size() == 3U && progress.history[0].node == node("ford") &&
            progress.history[1].node == node("watch") &&
            progress.history[2].node == node("muster"),
        "by the route it walked, in order"
    );
    expect(
        progress.history[0].cause.value == 0U &&
            progress.history[1].cause.value != 0U &&
            progress.history[2].cause.value != 0U,
        "with the entry uncaused and every later step naming its completion"
    );

    // The proof that the route is what is being persisted: the same campaign
    // that reached the same node the other way is different bytes.
    campaign::CampaignState other = told_campaign();
    const campaign::CampaignGraph graph = tarnholt_graph();
    expect(
        campaign::begin_campaign(other, graph) ==
            campaign::ProgressionError::none,
        "another campaign enters the same graph"
    );
    // Unrecord the objective the branch reads, so the fallback is taken.
    other.objectives.clear();
    const auto lost = campaign::make_outcome_batch(
        source(0x7777ULL, 0U), {campaign::add_item(store, torch, 1U)}
    );
    expect(
        campaign::complete_node(other, graph, lost).target == node("retreat"),
        "and goes the other way"
    );
    expect(
        campaign::save_campaign(campaign::make_campaign_save(other, requirements())) !=
            bytes,
        "and its save is not the same save"
    );

    // And a resumed campaign carries on from where it stood.
    campaign::CampaignState resumed = loaded.save.state;
    const auto muster_done = campaign::make_outcome_batch(
        source(0x9999ULL, 2U), {}
    );
    const campaign::NodeCompletion onward =
        campaign::complete_node(resumed, graph, muster_done);
    expect(
        onward.advanced && onward.target == node("ending"),
        "a route restored from bytes advances from where it left off"
    );
}

// The one optional section, and what its absence means.
void the_route_section_is_absent_rather_than_empty() {
    const std::vector<std::uint8_t> unstarted = campaign::save_campaign(told_save());
    const std::uint32_t type =
        static_cast<std::uint32_t>(campaign::SaveSectionType::progression);
    expect(
        entry_of(unstarted, type) == unstarted.size(),
        "a campaign that never entered a graph writes no progression section"
    );
    expect(
        section_count(unstarted) == 5U,
        "which is the five sections every save has always carried"
    );
    const campaign::SaveLoadResult old_save =
        campaign::load_campaign(unstarted, campaign::SaveLoadOptions{});
    expect(
        static_cast<bool>(old_save),
        "and such a save loads — which is what every save written before this "
        "section existed is"
    );
    expect(
        !old_save.save.state.progress.active,
        "and means the campaign has not entered a graph"
    );
    expect(
        old_save.save.state.progress.history.empty(),
        "with no route to resume"
    );

    const std::vector<std::uint8_t> walked = campaign::save_campaign(walked_save());
    const std::size_t entry = entry_of(walked, type);
    expect(entry != walked.size(), "a walked campaign writes one");
    if (entry == walked.size()) {
        return;
    }
    expect(section_count(walked) == 6U, "as the sixth section");
    const campaign::SaveSectionSchema schema =
        campaign::save_section_schema(campaign::SaveSectionType::progression);
    expect(
        read_u16(walked, entry + 4) == schema.major &&
            read_u16(walked, entry + 6) == schema.minor &&
            schema.major == 1U,
        "at its own schema version, independent of the other five"
    );
    expect(
        read_u32(walked, entry + 8) == 0U,
        "and marked optional, so a build that predates it retains rather than "
        "refuses"
    );
    expect(
        !campaign::save_section_required(campaign::SaveSectionType::progression) &&
            campaign::save_section_required(campaign::SaveSectionType::roster),
        "which is what this build says about the section"
    );
    expect(
        campaign::save_section_name(type) == "progression" &&
            campaign::is_known_save_section(type),
        "and it has a name for a diagnostic to print"
    );

    // Dropping it from a decoded save is a legal save, not a missing section.
    campaign::SaveDecodeResult decoded =
        campaign::decode_save_envelope(walked, campaign::SaveLoadOptions{});
    expect(static_cast<bool>(decoded), "the walked save decodes");
    campaign::DecodedSave without = decoded.decoded;
    without.sections.erase(
        std::remove_if(
            without.sections.begin(),
            without.sections.end(),
            [](const campaign::SaveSectionView& view) {
                return view.type == type;
            }
        ),
        without.sections.end()
    );
    const campaign::SaveLoadResult stripped =
        campaign::interpret_save(without, campaign::SaveLoadOptions{});
    expect(
        static_cast<bool>(stripped),
        "and a save without it is not a save missing a required section"
    );
    expect(
        !stripped.save.state.progress.active,
        "it is a campaign that has not started"
    );
}

// The same discipline every other section is held to, applied to this one.
void a_damaged_route_is_refused_and_named() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(walked_save());
    const std::uint32_t type =
        static_cast<std::uint32_t>(campaign::SaveSectionType::progression);
    const std::size_t entry = entry_of(good, type);
    expect(entry != good.size(), "the walked save carries a route");
    if (entry == good.size()) {
        return;
    }
    const std::uint32_t offset = read_u32(good, entry + 12);

    std::vector<std::uint8_t> flipped = good;
    flipped[offset + 8U] ^= 0x40U;
    const campaign::SaveLoadResult corrupt =
        campaign::load_campaign(flipped, campaign::SaveLoadOptions{});
    expect(
        corrupt.error == campaign::SaveError::checksum_mismatch,
        "a flipped bit in the route fails that section's checksum"
    );
    expect(corrupt.section == type, "and the diagnostic names the progression");

    bool all_refused = true;
    for (std::size_t length = 0; length < good.size(); ++length) {
        const std::vector<std::uint8_t> cut(
            good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length)
        );
        if (static_cast<bool>(
                campaign::load_campaign(cut, campaign::SaveLoadOptions{})
            )) {
            all_refused = false;
        }
    }
    expect(all_refused, "and every prefix of a walked save is refused");

    // A count larger than the bytes that hold it.
    std::vector<std::uint8_t> bomb = good;
    write_u32(bomb, offset + 56U, 0xffffffffU);
    refresh(bomb);
    expect(
        campaign::load_campaign(bomb, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_section,
        "four billion route steps do not fit in a hundred bytes"
    );

    // A count the bytes could satisfy, over the caller's cap.
    campaign::SaveLoadOptions tight;
    tight.limits.maximum_progression_entries = 2U;
    expect(
        campaign::load_campaign(good, tight).error ==
            campaign::SaveError::invalid_section,
        "and a route inside the bytes but over the cap is refused too"
    );

    // A section that is present and says the campaign walked nowhere. Presence
    // means started, so a route of no steps is a contradiction.
    std::vector<std::uint8_t> empty(
        good.begin(), good.begin() + static_cast<std::ptrdiff_t>(offset + 60U)
    );
    write_u32(empty, offset + 56U, 0U);
    write_u32(empty, entry + 16, 60U);
    write_u32(empty, 12, static_cast<std::uint32_t>(empty.size()));
    refresh(empty);
    expect(
        campaign::load_campaign(empty, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_section,
        "a progression section holding no steps is refused"
    );

    // A route whose last step is not the active node: an arrangement no
    // sequence of legal advances produces.
    std::vector<std::uint8_t> adrift = good;
    write_u64(adrift, offset + 28U + 20U, 0xdeadbeefULL);
    refresh(adrift);
    const campaign::SaveLoadResult wandering =
        campaign::load_campaign(adrift, campaign::SaveLoadOptions{});
    expect(
        wandering.error == campaign::SaveError::invalid_state,
        "an active node the route does not end at is refused"
    );
    expect(
        wandering.state_error == campaign::StateError::inconsistent_progression,
        "and the diagnostic is the invariant it broke"
    );

    // A step caused by a batch this campaign never committed.
    std::vector<std::uint8_t> invented = good;
    write_u64(invented, offset + 60U + 36U + 28U, 0x1234ULL);
    refresh(invented);
    const campaign::SaveLoadResult uncommitted =
        campaign::load_campaign(invented, campaign::SaveLoadOptions{});
    expect(
        uncommitted.error == campaign::SaveError::invalid_state &&
            uncommitted.state_error ==
                campaign::StateError::inconsistent_progression,
        "and so is a step caused by an outcome the campaign never applied"
    );
}

// Deterministic bytes: the same campaign, twice, and told to two campaigns in
// different orders. No clock, no address, no iteration order.
void the_same_campaign_is_the_same_bytes() {
    const std::vector<std::uint8_t> once = campaign::save_campaign(told_save());
    const std::vector<std::uint8_t> twice = campaign::save_campaign(told_save());
    expect(once == twice, "serializing one campaign twice yields identical bytes");

    // Two independent batches, applied in opposite orders. The campaign that
    // results is the same campaign, and the encoder is not allowed to remember
    // which order it arrived in. Every collection has a stated canonical
    // order, and the encoder walks it rather than the insertion history.
    const campaign::CampaignOutcomeBatch left = campaign::make_outcome_batch(
        source(0x3333ULL, 0U),
        {campaign::recruit_unit(first, lancer), campaign::add_item(store, potion, 2U)}
    );
    const campaign::CampaignOutcomeBatch right = campaign::make_outcome_batch(
        source(0x4444ULL, 0U),
        {campaign::recruit_unit(second, healer), campaign::add_item(store, torch, 5U)}
    );

    campaign::CampaignState forwards;
    campaign::CampaignState backwards;
    expect(
        static_cast<bool>(campaign::apply_outcome(forwards, left)) &&
            static_cast<bool>(campaign::apply_outcome(forwards, right)),
        "one campaign hears the two battles in one order"
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(backwards, right)) &&
            static_cast<bool>(campaign::apply_outcome(backwards, left)),
        "and another hears them in the other"
    );
    expect(
        campaign::save_campaign(campaign::make_campaign_save(forwards, {})) ==
            campaign::save_campaign(campaign::make_campaign_save(backwards, {})),
        "and the two agree byte for byte"
    );

    // Nor may it depend on how the containers holding the campaign were grown.
    campaign::CampaignState roomy = forwards;
    roomy.units.reserve(1024);
    roomy.store.reserve(1024);
    roomy.applied_outcomes.reserve(1024);
    expect(
        campaign::save_campaign(campaign::make_campaign_save(roomy, {})) ==
            campaign::save_campaign(campaign::make_campaign_save(forwards, {})),
        "and neither depends on a vector's capacity"
    );
}

// The other direction, which is what makes a save a stable artefact: bytes that
// went through this build come back out unchanged.
void a_loaded_save_re_encodes_to_the_bytes_it_came_from() {
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(told_save());
    const campaign::SaveLoadResult loaded =
        campaign::load_campaign(bytes, campaign::SaveLoadOptions{});
    expect(static_cast<bool>(loaded), "the save loads");
    expect(
        campaign::save_campaign(loaded.save) == bytes,
        "save(load(bytes)) is the bytes it was given"
    );
}

// A representative campaign, measured. The number is here so that the console
// storage adapters have a figure rather than a guess: a console save is
// thirty-two kilobytes of SRAM at best and two kilobytes of EEPROM at worst,
// and the difference between those two decides whether a roster fits or has to
// be compacted first.
void a_representative_campaign_has_a_measured_size() {
    campaign::CampaignState state;
    std::vector<campaign::CampaignOutcomeOperation> recruitment;
    for (std::uint64_t index = 1; index <= 12U; ++index) {
        recruitment.push_back(campaign::recruit_unit(
            campaign::PersistentEntityId{index}, index % 2U == 0U ? lancer : healer
        ));
        recruitment.push_back(campaign::set_availability(
            campaign::PersistentEntityId{index}, campaign::Availability::available
        ));
        recruitment.push_back(
            campaign::add_item(campaign::PersistentEntityId{index}, potion, 2U)
        );
        recruitment.push_back(
            campaign::add_item(campaign::PersistentEntityId{index}, torch, 1U)
        );
    }
    commit(state, 0x4444ULL, std::move(recruitment));

    std::vector<campaign::CampaignOutcomeOperation> chapter;
    for (std::uint64_t index = 0; index < 8U; ++index) {
        chapter.push_back(campaign::record_objective(
            {package(1), core::ContentCategory::objective, 0x100ULL + index},
            campaign::ObjectiveOutcome::satisfied
        ));
        chapter.push_back(campaign::set_world_flag(
            {package(1), core::ContentCategory::campaign, 0x200ULL + index},
            campaign::WorldValue{campaign::WorldValueType::integer,
                                 static_cast<std::int64_t>(index)}
        ));
        chapter.push_back(campaign::add_item(
            store, {package(1), core::ContentCategory::item, 0x300ULL + index}, 4U
        ));
    }
    commit(state, 0x5555ULL, std::move(chapter));

    // Twenty completed battles, which is a campaign's worth of history.
    for (std::uint64_t battle = 0; battle < 20U; ++battle) {
        commit(
            state,
            0x6000ULL + battle,
            {campaign::grant_experience(campaign::PersistentEntityId{1}, 30U)}
        );
    }

    const std::vector<std::uint8_t> bytes =
        campaign::save_campaign(campaign::make_campaign_save(state, {{package(1), 3U, 1U}}));

    // 64 bytes of header, 32 of package requirement and 160 of directory, then
    // twelve members carrying two stacks each at 132 bytes apiece (112 before
    // a level-up had anything to record, and twenty more for the ten stats it
    // can grow), eight store stacks at 32, eight objectives at 32, eight typed
    // world values at 40, and twenty-two committed outcome ids at 8.
    expect(
        bytes.size() == 2868U,
        "a representative campaign encodes to 2868 bytes"
    );
    expect(
        bytes.size() < 32U * 1024U,
        "which is inside a Nintendo 64 SRAM save with room to grow"
    );
    expect(
        static_cast<bool>(campaign::load_campaign(bytes, campaign::SaveLoadOptions{})),
        "and it loads"
    );
    std::cerr << "representative campaign: " << bytes.size() << " bytes\n";

    // The same campaign, standing somewhere. A route costs its own directory
    // entry and thirty-six bytes a step, and a campaign in progress always has
    // one. So this, and not the figure above, is what a console adapter
    // budgets for.
    const campaign::CampaignGraph graph = tarnholt_graph();
    expect(
        campaign::begin_campaign(state, graph) ==
            campaign::ProgressionError::none,
        "the representative campaign enters a graph"
    );
    for (std::uint64_t step = 0; step < 3U; ++step) {
        expect(
            campaign::complete_node(
                state,
                graph,
                campaign::make_outcome_batch(
                    source(0x7000ULL + step, 100U + step),
                    {campaign::grant_experience(first, 5U)}
                )
            ).advanced,
            "and walks it to its end"
        );
    }
    const std::vector<std::uint8_t> walked =
        campaign::save_campaign(campaign::make_campaign_save(state, {{package(1), 3U, 1U}}));
    // 32 more bytes of directory entry, 60 of section head, and four route
    // steps at 36 (the entry and three completions), plus three more
    // committed outcome ids at 8 apiece. The experience they granted needs no
    // bytes: a progression field is fixed width whatever it holds.
    expect(
        walked.size() == 3128U,
        "a representative campaign standing in its graph encodes to 3128 bytes"
    );
    expect(
        walked.size() < 32U * 1024U,
        "still inside a Nintendo 64 SRAM save with room to grow"
    );
    expect(
        static_cast<bool>(campaign::load_campaign(walked, campaign::SaveLoadOptions{})),
        "and it loads too"
    );
    std::cerr << "representative campaign, walked: " << walked.size() << " bytes\n";
}

// Corruption, at both levels of integrity, named where it happened.
void a_flipped_bit_is_caught_and_located() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());

    std::vector<std::uint8_t> header = good;
    header[24] ^= 0x01U;
    const campaign::SaveLoadResult from_header =
        campaign::load_campaign(header, campaign::SaveLoadOptions{});
    expect(
        from_header.error == campaign::SaveError::checksum_mismatch,
        "a flipped bit in the header fails the envelope checksum"
    );
    expect(
        from_header.section == 0U,
        "and the diagnostic says it was the envelope rather than a section"
    );

    const std::size_t roster = entry_of(good, 1U);
    std::vector<std::uint8_t> body = good;
    body[read_u32(good, roster + 12) + 8U] ^= 0x80U;
    const campaign::SaveLoadResult from_body =
        campaign::load_campaign(body, campaign::SaveLoadOptions{});
    expect(
        from_body.error == campaign::SaveError::checksum_mismatch,
        "a flipped bit inside a section fails that section's checksum"
    );
    expect(from_body.section == 1U, "and the diagnostic names the roster");

    std::vector<std::uint8_t> directory = good;
    directory[roster + 4] ^= 0x01U;
    expect(
        campaign::load_campaign(directory, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::checksum_mismatch,
        "and a flipped bit in the directory fails the envelope checksum too"
    );

    std::vector<std::uint8_t> stranger = good;
    stranger[1] = 'X';
    expect(
        campaign::load_campaign(stranger, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_magic,
        "and something that is not a save at all is refused for not being one"
    );
}

// Every prefix of a valid save, in order. None may be accepted and none may
// read a byte it does not have.
void every_truncation_of_a_save_is_refused() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());
    bool all_refused = true;
    for (std::size_t length = 0; length < good.size(); ++length) {
        const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        if (static_cast<bool>(campaign::load_campaign(cut, campaign::SaveLoadOptions{}))) {
            all_refused = false;
        }
    }
    expect(all_refused, "no prefix of a save is a save");
    expect(
        campaign::load_campaign({}, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::truncated,
        "and no bytes at all is a truncation rather than a crash"
    );

    // A save that claims to be longer than it is, checked before the claim is
    // used to index anything.
    std::vector<std::uint8_t> lying = good;
    write_u32(lying, 12, static_cast<std::uint32_t>(good.size() + 4096U));
    refresh(lying);
    expect(
        campaign::load_campaign(lying, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::truncated,
        "and a save that overstates its own length is refused"
    );
}

// A directory whose own arithmetic walks off the end of the file.
//
// Each section starts at the next four-byte boundary after the last, and
// nothing requires the file to end on one. So a section whose size is not a
// multiple of four puts the *next* section's mandatory start up to three bytes
// past the final byte. The directory is then required to name that start,
// and no amount of checksum agreement makes it readable.
//
// `save_campaign` cannot be mutated into this shape: it aligns every section
// it writes, so the end of its last section is always a boundary. The
// directory has to be built.
void a_section_starting_past_the_end_is_refused() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());

    // The header and package table of a save that loads, followed by a
    // directory of exactly two entries.
    const std::size_t directory =
        campaign::save_header_size +
        package_count(good) * campaign::save_package_entry_size;
    const std::size_t metadata_end =
        directory + 2U * campaign::save_directory_entry_size;

    std::vector<std::uint8_t> forged(
        good.begin(), good.begin() + static_cast<std::ptrdiff_t>(directory)
    );
    forged.resize(metadata_end, 0U);
    write_u32(forged, 36, 2U);
    write_u32(forged, 40, static_cast<std::uint32_t>(directory));

    // One byte of payload. The file now ends three bytes below a boundary.
    forged.push_back(0x5aU);
    write_u32(forged, 12, static_cast<std::uint32_t>(forged.size()));

    // The first section is honest, so the walk reaches the second.
    write_u32(forged, directory, 1U);
    write_u32(forged, directory + 12, static_cast<std::uint32_t>(metadata_end));
    write_u32(forged, directory + 16, 1U);
    seal_section(forged, directory);

    // The second names the start the alignment rule demands, already past the
    // last byte, and claims sixty-four bytes there. Its checksum is left
    // wrong: an implementation that reaches for those bytes at all has already
    // read what it must not, whatever it then concludes about them.
    const std::size_t liar = directory + campaign::save_directory_entry_size;
    write_u32(forged, liar, 2U);
    write_u32(
        forged, liar + 12, static_cast<std::uint32_t>(metadata_end + 4U)
    );
    write_u32(forged, liar + 16, 64U);
    seal_envelope(forged);

    const campaign::SaveLoadResult read =
        campaign::load_campaign(forged, campaign::SaveLoadOptions{});
    expect(
        read.error == campaign::SaveError::truncated,
        "a section starting past the last byte is a truncation, not a checksum"
    );
    expect(read.section == 2U, "and the diagnostic names the section that lied");

    // The same start with nothing claimed at it. Zero bytes are readable
    // anywhere, which is exactly why the offset has to be bounded on its own
    // rather than only through the size beside it.
    std::vector<std::uint8_t> empty = forged;
    write_u32(empty, liar + 16, 0U);
    seal_section(empty, liar);
    seal_envelope(empty);
    expect(
        campaign::load_campaign(empty, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::truncated,
        "and so is an empty section starting past it"
    );
}

// The cap is a comparison, not a copy: an oversized save is refused before its
// first byte is read.
void an_oversized_save_is_refused_before_it_is_read() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());
    campaign::SaveLoadOptions options;
    options.limits.maximum_bytes = static_cast<std::uint32_t>(good.size() - 1U);
    expect(
        campaign::load_campaign(good, options).error == campaign::SaveError::oversized,
        "a save over the caller's byte budget is refused as oversized"
    );

    options.limits.maximum_bytes = static_cast<std::uint32_t>(good.size());
    expect(
        static_cast<bool>(campaign::load_campaign(good, options)),
        "and a save exactly at it is not"
    );
}

// The bound that matters. A hostile count is refused against the bytes that
// remain and against a hard cap, before anything is reserved for it.
void a_hostile_length_allocates_nothing() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());

    const std::size_t roster = entry_of(good, 1U);
    const std::uint32_t roster_offset = read_u32(good, roster + 12);

    std::vector<std::uint8_t> bomb = good;
    write_u32(bomb, roster_offset, 0xffffffffU);
    refresh(bomb);
    expect(
        campaign::load_campaign(bomb, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_section,
        "four billion roster members do not fit in two thousand bytes"
    );

    // The same lie one level down: a member whose carried-item count is larger
    // than the section it lives in.
    std::vector<std::uint8_t> nested = good;
    write_u32(nested, roster_offset + 4U + 64U, 0x7fffffffU);
    refresh(nested);
    expect(
        campaign::load_campaign(nested, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_section,
        "and neither does an inventory larger than the save that holds it"
    );

    // A cap the bytes could satisfy, refused anyway. The two halves of the
    // bound are separate on purpose: one keeps a small save honest, the other
    // keeps a large one affordable.
    campaign::SaveLoadOptions tight;
    tight.limits.maximum_units = 2U;
    expect(
        campaign::load_campaign(good, tight).error == campaign::SaveError::invalid_section,
        "and a roster inside the bytes but over the caller's cap is refused too"
    );

    campaign::SaveLoadOptions few_sections;
    few_sections.limits.maximum_sections = 4U;
    expect(
        campaign::load_campaign(good, few_sections).error ==
            campaign::SaveError::invalid_directory,
        "and a directory over its cap never becomes an allocation"
    );

    campaign::SaveLoadOptions few_packages;
    few_packages.limits.maximum_packages = 1U;
    expect(
        campaign::load_campaign(good, few_packages).error ==
            campaign::SaveError::invalid_package_table,
        "and neither does a package table over its own"
    );
}

// Compatibility, stated before the payload is interpreted.
void an_incompatible_save_is_refused_with_a_reason() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());

    campaign::SaveLoadOptions from_the_future;
    from_the_future.engine = core::Version{0, 0, 9};
    expect(
        campaign::load_campaign(good, from_the_future).error ==
            campaign::SaveError::incompatible_engine,
        "a save written by a newer engine is refused rather than half-read"
    );

    campaign::SaveLoadOptions from_the_past;
    from_the_past.minimum_engine = core::Version{9, 0, 0};
    expect(
        campaign::load_campaign(good, from_the_past).error ==
            campaign::SaveError::incompatible_engine,
        "and so is one older than this build reads without a migration"
    );

    campaign::SaveLoadOptions other_rules;
    other_rules.rules_contract = campaign::save_rules_contract + 1U;
    expect(
        campaign::load_campaign(good, other_rules).error ==
            campaign::SaveError::incompatible_rules,
        "and a save whose vocabulary means something else is refused by contract"
    );

    std::vector<std::uint8_t> newer = good;
    newer[4] = static_cast<std::uint8_t>(campaign::save_envelope_major + 1U);
    refresh(newer);
    expect(
        campaign::load_campaign(newer, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::unsupported_envelope,
        "and an envelope shape this build cannot parse is refused at the door"
    );

    // The seam a migration slots into: a known section at a schema major this
    // build does not read. The registry runs between the decode and the
    // interpretation (`tests/campaign/migration_test.cpp`); reaching here means
    // no migration claimed the section, and then the answer is a refusal rather
    // than a guess.
    std::vector<std::uint8_t> ahead = good;
    write_u32(ahead, entry_of(good, 4U) + 4U, 2U);
    refresh(ahead);
    const campaign::SaveLoadResult migrated =
        campaign::load_campaign(ahead, campaign::SaveLoadOptions{});
    expect(
        migrated.error == campaign::SaveError::unsupported_schema,
        "a section at an unread schema major waits for a migration"
    );
    expect(migrated.section == 4U, "and the diagnostic names which section");
    expect(
        static_cast<bool>(campaign::decode_save_envelope(
            ahead, campaign::SaveLoadOptions{}
        )),
        "while the envelope around it decodes, which is what a migration needs"
    );
}

// Unknown sections: the forward-compatibility rule, in both directions.
void an_unknown_section_is_retained_skipped_or_refused() {
    campaign::CampaignSave save = told_save();
    campaign::RetainedSection future;
    future.type = 4242U;
    future.schema_major = 3U;
    future.schema_minor = 1U;
    future.flags = 0U;
    future.bytes = {1U, 2U, 3U, 4U, 5U};
    save.retained.push_back(future);
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(save);

    const campaign::SaveLoadResult retained =
        campaign::load_campaign(bytes, campaign::SaveLoadOptions{});
    expect(static_cast<bool>(retained), "an unknown optional section does not stop a load");
    expect(
        retained.save.retained.size() == 1U && retained.save.retained.front() == future,
        "and comes back byte for byte, schema version and all"
    );
    expect(
        campaign::save_campaign(retained.save) == bytes,
        "so an older build that loads and saves does not delete a newer build's data"
    );

    campaign::SaveLoadOptions dropping;
    dropping.retain_unknown_sections = false;
    const campaign::SaveLoadResult skipped = campaign::load_campaign(bytes, dropping);
    expect(static_cast<bool>(skipped), "a caller may skip it instead");
    expect(skipped.save.retained.empty(), "and then it is gone");
    expect(
        campaign::save_campaign(skipped.save) != bytes,
        "which is a rewrite of the save and says so by changing the bytes"
    );

    campaign::CampaignSave demanding = told_save();
    future.flags = campaign::save_section_flag_required;
    demanding.retained.push_back(future);
    const campaign::SaveLoadResult refused = campaign::load_campaign(
        campaign::save_campaign(demanding), campaign::SaveLoadOptions{}
    );
    expect(
        refused.error == campaign::SaveError::unknown_required_section,
        "but an unknown section the save marks required rejects the load"
    );
    expect(refused.section == 4242U, "and the diagnostic names it");

    // A retained section that collides with one this build owns is dropped by
    // the writer rather than written into a save that would not load.
    campaign::CampaignSave colliding = told_save();
    colliding.retained.push_back({1U, 1U, 0U, 0U, {9U}});
    const std::vector<std::uint8_t> without = campaign::save_campaign(colliding);
    expect(
        without == campaign::save_campaign(told_save()),
        "a retained section wearing a known section's type is not written"
    );
}

// A save the envelope accepts but the campaign rules do not.
void bytes_cannot_smuggle_in_a_campaign_no_rule_could_reach() {
    const campaign::CampaignSave save = told_save();
    const std::vector<std::uint8_t> good = campaign::save_campaign(save);
    const std::uint32_t roster_offset = read_u32(good, entry_of(good, 1U) + 12);

    // The first member is available and carries two stacks. Call them dead
    // instead: permanent death returns a member's kit to the store, so a dead
    // member holding equipment is an arrangement no operation produces.
    std::vector<std::uint8_t> risen = good;
    risen[roster_offset + 4U + 36U] =
        static_cast<std::uint8_t>(campaign::Availability::dead);
    refresh(risen);
    const campaign::SaveLoadResult rejected =
        campaign::load_campaign(risen, campaign::SaveLoadOptions{});
    expect(
        rejected.error == campaign::SaveError::invalid_state,
        "a campaign the whole-state check refuses is refused"
    );
    expect(
        rejected.state_error == campaign::StateError::inconsistent_availability,
        "and the diagnostic is the invariant it broke"
    );

    // A value that is not a member of the enumeration at all.
    std::vector<std::uint8_t> nonsense = good;
    nonsense[roster_offset + 4U + 36U] = 200U;
    refresh(nonsense);
    expect(
        campaign::load_campaign(nonsense, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_section,
        "and a byte that names no availability is refused before the state check"
    );

    // A reserved field that is not reserved.
    std::vector<std::uint8_t> squatting = good;
    squatting[roster_offset + 4U + 37U] = 1U;
    refresh(squatting);
    expect(
        campaign::load_campaign(squatting, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_section,
        "and a reserved byte carrying a value is refused rather than ignored"
    );
}

// A section this build requires and the save does not carry. Built through the
// decoded form, because the writer will not produce one.
void a_campaign_missing_a_section_is_not_a_campaign() {
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(told_save());
    campaign::SaveDecodeResult decoded =
        campaign::decode_save_envelope(bytes, campaign::SaveLoadOptions{});
    expect(static_cast<bool>(decoded), "the whole save decodes");

    campaign::DecodedSave without = decoded.decoded;
    without.sections.erase(
        std::remove_if(
            without.sections.begin(),
            without.sections.end(),
            [](const campaign::SaveSectionView& view) {
                return view.type == static_cast<std::uint32_t>(
                                        campaign::SaveSectionType::world
                                    );
            }
        ),
        without.sections.end()
    );
    const campaign::SaveLoadResult result =
        campaign::interpret_save(without, campaign::SaveLoadOptions{});
    expect(
        result.error == campaign::SaveError::missing_required_section,
        "and without one of them it is not a campaign"
    );
    expect(
        result.section == static_cast<std::uint32_t>(campaign::SaveSectionType::world),
        "and the diagnostic names the section that is not there"
    );
}

// The design's "migrate into temporary state", as a property: a rejected load
// leaves the session holding exactly what it was holding.
void a_rejected_load_leaves_the_live_session_untouched() {
    campaign::CampaignSave live = told_save();
    const campaign::CampaignSave before = live;
    const std::uint64_t fold = campaign::canonical_hash(live.state);

    const std::vector<std::uint8_t> good =
        campaign::save_campaign(campaign::make_campaign_save(campaign::CampaignState{}, {}));

    std::vector<std::uint8_t> corrupt = good;
    corrupt[read_u32(good, entry_of(good, 1U) + 12)] ^= 0xffU;
    expect(
        campaign::load_campaign_into(live, corrupt, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::checksum_mismatch,
        "a corrupt save is refused"
    );
    expect(live == before, "and the session is the campaign it was already holding");
    expect(campaign::canonical_hash(live.state) == fold, "unchanged, fold for fold");

    expect(
        campaign::load_campaign_into(live, {}, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::truncated,
        "so is nothing at all"
    );
    expect(live == before, "and still the session is untouched");

    expect(
        static_cast<bool>(
            campaign::load_campaign_into(live, good, campaign::SaveLoadOptions{})
        ),
        "and only a save that passes every gate replaces it"
    );
    expect(live.state.units.empty(), "with the campaign the save actually held");
}

// The layout is canonical: there is one byte encoding per save, so a fixture
// cannot drift by padding differently.
void the_layout_admits_exactly_one_encoding() {
    const std::vector<std::uint8_t> good = campaign::save_campaign(told_save());

    std::vector<std::uint8_t> moved = good;
    write_u32(moved, 40, directory_offset(good) + 4U);
    refresh(moved);
    expect(
        campaign::load_campaign(moved, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_directory,
        "a directory that does not follow the package table is refused"
    );

    std::vector<std::uint8_t> padded = good;
    padded.push_back(0U);
    write_u32(padded, 12, static_cast<std::uint32_t>(padded.size()));
    refresh(padded);
    expect(
        campaign::load_campaign(padded, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_header,
        "and so is a byte hiding after the last section"
    );

    std::vector<std::uint8_t> shifted = good;
    const std::size_t store_entry = entry_of(good, 2U);
    write_u32(shifted, store_entry + 12, read_u32(good, store_entry + 12) + 4U);
    refresh(shifted);
    expect(
        campaign::load_campaign(shifted, campaign::SaveLoadOptions{}).error ==
            campaign::SaveError::invalid_directory,
        "and so is a gap between two sections"
    );
}

}  // namespace

int main() {
    the_envelope_announces_itself_before_anything_is_interpreted();
    every_section_carries_its_own_version();
    a_campaign_survives_the_round_trip();
    a_branched_route_survives_the_round_trip();
    the_route_section_is_absent_rather_than_empty();
    a_damaged_route_is_refused_and_named();
    the_same_campaign_is_the_same_bytes();
    a_loaded_save_re_encodes_to_the_bytes_it_came_from();
    a_representative_campaign_has_a_measured_size();
    a_flipped_bit_is_caught_and_located();
    every_truncation_of_a_save_is_refused();
    a_section_starting_past_the_end_is_refused();
    an_oversized_save_is_refused_before_it_is_read();
    a_hostile_length_allocates_nothing();
    an_incompatible_save_is_refused_with_a_reason();
    an_unknown_section_is_retained_skipped_or_refused();
    bytes_cannot_smuggle_in_a_campaign_no_rule_could_reach();
    a_campaign_missing_a_section_is_not_a_campaign();
    a_rejected_load_leaves_the_live_session_untouched();
    the_layout_admits_exactly_one_encoding();
    return failures == 0 ? 0 : 1;
}
