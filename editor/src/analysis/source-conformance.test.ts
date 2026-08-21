// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import minimal from "../../../tests/fixtures/source_projects/valid/minimal.json?raw";
import typedContent from "../../../tests/fixtures/source_projects/valid/typed-content.json?raw";
import authoringRegistries from "../../../tests/fixtures/source_projects/valid/authoring-registries.json?raw";
import scriptBindings from "../../../tests/fixtures/source_projects/valid/script-bindings.json?raw";
import nonlinearCampaign from "../../../tests/fixtures/source_projects/valid/nonlinear-campaign.json?raw";
import encounterPlacements from "../../../tests/fixtures/source_projects/valid/encounter-placements.json?raw";
import characterTraversal from "../../../tests/fixtures/source_projects/valid/character-traversal.json?raw";
import campaignRoster from "../../../tests/fixtures/source_projects/valid/campaign-roster.json?raw";
import deploymentZone from "../../../tests/fixtures/source_projects/valid/deployment-zone.json?raw";
import campaignStockAndCap from "../../../tests/fixtures/source_projects/valid/campaign-stock-and-cap.json?raw";
import campaignMemberSpecificity from "../../../tests/fixtures/source_projects/valid/campaign-member-specificity.json?raw";
import campaignMemberSpecificitySemantics from "../../../tests/fixtures/source_projects/invalid/campaign-member-specificity-semantics.json?raw";
import deploymentSemantics from "../../../tests/fixtures/source_projects/invalid/deployment-semantics.json?raw";
import deploymentOnAStoryNode from "../../../tests/fixtures/source_projects/invalid/deployment-on-a-story-node.json?raw";
import deploymentCapacityUnreachable from "../../../tests/fixtures/source_projects/invalid/deployment-capacity-unreachable.json?raw";
import deploymentStatesNothing from "../../../tests/fixtures/source_projects/invalid/deployment-states-nothing.json?raw";
import campaignGrantSemantics from "../../../tests/fixtures/source_projects/invalid/campaign-grant-semantics.json?raw";
import badIdentifier from "../../../tests/fixtures/source_projects/invalid/bad-identifier.json?raw";
import duplicateIdentifier from "../../../tests/fixtures/source_projects/invalid/duplicate-identifier.json?raw";
import malformed from "../../../tests/fixtures/source_projects/invalid/malformed.json?raw";
import mapShape from "../../../tests/fixtures/source_projects/invalid/map-shape.json?raw";
import missingReference from "../../../tests/fixtures/source_projects/invalid/missing-reference.json?raw";
import missingWeaponType from "../../../tests/fixtures/source_projects/invalid/missing-weapon-type.json?raw";
import missingItemType from "../../../tests/fixtures/source_projects/invalid/missing-item-type.json?raw";
import missingClassWeaponType from "../../../tests/fixtures/source_projects/invalid/missing-class-weapon-type.json?raw";
import missingStartingItem from "../../../tests/fixtures/source_projects/invalid/missing-starting-item.json?raw";
import missingAuthoringReferences from "../../../tests/fixtures/source_projects/invalid/missing-authoring-references.json?raw";
import scriptBindingSemantics from "../../../tests/fixtures/source_projects/invalid/script-binding-semantics.json?raw";
import unsafeScriptPath from "../../../tests/fixtures/source_projects/invalid/unsafe-script-path.json?raw";
import campaignFlowSemantics from "../../../tests/fixtures/source_projects/invalid/campaign-flow-semantics.json?raw";
import campaignFlowRouting from "../../../tests/fixtures/source_projects/invalid/campaign-flow-routing.json?raw";
import encounterPlacementSemantics from "../../../tests/fixtures/source_projects/invalid/encounter-placement-semantics.json?raw";
import campaignRosterSemantics from "../../../tests/fixtures/source_projects/invalid/campaign-roster-semantics.json?raw";
import unknownCrossing from "../../../tests/fixtures/source_projects/invalid/unknown-crossing.json?raw";
import weaponAccuracy from "../../../tests/fixtures/source_projects/valid/weapon-accuracy.json?raw";
import accuracyOutOfRange from "../../../tests/fixtures/source_projects/invalid/accuracy-out-of-range.json?raw";
import unitGrowthRates from "../../../tests/fixtures/source_projects/valid/unit-growth-rates.json?raw";
import growthOutOfRange from "../../../tests/fixtures/source_projects/invalid/growth-out-of-range.json?raw";
import richerStatLine from "../../../tests/fixtures/source_projects/valid/richer-stat-line.json?raw";
import statLineOutOfRange from "../../../tests/fixtures/source_projects/invalid/stat-line-out-of-range.json?raw";
import statPastTheDamageCap from "../../../tests/fixtures/source_projects/invalid/stat-past-the-damage-cap.json?raw";
import itemEffects from "../../../tests/fixtures/source_projects/valid/item-effects.json?raw";
import itemEffectOutOfRange from "../../../tests/fixtures/source_projects/invalid/item-effect-out-of-range.json?raw";
import unitDrops from "../../../tests/fixtures/source_projects/valid/unit-drops.json?raw";
import missingDropItem from "../../../tests/fixtures/source_projects/invalid/missing-drop-item.json?raw";
import dropHalfAuthored from "../../../tests/fixtures/source_projects/invalid/drop-half-authored.json?raw";
import dropOutOfRange from "../../../tests/fixtures/source_projects/invalid/drop-out-of-range.json?raw";
import survivingWaves from "../../../tests/fixtures/source_projects/valid/surviving-waves.json?raw";
import arrivalSemantics from "../../../tests/fixtures/source_projects/invalid/arrival-semantics.json?raw";
import gameWideTurnOrder from "../../../tests/fixtures/source_projects/valid/game-wide-turn-order.json?raw";
import unknownTurnOrderDefault from "../../../tests/fixtures/source_projects/invalid/unknown-turn-order-default.json?raw";
// The two settings a project states about what a fall costs. They are separate
// settings that happen to travel together, so each is checked on its own and
// the pair is checked stated side by side.
import characterLossRecoverable from "../../../tests/fixtures/source_projects/valid/character-loss-recoverable.json?raw";
import testingInvulnerability from "../../../tests/fixtures/source_projects/valid/testing-invulnerability.json?raw";
import unknownCharacterLoss from "../../../tests/fixtures/source_projects/invalid/unknown-character-loss.json?raw";
// The presentation pair the command-line analyzer checks. A project-level menu
// is exactly the kind of field the two must agree about, so both menus are
// checked here as well.
import presentationChoices from "../../../tests/fixtures/source_projects/valid/presentation-choices.json?raw";
import unknownCharacterStyle from "../../../tests/fixtures/source_projects/invalid/unknown-character-style.json?raw";
import unknownSceneBackdrop from "../../../tests/fixtures/source_projects/invalid/unknown-scene-backdrop.json?raw";
// Who a scene says its speakers are. The valid fixture holds the three cases a
// client has to tell apart; the invalid one holds every mistake a cast can
// make that JSON Schema cannot see, on the paths the command-line analyzer
// reports them at.
import sceneCast from "../../../tests/fixtures/source_projects/valid/scene-cast.json?raw";
import sceneCastSemantics from "../../../tests/fixtures/source_projects/invalid/scene-cast-semantics.json?raw";
import { analyzeSourceProject } from "./source-analysis";

describe("source-tool conformance fixtures", () => {
  it.each([
    ["valid/minimal.json", minimal, []],
    ["valid/typed-content.json", typedContent, []],
    ["valid/authoring-registries.json", authoringRegistries, []],
    ["valid/script-bindings.json", scriptBindings, []],
    ["valid/nonlinear-campaign.json", nonlinearCampaign, []],
    ["valid/encounter-placements.json", encounterPlacements, []],
    ["valid/character-traversal.json", characterTraversal, []],
    ["valid/campaign-roster.json", campaignRoster, []],
    ["valid/deployment-zone.json", deploymentZone, []],
    ["valid/campaign-stock-and-cap.json", campaignStockAndCap, []],
    [
      "valid/campaign-member-specificity.json",
      campaignMemberSpecificity,
      []
    ],
    ["valid/item-effects.json", itemEffects, []],
    ["valid/weapon-accuracy.json", weaponAccuracy, []],
    ["valid/unit-growth-rates.json", unitGrowthRates, []],
    ["valid/richer-stat-line.json", richerStatLine, []],
    ["valid/unit-drops.json", unitDrops, []],
    ["valid/surviving-waves.json", survivingWaves, []],
    ["valid/game-wide-turn-order.json", gameWideTurnOrder, []],
    ["valid/character-loss-recoverable.json", characterLossRecoverable, []],
    ["valid/testing-invulnerability.json", testingInvulnerability, []],
    ["valid/presentation-choices.json", presentationChoices, []],
    ["valid/scene-cast.json", sceneCast, []],
    ["invalid/scene-cast-semantics.json", sceneCastSemantics, [
      ["SOURCE_DIALOGUE_SPEAKER_CAST_TWICE", "/dialogues/0/cast/1/speaker"],
      ["SOURCE_REF_MISSING", "/dialogues/0/cast/2/unitTypeId"],
      ["SOURCE_DIALOGUE_CAST_SPEAKS_NO_LINE", "/dialogues/0/cast/2/speaker"]
    ]],
    ["invalid/unknown-turn-order-default.json", unknownTurnOrderDefault, [
      ["SOURCE_SCHEMA_INVALID", "/defaultTurnOrder"]
    ]],
    ["invalid/unknown-character-loss.json", unknownCharacterLoss, [
      ["SOURCE_SCHEMA_INVALID", "/characterLoss"]
    ]],
    ["invalid/unknown-character-style.json", unknownCharacterStyle, [
      ["SOURCE_SCHEMA_INVALID", "/characterStyleId"]
    ]],
    ["invalid/unknown-scene-backdrop.json", unknownSceneBackdrop, [
      ["SOURCE_SCHEMA_INVALID", "/dialogues/0/backgroundId"]
    ]],
    ["invalid/arrival-semantics.json", arrivalSemantics, [
      [
        "SOURCE_CAMPAIGN_ARRIVAL_MEMBER",
        "/campaigns/0/flow/nodes/0/placements/0/arrival"
      ],
      ["SOURCE_OBJECTIVE_ROUNDS_MISMATCH", "/objectives/0/rounds"]
    ]],
    ["invalid/bad-identifier.json", badIdentifier, [
      ["SOURCE_SCHEMA_INVALID", "/classes/0/id"]
    ]],
    ["invalid/duplicate-identifier.json", duplicateIdentifier, [
      ["SOURCE_ID_DUPLICATE", "/classes/1/id"]
    ]],
    ["invalid/malformed.json", malformed, [
      ["SOURCE_JSON_INVALID", "/"]
    ]],
    ["invalid/unknown-crossing.json", unknownCrossing, [
      ["SOURCE_SCHEMA_INVALID", "/classes/0/traversal/crossings/0"]
    ]],
    ["invalid/accuracy-out-of-range.json", accuracyOutOfRange, [
      ["SOURCE_SCHEMA_INVALID", "/weapons/0/accuracy"],
      ["SOURCE_SCHEMA_INVALID", "/abilities/0/accuracy"]
    ]],
    ["invalid/item-effect-out-of-range.json", itemEffectOutOfRange, [
      ["SOURCE_SCHEMA_INVALID", "/items/0/kind"],
      ["SOURCE_SCHEMA_INVALID", "/items/1/power"]
    ]],
    ["invalid/stat-line-out-of-range.json", statLineOutOfRange, [
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/skill"],
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/luck"],
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/evasion"],
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/magic"],
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/0/growthRates/skill"],
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/0/growthRates/magic"]
    ]],
    // Every number the damage arithmetic reads stops at the engine's
    // `maximum_stat`, written past it in each of the seven places one can be
    // written. `tools/source_schema/test.mjs` asserts the same seven instance
    // paths on the same file and checks the schema's cap against the engine
    // header; the native compiler asserts them again in
    // `tests/game_content/source_fixtures_test.cpp`.
    ["invalid/stat-past-the-damage-cap.json", statPastTheDamageCap, [
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/strength"],
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/defense"],
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/resistance"],
      ["SOURCE_SCHEMA_INVALID", "/classes/0/baseStats/magic"],
      ["SOURCE_SCHEMA_INVALID", "/weapons/0/power"],
      ["SOURCE_SCHEMA_INVALID", "/items/0/power"],
      ["SOURCE_SCHEMA_INVALID", "/abilities/0/power"]
    ]],
    ["invalid/growth-out-of-range.json", growthOutOfRange, [
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/0/growthRates/health"],
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/0/growthRates/strength"],
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/1/experiencePerLevel"]
    ]],
    ["invalid/map-shape.json", mapShape, [
      ["SOURCE_MAP_SHAPE_INVALID", "/maps/0/terrain"]
    ]],
    ["invalid/missing-reference.json", missingReference, [
      ["SOURCE_REF_MISSING", "/unitTypes/0/classId"]
    ]],
    ["invalid/missing-weapon-type.json", missingWeaponType, [
      ["SOURCE_REF_MISSING", "/weapons/0/weaponTypeId"]
    ]],
    ["invalid/missing-item-type.json", missingItemType, [
      ["SOURCE_REF_MISSING", "/items/0/itemTypeId"]
    ]],
    ["invalid/missing-class-weapon-type.json", missingClassWeaponType, [
      ["SOURCE_REF_MISSING", "/classes/0/allowedWeaponTypeIds/0"]
    ]],
    ["invalid/missing-starting-item.json", missingStartingItem, [
      ["SOURCE_REF_MISSING", "/unitTypes/0/startingItemIds/0"]
    ]],
    ["invalid/missing-drop-item.json", missingDropItem, [
      ["SOURCE_REF_MISSING", "/unitTypes/0/dropItemId"]
    ]],
    ["invalid/drop-half-authored.json", dropHalfAuthored, [
      ["SOURCE_DROP_INCOMPLETE", "/unitTypes/0/dropItemId"],
      ["SOURCE_DROP_INCOMPLETE", "/unitTypes/1/dropChance"]
    ]],
    ["invalid/drop-out-of-range.json", dropOutOfRange, [
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/0/dropChance"],
      ["SOURCE_SCHEMA_INVALID", "/unitTypes/1/dropChance"]
    ]],
    ["invalid/missing-authoring-references.json", missingAuthoringReferences, [
      ["SOURCE_REF_MISSING", "/unitTypes/0/factionId"],
      ["SOURCE_REF_MISSING", "/unitTypes/0/abilityIds/0"],
      ["SOURCE_REF_MISSING", "/campaigns/0/objectiveIds/0"],
      ["SOURCE_REF_MISSING", "/campaigns/0/dialogueIds/0"]
    ]],
    ["invalid/script-binding-semantics.json", scriptBindingSemantics, [
      [
        "SOURCE_SCRIPT_PARAMETER_DUPLICATE",
        "/abilities/0/scriptBindings/0/parameters/1/name"
      ],
      [
        "SOURCE_REF_MISSING",
        "/abilities/0/scriptBindings/0/parameters/1/value/sourceKey"
      ],
      [
        "SOURCE_SCRIPT_SLOT_DUPLICATE",
        "/abilities/0/scriptBindings/1/slot"
      ]
    ]],
    ["invalid/unsafe-script-path.json", unsafeScriptPath, [
      [
        "SOURCE_SCHEMA_INVALID",
        "/abilities/0/scriptBindings/0/scriptPath"
      ]
    ]],
    // Three rules this analyzer, `tools/source_schema/validate.mjs` and the
    // native compiler all hold, on one fixture carrying them and nothing else.
    // The other two assert the same three faults on the same file, so a rule
    // that drifts in any one of the three shows up in the other two.
    ["invalid/campaign-flow-routing.json", campaignFlowRouting, [
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/dialogueIds/1"],
      [
        "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
        "/campaigns/0/flow/nodes/0/transitions/1/priority"
      ],
      [
        "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
        "/campaigns/0/flow/nodes/0/transitions/3"
      ]
    ]],
    ["invalid/campaign-flow-semantics.json", campaignFlowSemantics, [
      ["SOURCE_CAMPAIGN_NODE_ID_DUPLICATE", "/campaigns/0/flow/nodes/2/id"],
      ["SOURCE_CAMPAIGN_ENTRY_MISSING", "/campaigns/0/flow/entryNodeId"],
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/mapId"],
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/objectiveIds/0"],
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/dialogueIds/0"],
      [
        "SOURCE_CAMPAIGN_TRANSITION_TARGET_MISSING",
        "/campaigns/0/flow/nodes/0/transitions/0/targetNodeId"
      ],
      [
        "SOURCE_REF_MISSING",
        "/campaigns/0/flow/nodes/0/transitions/0/when/conditions/0/objectiveId"
      ],
      [
        "SOURCE_CAMPAIGN_OBJECTIVE_RESULT_UNKNOWN",
        "/campaigns/0/flow/nodes/0/transitions/0/when/conditions/0/result"
      ],
      [
        "SOURCE_REF_MISSING",
        "/campaigns/0/flow/nodes/0/transitions/0/when/conditions/1/itemId"
      ],
      [
        "SOURCE_CAMPAIGN_TRANSITION_ID_DUPLICATE",
        "/campaigns/0/flow/nodes/0/transitions/1/id"
      ],
      [
        "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
        "/campaigns/0/flow/nodes/0/transitions/1/priority"
      ],
      [
        "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
        "/campaigns/0/flow/nodes/0/transitions/2"
      ],
      [
        "SOURCE_CAMPAIGN_NODE_UNREACHABLE",
        "/campaigns/1/flow/nodes/1/id"
      ]
    ]],
    ["invalid/encounter-placement-semantics.json", encounterPlacementSemantics, [
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/placements/0/unitTypeId"],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_ID_DUPLICATE",
        "/campaigns/0/flow/nodes/0/placements/1/id"
      ],
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/placements/1/unitTypeId"],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_TILE_OCCUPIED",
        "/campaigns/0/flow/nodes/0/placements/1/x"
      ],
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/placements/2/unitTypeId"],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_OUT_OF_BOUNDS",
        "/campaigns/0/flow/nodes/0/placements/2/x"
      ]
    ]],
    ["invalid/deployment-semantics.json", deploymentSemantics, [
      [
        "SOURCE_CAMPAIGN_DEPLOYMENT_TILE_DUPLICATE",
        "/campaigns/0/flow/nodes/0/deployment/tiles/1/x"
      ],
      [
        "SOURCE_CAMPAIGN_DEPLOYMENT_OUT_OF_BOUNDS",
        "/campaigns/0/flow/nodes/0/deployment/tiles/2/x"
      ],
      [
        "SOURCE_CAMPAIGN_DEPLOYMENT_UNOCCUPIED",
        "/campaigns/0/flow/nodes/0/deployment/id"
      ]
    ]],
    ["invalid/deployment-on-a-story-node.json", deploymentOnAStoryNode, [
      ["SOURCE_SCHEMA_INVALID", "/campaigns/0/flow/nodes/1/deployment"],
      ["SOURCE_SCHEMA_INVALID", "/campaigns/0/flow/nodes/1"]
    ]],
    [
      "invalid/deployment-capacity-unreachable.json",
      deploymentCapacityUnreachable,
      [
        [
          "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE",
          "/campaigns/0/flow/nodes/0/deployment/capacity"
        ],
        [
          "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE",
          "/campaigns/0/flow/nodes/1/deployment/capacity"
        ]
      ]
    ],
    ["invalid/deployment-states-nothing.json", deploymentStatesNothing, [
      [
        "SOURCE_CAMPAIGN_DEPLOYMENT_EMPTY",
        "/campaigns/0/flow/nodes/0/deployment/id"
      ]
    ]],
    ["invalid/campaign-grant-semantics.json", campaignGrantSemantics, [
      ["SOURCE_REF_MISSING", "/campaigns/0/startingStore/0/itemId"],
      [
        "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
        "/campaigns/0/startingStore/2/itemId"
      ],
      [
        "SOURCE_CAMPAIGN_GRANT_SUBJECT_INVALID",
        "/campaigns/0/startingStore/3/itemId"
      ],
      [
        "SOURCE_CAMPAIGN_GRANT_SUBJECT_INVALID",
        "/campaigns/0/startingStore/4/quantity"
      ],
      [
        "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
        "/campaigns/0/startingStore/6/weaponId"
      ],
      ["SOURCE_REF_MISSING", "/campaigns/0/flow/nodes/0/grants/0/itemId"],
      [
        "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
        "/campaigns/0/flow/nodes/0/grants/2/itemId"
      ]
    ]],
    ["invalid/campaign-roster-semantics.json", campaignRosterSemantics, [
      ["SOURCE_CAMPAIGN_ROSTER_EMPTY", "/campaigns/0/roster"],
      ["SOURCE_REF_MISSING", "/campaigns/1/roster/2/unitTypeId"],
      [
        "SOURCE_CAMPAIGN_MEMBER_ID_DUPLICATE",
        "/campaigns/1/flow/nodes/0/recruits/0/id"
      ],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_MISSING",
        "/campaigns/1/flow/nodes/0/placements/1"
      ],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_UNKNOWN",
        "/campaigns/1/flow/nodes/0/placements/2/memberId"
      ],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_FORBIDDEN",
        "/campaigns/1/flow/nodes/0/placements/3/memberId"
      ],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_DUPLICATE",
        "/campaigns/1/flow/nodes/0/placements/4/memberId"
      ],
      [
        "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_TYPE_MISMATCH",
        "/campaigns/1/flow/nodes/0/placements/5/unitTypeId"
      ]
    ]],
    [
      "invalid/campaign-member-specificity-semantics.json",
      campaignMemberSpecificitySemantics,
      [
        [
          "SOURCE_CAMPAIGN_SPECIFICITY_EMPTY",
          "/campaigns/0/roster/0/specificity"
        ],
        [
          "SOURCE_CAMPAIGN_STAT_DELTA_ZERO",
          "/campaigns/0/roster/1/specificity/stats/luck"
        ],
        [
          "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
          "/campaigns/0/roster/2/specificity/stats/health"
        ],
        [
          "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
          "/campaigns/0/roster/3/specificity/stats/movement"
        ],
        [
          "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
          "/campaigns/0/roster/4/specificity/stats/movement"
        ],
        ["SOURCE_REF_MISSING", "/campaigns/0/roster/5/unitTypeId"],
        [
          "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
          "/campaigns/0/flow/nodes/0/recruits/0/specificity/stats/speed"
        ]
      ]
    ]
  ])("%s has the same acceptance and diagnostic identities", (
    sourcePath,
    text,
    expected
  ) => {
    const diagnostics = analyzeSourceProject(sourcePath, text).diagnostics;
    expect(diagnostics.map(({ code, instancePath }) => [code, instancePath]))
      .toEqual(expected);
  });
});
