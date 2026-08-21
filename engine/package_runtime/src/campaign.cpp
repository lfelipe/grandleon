// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/campaign.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace grandleon::package_runtime {
namespace {

class Reader final {
public:
    Reader(
        const package_format::LoadedPackage& package,
        const package_format::RecordView& record
    )
        : bytes_(package.byte_data()),
          cursor_(record.payload_offset),
          end_(static_cast<std::size_t>(record.payload_offset) +
               record.payload_size) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = bytes_[cursor_++];
        return true;
    }

    bool u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(
            bytes_[cursor_] |
            (static_cast<std::uint16_t>(bytes_[cursor_ + 1]) << 8U)
        );
        cursor_ += 2;
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (remaining() < 4) return false;
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(bytes_[cursor_++]) << shift;
        }
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(bytes_[cursor_++]) << shift;
        }
        return true;
    }

    bool skip_string() {
        std::uint16_t size = 0;
        if (!u16(size) || remaining() < size) return false;
        cursor_ += size;
        return true;
    }

    // A length-prefixed string, reported as where it is in the package rather
    // than copied out of it: the bytes outlive every definition decoded from
    // them, and a console has no allocator to spare for a name it will print
    // once. An offset rather than a pointer, so that the answer stays right
    // across a copy of the package and across an append to its bytes.
    bool string(std::uint32_t& offset, std::uint16_t& size) {
        std::uint16_t length = 0;
        if (!u16(length) || remaining() < length) return false;
        offset = static_cast<std::uint32_t>(cursor_);
        size = length;
        cursor_ += length;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return cursor_ <= end_ ? end_ - cursor_ : 0;
    }

    [[nodiscard]] bool finished() const noexcept {
        return cursor_ == end_;
    }

private:
    const std::uint8_t* bytes_;
    std::size_t cursor_;
    std::size_t end_;
};

// The fewest bytes each repeated thing in a campaign record can occupy, so a
// declared count can be measured against the bytes actually present before any
// container is asked to hold that many. Each is the record's fixed fields with
// every variable part at its shortest: a node with no dialogue, no target and
// no branches; a member whose name is empty; a specificity with no deltas.
//
// They are checked by division rather than multiplication, because the product
// of a hostile count and a stride is not representable everywhere the quotient
// is.
constexpr std::size_t node_minimum_size = 23;
constexpr std::size_t member_minimum_size = 26;
constexpr std::size_t grant_minimum_size = 20;
constexpr std::size_t specificity_minimum_size = 10;

}  // namespace

std::string_view error_name(CampaignError error) noexcept {
    switch (error) {
        case CampaignError::none: return "none";
        case CampaignError::missing_section: return "missing_section";
        case CampaignError::missing_record: return "missing_record";
        case CampaignError::malformed_payload: return "malformed_payload";
        case CampaignError::missing_reference: return "missing_reference";
        case CampaignError::unsupported_flow: return "unsupported_flow";
        case CampaignError::already_complete: return "already_complete";
        case CampaignError::outcome_incomplete: return "outcome_incomplete";
    }
    return "unknown";
}

std::string_view CampaignMember::name_in(
    const package_format::LoadedPackage& package
) const noexcept {
    const std::uint8_t* const bytes = package.byte_data();
    const std::size_t begin = name_offset;
    const std::size_t size = name_size;
    // Bounded again here rather than trusted from the decode. The decode
    // checked these bytes against the package it read them from, and this is
    // the only place that can check them against the package in hand.
    if (bytes == nullptr || size == 0 || begin > package.byte_size() ||
        size > package.byte_size() - begin) {
        return {};
    }
    return std::string_view(
        reinterpret_cast<const char*>(bytes + begin), size
    );
}

std::string_view character_loss_name(CharacterLoss loss) noexcept {
    switch (loss) {
        case CharacterLoss::permanent: return "permanent";
        case CharacterLoss::recoverable: return "recoverable";
    }
    return "unknown";
}

CampaignLoadResult load_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id
) {
    auto fail = [](CampaignError error) {
        CampaignLoadResult result;
        result.error = error;
        return result;
    };
    if (package.find(package_format::SectionType::campaigns) == nullptr) {
        return fail(CampaignError::missing_section);
    }
    const auto* record =
        package.find(package_format::SectionType::campaigns, campaign_id);
    if (record == nullptr) return fail(CampaignError::missing_record);

    Reader reader(package, *record);
    CampaignLoadResult result;
    std::uint16_t node_count = 0;
    if (!reader.skip_string() ||
        !reader.u64(result.definition.entry_node_id) ||
        !reader.u16(node_count) || node_count == 0 ||
        static_cast<std::size_t>(node_count) >
            reader.remaining() / node_minimum_size) {
        return fail(CampaignError::malformed_payload);
    }
    result.definition.nodes.reserve(node_count);
    std::set<std::uint64_t> node_ids;
    for (std::uint16_t index = 0; index < node_count; ++index) {
        CampaignNode node;
        std::uint8_t encoded_kind = 0;
        std::uint16_t target_count = 0;
        std::uint16_t dialogue_count = 0;
        if (!reader.u64(node.id) || !reader.u8(encoded_kind) ||
            !reader.u64(node.encounter_id) || !reader.u16(dialogue_count) ||
            node.id == 0 || !node_ids.insert(node.id).second) {
            return fail(CampaignError::malformed_payload);
        }
        for (std::uint16_t index = 0; index < dialogue_count; ++index) {
            std::uint64_t dialogue_id = 0;
            if (!reader.u64(dialogue_id)) {
                return fail(CampaignError::malformed_payload);
            }
            node.dialogue_ids.push_back(dialogue_id);
        }
        if (!reader.u16(target_count)) {
            return fail(CampaignError::malformed_payload);
        }
        if (encoded_kind == 1U || encoded_kind == 3U) {
            node.kind = encoded_kind == 1U ? CampaignNodeKind::encounter
                                           : CampaignNodeKind::story;
            if (node.kind == CampaignNodeKind::encounter) {
                if (node.encounter_id == 0 ||
                    package.find(
                        package_format::SectionType::encounters,
                        node.encounter_id
                    ) == nullptr) {
                    return fail(CampaignError::missing_reference);
                }
            } else if (node.encounter_id != 0) {
                return fail(CampaignError::unsupported_flow);
            }
            // At most one unconditional transition. More than one would make
            // the taken edge depend on authoring order.
            if (target_count > 1U) return fail(CampaignError::unsupported_flow);
            if (target_count == 1U) {
                if (!reader.u64(node.unconditional_target_id)) {
                    return fail(CampaignError::malformed_payload);
                }
                node.has_unconditional_target = true;
            }
        } else if (encoded_kind == 2U) {
            node.kind = CampaignNodeKind::terminal;
            if (node.encounter_id != 0 || target_count != 0) {
                return fail(CampaignError::unsupported_flow);
            }
        } else {
            return fail(CampaignError::unsupported_flow);
        }

        std::uint16_t branch_count = 0;
        if (!reader.u16(branch_count)) {
            return fail(CampaignError::malformed_payload);
        }
        if (node.kind == CampaignNodeKind::terminal && branch_count != 0U) {
            return fail(CampaignError::unsupported_flow);
        }
        for (std::uint16_t branch_index = 0; branch_index < branch_count;
             ++branch_index) {
            CampaignBranch branch;
            std::uint8_t encoded_combinator = 0;
            std::uint16_t predicate_count = 0;
            if (!reader.u64(branch.target_id) || !reader.u16(branch.priority) ||
                !reader.u8(encoded_combinator) ||
                !reader.u16(predicate_count)) {
                return fail(CampaignError::malformed_payload);
            }
            if (encoded_combinator < 1U || encoded_combinator > 3U ||
                predicate_count == 0U) {
                return fail(CampaignError::unsupported_flow);
            }
            // `not` negates exactly one predicate, which is what the source
            // schema allows; anything else would be ambiguous.
            if (encoded_combinator == 3U && predicate_count != 1U) {
                return fail(CampaignError::unsupported_flow);
            }
            branch.combinator =
                encoded_combinator == 2U   ? ConditionCombinator::any
                : encoded_combinator == 3U ? ConditionCombinator::none
                                           : ConditionCombinator::all;
            for (std::uint16_t index = 0; index < predicate_count; ++index) {
                CampaignPredicate predicate;
                std::uint8_t encoded_kind = 0;
                if (!reader.u64(predicate.subject) ||
                    !reader.u8(encoded_kind)) {
                    return fail(CampaignError::malformed_payload);
                }
                // Three is a world flag, and it brings a tail. One and two are
                // an objective's two results, exactly as they always were, and
                // they bring nothing, so a package that asks only about
                // objectives is read by byte-identical code paths.
                if (encoded_kind == 3U) {
                    std::uint8_t value_type = 0;
                    std::uint64_t raw = 0;
                    if (!reader.u8(value_type) || !reader.u64(raw)) {
                        return fail(CampaignError::malformed_payload);
                    }
                    // The world-value vocabulary is boolean and integer. A
                    // third type is a package this runtime cannot evaluate,
                    // and saying so beats guessing.
                    if (value_type != 1U && value_type != 2U) {
                        return fail(CampaignError::unsupported_flow);
                    }
                    predicate.kind = CampaignPredicateKind::world_flag_equals;
                    predicate.value_type = value_type;
                    predicate.value = static_cast<std::int64_t>(raw);
                    branch.predicates.push_back(predicate);
                    continue;
                }
                if (encoded_kind != 1U && encoded_kind != 2U) {
                    return fail(CampaignError::unsupported_flow);
                }
                predicate.kind = CampaignPredicateKind::objective_result;
                predicate.result = encoded_kind == 1U
                                       ? ObjectiveOutcome::satisfied
                                       : ObjectiveOutcome::failed;
                branch.predicates.push_back(predicate);
            }
            node.branches.push_back(std::move(branch));
        }
        if (node.kind != CampaignNodeKind::terminal &&
            !node.has_unconditional_target && node.branches.empty()) {
            return fail(CampaignError::unsupported_flow);
        }
        std::stable_sort(
            node.branches.begin(),
            node.branches.end(),
            [](const CampaignBranch& lhs, const CampaignBranch& rhs) {
                return lhs.priority < rhs.priority;
            }
        );
        result.definition.nodes.push_back(node);
    }
    // The company, if the package carries one. A campaign compiled before
    // rosters were authorable ends at its last node and decodes exactly as it
    // always did; whoever needs a company refuses that campaign by its own
    // name rather than inventing one here.
    if (reader.remaining() != 0U) {
        std::uint16_t member_count = 0;
        if (!reader.u16(member_count) ||
            static_cast<std::size_t>(member_count) >
                reader.remaining() / member_minimum_size) {
            return fail(CampaignError::malformed_payload);
        }
        result.definition.members.reserve(member_count);
        std::set<std::uint64_t> member_ids;
        for (std::uint16_t index = 0; index < member_count; ++index) {
            CampaignMember member;
            if (!reader.u64(member.id) ||
                !reader.string(member.name_offset, member.name_size) ||
                !reader.u64(member.unit_type_id) ||
                !reader.u64(member.join_node_id) || member.id == 0U ||
                member.unit_type_id == 0U ||
                !member_ids.insert(member.id).second) {
                return fail(CampaignError::malformed_payload);
            }
            result.definition.members.push_back(member);
        }
        // What the campaign puts in its store by authoring, if the package
        // carries any. A campaign compiled before grants were authorable ends
        // at its last member and decodes exactly as it always did, and so does
        // one that grants nothing.
        if (reader.remaining() != 0U) {
            std::uint16_t grant_count = 0;
            if (!reader.u16(grant_count) ||
                static_cast<std::size_t>(grant_count) >
                    reader.remaining() / grant_minimum_size) {
                return fail(CampaignError::malformed_payload);
            }
            result.definition.grants.reserve(grant_count);
            for (std::uint16_t index = 0; index < grant_count; ++index) {
                CampaignItemGrant grant;
                if (!reader.u64(grant.join_node_id) ||
                    !reader.u64(grant.item_id) ||
                    !reader.u32(grant.quantity) || grant.item_id == 0U ||
                    grant.quantity == 0U) {
                    return fail(CampaignError::malformed_payload);
                }
                result.definition.grants.push_back(grant);
            }
            // What the author wrote about individual members beyond their unit
            // types, if the package carries any. A campaign compiled before
            // specificities were authorable ends at its last grant and decodes
            // exactly as it always did, and so does one in which nobody is
            // written to be anything but their class.
            //
            // Nested inside the grants tail rather than beside it, because
            // these are positional tails and "the bytes ran out" is how each
            // one says it is absent. A campaign that authors a specificity and
            // no grants therefore writes a grant count of zero to hold this
            // tail's place, which the loop above reads as no grants: the same
            // campaign it would have decoded either way.
            if (reader.remaining() != 0U) {
                std::uint16_t specificity_count = 0;
                if (!reader.u16(specificity_count) ||
                    static_cast<std::size_t>(specificity_count) >
                        reader.remaining() / specificity_minimum_size) {
                    return fail(CampaignError::malformed_payload);
                }
                result.definition.specificities.reserve(specificity_count);
                for (std::uint16_t index = 0; index < specificity_count;
                     ++index) {
                    MemberSpecificity specificity;
                    std::uint8_t delta_count = 0;
                    if (!reader.u64(specificity.member_id) ||
                        !reader.u8(delta_count) ||
                        specificity.member_id == 0U ||
                        delta_count > specific_stat_count) {
                        return fail(CampaignError::malformed_payload);
                    }
                    for (std::uint8_t entry = 0; entry < delta_count; ++entry) {
                        std::uint8_t stat = 0;
                        std::uint16_t delta = 0;
                        // An unknown selector is refused rather than skipped. A
                        // package naming a stat this engine does not have was
                        // written by a newer compiler, and reading it as "the
                        // deltas I recognised and a shrug" would be a board
                        // that silently differs from the one the author built.
                        //
                        // A written zero is refused for the reason the compiler
                        // refuses one: it is an author saying nothing with a
                        // number, and omitting is how nothing is said. That
                        // every written delta is therefore non-zero is what
                        // makes the duplicate check below a comparison against
                        // zero rather than a second array of flags.
                        if (!reader.u8(stat) || !reader.u16(delta) ||
                            stat >= specific_stat_count || delta == 0U ||
                            specificity.stat_deltas[stat] != 0) {
                            return fail(CampaignError::malformed_payload);
                        }
                        specificity.stat_deltas[stat] =
                            static_cast<std::int16_t>(delta);
                    }
                    if (!reader.u8(specificity.reach_bonus) ||
                        specificity.empty()) {
                        return fail(CampaignError::malformed_payload);
                    }
                    result.definition.specificities.push_back(specificity);
                }
                // And what this campaign does when somebody falls, if the
                // package says anything about it. A campaign compiled before a
                // project could state a rule ends at its last specificity and
                // decodes exactly as it always did: as a campaign whose
                // characters are lost permanently, which is the only thing every
                // campaign before this ever meant.
                //
                // Nested inside the specificity tail rather than beside it, for
                // the reason that tail is nested inside the grants tail: these
                // are positional and "the bytes ran out" is how each one says it
                // is absent. A project that states a rule and authors no
                // specificities therefore writes a specificity count of zero to
                // hold this tail's place, exactly as one that authors a
                // specificity and no grants writes a grant count of zero.
                //
                // Both bytes are refused rather than shrugged at when they say
                // something this engine does not know. A package written by a
                // newer compiler that states a rule this one has never heard of
                // would otherwise be played under a rule its author did not
                // choose, and getting that wrong costs a player a character.
                if (reader.remaining() != 0U) {
                    std::uint8_t loss = 0;
                    std::uint8_t testing = 0;
                    if (!reader.u8(loss) || !reader.u8(testing) ||
                        (loss !=
                             static_cast<std::uint8_t>(CharacterLoss::permanent) &&
                         loss !=
                             static_cast<std::uint8_t>(CharacterLoss::recoverable)) ||
                        testing > 1U) {
                        return fail(CampaignError::malformed_payload);
                    }
                    result.definition.character_loss =
                        static_cast<CharacterLoss>(loss);
                    result.definition.invulnerable_for_testing = testing != 0U;
                    // And the weapons the company is handed, if the package
                    // carries any. A campaign compiled before a weapon could
                    // be handed over ends at its loss rule and decodes exactly
                    // as it always did, as a campaign handed none.
                    //
                    // Nested inside the loss tail for the reason that tail is
                    // nested inside the specificities: these are positional
                    // and "the bytes ran out" is how each says it is absent. A
                    // campaign that hands over a weapon and states no loss
                    // rule therefore writes the rule it already meant to hold
                    // this tail's place, exactly as one that authors a
                    // specificity and no grants writes a grant count of zero.
                    //
                    // The records join the store's own table rather than
                    // sitting apart: a grant is a grant, and which of the two
                    // tables it was written in is a fact about the bytes and
                    // not about the campaign.
                    if (reader.remaining() != 0U) {
                        std::uint16_t weapon_grant_count = 0;
                        if (!reader.u16(weapon_grant_count) ||
                            static_cast<std::size_t>(weapon_grant_count) >
                                reader.remaining() / grant_minimum_size) {
                            return fail(CampaignError::malformed_payload);
                        }
                        for (std::uint16_t index = 0;
                             index < weapon_grant_count; ++index) {
                            CampaignItemGrant grant;
                            if (!reader.u64(grant.join_node_id) ||
                                !reader.u64(grant.weapon_id) ||
                                !reader.u32(grant.quantity) ||
                                grant.weapon_id == 0U || grant.quantity == 0U) {
                                return fail(CampaignError::malformed_payload);
                            }
                            result.definition.grants.push_back(grant);
                        }
                    }
                }
            }
        }
    }
    if (!reader.finished()) return fail(CampaignError::malformed_payload);

    const auto find_node = [&result](std::uint64_t id) {
        return std::find_if(
            result.definition.nodes.begin(),
            result.definition.nodes.end(),
            [id](const CampaignNode& node) { return node.id == id; }
        );
    };
    if (find_node(result.definition.entry_node_id) ==
        result.definition.nodes.end()) {
        return fail(CampaignError::missing_reference);
    }
    for (const CampaignNode& node : result.definition.nodes) {
        if (node.has_unconditional_target &&
            find_node(node.unconditional_target_id) ==
                result.definition.nodes.end()) {
            return fail(CampaignError::missing_reference);
        }
        for (const CampaignBranch& branch : node.branches) {
            if (find_node(branch.target_id) == result.definition.nodes.end()) {
                return fail(CampaignError::missing_reference);
            }
        }
    }
    for (const CampaignMember& member : result.definition.members) {
        if (member.join_node_id != 0U &&
            find_node(member.join_node_id) == result.definition.nodes.end()) {
            return fail(CampaignError::missing_reference);
        }
    }
    for (const CampaignItemGrant& grant : result.definition.grants) {
        if (grant.join_node_id != 0U &&
            find_node(grant.join_node_id) == result.definition.nodes.end()) {
            return fail(CampaignError::missing_reference);
        }
    }
    std::sort(
        result.definition.nodes.begin(),
        result.definition.nodes.end(),
        [](const CampaignNode& lhs, const CampaignNode& rhs) {
            return lhs.id < rhs.id;
        }
    );
    return result;
}

CampaignCursor::CampaignCursor(CampaignDefinition definition)
    : definition_(std::move(definition)) {
    const auto found = std::lower_bound(
        definition_.nodes.begin(),
        definition_.nodes.end(),
        definition_.entry_node_id,
        [](const CampaignNode& node, std::uint64_t id) {
            return node.id < id;
        }
    );
    // load_campaign guarantees the entry node exists, but this constructor is
    // public API over any definition. A missing entry must not leave the
    // cursor indexing past the node list, so it degrades to an immediately
    // complete synthetic terminal instead.
    if (found == definition_.nodes.end() ||
        found->id != definition_.entry_node_id) {
        definition_.nodes.push_back(
            {definition_.entry_node_id, CampaignNodeKind::terminal,
             0, {}, 0, false, {}}
        );
        current_index_ = definition_.nodes.size() - 1;
        return;
    }
    current_index_ =
        static_cast<std::size_t>(found - definition_.nodes.begin());
}

const CampaignNode& CampaignCursor::current() const noexcept {
    return definition_.nodes[current_index_];
}

bool CampaignCursor::complete() const noexcept {
    return current().kind == CampaignNodeKind::terminal;
}

namespace {

bool holds(
    const CampaignPredicate& predicate,
    const std::vector<simulation::ObjectiveResult>& objectives
) noexcept {
    for (const simulation::ObjectiveResult& objective : objectives) {
        if (objective.id != predicate.subject) continue;
        return predicate.result == ObjectiveOutcome::satisfied
                   ? objective.state == simulation::ObjectiveState::satisfied
                   : objective.state == simulation::ObjectiveState::failed;
    }
    // An objective the encounter never reported cannot have reached a result.
    return false;
}

bool matches(
    const CampaignBranch& branch,
    const std::vector<simulation::ObjectiveResult>& objectives
) noexcept {
    if (branch.predicates.empty()) return false;
    switch (branch.combinator) {
        case ConditionCombinator::all:
            for (const CampaignPredicate& predicate : branch.predicates) {
                if (!holds(predicate, objectives)) return false;
            }
            return true;
        case ConditionCombinator::any:
            for (const CampaignPredicate& predicate : branch.predicates) {
                if (holds(predicate, objectives)) return true;
            }
            return false;
        case ConditionCombinator::none:
            return !holds(branch.predicates.front(), objectives);
    }
    return false;
}

}  // namespace

CampaignError CampaignCursor::advance_story() noexcept {
    if (complete()) return CampaignError::already_complete;
    if (current().kind != CampaignNodeKind::story) {
        return CampaignError::unsupported_flow;
    }
    return advance_after(
        simulation::Outcome::first_side_won,
        {}
    );
}

CampaignError CampaignCursor::advance_after(
    simulation::Outcome outcome
) noexcept {
    return advance_after(outcome, {});
}

CampaignError CampaignCursor::advance_after(
    simulation::Outcome outcome,
    const std::vector<simulation::ObjectiveResult>& objectives
) noexcept {
    if (complete()) return CampaignError::already_complete;
    if (outcome == simulation::Outcome::ongoing) {
        return CampaignError::outcome_incomplete;
    }
    const CampaignNode& node = current();
    std::uint64_t target = 0;
    bool decided = false;
    // Branches are already ordered by ascending priority value.
    for (const CampaignBranch& branch : node.branches) {
        if (matches(branch, objectives)) {
            target = branch.target_id;
            decided = true;
            break;
        }
    }
    if (!decided) {
        if (!node.has_unconditional_target) {
            return CampaignError::unsupported_flow;
        }
        target = node.unconditional_target_id;
    }
    const auto found = std::lower_bound(
        definition_.nodes.begin(),
        definition_.nodes.end(),
        target,
        [](const CampaignNode& candidate, std::uint64_t id) {
            return candidate.id < id;
        }
    );
    // The same reason the constructor checks its entry node: `load_campaign`
    // resolves every branch target and every unconditional target against the
    // node list, but this is public API over any definition, and a target the
    // list does not hold must not leave the cursor indexing past it. Refused
    // by name and the cursor left where it stood, because a definition naming
    // a node it does not have is a definition to say no to rather than a
    // campaign to keep walking.
    if (found == definition_.nodes.end() || found->id != target) {
        return CampaignError::missing_reference;
    }
    current_index_ =
        static_cast<std::size_t>(found - definition_.nodes.begin());
    return CampaignError::none;
}

}  // namespace grandleon::package_runtime
