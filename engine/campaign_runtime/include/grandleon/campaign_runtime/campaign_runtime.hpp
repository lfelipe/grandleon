// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/identity.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/state.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/progression.hpp>
#include <grandleon/package_runtime/starting_kit.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// Where the compiled content and the persistent campaign meet.
//
// `engine/campaign/README.md` names this module: "`CampaignCursor` is not
// durable state. It walks an *authored* flow decoded out of a package section:
// which node is next, which branch its predicates take. That is content, and
// it belongs with the other decoded content. What lives here is what the
// *player* accumulated... The two meet in `engine/campaign_runtime`, which
// links both and adds a dependency to neither."
//
// This is above both modules. It links `grandleon::campaign` and
// `grandleon::package_runtime` and adds nothing to either: the campaign still
// links only the portable core, so a save layer still reaches a roster without
// dragging in a rules engine, and the package runtime still knows nothing
// about a roster, so a package still decodes without one. The simulation is
// below both and learns nothing at all, which is the constraint that matters:
// a battle is battle-local, and the day it reads a roster is the day a
// canonical hash depends on a save file.
//
// Two joins live here.
//
// ## The graph, out of a package
//
// `build_campaign_graph` translates one decoded `CampaignDefinition` into a
// `campaign::CampaignGraph`. It is a translation and not a new authoring
// surface: the source schema authors nodes, priorities, conditional
// transitions and a single unconditional target, and the compiler encodes
// them. What this module adds is the *evaluation and persistence* semantics
// for a graph the schema already authors, so it moves no schema, no package
// byte, and no golden.
//
// What that leaves undone is stated plainly rather than implied. The source
// schema's `inventoryAtLeast` and `worldFlagEquals` predicates are authorable
// and schema-valid, and the compiler refuses them with "only objectiveResult
// predicates are executable in the vertical runtime". That sentence is
// inexact about the *runtime* (`campaign::predicate_holds` evaluates all
// three) and exact about the *compiler*, which has no encoding for the other
// two. Teaching it one is a package-format change and therefore its own piece
// of work; until then a creator can ask two of the three questions only
// through content this layer is handed directly.
//
// ## The roster, into an encounter
//
// `load_encounter_for_campaign` is the requirement "encounter creation SHALL
// consult persistent campaign availability and SHALL NOT spawn a uniquely
// identified character that is dead, retired, unrecruited, or otherwise
// unavailable". A campaign member is joined to an authored placement by that
// placement's source-key identity: the same identity an objective uses to name
// a target, and the only one that is the same character across two encounters.
//
// `package_runtime::load_encounter` means exactly what it says and no more: no
// campaign attached, no exclusion, the same board. That is deliberate, and it
// is why every golden holds.

namespace grandleon::campaign_runtime {

// ---------------------------------------------------------------------------
// The graph
// ---------------------------------------------------------------------------

enum class GraphSourceError : std::uint8_t {
    none = 0,
    // The campaign's own identity was not given, so its nodes could not be
    // namespaced.
    unidentified_campaign,
    // The translated graph failed `campaign::validate_graph`;
    // `CampaignGraphSource::graph_error` says how. The commonest cause is two
    // conditional transitions out of one node sharing a priority, which the
    // compiled flow can still express and the graph semantics cannot accept:
    // an edge decided by array order is not a decided edge.
    invalid_graph,
};

[[nodiscard]] std::string_view graph_source_error_name(
    GraphSourceError error
) noexcept;

struct CampaignGraphSource final {
    GraphSourceError error{GraphSourceError::none};
    campaign::GraphError graph_error{campaign::GraphError::none};
    campaign::CampaignGraph graph;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == GraphSourceError::none;
    }
};

// Translate a decoded authored flow into the graph the persistent layer
// evaluates. `campaign_id` is the campaign's own stable content id, which the
// caller already had to know to load the flow at all.
[[nodiscard]] CampaignGraphSource build_campaign_graph(
    const core::PackageId& package,
    std::uint64_t campaign_id,
    const package_runtime::CampaignDefinition& definition
);

// The same, straight out of a mounted package. A flow the package rejects
// answers with the package runtime's own diagnostic rather than a translated
// one, because a graph that was never decoded has nothing to say about itself.
struct CampaignGraphLoad final {
    package_runtime::CampaignError load_error{package_runtime::CampaignError::none};
    CampaignGraphSource source;

    [[nodiscard]] explicit operator bool() const noexcept {
        return load_error == package_runtime::CampaignError::none &&
               static_cast<bool>(source);
    }
};

[[nodiscard]] CampaignGraphLoad load_campaign_graph(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id
);

// Which node of a graph an encounter is fought at, so a caller holding a
// position can find the board to load. Zero when the node is not an encounter
// node or is not in the flow.
[[nodiscard]] std::uint64_t encounter_of_node(
    const package_runtime::CampaignDefinition& definition,
    const campaign::DefinitionRef& node
) noexcept;

// The node reference a flow node id becomes, and the reverse. Stated as
// functions rather than left to each caller, because a node identity that two
// call sites spell differently is a save that stops resuming.
[[nodiscard]] campaign::DefinitionRef campaign_node_ref(
    const core::PackageId& package,
    std::uint64_t node_id
) noexcept;

// ---------------------------------------------------------------------------
// The roster
// ---------------------------------------------------------------------------

// Which campaign member stands in for one authored placement.
//
// The placement is named by its source-key identity rather than by its
// per-encounter instance id, because the instance id is different in every
// encounter the character appears in and the source key is not. Who is on the
// roster is campaign state and never content, so this table is supplied by the
// caller. The package does not know the player's roster and must not.
struct RosterAssignment final {
    std::uint64_t placement_source_key{};
    campaign::PersistentEntityId member{};
};

enum class RosterError : std::uint8_t {
    none = 0,
    // The encounter itself did not load; `CampaignEncounter::load_error` says
    // why.
    encounter_rejected,
    // Two assignments name one placement, or one member twice. Either would
    // make the board depend on which was read first.
    duplicate_assignment,
    // An assignment named the reserved zero member id.
    reserved_identity,
    // Every unit of a side was excluded. A board one side cannot field is not
    // an encounter, and publishing it would be a battle decided before it
    // began.
    side_emptied,
    // An excluded member was an objective's target. The encounter cannot be
    // fought as authored (protecting or defeating somebody who is not there
    // is not a rule with an answer), and the spec's other branch, an authored
    // unavailable-character route, is content this schema does not yet carry.
    // Refusing names the problem instead of publishing a board whose objective
    // can never resolve.
    unavailable_objective_target,
    // More of the company would take the field than the encounter's authored
    // `deployment.capacity` allows. The player answers it by benching
    // somebody: the engine refuses rather than trimming, because choosing who
    // fights is the one decision this layer exists to leave with them, and an
    // auto-trim would have to pick deterministically and would therefore
    // silently prefer whoever was recruited first.
    //
    // A cap never leaves a placement standing empty. A member the cap keeps in
    // is a member the exclusion pass drops, exactly as it drops a dead or a
    // benched one, so the two consequences a smaller board can have are the two
    // it already had: `side_emptied` and `unavailable_objective_target`. That
    // the cap invents no third failure is why it can be a count rather than a
    // second arrangement of the battle.
    over_deployment_capacity,
};

[[nodiscard]] std::string_view roster_error_name(RosterError error) noexcept;

struct CampaignEncounter final {
    RosterError error{RosterError::none};
    package_runtime::EncounterLoadError load_error{
        package_runtime::EncounterLoadError::none
    };
    // The board, with every unavailable member left off it.
    package_runtime::EncounterLoadResult encounter;
    // Board id to campaign member, for the units that were placed. Partial in
    // one direction on purpose: a placement no assignment names is a summon, a
    // bystander, or the opposing side, and has no campaign identity at all.
    campaign::BattleBinding binding;
    // Who the roster kept off the board, ascending. A permanently dead member
    // appears here and nowhere else, however many later maps name them.
    std::vector<campaign::PersistentEntityId> excluded;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == RosterError::none;
    }
};

// Load an encounter for a campaign, leaving off everyone the roster says
// cannot take the field.
//
// A member who is unrecruited, retired or dead is excluded, and no map naming
// them changes that. An assignment naming somebody the roster has never heard
// of is excluded too: a character the campaign does not hold is not a
// character it can deploy.
// Load an encounter for a campaign, leaving off everyone the roster says
// cannot take the field and giving everyone who does take it the points their
// level-ups granted and the kit the campaign holds for them.
[[nodiscard]] CampaignEncounter load_encounter_for_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t encounter_id,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
);

// The same, for a caller that can name the campaign the board is being loaded
// for.
//
// The four-argument form above joins a roster to a board and applies what the
// characters *earned*. This one also applies what an author *wrote* about them,
// and it needs the campaign identity to do it because a specificity is authored
// on the campaign record. `package_runtime::load_encounter` deliberately does
// not know which campaign a board belongs to, and a board is loadable outside
// every campaign.
//
// A campaign identity of zero is exactly the four-argument form, and a package
// that holds no such campaign loads the board with no specificities rather than
// refusing it: a board that cannot find a campaign is a board with no campaign,
// which is a state this module has always had a reading for.
[[nodiscard]] CampaignEncounter load_encounter_for_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    std::uint64_t encounter_id,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
);

// What a campaign says about its members beyond their unit types, as a board
// carries it.
//
// Content in, a table out: no rule, no state, no stream. It is the shape
// `starting_kit`, `starting_store` and `node_item_grants` all have, and for the
// same reason. It is stated as a function rather than left to each caller
// because every caller that hands out a board has to attach the same table, and
// a table each caller built for itself is a table each caller could build
// differently.
//
// The result holds an entry only for a member who authors something, in
// authored member order, which is the order the campaign record carries.
[[nodiscard]] std::vector<package_runtime::MemberSpecificity>
member_specificities(const package_runtime::CampaignDefinition& definition);

// The same join, over a board the caller already has.
//
// Joining a roster to a board never needed a package; it needed a board. The
// split is stated because there is a client that cannot produce the first and
// can produce the second: the editor plays content that has never been
// compiled, so its Play mode holds an `EncounterLoadResult` it built from
// unsaved source and no package at all. Giving that client its own exclusion
// pass would be the one duplication this module exists to prevent, so the pass
// takes the board instead.
//
// `load_encounter_for_campaign` is exactly `load_encounter` followed by this,
// including the order its refusals are decided in: an assignment table that
// cannot be believed is refused before a board is read, here as there.
[[nodiscard]] CampaignEncounter join_campaign_roster(
    package_runtime::EncounterLoadResult&& loaded,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
);

// Which members this board has a placement for, ascending.
//
// A reading rather than a rule, and the distinction is the whole of why it is
// three lines: it looks at the board's own placement identities and the
// caller's assignment table, consults no campaign state, and decides nothing
// about who may take the field. The exclusion pass above is still the only
// thing that decides that.
//
// It exists because a between-battle screen that offers "field this member"
// must not offer it for somebody the next board has nowhere to stand. Fielding
// them would be an availability the campaign accepts and a board that changes
// not at all, which is the one kind of refusal worse than a refusal: a gesture
// that succeeds and does nothing.
//
// A member the table names and the board does not place is absent from the
// result whatever the campaign holds about them: a future recruit, a member of
// another chapter's company, or somebody the author simply left off this map.
[[nodiscard]] std::vector<campaign::PersistentEntityId> members_a_board_places(
    const package_runtime::EncounterLoadResult& board,
    const std::vector<RosterAssignment>& roster
);

// Which members would actually take this board's field, ascending.
//
// `members_a_board_places` answers "has this board anywhere to put them"; this
// answers "and would they be let out": the placed members the campaign says
// are deployable, which is exactly the set the exclusion pass keeps and
// therefore exactly the set the capacity is counted against.
//
// A reading, exposed for the same reason `simulation::deployable_tiles` is
// exposed: it is the judgement the rule already makes, published so that no
// client re-implements it and no screen offers a gesture the engine would
// refuse. A client compares its size against `deployment_capacity` before it
// commits a fielding, and shows the name the engine would have shown. That
// check is an early copy of the engine's and never a substitute for it.
// `join_campaign_roster` refuses over-cap whatever any client believed.
[[nodiscard]] std::vector<campaign::PersistentEntityId> members_a_board_fields(
    const package_runtime::EncounterLoadResult& board,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
);

// ---------------------------------------------------------------------------
// The satchel
// ---------------------------------------------------------------------------

// What a character carries into a campaign's battle is what the campaign holds
// for them, and this is the pair of functions that makes that one sentence
// true at both ends.
//
// **On the way to the board**, the join above fills every bound unit's
// `item_ids` and `item_counts` from that member's `campaign::PersistentUnit::carried`
// stacks, in the same pass that adds the points their level-ups granted. A unit
// no member stands in (an enemy, a bystander, a summon) is untouched and goes
// on carrying exactly what its type lists, which is what keeps a board with no
// campaign attached the board it always was.
//
// The order is the *type's* authored order for every identity the kit and the
// type agree on, and ascending item identity for anything else the kit holds. A
// campaign's collections are sorted by identity because two campaigns told the
// same things must be the same bytes; an authored list is not sorted, and
// `item_ids` is hashed in order. Walking the authored list first is what makes a
// member holding one of each authored item reproduce the authored board exactly,
// canonical hash and all, which is the property every golden in the gate rests
// on.
//
// **On the way in**, `starting_kit` is where a unit type's authored list is
// read for a campaign: once, in the batch that recruits the member, and never
// again. That is the whole of what `startingItemIds` means with a campaign
// attached, and it is why an author still states what a character starts with in
// exactly one place. Without a campaign the list keeps its old meaning
// unchanged (the satchel, every battle) because
// `package_runtime::load_encounter` is untouched.
//
// A kit that holds something the board does not register is carried through
// rather than quietly dropped: `create_encounter` refuses an unregistered item
// by name. With kit movement deferred a kit is always a subset of what its type
// lists, so the case cannot arise yet; refusing loudly is the right failure for
// the wave that will make it possible.

struct StartingKit final {
    // False when the package does not carry the unit type, or its record does
    // not decode. A type that lists nothing is a success carrying nothing.
    bool found{false};
    // `campaign::add_item` against the member, one per authored item, in the
    // order the type lists them.
    std::vector<campaign::CampaignOutcomeOperation> operations;

    [[nodiscard]] explicit operator bool() const noexcept { return found; }
};

// The kit a member joins with, as operations for the batch that recruits them.
//
// Ordinary committed operations rather than a special rule, so that stocking is
// atomic with the recruitment that caused it, idempotent on a retry, and
// carried by a save with nothing new to teach the save format.
[[nodiscard]] StartingKit starting_kit(
    const package_format::LoadedPackage& package,
    campaign::PersistentEntityId member,
    std::uint64_t unit_type_id
);

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

// What a campaign is given by its author, as operations for the batch that
// gives it.
//
// The store is the second half of the sentence the satchel above is the first
// half of. `starting_kit` says what one character joins holding; this says what
// the *company* holds: the shared store, `campaign::CampaignState::store`,
// addressed by the reserved zero owner that `campaign::add_item` has always
// read as "the army".
//
// Two authored fields, one function, because they are one fact told at two
// moments. `campaign.startingStore` is what the company is founded with and
// carries a join node of zero; `campaignNode.grants` is what passing a node
// puts there and carries that node. Ask for zero and you get the founding
// stock; ask for a node and you get that node's grants, in authored order.
//
// **A grant is an event, not a statement about how much the store should
// hold.** "Passing this node put three tonics in the store" composes with
// everything else that ever touched the store, survives a retry by identity
// rather than by comparison, and can be narrated straight off the batch. "The
// store holds three tonics here" would have to decide what it means when the
// store already holds five, and it would make the store the one collection
// whose history does not reconstruct it.
//
// **So a route that loops grants again, and a retry does not.** A node reached
// a second time commits under an `OutcomeSource` whose sequence has moved, so
// it is a different batch and the blessing is given twice, which is what a road
// past the abbot twice is for. A batch recomputed from an unchanged campaign
// folds the same source over the same operations, so it is the same id and
// `apply_outcome` answers `already_applied` and changes nothing. The whole
// distinction is in the sequence, which is why a caller derives it at the
// moment it builds the batch and never caches it.
//
// Ordinary committed operations rather than a rule of their own, on exactly the
// terms the satchel is: atomic with the founding or the transition that caused
// it, idempotent on a retry, and carried by a save with nothing new to teach
// the save format: a quantity of an item identity is what an `InventoryStack`
// has always held.
[[nodiscard]] std::vector<campaign::CampaignOutcomeOperation> node_item_grants(
    const core::PackageId& package,
    const package_runtime::CampaignDefinition& definition,
    std::uint64_t node_id
);

// ---------------------------------------------------------------------------
// Growth
// ---------------------------------------------------------------------------

// The bound every growth chance is rolled against. Chances are authored as
// whole percentages and rolled as whole percentages: one number, one meaning,
// and the number an author writes is the number this divides by, the same
// honesty rule accuracy keeps (`engine/simulation/src/encounter.cpp`).
inline constexpr std::uint32_t growth_chance_bound = 100U;

// One character's level-up, as it happened. Not a rule and not state: a record
// of what the rolls said, so a surface can narrate it without re-deriving it
// and a test can assert it without a save.
struct LevelUp final {
    campaign::PersistentEntityId member{};
    std::uint16_t from_level{};
    std::uint16_t to_level{};
    // Points gained per stat across every level in this batch, indexed by
    // `campaign::GrowableStat`.
    std::array<std::uint16_t, campaign::growable_stat_count> points{};
};

enum class ProgressionSourceError : std::uint8_t {
    none = 0,
    // A board unit bound to a campaign member the roster does not hold, or
    // holds as somebody who could not have been there. The batch is refused
    // rather than built, because an operation `apply_outcome` would reject is
    // worse than no operation at all.
    unknown_member,
    // A unit type the package does not carry, or whose growth block is not one.
    unreadable_unit_type,
};

[[nodiscard]] std::string_view progression_source_error_name(
    ProgressionSourceError error
) noexcept;

struct BattleProgression final {
    ProgressionSourceError error{ProgressionSourceError::none};
    // The consequences, ready to go into a `CampaignOutcomeBatch` beside
    // whatever else the battle decided.
    //
    // The progression half comes first, ascending by member, and within a
    // member: the experience, then the levels, then what the levels gave. The
    // inventory half is appended after all of it (what fell, then what was
    // spent) so that adding it moved no operation that was already there and
    // no growth draw that was already taken.
    std::vector<campaign::CampaignOutcomeOperation> operations;
    // One entry per member who reached a new level, ascending by member.
    std::vector<LevelUp> level_ups;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ProgressionSourceError::none;
    }
};

// What a finished battle did to the roster's numbers.
//
// This is where experience is earned and where growth rolls, and both answers
// are deliberate.
//
// **Experience is derived at battle end, not carried through the battle.** The
// simulation never learns that a campaign exists. Nothing about a level is
// battle-local state, so no `UnitSnapshot` grew a field, no canonical hash
// moved, no forecast changed, and every golden holds. What the battle
// contributes is what it already recorded: a `unit_defeated` event names the
// unit that fell and, in `related_unit_id`, the unit that felled it. That pair
// plus the board's unit types is the whole input.
//
// **The rule, as an author sees it.** Defeating a unit grants that unit type's
// authored `experienceAward` to the roster member who struck the felling blow.
// A member the battle itself buried earns nothing. A character's level is
// `1 + lifetime experience / their own type's experiencePerLevel`, capped at
// `campaign::maximum_progression_level`; a character already at the cap earns
// nothing, so the batch contains nothing about them at all.
//
// **Growth rolls here, on a stream seeded from the completed battle.**
// `campaign::derive_growth_seed` explains the seed. The consumption order is
// fixed once, and it is:
//
//   1. Members are taken in ascending persistent id, whatever order the events
//      arrived in. Who earned what decides the order; when they earned it does
//      not.
//   2. A member who reached no new level draws nothing. So does a member at the
//      level cap, and a member the battle buried.
//   3. For each level gained, in order, every growable stat is rolled once, in
//      `campaign::GrowableStat` order: health, strength, defense, resistance,
//      movement, action points, skill, luck, evasion, magic. Two levels in one
//      battle is twenty rolls, not ten doubled. The last four were appended
//      when the stat line grew, and appended is the operative word: an existing
//      seed's first six draws per level are the six draws they always were.
//   4. A chance of zero draws nothing and a chance of a hundred draws nothing,
//      exactly as `core::RandomState::roll_chance` defines them. A unit type
//      that authored no growth therefore moves the stream not at all, which is
//      what keeps this order checkable against the content.
//   5. The bound is a hundred, because the chance is a whole percentage.
//
// **What a battle did to what the campaign owns, from the same events.** Two
// event types carry an inventory consequence, and both are derivable from the
// events alone, which is what let the battle stay ignorant of campaigns:
//
//   * `item_used` names who spent one of what, and exactly one is spent per
//     event. Each becomes one `consume_item` of that identity.
//   * `item_dropped` names who fell, who felled them, and what fell, and
//     exactly one thing falls per event. Each becomes one `add_item`.
//
// **A spend lands on the spender's kit; a drop lands in the shared store.** The
// campaign model has both (`campaign::CampaignState::store` beside
// `PersistentUnit::carried`), and each half has its own right owner.
//
// A spend belongs to the spender because that is where the thing came from: a
// campaign character takes the field carrying their own kit and nothing else
// (see "The satchel" above), so charging the store would claim the army paid for
// something it never held. The reasoning turns on that premise and not on the
// rule: where a satchel comes from the *unit type* rather than from the
// campaign, the opposite is right, because charging a member's kit would then
// claim they held something the campaign never gave them.
//
// A drop belongs to the army, because a thing picked off a battlefield belongs
// to whoever is marching rather than to whichever hand struck last. That is
// the same judgement `record_permanent_death` makes when it returns a dead
// member's kit to the store.
//
// **The binding decides whether the consequence is the campaign's at all.** An
// enemy drinking its own tonic, or felling another enemy, is not the campaign's
// business: the acting unit (the spender, or the *claimant* of a drop) must
// bind to a roster member. A drop nobody on the roster claimed is recorded by
// the battle and ignored here.
//
// **What fell is added before what was spent is consumed**, so a batch that
// somehow both gains and spends the same identity is not refused for an
// ordering. `consume_item` is refused when the owner holds fewer, which is what
// makes an inventory consequence a fact rather than a wish. And because the
// owner is the character who carried it, that refusal cannot be reached by
// ordinary play: what a character can spend is what the campaign put in their
// hands.
//
// **These operations must be committed before the deaths.** `apply_outcome`
// refuses any operation against a permanently dead member, so a batch that
// buries a character before recording the draught they drank refuses itself.
// The client orders a battle's consequences as what the characters did, then who
// did not come back, which is also the true sequence and what leaves the
// permanent death returning only what is actually left of a kit.
//
// `board` is the definition the battle was fought from: the one
// `load_encounter_for_campaign` published, not the one the package alone
// holds, because that is what says which unit type each board id was.
[[nodiscard]] BattleProgression derive_battle_progression(
    const package_format::LoadedPackage& package,
    const campaign::CampaignState& state,
    const simulation::EncounterDefinition& board,
    const campaign::BattleBinding& binding,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source
);

// One board unit and the type it was fielded as.
//
// This is the *whole* of what the derivation above reads out of a board: an
// experience award is a property of the unit type of whoever was defeated, and
// everything else it needs comes from the events, the binding, the campaign
// state and the package. Naming that plainly lets a caller who cannot afford to
// keep a whole encounter alive across a battle keep this instead, which on a
// machine that counts its heap in kilobytes is the difference between a
// campaign that can publish a board and one that cannot.
struct BoardUnitType final {
    simulation::UnitId unit{};
    std::uint64_t unit_type_id{};
};

// The table above, read off a board, in the board's own unit order.
//
// Waves are expanded through `simulation::expand_arrivals`, so the table holds
// a row for every character the battle will actually field rather than only for
// the placements the author wrote. The second of a wave is a character somebody
// can kill, and a table that did not name it would leave the campaign unable to
// say what the kill was worth.
[[nodiscard]] std::vector<BoardUnitType> board_unit_types(
    const simulation::EncounterDefinition& board
);

// The same derivation, over the table instead of the board.
//
// The overload above is this one plus `board_unit_types`, so the two cannot
// derive different campaigns from the same battle.
[[nodiscard]] BattleProgression derive_battle_progression(
    const package_format::LoadedPackage& package,
    const campaign::CampaignState& state,
    const std::vector<BoardUnitType>& board,
    const campaign::BattleBinding& binding,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source
);

}  // namespace grandleon::campaign_runtime
