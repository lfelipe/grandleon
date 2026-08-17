// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/specificity.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::package_runtime {

enum class EncounterLoadError : std::uint8_t {
    none = 0,
    missing_section,
    missing_record,
    malformed_payload,
    missing_reference,
    unsupported_objective,
};

[[nodiscard]] std::string_view error_name(
    EncounterLoadError error
) noexcept;

// How an unattended unit acts, kept beside the definition rather than inside
// it: behaviour is policy, and the simulation must not depend on it.
struct UnitBehaviorBinding final {
    simulation::UnitId unit_id{};
    tactics::Behavior behavior{tactics::Behavior::hold};
    std::vector<simulation::Position> patrol;
};

// Which authored placement one unit on the board came from.
//
// The instance id is encounter-specific; the source key identity is not. A
// character who appears in three encounters is three placements with three
// instance ids and one source key, which is why an objective names a target by
// source key rather than by instance, and why the campaign layer joins a
// roster to a board through this and nothing else.
struct PlacementIdentity final {
    simulation::UnitId unit_id{};
    std::uint64_t source_key_id{};
};

// What has to happen on the board for a moment's scene to play. The three a
// battle reports events for: the board being drawn, a character talked off it,
// and a character defeated. Leaving and dying are different facts and are
// reported by different events, so they are different triggers here.
enum class MomentTrigger : std::uint8_t {
    stage_opens = 1,
    character_talked = 2,
    character_falls = 3,
};

// A scene played while a battle is on, and its occasion.
//
// It travels beside the encounter rather than inside the definition, on the
// same terms as terrain identities and placement identities: the rules never
// ask what a moment is, and a rule that could would be a rule that behaved
// differently because somebody wrote a line of dialogue. `talk_record_id` is
// already opaque to the simulation for that reason, and this is the same
// argument one layer out.
struct EncounterMoment final {
    std::uint64_t id{};
    MomentTrigger trigger{MomentTrigger::stage_opens};
    // The placement this is about, as the author's own placement identity.
    // Zero for a moment about the board rather than about anybody.
    std::uint64_t placement_id{};
    std::uint64_t dialogue_id{};
};

struct EncounterLoadResult final {
    EncounterLoadError error{EncounterLoadError::none};
    simulation::EncounterDefinition definition;
    std::vector<UnitBehaviorBinding> behaviors;
    // One entry per unit in `definition.units`, in the same order.
    std::vector<PlacementIdentity> placements;
    // What is said while this battle is on, in the order it was authored. Empty
    // for an encounter nobody speaks during, which is every encounter written
    // before moments existed.
    std::vector<EncounterMoment> moments;
    // Row-major terrain identities for the encounter's map, width x height.
    // What a cell *is*, for a client that draws it: the identity resolves
    // through the package's presentation join into a picture. What a cell
    // *asks* of whoever stands in it travels inside the definition, as
    // `definition.terrain`, because that is a rule.
    std::vector<std::uint64_t> terrain;
    // The identity of the deployment region this encounter authors, or zero
    // for one that authors none. The region's *tiles* travel inside the
    // definition, as `definition.deployment_tiles`, because they are a rule;
    // the identity is here for the same reason the terrain identities are, so
    // a diagnostic or an editor can name the thing the author wrote.
    std::uint64_t deployment_zone_id{};
    // How many of a campaign's company this encounter lets take the field, or
    // zero for one that caps nothing, which is what every board says by
    // default and what the placements alone have always meant.
    //
    // Here rather than inside the definition, and the split is the same one
    // the zone's identity makes for a different reason. The zone's *tiles* are
    // a rule of the battle and travel inside; a capacity is not a rule of the
    // battle at all. Who is allowed out of a company is a campaign judgement,
    // made by `campaign_runtime` above this layer and never by the simulation,
    // because the day a canonical hash depends on how many of an army were let
    // out is the day it depends on a save file.
    std::uint16_t deployment_capacity{};
    // What the campaign says about the members this board fields, beyond their
    // unit types, by authored member identity. That is the same identity
    // `placements` already carries as a source key.
    //
    // Empty for a board loaded outside any campaign, which is every board a
    // conformance replay, a rosterless playtest or a console ROM without a
    // campaign loads, and which is why none of them moves.
    //
    // Here rather than inside the definition for the reason the capacity is:
    // what an author wrote about a *character* is a campaign judgement, and the
    // simulation never learns it. And filled by whoever knows the campaign
    // rather than by `load_encounter`, because a specificity is authored on the
    // campaign record and `load_encounter` deliberately does not know which
    // campaign a board belongs to. `campaign_runtime::member_specificities`
    // turns a campaign definition into this table, and the caller that has one
    // attaches it.
    //
    // It rides on the board so that the join can read it, and the join takes a
    // board rather than a package precisely so the editor's Play mode, which
    // has content that was never compiled, goes through the same pass as
    // everything else. A caller that can build a board can apply a specificity.
    std::vector<MemberSpecificity> member_specificities;
    // Whether the campaign this board belongs to has declared that its company
    // cannot be reduced below one health.
    //
    // It rides here for exactly the reasons `member_specificities` rides here,
    // and it is the same journey: authored on the campaign record, attached by
    // the caller that knows which campaign is being played, and applied by the
    // roster join onto the units members actually stand in. `load_encounter`
    // does not know which campaign a board belongs to and this is not its
    // business to find out.
    //
    // False for a board loaded outside any campaign, which is every board a
    // conformance replay, a rosterless playtest or a console ROM without a
    // campaign loads, which is why none of them moves.
    bool company_endures{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == EncounterLoadError::none;
    }
};

// Decodes one already structurally validated package encounter. No partially
// decoded definition is published when any payload or reference is invalid.
[[nodiscard]] EncounterLoadResult load_encounter(
    const package_format::LoadedPackage& package,
    std::uint64_t encounter_id
);

}  // namespace grandleon::package_runtime
