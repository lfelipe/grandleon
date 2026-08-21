// SPDX-License-Identifier: MIT
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

// Static, unlike the two below: this module and its example chain depend on
// nothing outside Node, so a missing `npm ci` cannot be what went wrong.
import {
  CURRENT_SOURCE_VERSION,
  FIRST_SOURCE_VERSION,
  MAXIMUM_MIGRATION_STEPS,
  SourceMigrationRegistry,
  planChanges,
  planUpgrade,
  sourceMigrations,
  upgradeProject
} from "./migration.mjs";
import {
  EXAMPLE_OLDEST,
  EXAMPLE_STEPS,
  EXAMPLE_STEPS_THAT_THROW,
  EXAMPLE_STEPS_WITH_A_HOLE,
  FIRST_CHANGE,
  SECOND_CHANGE,
  THIRD_CHANGE,
  FOURTH_CHANGE
} from "./migration-example.mjs";
import { main as upgradeMain, refusalMessage } from "./upgrade.mjs";

/** A registry holding the given steps. Steps are data; this makes a chain. */
const registryOf = (steps) => steps.reduce(
  (registry, step) => registry.add(step),
  new SourceMigrationRegistry()
);
const exampleMigrations = () => registryOf(EXAMPLE_STEPS);
const chainWithAHole = () => registryOf(EXAMPLE_STEPS_WITH_A_HOLE);
const migrationsThatThrow = () => registryOf(EXAMPLE_STEPS_THAT_THROW);

// Imported dynamically so that a missing dependency can be explained rather
// than merely reported. This directory has its own dependency tree, separate
// from the editor's, and it is the one people forget: nothing in the editor
// references it, and the first thing to notice is this suite, running under
// ctest as `grandleon.source_schema`, two directories away from the cause.
let validateProject;
let statDeltaBounds;
let serializeCanonical;
try {
  ({ validateProject, statDeltaBounds } = await import("./validate.mjs"));
  ({ serializeCanonical } = await import("./roundtrip.mjs"));
} catch (error) {
  if (error?.code !== "ERR_MODULE_NOT_FOUND") throw error;
  const missing = /Cannot find package '([^']+)'/.exec(error.message)?.[1];
  console.error(
    `${missing ? `Cannot find package '${missing}'` : error.message}\n\n` +
    "tools/source_schema installs its dependencies separately from the " +
    "editor. Install them:\n\n" +
    "  npm ci --prefix tools/source_schema\n\n" +
    "or run scripts/setup.sh, which prepares both dependency trees and " +
    "everything else the gate needs."
  );
  process.exit(1);
}

const directory = path.dirname(fileURLToPath(import.meta.url));
const fixtures = path.resolve(directory, "../../tests/fixtures/source_projects");

const validDiagnostics = validateProject(path.join(fixtures, "valid/minimal.json"));
assert.deepEqual(validDiagnostics, [], "minimal project must be valid");
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/typed-content.json")),
  [],
  "typed weapon and item content must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/authoring-registries.json")),
  [],
  "authoring-only registries and their typed references must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/script-bindings.json")),
  [],
  "typed inert script bindings must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/nonlinear-campaign.json")),
  [],
  "campaign cycles, conditional branches, and recombination must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/encounter-placements.json")),
  [],
  "typed two-side encounter placements must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/encounter-talk.json")),
  [],
  "a placement that says who can be talked to, and what talking records, must "
    + "be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/deployment-zone.json")),
  [],
  "an encounter that states the region its player arranges in must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/campaign-roster.json")),
  [],
  "a campaign founded by a named company, a node that recruits another "
    + "member, and boards that field them by name must be valid, including the "
    + "same member standing on two of them"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/campaign-stock-and-cap.json")),
  [],
  "a campaign founded with a store, a node that grants more of it, a board "
    + "that states both a region and a cap, and a board that states a cap "
    + "alone must all be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/campaign-member-specificity.json")),
  [],
  "a founding member authored with stat deltas and a range bonus, a recruit "
    + "authored with a bonus alone, and a member who is exactly her class must "
    + "all be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/presentation-choices.json")),
  [],
  "a project naming a season, a character style and a figure, and a character "
    + "in it naming a style and a figure of its own, must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/scene-cast.json")),
  [],
  "a scene naming who its speakers are, a speaker it names nobody for, and a "
    + "second scene casting nobody at all must all be valid"
);
{
  // Everything a cast can get wrong that the schema cannot see. All three are
  // asserted on their exact paths, because the native reader is held to the
  // same two in tests/game_content/source_project_test.cpp and the pair of
  // analyzers agreeing about *where* is the whole point of the fixture.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/scene-cast-semantics.json")
  );
  assert.ok(
    diagnostics.some(
      ({ code, instancePath }) =>
        code === "SOURCE_DIALOGUE_SPEAKER_CAST_TWICE" &&
        instancePath === "/dialogues/0/cast/1/speaker"
    ),
    "one speaker cast twice must be refused at the second entry"
  );
  assert.ok(
    diagnostics.some(
      ({ code, instancePath }) =>
        code === "SOURCE_DIALOGUE_CAST_SPEAKS_NO_LINE" &&
        instancePath === "/dialogues/0/cast/2/speaker"
    ),
    "a cast entry that speaks no line must be named rather than ignored"
  );
  assert.ok(
    diagnostics.some(
      ({ code, instancePath }) =>
        code === "SOURCE_REF_MISSING" &&
        instancePath === "/dialogues/0/cast/2/unitTypeId"
    ),
    "a cast naming a unit type the project does not declare must be refused"
  );
}
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/minimal.json")),
  [],
  "a project naming neither presentation choice must be valid, so a project "
    + "written before the menus existed still loads"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/game-wide-turn-order.json")),
  [],
  "a project stating the turn order its battles default to, with one board "
    + "inheriting it and one stating its own, must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/character-loss-recoverable.json")),
  [],
  "a project stating that a character who falls is carried off and rejoins "
    + "the company must be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/testing-invulnerability.json")),
  [],
  "and so must one asking for the testing invulnerability, stated beside the "
    + "loss rule it is deliberately not a value of — the two are separate "
    + "settings and a project may state both"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/character-traversal.json")),
  [],
  "a class that flies, one that crosses named terrain, and one that walks "
    + "must all be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/weapon-accuracy.json")),
  [],
  "a weapon that always lands, one authored at ninety, one at zero, and a "
    + "cast that can fizzle must all be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/item-effects.json")),
  [],
  "an item authored to restore and one authored with no effect at all must "
    + "both be valid, because omitted is what every item written before items "
    + "could be spent says"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/unit-growth-rates.json")),
  [],
  "a unit type that authors every growth chance, one that authors some, and "
    + "one that authors none must all be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/unit-drops.json")),
  [],
  "a unit type that always leaves something behind, one that leaves it three "
    + "times in five, and one that leaves nothing must all be valid"
);
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/surviving-waves.json")),
  [],
  "a map won by surviving seven rounds, with a wave that arrives every three "
    + "and comes at the player, must be valid"
);
{
  // The two facts a wave can get wrong that no schema can catch: a roster
  // member cannot be a reinforcement, and a round count belongs to the one
  // kind that reads it.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/arrival-semantics.json")
  );
  assert.ok(
    diagnostics.some(
      ({ code, instancePath }) =>
        code === "SOURCE_CAMPAIGN_ARRIVAL_MEMBER" &&
        instancePath === "/campaigns/0/flow/nodes/0/placements/0/arrival"
    ),
    "a placement that fields a roster member must not also arrive"
  );
  assert.ok(
    diagnostics.some(
      ({ code, instancePath }) =>
        code === "SOURCE_OBJECTIVE_ROUNDS_MISMATCH" &&
        instancePath === "/objectives/0/rounds"
    ),
    "and a round count on a kind that cannot read one must be refused"
  );
}
assert.deepEqual(
  validateProject(path.join(fixtures, "valid/richer-stat-line.json")),
  [],
  "a class with skill and evasion, one with luck, one with magic, and one "
    + "with none of the four must all be valid"
);
{
  // The four stats that decide whether a blow lands are bounded at nought,
  // like every other stat in the block, and their growth chances are bounded
  // at nought and a hundred like every other chance.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/stat-line-out-of-range.json")
  );
  for (const stat of ["skill", "luck", "evasion", "magic"]) {
    assert.ok(
      diagnostics.some(
        ({ instancePath }) =>
          instancePath === `/classes/0/baseStats/${stat}`
      ),
      `a negative ${stat} must be refused, naming the class and the field`
    );
  }
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/0/growthRates/skill"
    ),
    "and a growth chance above a hundred for one of them must be refused"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/0/growthRates/magic"
    ),
    "and a negative one the same way"
  );
}
{
  // The ceiling on every number the damage arithmetic reads, checked against
  // the engine that owns it.
  //
  // The number belongs to the rules: `simulation::maximum_stat` is what
  // `create_encounter` and the package loader both refuse a unit for passing,
  // and `tools/game_content` asks the engine for it rather than writing it out.
  // A JSON schema cannot ask, so it writes the number and this reads the
  // engine's header to check every place it is written still says the same
  // thing. If any of them parts from the engine, a project this validator
  // accepts is a project the board turns away, which is the bug this pins
  // shut.
  //
  // Four of the seven share `common.schema.json`'s `damageStat`. The other
  // three carry their own copy because they carry a description or a floor of
  // their own beside it, and the editor's form model resolves a `$ref` by
  // replacing the whole field, so a keyword written next to one is a keyword
  // an author never sees. They are listed here for that reason.
  const schemaOf = (name) => JSON.parse(
    fs.readFileSync(
      path.resolve(directory, `../../schemas/source/v1/${name}.schema.json`),
      "utf8"
    )
  );
  const encounterHeader = fs.readFileSync(
    path.resolve(
      directory,
      "../../engine/simulation/include/grandleon/simulation/encounter.hpp"
    ),
    "utf8"
  );
  const declared =
    /maximum_stat\s*=\s*(\d+)\s*;/.exec(encounterHeader)?.[1];
  assert.ok(
    declared !== undefined,
    "simulation::maximum_stat must be readable from the engine header; if it "
      + "moved, this check and the schemas move with it"
  );
  const common = schemaOf("common");
  const statBlock = common.$defs.statBlock.properties;
  const capped = {
    "common.schema.json #/$defs/damageStat": common.$defs.damageStat,
    "statBlock.strength": statBlock.strength,
    "statBlock.defense": statBlock.defense,
    "statBlock.resistance": statBlock.resistance,
    "statBlock.magic": statBlock.magic,
    "weapon.power": schemaOf("weapon").properties.power,
    "item.power": schemaOf("item").properties.power,
    "ability.power": schemaOf("ability").properties.power
  };
  for (const [where, field] of Object.entries(capped)) {
    const maximum = field.maximum ?? common.$defs[
      String(field.$ref).slice(String(field.$ref).lastIndexOf("/") + 1)
    ]?.maximum;
    assert.equal(
      maximum,
      Number(declared),
      `${where} must cap where simulation::maximum_stat does, or the schema `
        + "admits projects the runtime refuses to open"
    );
  }

  // The delta table this validator derives from that block, read back. A
  // character may be made anything they could have been made a class, so a
  // delta lands inside its own stat's bounds and nowhere else. The four
  // that stand in the damage arithmetic land inside the engine's bound rather
  // than inside what the field holds. Asserted directly because the fixtures
  // that exercise the rule do it on health, movement and speed, none of which
  // would notice a `$ref` this went on to stop following.
  assert.equal(
    statDeltaBounds.strength.maximum,
    Number(declared),
    "a strength delta must land inside the bound the rules hold a unit to"
  );
  assert.equal(statDeltaBounds.magic.maximum, Number(declared));
  assert.equal(statDeltaBounds.defense.maximum, Number(declared));
  assert.equal(statDeltaBounds.resistance.maximum, Number(declared));
  assert.deepEqual(
    statDeltaBounds.skill,
    { minimum: 0, maximum: 32767, whenOmitted: 0 },
    "and a skill delta inside the whole of what the field holds"
  );
  assert.deepEqual(
    statDeltaBounds.movement,
    { minimum: 1, maximum: 255, whenOmitted: 1 },
    "while movement keeps the floor the schema does not state"
  );

  // Every field that reference is supposed to reach, refused in one project.
  // The native compiler asserts the same seven places on the same fixture in
  // `tests/game_content/source_fixtures_test.cpp`, and the editor asserts them
  // again in `source-conformance.test.ts`.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/stat-past-the-damage-cap.json")
  );
  for (const instancePath of [
    "/classes/0/baseStats/strength",
    "/classes/0/baseStats/defense",
    "/classes/0/baseStats/resistance",
    "/classes/0/baseStats/magic",
    "/weapons/0/power",
    "/items/0/power",
    "/abilities/0/power"
  ]) {
    assert.ok(
      diagnostics.some((found) => found.instancePath === instancePath),
      `${instancePath} past the damage cap must be refused`
    );
  }
  // And the three the rules only ask to be non-negative keep the whole of what
  // the field holds: the same project writes 32767 into each and is refused
  // nowhere but the seven above.
  for (const stat of ["skill", "luck", "evasion"]) {
    assert.ok(
      !diagnostics.some(
        (found) => found.instancePath === `/classes/0/baseStats/${stat}`
      ),
      `${stat} is percentage points on a chance the rules clamp, so 32767 of `
        + "it must stand"
    );
  }
}
{
  // The same rule on the other authored chance, and the level cost beside it.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/growth-out-of-range.json")
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/0/growthRates/health"
    ),
    "a growth chance above a hundred must be refused, naming the stat"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/0/growthRates/strength"
    ),
    "and a negative one must be refused the same way"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/1/experiencePerLevel"
    ),
    "and a level that costs nothing must be refused, naming the unit type"
  );
}
{
  // A drop is authored as a pair, in both directions. Neither half means
  // anything alone, and honouring one of them would leave an author wondering
  // why the picket never drops anything.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/drop-half-authored.json")
  ).map(({ code, instancePath }) => ({ code, instancePath }));
  assert.deepEqual(
    diagnostics,
    [
      {
        code: "SOURCE_DROP_INCOMPLETE",
        instancePath: "/unitTypes/0/dropItemId"
      },
      {
        code: "SOURCE_DROP_INCOMPLETE",
        instancePath: "/unitTypes/1/dropChance"
      }
    ],
    "both halves of a half-authored drop must be refused, each naming the "
      + "field that is there rather than the one that is not"
  );
}
{
  // The same whole-percentage rule the other chances keep, and the extra one a
  // drop keeps: a nought is refused because 'never leaves anything' is already
  // spelled by authoring neither field.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/drop-out-of-range.json")
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/0/dropChance"
    ),
    "a drop chance above a hundred must be refused, naming the unit type"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/1/dropChance"
    ),
    "and a chance of nothing must be refused, so leaving nothing has one "
      + "spelling"
  );
}
{
  // A chance is a whole percentage. Anything outside [0, 100] is an author
  // meaning something the rules cannot express, and the roll would read it as
  // certainty or as impossibility without saying so.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/accuracy-out-of-range.json")
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/weapons/0/accuracy"
    ),
    "an accuracy above a hundred must be refused, naming the weapon"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/abilities/0/accuracy"
    ),
    "and a negative accuracy must be refused, naming the ability"
  );
}
{
  // An item's effect vocabulary is closed, and its power is a number a rule
  // reads rather than a note. A kind the engine has no verb for, or a restore
  // of nothing, would both compile into an item that does nothing while
  // claiming otherwise.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/item-effect-out-of-range.json")
  );
  assert.ok(
    diagnostics.some(({ instancePath }) => instancePath === "/items/0/kind"),
    "an item kind outside the vocabulary must be refused, naming the item"
  );
  assert.ok(
    diagnostics.some(({ instancePath }) => instancePath === "/items/1/power"),
    "and a restore of nothing must be refused, naming the field"
  );
}
{
  // A crossing outside the vocabulary is refused, the same way an unknown
  // season or style is. What a character crosses is a rule, so a name the
  // engine cannot act on must not reach it.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/unknown-crossing.json")
  );
  assert.ok(
    diagnostics.length > 0,
    "a crossing the terrain vocabulary does not name must be refused"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) =>
        instancePath === "/classes/0/traversal/crossings/0"
    ),
    "the refusal must name the crossing that is not in the vocabulary"
  );
}
{
  // A style off the menu is refused by the schema, the same way an unknown
  // season is. The native reader's matching refusal is asserted in
  // tests/game_content/source_project_test.cpp.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/unknown-character-style.json")
  );
  assert.ok(
    diagnostics.length > 0,
    "a character style the art library does not offer must be refused"
  );
  assert.ok(
    diagnostics.some(({ instancePath }) => instancePath === "/characterStyleId"),
    "the refusal must name the field that carries the unknown style"
  );
}

{
  // The same refusal one level down. A character may name any style the
  // library holds (that is the whole point of the field) but not one it does
  // not, and the path names the character as well as the field, because a
  // diagnostic that named only the field would not say which character is
  // wrong.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/unknown-unit-character-style.json")
  );
  assert.ok(
    diagnostics.length > 0,
    "a character naming a style the art library does not offer must be refused"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/unitTypes/0/characterStyleId"
    ),
    "the refusal must name the character that carries the unknown style"
  );
}

{
  // A backdrop off the menu is refused the same way, and the path names the
  // scene as well as the field: the choice is authored per scene, so a
  // diagnostic that named only the field would not say which scene is wrong.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/unknown-scene-backdrop.json")
  );
  assert.ok(
    diagnostics.length > 0,
    "a backdrop the art library does not offer must be refused"
  );
  assert.ok(
    diagnostics.some(
      ({ instancePath }) => instancePath === "/dialogues/0/backgroundId"
    ),
    "the refusal must name the scene that carries the unknown backdrop"
  );
}
{
  // A turn order off the menu is refused rather than replaced: a project that
  // quietly fell back to alternating would hide a misspelling that changes
  // every battle in the game. The native reader's matching refusal is asserted
  // in tests/game_content/source_project_test.cpp.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/unknown-turn-order-default.json")
  );
  assert.ok(
    diagnostics.length > 0,
    "a turn order the simulation does not offer must be refused"
  );
  assert.ok(
    diagnostics.some(({ instancePath }) => instancePath === "/defaultTurnOrder"),
    "the refusal must name the field that carries the unknown turn order"
  );
}
{
  // A loss rule off the menu is refused for the same reason, and the cost of
  // getting it wrong is higher: a project that quietly fell back to permanent
  // loss would hide a misspelling that takes a player's characters away for
  // good. The native reader's matching refusal is asserted in
  // tests/game_content/source_project_test.cpp.
  const diagnostics = validateProject(
    path.join(fixtures, "invalid/unknown-character-loss.json")
  );
  assert.ok(
    diagnostics.length > 0,
    "a fate for a fallen character that is not on the menu must be refused"
  );
  assert.ok(
    diagnostics.some(({ instancePath }) => instancePath === "/characterLoss"),
    "the refusal must name the field that carries the unknown loss rule"
  );
}

const identifierDiagnostics = validateProject(
  path.join(fixtures, "invalid/bad-identifier.json")
);
assert.ok(
  identifierDiagnostics.some(
    (item) =>
      item.code === "SOURCE_SCHEMA_INVALID" &&
      item.instancePath === "/classes/0/id" &&
      item.line > 1 &&
      item.column > 1
  ),
  "invalid stable identifiers must have a source path and JSON instance path"
);

const referenceDiagnostics = validateProject(
  path.join(fixtures, "invalid/missing-reference.json")
);
assert.deepEqual(
  referenceDiagnostics.map(({ code, instancePath }) => ({ code, instancePath })),
  [{ code: "SOURCE_REF_MISSING", instancePath: "/unitTypes/0/classId" }],
  "missing references must have a stable diagnostic code and semantic location"
);

const duplicateDiagnostics = validateProject(
  path.join(fixtures, "invalid/duplicate-identifier.json")
);
assert.deepEqual(
  duplicateDiagnostics.map(({ code, instancePath }) => ({ code, instancePath })),
  [{ code: "SOURCE_ID_DUPLICATE", instancePath: "/classes/1/id" }]
);

const mapDiagnostics = validateProject(path.join(fixtures, "invalid/map-shape.json"));
assert.deepEqual(
  mapDiagnostics.map(({ code, instancePath }) => ({ code, instancePath })),
  [{ code: "SOURCE_MAP_SHAPE_INVALID", instancePath: "/maps/0/terrain" }]
);

const syntaxDiagnostics = validateProject(
  path.join(fixtures, "invalid/malformed.json")
);
assert.equal(syntaxDiagnostics[0].code, "SOURCE_JSON_INVALID");
assert.ok(syntaxDiagnostics[0].line > 1, "syntax error must report a source line");

for (const [filename, code, instancePath] of [
  ["missing-weapon-type.json", "SOURCE_REF_MISSING", "/weapons/0/weaponTypeId"],
  ["missing-item-type.json", "SOURCE_REF_MISSING", "/items/0/itemTypeId"],
  [
    "missing-class-weapon-type.json",
    "SOURCE_REF_MISSING",
    "/classes/0/allowedWeaponTypeIds/0"
  ],
  [
    "missing-starting-item.json",
    "SOURCE_REF_MISSING",
    "/unitTypes/0/startingItemIds/0"
  ],
  [
    "missing-drop-item.json",
    "SOURCE_REF_MISSING",
    "/unitTypes/0/dropItemId"
  ]
]) {
  assert.deepEqual(
    validateProject(path.join(fixtures, "invalid", filename))
      .map(({ code: foundCode, instancePath: foundPath }) => ({
        code: foundCode,
        instancePath: foundPath
      })),
    [{ code, instancePath }],
    `${filename} must produce a stable typed-reference diagnostic`
  );
}

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/missing-authoring-references.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    { code: "SOURCE_REF_MISSING", instancePath: "/unitTypes/0/factionId" },
    { code: "SOURCE_REF_MISSING", instancePath: "/unitTypes/0/abilityIds/0" },
    { code: "SOURCE_REF_MISSING", instancePath: "/campaigns/0/objectiveIds/0" },
    { code: "SOURCE_REF_MISSING", instancePath: "/campaigns/0/dialogueIds/0" }
  ]
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/script-binding-semantics.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_SCRIPT_PARAMETER_DUPLICATE",
      instancePath: "/abilities/0/scriptBindings/0/parameters/1/name"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/abilities/0/scriptBindings/0/parameters/1/value/sourceKey"
    },
    {
      code: "SOURCE_SCRIPT_SLOT_DUPLICATE",
      instancePath: "/abilities/0/scriptBindings/1/slot"
    }
  ],
  "script binding diagnostics must identify exact duplicate and reference paths"
);

assert.deepEqual(
  validateProject(path.join(fixtures, "invalid/unsafe-script-path.json"))
    .map(({ code, instancePath }) => ({ code, instancePath })),
  [{
    code: "SOURCE_SCHEMA_INVALID",
    instancePath: "/abilities/0/scriptBindings/0/scriptPath"
  }],
  "script paths must remain lexically inside the project"
);

// Three rules this validator and the native compiler both hold, on one fixture
// that carries them and nothing else. The compiler asserts the same three faults
// on the same file in `tests/game_content/source_fixtures_test.cpp`, and the
// editor asserts them again in `source-conformance.test.ts`. Held in three
// places and pinned in three, because a rule only one analyzer knows is a rule
// that will drift out of step with the other two.
assert.deepEqual(
  validateProject(path.join(fixtures, "invalid/campaign-flow-routing.json"))
    .map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/dialogueIds/1"
    },
    {
      code: "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/1/priority"
    },
    {
      code: "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/3"
    }
  ],
  "a dangling scene, a shared priority and a second fallback are all named"
);

assert.deepEqual(
  validateProject(path.join(fixtures, "invalid/campaign-flow-semantics.json"))
    .map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_CAMPAIGN_NODE_ID_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/2/id"
    },
    {
      code: "SOURCE_CAMPAIGN_ENTRY_MISSING",
      instancePath: "/campaigns/0/flow/entryNodeId"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/mapId"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/objectiveIds/0"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/dialogueIds/0"
    },
    {
      code: "SOURCE_CAMPAIGN_TRANSITION_TARGET_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/0/targetNodeId"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/0/when/conditions/0/objectiveId"
    },
    {
      code: "SOURCE_CAMPAIGN_OBJECTIVE_RESULT_UNKNOWN",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/0/when/conditions/0/result"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/0/when/conditions/1/itemId"
    },
    {
      code: "SOURCE_CAMPAIGN_TRANSITION_ID_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/1/id"
    },
    {
      code: "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/1/priority"
    },
    {
      code: "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/transitions/2"
    },
    {
      code: "SOURCE_CAMPAIGN_NODE_UNREACHABLE",
      instancePath: "/campaigns/1/flow/nodes/1/id"
    }
  ],
  "campaign graph diagnostics must use deterministic semantic paths"
);

// A talk that names no flag: an author saying somebody is talkable without
// saying what talking to them does, which nothing downstream could act on.
assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/encounter-talk-without-a-flag.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_SCHEMA_INVALID",
      instancePath: "/campaigns/0/flow/nodes/0/placements/1/talk"
    }
  ],
  "a talk stating no flag is refused at the talk's own path"
);
assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/encounter-placement-semantics.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/placements/0/unitTypeId"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_ID_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/placements/1/id"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/placements/1/unitTypeId"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_TILE_OCCUPIED",
      instancePath: "/campaigns/0/flow/nodes/0/placements/1/x"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/placements/2/unitTypeId"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_OUT_OF_BOUNDS",
      instancePath: "/campaigns/0/flow/nodes/0/placements/2/x"
    }
  ],
  "placement diagnostics must agree on identity, references, occupancy, and bounds"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/deployment-semantics.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_CAMPAIGN_DEPLOYMENT_TILE_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/deployment/tiles/1/x"
    },
    {
      code: "SOURCE_CAMPAIGN_DEPLOYMENT_OUT_OF_BOUNDS",
      instancePath: "/campaigns/0/flow/nodes/0/deployment/tiles/2/x"
    },
    {
      code: "SOURCE_CAMPAIGN_DEPLOYMENT_UNOCCUPIED",
      instancePath: "/campaigns/0/flow/nodes/0/deployment/id"
    }
  ],
  "deployment diagnostics must agree on duplicate tiles, bounds, and a region nobody stands in"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/deployment-capacity-unreachable.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE",
      instancePath: "/campaigns/0/flow/nodes/0/deployment/capacity"
    },
    {
      code: "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE",
      instancePath: "/campaigns/0/flow/nodes/1/deployment/capacity"
    }
  ],
  "a cap the board can never reach must be refused at the cap, whether it "
    + "equals the placements the encounter authors or exceeds them"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/deployment-states-nothing.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_CAMPAIGN_DEPLOYMENT_EMPTY",
      instancePath: "/campaigns/0/flow/nodes/0/deployment/id"
    }
  ],
  "a deployment that states neither a region nor a cap must be refused at the "
    + "deployment's own identity, where every other deployment fault is "
    + "refused, and in a sentence rather than a failed keyword"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/campaign-grant-semantics.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/startingStore/0/itemId"
    },
    {
      code: "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
      instancePath: "/campaigns/0/startingStore/2/itemId"
    },
    {
      code: "SOURCE_CAMPAIGN_GRANT_SUBJECT_INVALID",
      instancePath: "/campaigns/0/startingStore/3/itemId"
    },
    {
      code: "SOURCE_CAMPAIGN_GRANT_SUBJECT_INVALID",
      instancePath: "/campaigns/0/startingStore/4/quantity"
    },
    {
      code: "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
      instancePath: "/campaigns/0/startingStore/6/weaponId"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/flow/nodes/0/grants/0/itemId"
    },
    {
      code: "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
      instancePath: "/campaigns/0/flow/nodes/0/grants/2/itemId"
    }
  ],
  "a store or a grant naming an item nothing defines, naming one thing twice, "
    + "or naming both an item and a weapon or neither, must be refused at the "
    + "entry that names it. A weapon and an item are counted apart: the legal "
    + "weapon grant at index five is what makes the duplicate at six a "
    + "duplicate and not the first of its kind"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/deployment-on-a-story-node.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_SCHEMA_INVALID",
      instancePath: "/campaigns/0/flow/nodes/1/deployment"
    },
    {
      code: "SOURCE_SCHEMA_INVALID",
      instancePath: "/campaigns/0/flow/nodes/1"
    }
  ],
  "only an encounter node may state a region"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/campaign-roster-semantics.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_CAMPAIGN_ROSTER_EMPTY",
      instancePath: "/campaigns/0/roster"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/1/roster/2/unitTypeId"
    },
    {
      code: "SOURCE_CAMPAIGN_MEMBER_ID_DUPLICATE",
      instancePath: "/campaigns/1/flow/nodes/0/recruits/0/id"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_MISSING",
      instancePath: "/campaigns/1/flow/nodes/0/placements/1"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_UNKNOWN",
      instancePath: "/campaigns/1/flow/nodes/0/placements/2/memberId"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_FORBIDDEN",
      instancePath: "/campaigns/1/flow/nodes/0/placements/3/memberId"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_DUPLICATE",
      instancePath: "/campaigns/1/flow/nodes/0/placements/4/memberId"
    },
    {
      code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_TYPE_MISMATCH",
      instancePath: "/campaigns/1/flow/nodes/0/placements/5/unitTypeId"
    }
  ],
  "roster diagnostics must name the campaign that nobody can found, the "
    + "member identity claimed twice, and each placement that fields the wrong "
    + "member, nobody, or somebody on the wrong side"
);

assert.deepEqual(
  validateProject(path.join(
    fixtures,
    "invalid/campaign-member-specificity-semantics.json"
  )).map(({ code, instancePath }) => ({ code, instancePath })),
  [
    {
      code: "SOURCE_CAMPAIGN_SPECIFICITY_EMPTY",
      instancePath: "/campaigns/0/roster/0/specificity"
    },
    {
      code: "SOURCE_CAMPAIGN_STAT_DELTA_ZERO",
      instancePath: "/campaigns/0/roster/1/specificity/stats/luck"
    },
    {
      code: "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
      instancePath: "/campaigns/0/roster/2/specificity/stats/health"
    },
    {
      code: "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
      instancePath: "/campaigns/0/roster/3/specificity/stats/movement"
    },
    {
      code: "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
      instancePath: "/campaigns/0/roster/4/specificity/stats/movement"
    },
    {
      code: "SOURCE_REF_MISSING",
      instancePath: "/campaigns/0/roster/5/unitTypeId"
    },
    {
      code: "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
      instancePath: "/campaigns/0/flow/nodes/0/recruits/0/specificity/stats/speed"
    }
  ],
  "a specificity that says nothing, a delta of nought, and a delta landing "
    + "outside what the stat's own field admits must each be refused at the "
    + "path that says it, on a founding member and on a recruit alike, and a "
    + "member whose unit type nothing defines must be reported as the "
    + "unresolved reference it is rather than as a delta nobody could measure"
);

const roundTripSource = {
  zeta: 2,
  extensions: {
    "example:future": {
      retained: true,
      nested: ["a", "b"]
    }
  },
  alpha: 1
};
const firstSerialization = serializeCanonical(roundTripSource);
const secondSerialization = serializeCanonical(JSON.parse(firstSerialization));
assert.equal(firstSerialization, secondSerialization, "canonical output must be stable");
assert.deepEqual(
  JSON.parse(firstSerialization).extensions,
  roundTripSource.extensions,
  "allowed unknown extension content must survive a no-edit round trip"
);
assert.ok(
  firstSerialization.indexOf("\"alpha\"") < firstSerialization.indexOf("\"zeta\""),
  "object keys must have deterministic order for reviewable diffs"
);

{
  // The version a project declares, and every place that spells it out.
  //
  // One home: `migration.mjs` derives it from the chain of migrations, because
  // a version exists exactly when a step arrives at it. Everything below writes
  // the string again because it cannot do otherwise: a JSON schema cannot
  // import a module, C++ cannot either, and an authored game is a file. So this
  // reads each of them and checks they still agree, exactly as the stat-cap
  // check above reads the engine's header.
  //
  // A site that drifts is not a cosmetic problem. The schema's `const` decides
  // what the editor will open, the compiler's constant decides what the
  // toolchain will build, and the shipped games have to be openable by both.
  // Any two of them disagreeing means a file one half of the repository writes
  // is a file the other half turns away.
  const read = (relative) => fs.readFileSync(
    path.resolve(directory, relative),
    "utf8"
  );
  const oneOf = (text, pattern, where) => {
    const found = pattern.exec(text)?.[1];
    assert.ok(
      found !== undefined,
      `${where} must still state the source version where this can read it; if `
        + "it moved, this check moves with it rather than falling silent"
    );
    return found;
  };

  assert.equal(
    CURRENT_SOURCE_VERSION,
    sourceMigrations().current(),
    "the current version is the end of the shipped chain and nothing else"
  );

  const elsewhere = {
    "schemas/source/v1/project.schema.json": JSON.parse(
      read("../../schemas/source/v1/project.schema.json")
    ).properties.schemaVersion.const,
    "tools/game_content/.../source_project.hpp": oneOf(
      read("../../tools/game_content/include/grandleon/game_content/source_project.hpp"),
      /supported_source_schema\s*=\s*"([^"]+)"/,
      "the native compiler's supported_source_schema"
    ),
    "games/demo/source/project.json": JSON.parse(
      read("../../games/demo/source/project.json")
    ).schemaVersion,
    "games/tarnholt/source/project.json": JSON.parse(
      read("../../games/tarnholt/source/project.json")
    ).schemaVersion
  };
  for (const [where, declared] of Object.entries(elsewhere)) {
    assert.equal(
      declared,
      CURRENT_SOURCE_VERSION,
      `${where} must say the version the migration registry ends at, or one `
        + "half of the repository writes files the other half refuses"
    );
  }

  // The editor is not on that list because it does not restate the version at
  // all: it imports this registry. That is the better answer wherever a site
  // can reach a module, and it is checked where it can be run rather than
  // read: `editor/src/domain/source-migration.test.ts` asserts that the project
  // a new game starts from declares exactly `CURRENT_SOURCE_VERSION`. What is
  // pinned here is only what cannot import: a JSON schema, a C++ constant, and
  // two authored files.
  for (const editorFile of [
    "../../editor/src/domain/source-project-document.ts",
    "../../editor/src/analysis/source-analysis.ts"
  ]) {
    assert.ok(
      read(editorFile).includes("CURRENT_SOURCE_VERSION"),
      `${editorFile} must ask the registry for the version rather than write `
        + "it again; a literal here is a site this check cannot see drift in"
    );
  }
}

{
  // The registry, on the chain this build actually ships and on one that does
  // not.
  //
  // The shipped chain is short, so the rules themselves are still exercised
  // against `migration-example.mjs`, whose steps describe changes the format
  // never made and which nothing outside a test may import. What is checked
  // here is that the real chain is walkable and arrives where every other site
  // says it does.
  assert.equal(
    sourceMigrations().size,
    2,
    "two steps ship: the ones that reach 1.1.0 and 1.2.0"
  );
  assert.deepEqual(
    sourceMigrations().versions(),
    [FIRST_SOURCE_VERSION, "1.1.0", "1.2.0"],
    "and therefore three versions a project may have been written at"
  );

  // A project written before moments existed walks all the way up and is left
  // alone on the way. Every field either step is about is optional, and absent
  // means what that project already said - a battle nobody speaks during,
  // drawn as sprites, with no weapon better than any other - so the steps move
  // the version and touch nothing else.
  //
  // Both steps in one walk is the property that matters here: an author who
  // skipped a release is not asked to upgrade twice.
  {
    const old = { schemaVersion: FIRST_SOURCE_VERSION, title: "An Old Game" };
    const walked = upgradeProject(sourceMigrations(), old);
    assert.equal(walked.ok, true, "the shipped chain is walkable");
    assert.deepEqual(
      walked.applied.map((step) => `${step.from}->${step.to}`),
      ["1.0.0->1.1.0", "1.1.0->1.2.0"],
      "both steps, in the order the chain holds them"
    );
    assert.equal(walked.project.schemaVersion, "1.2.0", "arriving at the version");
    assert.equal(walked.project.title, "An Old Game", "with the game untouched");
    assert.equal(
      walked.changed.length,
      2,
      "and one sentence for each step, which is what the author is shown"
    );
    assert.equal(
      old.schemaVersion,
      FIRST_SOURCE_VERSION,
      "and the caller's own project left where it was"
    );
  }

  // A chain runs in order, and the project it produces is the one the last step
  // left behind rather than the one any earlier step did.
  const project = { schemaVersion: EXAMPLE_OLDEST, title: "An Old Game" };
  const walked = upgradeProject(exampleMigrations(), project);
  assert.equal(walked.ok, true, "the registered steps make a walkable chain");
  assert.deepEqual(
    walked.applied.map((step) => `${step.from}->${step.to}`),
    ["0.8.0->0.9.0", "0.9.0->1.0.0", "1.0.0->1.1.0", "1.1.0->1.2.0"],
    "one version at a time, in order, and never in a leap"
  );
  assert.equal(walked.to, CURRENT_SOURCE_VERSION);
  assert.equal(
    walked.project.schemaVersion,
    CURRENT_SOURCE_VERSION,
    "the upgraded project declares where it arrived, not where it started"
  );
  assert.equal(walked.project.themeId, "temperate", "the first step ran");
  assert.equal(
    walked.project.defaultTurnOrder,
    "sideBlocks",
    "and the second ran after it, on what the first step left"
  );
  assert.equal(walked.project.title, "An Old Game", "and neither touched the rest");

  // The sentences an author reads, in the order the steps run. This is the list
  // the editor's dialog shows, so its order is the order things happened in.
  assert.deepEqual(
    walked.changed,
    [FIRST_CHANGE, SECOND_CHANGE, THIRD_CHANGE, FOURTH_CHANGE],
    "what changed, in the order it changed, in words an author can read"
  );
  assert.deepEqual(
    planChanges(planUpgrade(exampleMigrations(), EXAMPLE_OLDEST)),
    [FIRST_CHANGE, SECOND_CHANGE, THIRD_CHANGE, FOURTH_CHANGE],
    "and the same list before anything has run, which is what the dialog asks "
      + "the author to agree to"
  );
  assert.deepEqual(
    planChanges(planUpgrade(exampleMigrations(), "0.9.0")),
    [SECOND_CHANGE, THIRD_CHANGE, FOURTH_CHANGE],
    "a project part of the way up is told only what is left to do to it"
  );

  // Old project in, new project out. The caller's copy is untouched, which is
  // what makes a load transactional: there is never one project being
  // upgraded, only a chain of complete candidates.
  assert.deepEqual(
    project,
    { schemaVersion: EXAMPLE_OLDEST, title: "An Old Game" },
    "the caller's project is not moved"
  );

  // A gap is a gap, and it is named.
  const holed = upgradeProject(chainWithAHole(), { schemaVersion: EXAMPLE_OLDEST });
  assert.equal(holed.ok, false);
  assert.equal(holed.refusal, "missing_step");
  assert.equal(
    holed.stoppedAt,
    "0.9.0",
    "the chain stops where it stops, and says so, rather than skipping to the "
      + "step it does have"
  );
  assert.equal(holed.to, "1.1.0", "while still naming where it needed to reach");
  assert.equal(holed.project, undefined, "and hands back no half-upgraded game");

  // A version below everything the registry knows is the same answer: there is
  // no step out of it, and the honest thing is to say which version that was.
  const ancient = planUpgrade(exampleMigrations(), "0.5.0");
  assert.equal(ancient.ok, false);
  assert.equal(ancient.refusal, "missing_step");
  assert.equal(ancient.stoppedAt, "0.5.0");

  // Going backwards is refused rather than attempted.
  const newer = upgradeProject(exampleMigrations(), { schemaVersion: "2.0.0" });
  assert.equal(newer.ok, false);
  assert.equal(
    newer.refusal,
    "downgrade_refused",
    "a project written by a newer build knows things this one does not, and "
      + "the only honest transform down is a lossy one"
  );
  assert.equal(newer.from, "2.0.0");
  assert.equal(newer.to, CURRENT_SOURCE_VERSION);
  assert.equal(newer.project, undefined);

  // A file that declares nothing placeable is neither old nor new.
  for (const declared of [undefined, "", "one point oh", "1.0"]) {
    const nonsense = upgradeProject(
      exampleMigrations(),
      declared === undefined ? {} : { schemaVersion: declared }
    );
    assert.equal(nonsense.ok, false);
    assert.equal(
      nonsense.refusal,
      "unreadable_version",
      `${JSON.stringify(declared)} is not a version, and saying so is not the `
        + "same as saying the project is out of date"
    );
  }

  // A step that throws leaves the project exactly as it was, including the
  // damage that step did to the copy it was handed, which the caller never
  // sees because it was never their object.
  const fragile = { schemaVersion: EXAMPLE_OLDEST, title: "Untouched" };
  const failed = upgradeProject(migrationsThatThrow(), fragile);
  assert.equal(failed.ok, false);
  assert.equal(failed.refusal, "step_failed");
  assert.equal(failed.stoppedAt, EXAMPLE_OLDEST, "and names which step refused");
  assert.match(failed.detail, /not what the step expected/);
  assert.equal(failed.project, undefined);
  assert.deepEqual(
    fragile,
    { schemaVersion: EXAMPLE_OLDEST, title: "Untouched" },
    "a step that got half way through leaves nothing half way through"
  );

  // A step with nothing to tell the author cannot be built. The dialog lists
  // what a chain is about to do, so a step that says nothing is a step asking
  // for consent to something nobody described.
  for (const changed of [undefined, "", "   ", null, 7]) {
    assert.throws(
      () => new SourceMigrationRegistry().add({
        from: "1.0.0",
        to: "1.1.0",
        changed,
        apply: (one) => one
      }),
      /must say what changed/,
      `a migration whose sentence is ${JSON.stringify(changed)} must not exist`
    );
  }

  // Two steps out of one version is an ambiguity nobody could settle later.
  assert.throws(
    () => exampleMigrations().add({
      from: EXAMPLE_OLDEST,
      to: "0.9.0",
      changed: "a second answer to a question that has one",
      apply: (one) => one
    }),
    /already registered/
  );

  // A step must go forwards, and it must go somewhere real.
  assert.throws(
    () => new SourceMigrationRegistry().add({
      from: "1.1.0",
      to: "1.0.0",
      changed: "backwards",
      apply: (one) => one
    }),
    /must go forwards/
  );
  assert.throws(
    () => new SourceMigrationRegistry().add({
      from: "1.0.0",
      to: "tomorrow",
      changed: "nowhere",
      apply: (one) => one
    }),
    /must be a version/
  );
  assert.throws(
    () => new SourceMigrationRegistry().add({
      from: "1.0.0",
      to: "1.1.0",
      changed: "nothing to run",
      apply: undefined
    }),
    /must have something to apply/
  );

  // A hostile file claiming a distant version costs a bounded walk rather than
  // an unbounded one.
  const long = new SourceMigrationRegistry();
  for (let minor = 0; minor <= MAXIMUM_MIGRATION_STEPS; minor += 1) {
    long.add({
      from: `1.${minor}.0`,
      to: `1.${minor + 1}.0`,
      changed: `step ${minor}`,
      apply: (one) => one
    });
  }
  const capped = planUpgrade(long, "1.0.0");
  assert.equal(capped.ok, false);
  assert.equal(capped.refusal, "step_limit_exceeded");

  // A step that forgets to stamp the version it arrived at is stamped anyway:
  // the registry knows where the step was going and the step does not have to
  // be trusted to remember.
  const forgetful = new SourceMigrationRegistry().add({
    from: "1.0.0",
    to: "1.1.0",
    changed: "a step that changes nothing and says so",
    apply: (one) => ({ ...one })
  });
  assert.equal(
    upgradeProject(forgetful, { schemaVersion: "1.0.0" }).project.schemaVersion,
    "1.1.0"
  );

  // A project already at the current version is not an upgrade with no steps in
  // it, it is a project that needs nothing. It comes back unchanged. Written as
  // the registry's own answer rather than as a literal, so this stays about
  // "already current" when the format moves again.
  const current = { schemaVersion: CURRENT_SOURCE_VERSION, title: "Now" };
  const nothing = upgradeProject(sourceMigrations(), current);
  assert.equal(nothing.ok, true);
  assert.deepEqual(nothing.changed, []);
  assert.deepEqual(nothing.project, current);
  assert.notEqual(nothing.project, current, "and is still a copy, never the original");
}

{
  // The shipped games are current, and the upgrade tool says so rather than
  // touching them. `--check` is what a script asks; it must not write.
  const demo = path.resolve(directory, "../../games/demo/source/project.json");
  const before = fs.readFileSync(demo, "utf8");
  const said = [];
  const spoke = console.log;
  console.log = (line) => said.push(line);
  let verdict;
  try {
    verdict = upgradeMain(["--check", demo]);
  } finally {
    console.log = spoke;
  }
  assert.equal(verdict, 0, "a project at the current version is already up to date");
  assert.equal(fs.readFileSync(demo, "utf8"), before, "and was not rewritten");
  assert.match(said.join("\n"), new RegExp(`already made with Grandleon ${CURRENT_SOURCE_VERSION}`));

  // What a person is told when the file is from the future. The compiler and
  // this tool must not disagree about which direction the problem is in.
  assert.match(
    refusalMessage(
      { refusal: "downgrade_refused", from: "9.0.0", to: CURRENT_SOURCE_VERSION },
      "some/project.json"
    ),
    /made with a newer Grandleon \(9\.0\.0\)/
  );
  assert.match(
    refusalMessage(
      { refusal: "missing_step", from: "0.9.0", to: "1.0.0", stoppedAt: "0.9.0" },
      "some/project.json"
    ),
    /no way up from 0\.9\.0.*Nothing was changed/s
  );
}

console.log("source schema fixtures passed");
