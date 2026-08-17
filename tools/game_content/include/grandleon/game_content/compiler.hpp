// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace grandleon::game_content {

using StableId = std::uint64_t;

struct Stats final {
    std::int16_t health{};
    std::int16_t strength{};
    std::int16_t defense{};
    // Reduces magical damage the way defense reduces physical damage.
    std::int16_t resistance{};
    std::uint8_t movement{};
    std::uint8_t action_points{1};
    std::uint8_t speed{1};
    //: The richer stat line, appended at the tail of the class record rather
    //: than written beside the six above, so a package written before these
    //: existed still reads: a record that ends before them is a class with all
    //: four at zero, which is exactly what zero bytes would say. `skill` and
    //: `luck` raise the chance this class's strikes land, `evasion` and `luck`
    //: lower the chance strikes against it land, and `magic` prices a magical
    //: cast the way `strength` prices a swing. A physical cast is priced by
    //: neither: `strength` already reaches the board on every basic attack.
    std::int16_t skill{};
    std::int16_t luck{};
    std::int16_t evasion{};
    std::int16_t magic{};
};

struct StatModifiers final {
    std::int16_t health{};
    std::int16_t strength{};
    std::int16_t defense{};
    std::int16_t movement{};
};

struct WeaponType final {
    StableId id{};
    std::string name;
};

struct ItemType final {
    StableId id{};
    std::string name;
};

//: What a class may enter beyond open ground, in the simulation's own bits
//: (`grandleon::simulation::crossing_water` and its neighbours). Repeated here
//: rather than included so the compiler keeps depending on the package format
//: alone; the loader hands these straight to the engine, and the package
//: contract in tools/source_schema/SOURCE_FORMAT.md pins the encoding.
inline constexpr std::uint8_t crossing_none = 0U;
inline constexpr std::uint8_t crossing_water = 1U << 0U;
inline constexpr std::uint8_t crossing_heights = 1U << 1U;
inline constexpr std::uint8_t crossing_every = 1U << 7U;

//: The bit a `class.traversal.crossings` name asks for, or zero for a name the
//: vocabulary does not hold. The source schema enumerates exactly these.
[[nodiscard]] std::uint8_t crossing_bit(std::string_view name) noexcept;

struct UnitClass final {
    StableId id{};
    std::string name;
    Stats base_stats{};
    std::vector<StableId> allowed_weapon_types;
    //: Whether this class states a weapon allowance at all, which is a
    //: different question from what the allowance holds. A class that states
    //: none has unrestricted access (the legacy shape, and what
    //: `SOURCE_FORMAT.md` says an omitted `allowedWeaponTypeIds` means), while
    //: a class that states an empty one permits no weapon type whatsoever. The
    //: two encode identically, because the package writes a list and a list of
    //: nothing is a list of nothing; the difference is a rule about what an
    //: author may then equip, and it lives here because the encoded bytes
    //: cannot carry it.
    bool states_allowed_weapon_types{false};
    bool acts_after_attacking{false};
    //: Terrain this class crosses beyond open ground. Zero is a class that
    //: authored no traversal, and it walks, which is what every class did
    //: before terrain could stop anyone.
    std::uint8_t crossings{crossing_none};
};

struct Weapon final {
    StableId id{};
    std::string name;
    StableId type_id{};
    std::int16_t power{};
    std::uint8_t minimum_range{1};
    std::uint8_t maximum_range{1};
    //: How often this weapon lands, as a whole percentage in [0, 100]. One
    //: hundred is the default, and what every weapon authored before attacks
    //: could miss says: it always lands and rolls no number at all.
    std::uint8_t accuracy{100};
};

//: What spending an item in battle does. `none` is what every item authored
//: before items could be spent means, and it is a refusal at the point of use
//: rather than a silent nothing. Append-only: the value is written into the
//: package record and read by `simulation::ItemKind`.
enum class ItemKind : std::uint8_t {
    none = 0,
    restore = 1,
};

struct Item final {
    StableId id{};
    std::string name;
    StableId type_id{};
    std::uint16_t stack_limit{1};
    ItemKind kind{ItemKind::none};
    //: How much a restoring item gives back. Zero for an item that does
    //: nothing, and refused for one that claims to restore.
    std::int16_t power{};
};

//: Which stats a level-up may add a point to, and the order every level-up
//: rolls them in. The order is part of the rules rather than an implementation
//: detail: it fixes the growth stream's consumption order, so it is stated
//: once here and matched byte for byte by the record the compiler writes, by
//: `campaign::GrowableStat`, and by the schema's `growthRates` description.
//:
//: `speed` is deliberately absent: it orders a whole turn rather than pricing
//: one blow, so growing it reshuffles who acts when, and it deserves its own
//: decision. Everything else the stat line holds is here.
//:
//: The list is append-only. `skill`, `luck`, `evasion` and `magic` were added
//: at the end rather than beside the stats they read most like, because these
//: values are indexed into and persisted: an existing growth seed's first six
//: draws per level have to stay the six draws they always were.
enum class GrowableStat : std::uint8_t {
    health = 0,
    strength = 1,
    defense = 2,
    resistance = 3,
    movement = 4,
    action_points = 5,
    skill = 6,
    luck = 7,
    evasion = 8,
    magic = 9,
};

inline constexpr std::size_t growable_stat_count = 10;

//: How many chances a growth block carried before the richer stat line was
//: appended to it. A unit type record from such a package ends four bytes
//: early, and the four it does not carry are zero, which is what "never grows
//: that stat" has always encoded.
inline constexpr std::size_t growable_stat_count_before_richer_line = 6;

//: How often each stat gains a point on a level-up, as whole percentages in
//: [0, 100], indexed by `GrowableStat`. Zero never rolls.
struct GrowthRates final {
    std::array<std::uint8_t, growable_stat_count> chance{};
};

//: What every unit type says about growth when its author said nothing. A
//: package written before growth existed reads exactly this: worth no
//: experience to defeat, needing a hundred per level, and growing nothing.
inline constexpr std::uint16_t default_experience_per_level = 100;

//: How many styles the art library's character style menu holds. The source
//: schema's `characterStyleId` enumerates exactly these, in this order
//: (tools/placeholder_art/placeholder_art/styles.py).
//:
//: Declared here rather than beside the theme and backdrop menus below,
//: because a unit type carries one and a struct cannot default itself from a
//: constant it has not seen. The menu itself is one list all the same.
inline constexpr std::uint8_t character_style_count = 7;

//: The style a project that names none is drawn in: the first on the menu,
//: which is the roster the library already held and therefore the art as it
//: always was.
inline constexpr std::uint8_t default_character_style = 0;

//: A unit type that names no style of its own, and therefore follows the
//: game's. Deliberately the same value the menu lookup returns for a name the
//: menu does not hold, exactly as `archetype_unnamed` is: both mean "this
//: names no entry", and the reader refuses the second before it is stored.
inline constexpr std::uint8_t character_style_unnamed = character_style_count;

//: The menu index of a character style name, or `character_style_count` for a
//: name the menu does not hold.
[[nodiscard]] std::uint8_t character_style_index(std::string_view name) noexcept;

//: How many bodies a role can be drawn with. The second axis of the same
//: choice: a style says whose hand drew the role and a figure says at what
//: build, and every figure draws every archetype in every style, so the two
//: combine freely (tools/placeholder_art/placeholder_art/figures.py).
inline constexpr std::uint8_t character_figure_count = 2;

//: The figure a project that names none is drawn with: the first on the menu,
//: which is the sprite that shipped before figures existed.
inline constexpr std::uint8_t default_character_figure = 0;

//: A unit type that names no figure of its own, and therefore follows the
//: game's. The same "names no entry" convention `character_style_unnamed` and
//: `archetype_unnamed` use.
inline constexpr std::uint8_t character_figure_unnamed = character_figure_count;

//: The menu index of a figure name, or `character_figure_count` for a name the
//: menu does not hold.
[[nodiscard]] std::uint8_t character_figure_index(std::string_view name) noexcept;

struct UnitType final {
    StableId id{};
    std::string name;
    StableId class_id{};
    StableId faction_id{};
    std::vector<StableId> starting_weapons;
    std::vector<StableId> starting_items;
    std::vector<StableId> abilities;
    //: What defeating one of these is worth to whoever felled it.
    std::uint16_t experience_award{};
    //: Lifetime experience per level for a character of this type.
    std::uint16_t experience_per_level{default_experience_per_level};
    GrowthRates growth{};
    //: What one of these leaves behind when it falls, and how often as a whole
    //: percentage. Authored as a pair or not at all: both zero is a unit type
    //: that leaves nothing, which is what every unit type written before drops
    //: existed says, and either one alone is refused.
    StableId drop_item{};
    std::uint8_t drop_chance{};
    //: The style this one character is drawn in, or `character_style_unnamed`
    //: for one that follows the game's. Presentation only: no rule reads it,
    //: so it never enters canonical state and a character drawn as an undead
    //: knight fights exactly as the knight beside it.
    //:
    //: Last in the struct on purpose. Every fixture that builds a unit type
    //: positionally builds one that names no style, which is what a project
    //: written before this field existed says, so appending it here leaves
    //: those fixtures meaning what they meant.
    std::uint8_t character_style{character_style_unnamed};
    //: The body this one character is drawn with, or
    //: `character_figure_unnamed` for one that follows the game's.
    //: Presentation only, for the same reason the style beside it is, and
    //: appended for the same reason: a positionally built unit type names no
    //: figure, which is what every unit type written before figures says.
    std::uint8_t character_figure{character_figure_unnamed};
};

//: How many terrain kinds the art library draws, and therefore how many an
//: authored terrain name can resolve to (tools/placeholder_art).
inline constexpr std::uint8_t terrain_kind_count = 13;

//: A terrain name none of the library's keywords match. A presenter draws its
//: own fallback for these rather than nothing.
inline constexpr std::uint8_t terrain_kind_unknown = terrain_kind_count;

//: The terrain kind an authored name draws as: the first kind in the art
//: library's match order with a keyword in the lowered name. This is the whole
//: selection mechanism, and every client resolves a name through it.
[[nodiscard]] std::uint8_t terrain_kind_index(std::string_view name) noexcept;

//: What a cell asks of whoever would stand in it, in the simulation's own
//: numbering (`grandleon::simulation::Terrain`). Written into the map record as
//: gameplay data, beside the presentation join that says what the same cell
//: looks like and never inside it.
inline constexpr std::uint8_t passability_open = 0U;
inline constexpr std::uint8_t passability_water = 1U;
inline constexpr std::uint8_t passability_heights = 2U;

//: What a terrain kind asks of whoever would stand in it.
//:
//: The authored name resolves to a kind once, by the keyword convention above,
//: and then three tables read that one answer: the art library's, which says
//: what the kind looks like, this one, which says who may be there, and
//: `terrain_movement_cost`, which says what crossing it costs. None reads
//: another, and a kind the keywords do not match is open ground, because a game
//: that names its own terrain should not silently acquire a wall.
[[nodiscard]] std::uint8_t terrain_passability(std::uint8_t kind) noexcept;

//: The cheapest a cell can charge, and what an unpriced board charges
//: everywhere. Matches `grandleon::simulation::movement_cost_step`.
inline constexpr std::uint8_t movement_cost_step = 1U;

//: What a terrain kind charges whoever walks into it, in steps of a movement
//: allowance, written into the map record as gameplay data beside the
//: passability.
//:
//: Built in rather than authored, keyed on the same kind index passability is.
//: A project that wants to price its own ground would state a table beside its
//: terrain names in the source project and this would resolve against it. That
//: is a natural next step and nothing here forecloses it, because the number
//: reaching the package is per cell either way.
//:
//: A kind the keywords do not match charges one, for the reason it is open: a
//: game that names its own ground should not silently acquire a tax any more
//: than it should acquire a wall.
[[nodiscard]] std::uint8_t terrain_movement_cost(std::uint8_t kind) noexcept;

struct Map final {
    StableId id{};
    std::string name;
    std::uint16_t width{};
    std::uint16_t height{};
    std::vector<StableId> terrain;
    //: The art library's terrain kind each cell draws as, parallel to
    //: `terrain`. Presentation only: the cell's identity is its stable id, and
    //: no rule reads this.
    std::vector<std::uint8_t> terrain_kinds;
};

//: How many colours the art library's faction colour menu holds. The source
//: schema's `faction.color` enumerates exactly these, in this order.
inline constexpr std::uint8_t faction_colour_count = 6;

//: A faction that chose no colour. Its position in the project's faction list
//: chooses one instead, which only a presenter walking that list can know.
inline constexpr std::uint8_t faction_colour_unchosen = 0xFF;

struct Faction final {
    StableId id{};
    std::string name;
    std::uint8_t colour{faction_colour_unchosen};
};

//: The menu index of a colour name, or `faction_colour_unchosen` for a name
//: the menu does not hold.
[[nodiscard]] std::uint8_t faction_colour_index(std::string_view name) noexcept;

//: The colour a faction's characters wear: the colour it chose, or, when it
//: chose none, the menu colour at its own position in the project's faction
//: list, wrapping so a seventh faction still has one.
//:
//: The project's faction order is authoring semantics, and the compiler is the
//: only component that sees it, so this is where the fallback lives. Every
//: client resolves colour through this one function or through the resolved
//: value the compiler writes into the package.
[[nodiscard]] std::uint8_t resolved_faction_colour(
    const Faction& faction,
    std::size_t position
) noexcept;

enum class ObjectiveKind : std::uint8_t {
    defeat_all_opponents = 1,
    defeat_target = 2,
    protect_target = 3,
    // Hold out until a number of rounds has completed. Appended, so no value
    // renumbers and no package written before it re-means anything.
    survive_rounds = 4,
};

enum class ObjectiveSide : std::uint8_t {
    first = 1,
    second = 2,
};

struct Objective final {
    StableId id{};
    std::string name;
    ObjectiveKind kind{ObjectiveKind::defeat_all_opponents};
    ObjectiveSide side{ObjectiveSide::first};
    // Encounter placement the objective is about. Zero unless the kind needs one.
    StableId target_placement_id{};
    // How many rounds a `survive_rounds` objective is about, and zero for every
    // other kind, which is what every objective authored before that kind
    // says, and what keeps its record byte-identical: the count is written as a
    // tail only for the kind that can read one.
    std::uint16_t rounds{};
};

enum class AbilityKind : std::uint8_t {
    damage = 1,
    restore = 2,
};

enum class DamageType : std::uint8_t {
    physical = 1,
    magical = 2,
};

enum class AreaShape : std::uint8_t {
    single = 1,
    cross = 2,
    diamond = 3,
};

struct Ability final {
    StableId id{};
    std::string name;
    AbilityKind kind{AbilityKind::damage};
    DamageType damage_type{DamageType::physical};
    AreaShape area{AreaShape::single};
    std::int16_t power{};
    std::uint8_t minimum_range{1};
    std::uint8_t maximum_range{1};
    std::uint8_t radius{};
    //: How often this cast lands, on the same terms as a weapon's accuracy. A
    //: damaging area rolls once per unit it covers; a restoring ability never
    //: rolls, so the number is not read for one.
    std::uint8_t accuracy{100};
};

struct DialogueLine final {
    std::string speaker;
    std::string text;
    // Which of the scene's cast entries speaks this line, as the entry's
    // position plus one, so that zero is "the scene names nobody for this
    // line" and no encoding of a named character is ever the byte a missing
    // field would leave behind. Resolved here, by the one component that sees
    // both the cast and the lines, so that no client ever matches a speaker
    // string against a table. Presentation only: no rule reads it.
    std::uint8_t cast_entry{0};
};

//: One speaker of a scene, and the character they are. Authored as a list on
//: the scene rather than as a field on every line: a scene has a handful of
//: speakers and a great many lines, and a fact stated once cannot contradict
//: itself on line nine.
struct DialogueCastEntry final {
    std::string speaker;
    StableId unit_type_id{};
};

struct Dialogue final {
    StableId id{};
    std::string name;
    std::vector<DialogueLine> lines;
    //: The characters this scene's speakers are, in authored order. Every
    //: line's `cast_entry` indexes this. Empty is a scene that names none,
    //: which is every scene authored before a scene could.
    std::vector<DialogueCastEntry> cast;
    // What this scene is drawn against, as a menu index plus one, so that zero
    // is "the scene names none" and no encoding of a named backdrop is ever
    // the byte a missing field would leave behind. Presentation only: no rule
    // reads it, so it never enters canonical state.
    std::uint8_t backdrop{0};
};

enum class TurnOrder : std::uint8_t {
    alternating = 1,
    side_blocks = 2,
    initiative = 3,
};

enum class EncounterSide : std::uint8_t {
    first = 1,
    second = 2,
};

enum class UnitBehavior : std::uint8_t {
    hold = 1,
    patrol = 2,
    pursue = 3,
};

struct PatrolPoint final {
    std::int16_t x{};
    std::int16_t y{};
};

struct Placement final {
    StableId id{};
    // Identity of the placement's own source key, so that a globally scoped
    // objective can name a placement without knowing its encounter. A
    // placement that fields a roster member carries that member's identity
    // here instead, because the key a campaign joins a roster by is who stands
    // on the tile and not which tile they stand on.
    StableId source_key_id{};
    // The roster member fielded here, or zero for a placement that fields
    // nobody the campaign holds: the opposing side, a bystander, or an
    // encounter played outside any campaign.
    StableId member_id{};
    StableId unit_type_id{};
    EncounterSide side{EncounterSide::first};
    std::int16_t x{};
    std::int16_t y{};
    UnitBehavior behavior{UnitBehavior::hold};
    std::vector<PatrolPoint> patrol;
    // The world flag talking to this character raises, or zero for a character
    // no talk may reach, which is every placement authored before the gesture
    // existed, and every one that authors no `talk`.
    //
    // Zero costs nothing anywhere downstream: an encounter none of whose
    // placements state one writes no talks record, and a project none of whose
    // encounters do writes no talks section, so a package with nobody talkable
    // is byte-identical to the package it always was.
    StableId talk_flag_id{};
    // When this character comes in, as a round in progress, one based, and how
    // the arrival recurs. Zero, zero and zero is a character standing on the
    // board when the battle opens: the default, and what every placement
    // authored before waves says.
    //
    // `every` and `times` are authored together or not at all. The recurrence
    // is not expanded here: the simulation expands it, so the browser and the
    // consoles cannot disagree about what a wave means, and the package carries
    // what the author wrote.
    //
    // Zero costs nothing anywhere downstream, on the talk record's terms: an
    // encounter none of whose placements arrive writes no arrivals record, and
    // a project none of whose encounters do writes no arrivals section, so a
    // package with no waves is byte-identical to the package it always was.
    std::uint16_t arrival_round{};
    std::uint16_t arrival_every{};
    std::uint16_t arrival_times{};
    // What to call this character on this board, or empty for one named after
    // their unit type. A boss, a talkable captain, anybody on the second side:
    // this reaches all of them, and a roster member's name does not.
    //
    // Empty costs nothing anywhere downstream, on the talk record's terms: a
    // project none of whose placements name one writes no placement names
    // section, so a package where nobody is named is byte-identical to the
    // package it always was.
    std::string name;
};

// What an encounter says about the player's own troops before the first
// activation: the region they are arranged in, how many of them may take the
// field, or both. `id` is zero, `tiles` is empty and `capacity` is zero for an
// encounter that authors neither, which is an encounter with no deployment
// phase and no cap. That is what every encounter written before this says, and
// what keeps such a board byte-identical.
//
// The region says *where*, and it is a rule of the battle: the tiles travel
// inside the encounter definition and are hashed while the phase is open.
//
// The capacity says *how many*, and it is not a rule of the battle at all. Who
// is allowed out of a company is a campaign judgement, so the count travels
// beside the board rather than inside it and the simulation never learns it.
// Zero means no cap: every member the board places who is fit to fight goes
// out, which is what the placements alone have always meant.
struct DeploymentZone final {
    StableId id{};
    std::vector<PatrolPoint> tiles;
    std::uint16_t capacity{};
};

struct Encounter final {
    StableId id{};
    std::string name;
    StableId map_id{};
    std::vector<StableId> objective_ids;
    std::vector<Placement> placements;
    TurnOrder turn_order{TurnOrder::alternating};
    DeploymentZone deployment;
};

enum class CampaignNodeKind : std::uint8_t {
    encounter = 1,
    terminal = 2,
    story = 3,
};

enum class ObjectiveOutcome : std::uint8_t {
    satisfied = 1,
    failed = 2,
};

// How the predicates of one transition combine.
enum class ConditionCombinator : std::uint8_t {
    all = 1,
    any = 2,
    // Negates a single predicate, matching the schema's `not`.
    none = 3,
};

// What a predicate asks about. The tag shares one encoded byte with the
// objective result: an objective predicate writes its `ObjectiveOutcome` there
// and no tag at all, so it costs nothing for the other kinds existing, and
// every other kind takes a code above the two an outcome can be. Append-only.
enum class CampaignPredicateKind : std::uint8_t {
    objective_result = 1,
    world_flag_equals = 3,
};

// One question, as a flat record with a tag saying which fields carry meaning.
// That is the shape `campaign::TransitionPredicate` has had since the graph was
// built, mirrored here so the compiler and the runtime describe a predicate the
// same way.
//
// `subject` is the objective for `objective_result` and the flag key for
// `world_flag_equals`. It is one field rather than two because it is one slot
// in the record, not one namespace: which identity it holds follows from the
// tag, exactly as it does in the runtime's own record.
struct CampaignPredicate final {
    CampaignPredicateKind kind{CampaignPredicateKind::objective_result};
    StableId subject{};
    ObjectiveOutcome result{ObjectiveOutcome::satisfied};
    // Only for `world_flag_equals`: the `WorldValueType` the flag must be, and
    // the value it must equal. A boolean is carried as zero or one, which is
    // what the campaign state stores it as.
    std::uint8_t value_type{};
    std::int64_t value{};
};

// A transition taken only when its predicates hold. A bare objectiveResult is
// stored as `all` over one predicate, so there is one shape to evaluate.
struct CampaignConditionalTarget final {
    StableId target_id{};
    std::uint16_t priority{};
    ConditionCombinator combinator{ConditionCombinator::all};
    std::vector<CampaignPredicate> predicates;
};

struct CampaignNode final {
    StableId id{};
    CampaignNodeKind kind{CampaignNodeKind::encounter};
    StableId encounter_id{};
    // Presented in order when the node is entered: a cutscene is a story node
    // with several dialogues, not a new kind of node.
    std::vector<StableId> dialogue_ids;
    std::vector<StableId> unconditional_targets;
    std::vector<CampaignConditionalTarget> conditional_targets;
};

// One character the company holds, as the author wrote them.
//
// The identity is the member's own and never a placement's: two members of one
// unit type are two people, and a member who appears on four boards is one
// person on all four. `join_node_id` is zero for a member the campaign is
// founded with, and otherwise the node whose completion brings them in.
//: Which stats an author may write a delta over, and the order they are
//: indexed and encoded in. The first ten are `GrowableStat`'s ten at
//: `GrowableStat`'s own indices; `speed` is eleventh because it is the one
//: stat the two lists differ by. Written down here, in
//: `package_runtime::SpecificStat` which decodes it, and in
//: `campaign_runtime::SpecificStat` which applies it, and the tests check the
//: three against each other.
//:
//: `speed` is delta-able although it is not growable, and the difference is a
//: decision. Growth refuses it because a level-up is a roll that would
//: reshuffle turn order inside a battle the player is already standing in. An
//: authored delta draws from no stream, is fixed before the campaign is
//: founded, is identical on every playthrough and platform, and is on the info
//: sheet before the player commits. It is exactly as surprising as a class
//: authoring a different speed, which is the same fast knight an author can
//: already write.
//:
//: Append-only: the values are indexed into and encoded.
enum class SpecificStat : std::uint8_t {
    health = 0,
    strength = 1,
    defense = 2,
    resistance = 3,
    movement = 4,
    action_points = 5,
    skill = 6,
    luck = 7,
    evasion = 8,
    magic = 9,
    speed = 10,
};

inline constexpr std::size_t specific_stat_count = 11;

//: The authored name of one delta-able stat, in `SpecificStat` order. The
//: source spelling, because it is what a diagnostic's path has to name.
[[nodiscard]] std::string_view specific_stat_name(SpecificStat stat) noexcept;

//: What an author wrote about one character beyond their unit type.
//:
//: Zero deltas and a zero bonus is a member who authors nothing, which is
//: never written into a package: the whole tail is emitted only when somebody
//: authors something, which is what keeps a campaign that authors none
//: byte-identical to the campaign it was.
struct CampaignMemberSpecificity final {
    //: Added to whatever the class says, indexed by `SpecificStat`.
    std::array<std::int16_t, specific_stat_count> stat_deltas{};
    //: Which stats the author actually wrote, indexed the same way. Needed
    //: only to tell a stated zero from an omission: both hold zero in
    //: `stat_deltas`, and one of them is a diagnostic while the other is the
    //: ordinary case. Nothing downstream of the compiler needs it, because a
    //: stated zero never survives to be encoded.
    std::array<bool, specific_stat_count> stated{};
    //: Added to the maximum of the band of every weapon this character strikes
    //: with. Zero is no bonus.
    std::uint8_t reach_bonus{};

    [[nodiscard]] bool empty() const noexcept {
        if (reach_bonus != 0U) return false;
        for (const std::int16_t delta : stat_deltas) {
            if (delta != 0) return false;
        }
        return true;
    }
};

struct CampaignMember final {
    StableId id{};
    std::string name;
    StableId unit_type_id{};
    StableId join_node_id{};
    //: What makes this character more than their class. Empty for a member who
    //: is exactly their unit type, which is every member authored before this.
    CampaignMemberSpecificity specificity{};
    //: Whether the author stated a `specificity` object at all. Distinct from
    //: `specificity.empty()`, because stating the object and saying nothing in
    //: it is a claim that a character is specific without saying how, and that
    //: is refused rather than read as omission.
    bool states_specificity{false};
};

// A quantity of one item the campaign puts into its shared store, and where in
// the flow it is put there.
//
// The same shape and the same convention as `CampaignMember`, deliberately:
// `join_node_id` is zero for what the store is founded with, and otherwise the
// node whose completion puts it there. One table answers both authored fields
// because they are the same fact told at two moments.
struct CampaignItemGrant final {
    StableId item_id{};
    std::uint32_t quantity{};
    StableId join_node_id{};
};

struct Campaign final {
    StableId id{};
    std::string name;
    StableId entry_node_id{};
    std::vector<CampaignNode> nodes;
    // Everyone this campaign can ever hold: the founding members in authored
    // order, then each node's recruits in flow order. The order is the order
    // persistent identities are assigned in, so it is content and not a
    // detail.
    std::vector<CampaignMember> roster;
    // Everything this campaign ever puts in its store by authoring rather than
    // by play: the founding stock in authored order, then each node's grants in
    // flow order. The order is the order the operations are built in, so it is
    // content for the same reason the roster's order is.
    std::vector<CampaignItemGrant> grants;
};

//: What a campaign does with a character who falls in battle.
//:
//: A statement about what kind of game this is rather than a rule the battle
//: reads: under either value a character at no health leaves the field the
//: moment they reach it, and the only thing this decides is what the campaign
//: writes down once the battle is over. The compiler keeps its own copy of the
//: vocabulary, as it does for every other encoded enumeration here, because it
//: writes the wire format and the wire format is the contract.
//:
//: The values are serialized on the campaign record, so the list is
//: append-only and `package_runtime::CharacterLoss` must agree with it byte
//: for byte.
enum class CharacterLoss : std::uint8_t {
    //: A character who falls is dead, and no later map brings them back. What
    //: an absent rule means, and what every campaign compiled before a project
    //: could state one meant.
    permanent = 1,
    //: A character who falls is carried off the field and rejoins the company
    //: after the battle, still holding whatever the battle left them with.
    recoverable = 2,
};

//: How many themes the art library's biome and season menu holds. The source
//: schema's `themeId` enumerates exactly these, in this order
//: (tools/placeholder_art/placeholder_art/themes.py).
inline constexpr std::uint8_t theme_count = 4;

//: The theme a project that names none is drawn in: the first on the menu,
//: which substitutes nothing and is therefore the art as it always was.
inline constexpr std::uint8_t default_theme = 0;

//: The menu index of a theme name, or `theme_count` for a name the menu does
//: not hold.
[[nodiscard]] std::uint8_t theme_index(std::string_view name) noexcept;

//: The character style menu is declared above `UnitType`, which carries one.

//: How many backdrops the art library's scene backdrop menu holds. The source
//: schema's `backgroundId` enumerates exactly these, in this order
//: (tools/placeholder_art/placeholder_art/backdrops.py).
inline constexpr std::uint8_t backdrop_count = 7;

//: The menu index of a backdrop name, or `backdrop_count` for a name the menu
//: does not hold. There is deliberately no default beside it: a scene that
//: names no backdrop is drawn on the fill each client already used, which is
//: not an entry of this menu and must never resolve to one.
[[nodiscard]] std::uint8_t backdrop_index(std::string_view name) noexcept;

//: How many character archetypes the art library draws, in every style: knight,
//: archer, mage, stormcaller, healer, commander, rogue, beast. The roster is
//: closed at this length: a ninth would cost one draw routine in every style.
//: A style may not add, remove or rename one, so a class name selects the same
//: archetype whichever style draws it.
inline constexpr std::uint8_t archetype_count = 8;

//: A name none of the roster's words appear in. Only the keyword lookup ever
//: returns this; a unit type always resolves to a drawable archetype.
inline constexpr std::uint8_t archetype_unnamed = archetype_count;

//: The archetype a unit type that names none wears: the first on the roster,
//: which is the knight every client already drew for it.
inline constexpr std::uint8_t archetype_default = 0;

//: The archetype an authored name spells: the first on the art library's
//: roster whose own name appears in the lowered text, or `archetype_unnamed`.
//:
//: This is the whole convention, and it runs over more than unit types (a
//: console resolves a dialogue speaker's portrait through it too), so it is
//: published beside the resolved rule rather than hidden inside it.
[[nodiscard]] std::uint8_t archetype_index(std::string_view name) noexcept;

struct GameSource final {
    std::array<std::uint8_t, 16> game_id{};
    std::string title;
    std::uint32_t content_revision{};
    //: The season this game's ground is drawn in. Presentation only: no rule
    //: reads it, so it never enters canonical state.
    std::uint8_t theme{default_theme};
    //: The style this game's characters are drawn in. Presentation only, for
    //: the same reason as the theme above: no rule reads it, so it never
    //: enters canonical state. A console build resolves it once, at build
    //: time, and embeds only that style's art.
    std::uint8_t character_style{default_character_style};
    //: The body this game's characters are drawn with wherever a character
    //: names none of its own. Presentation only, like the style beside it.
    std::uint8_t character_figure{default_character_figure};
    //: What a fall costs this game's company. Not presentation: it is resolved
    //: into every campaign record the way the project's default turn order is
    //: resolved into every encounter's one turn-order byte, and it sits on the
    //: campaign because the campaign is what holds a company. A project that
    //: states nothing leaves it permanent, and a permanent-loss project that
    //: also asks for no testing invulnerability writes no tail at all, so its
    //: campaign records are the bytes they always were.
    CharacterLoss character_loss{CharacterLoss::permanent};
    //: Whether the members of this game's company cannot be reduced below one
    //: health.
    //:
    //: A testing aid rather than a way to play, and deliberately not a third
    //: value of `CharacterLoss`, because it is not an answer to the question
    //: that enumeration asks. It is compiled into the package all the same:
    //: it changes what the rules do, so two people holding the same package
    //: must get the same battles out of it, and a client that could switch it
    //: on for itself would be a client whose battles nobody else could
    //: reproduce.
    bool invulnerable_for_testing{false};
    package_format::VersionRange required_engine{};
    package_format::TargetProfile target{
        package_format::TargetProfile::portable
    };
    std::uint64_t required_features{};
    std::vector<WeaponType> weapon_types;
    std::vector<ItemType> item_types;
    std::vector<UnitClass> classes;
    std::vector<Weapon> weapons;
    std::vector<Item> items;
    std::vector<UnitType> unit_types;
    std::vector<Map> maps;
    std::vector<Faction> factions;
    std::vector<Objective> objectives;
    std::vector<Ability> abilities;
    std::vector<Dialogue> dialogues;
    std::vector<Encounter> encounters;
    std::vector<Campaign> campaigns;
};

//: The archetype a unit type's characters are drawn as: the one its class name
//: spells, or, when the class spells none, the one its own name spells, or
//: `archetype_default` when neither does.
//:
//: The class is searched first because that is what an author keys a role on,
//: and the unit type's own name after because a game may spell the role only
//: there. The compiler is the only component that sees both lists, so this is
//: where the rule lives. Every client resolves an archetype through this one
//: function or through the resolved value the compiler writes into the
//: package. Presentation only: no rule reads it.
[[nodiscard]] std::uint8_t resolved_archetype(
    const GameSource& source,
    const UnitType& unit_type
) noexcept;

enum class DiagnosticCode : std::uint8_t {
    missing_id,
    duplicate_id,
    missing_name,
    name_too_long,
    missing_reference,
    duplicate_reference,
    invalid_stat,
    invalid_range,
    disallowed_weapon,
    invalid_map,
    invalid_placement,
    // A pair of fields that mean nothing apart was authored one half at a
    // time. Today that is a unit type's drop: something to leave with no
    // chance of leaving it, or a chance with nothing to leave.
    incomplete_pair,
    // A campaign that no company can be founded from: no roster at all, or a
    // roster whose every member joins later. A campaign kept between battles
    // begins with somebody, and inventing that somebody is what this compiler
    // refuses to do.
    empty_roster,
    // A placement and the roster disagree about who is on the board: a
    // first-side placement fielding nobody or somebody the roster does not
    // hold, a second-side placement fielding a member, two placements on one
    // board fielding one member, or a placement whose unit type is not the
    // member's.
    invalid_member,
    // A deployment that cannot be met: no identity, neither a region nor a
    // capacity, a tile off the encounter's map, a tile named twice, a tile
    // whose terrain nobody could stand on, a region no first-side placement
    // stands inside, or a capacity no company could ever exceed because the
    // board has no more first-side placements than that. Reported at the
    // encounter's own path, because a deployment belongs to the battle and not
    // to the map it is fought on.
    invalid_deployment,
    // A grant the store cannot be given: a quantity of nothing, or one item
    // identity stated twice in one list, which is an author answering "how
    // many" twice with two different numbers.
    //
    // A grant naming an item the project does not hold is `missing_reference`
    // rather than this, because it is a reference that does not resolve and
    // that is what every unresolved reference in this compiler is called. The
    // authoring analyzers say the same thing the same way, as
    // `SOURCE_REF_MISSING` at the grant's own `itemId`.
    invalid_grant,
    // A specificity that says nothing or says something impossible: a stated
    // `specificity` object with neither a delta nor a range bonus in it, a
    // delta of zero, or a delta that lands a stat outside the range that
    // stat's own class field admits.
    //
    // That last is one rule rather than eleven, and it is the tightest one
    // available: an author may make a character anything they could have made
    // a class, and nothing they could not. Nothing is clamped. If this
    // compiler accepts a delta, the number the author wrote is the number that
    // reaches the board.
    //
    // A member whose unit type or whose unit type's class does not resolve is
    // `missing_reference` rather than this, and is reported instead of any
    // delta problem: a delta whose base cannot be determined is not a delta
    // problem, and a bad reference must never be reported as a bad number.
    invalid_specificity,
    // An objective and its round count disagree: a `surviveRounds` objective
    // with no count, or a count on a kind that could never read one. One code
    // for one fact, on the standard `incomplete_pair` sets: a number an author
    // wrote and nothing will ever consult is a mistake, not a nicety.
    invalid_objective,
    // An arrival nothing could honour: a first arrival in the round the battle
    // opens in or earlier, a gap between arrivals with no number of them or a
    // number with no gap, more arrivals than the engine will expand, or a
    // placement that fields a roster member and also arrives.
    //
    // That last is a gap rather than a rule: `campaign_runtime` joins a
    // member to a placement to field them and counts who takes the field
    // against the deployment capacity, and somebody who is not on the board at
    // the opening is neither fielded nor withheld. Answering that is a
    // campaign design question, so an author is refused rather than surprised.
    invalid_arrival,
    // A node whose outgoing edges do not decide where the campaign goes: two
    // transitions sharing one priority, or more than one conditionless
    // fallback. Both leave the taken edge to the order the transitions happen
    // to be written in, which is the one thing the "lowest matching value wins
    // independently of array order" rule exists to forbid. The runtime refuses
    // the second outright as `unsupported_flow`, and it cannot see the first at
    // all, because it stable-sorts on priority, so a tie silently resolves to
    // whichever transition was authored first.
    invalid_transition,
    // A board nothing decides: an encounter naming no objective at all.
    //
    // This is not a battle that runs long. `package_runtime`'s encounter
    // loader reads the objective count first and refuses a payload declaring
    // none, so an encounter compiled without one is a board that cannot be
    // *opened* — the campaign standing on it stops there, and every client
    // reports it as a board it could not decode rather than as a Stage its
    // author left unfinished.
    //
    // It is refused here because this compiler's promise is that it emits
    // nothing the runtime will not read. A package holding such a board has
    // already lost that promise, and the loss is invisible until somebody
    // reaches the Stage on a console.
    undecided_encounter,
};

[[nodiscard]] std::string_view diagnostic_name(
    DiagnosticCode code
) noexcept;

struct Diagnostic final {
    DiagnosticCode code{};
    std::string path;
    StableId related_id{};
};

struct CompileResult final {
    std::vector<Diagnostic> diagnostics;
    std::vector<std::uint8_t> package;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

// Validates all semantic references before producing any package bytes.
[[nodiscard]] CompileResult compile(const GameSource& source);

}  // namespace grandleon::game_content
