// SPDX-License-Identifier: MIT
import type { ErrorObject, ValidateFunction } from "ajv";
import type {
  DefinitionCategory,
  IndexedDefinition,
  IndexDiagnostic
} from "../domain/project-index";
import { ProjectIndex } from "../domain/project-index";
import type { ProjectPath } from "../domain/project-store";
import { CURRENT_SOURCE_VERSION } from "../domain/source-migration";
import generatedSourceProjectValidator from "../generated/source-v1-validator";
import { sourceV1Schemas } from "../generated/source-v1-schemas";
import type {
  CampaignFlow,
  CampaignItemGrant,
  CampaignRosterMember,
  CampaignTransition,
  ScriptBinding,
  SourceProject,
  StatDeltaBlock
} from "../generated/source-v1";

export interface SourceDiagnostic {
  readonly severity: "error";
  readonly code:
    | "SOURCE_JSON_INVALID"
    | "SOURCE_SCHEMA_INVALID"
    | "SOURCE_ID_DUPLICATE"
    | "SOURCE_REF_MISSING"
    | "SOURCE_SCRIPT_SLOT_DUPLICATE"
    | "SOURCE_SCRIPT_PARAMETER_DUPLICATE"
    | "SOURCE_MAP_SHAPE_INVALID"
    | "SOURCE_DROP_INCOMPLETE"
    | "SOURCE_CAMPAIGN_NODE_ID_DUPLICATE"
    | "SOURCE_CAMPAIGN_ENTRY_MISSING"
    | "SOURCE_CAMPAIGN_TRANSITION_ID_DUPLICATE"
    | "SOURCE_CAMPAIGN_TRANSITION_TARGET_MISSING"
    | "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE"
    | "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE"
    | "SOURCE_CAMPAIGN_NODE_UNREACHABLE"
    | "SOURCE_CAMPAIGN_PLACEMENT_ID_DUPLICATE"
    | "SOURCE_CAMPAIGN_PLACEMENT_TILE_OCCUPIED"
    | "SOURCE_CAMPAIGN_PLACEMENT_OUT_OF_BOUNDS"
    | "SOURCE_CAMPAIGN_ROSTER_EMPTY"
    | "SOURCE_CAMPAIGN_MEMBER_ID_DUPLICATE"
    | "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_MISSING"
    | "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_UNKNOWN"
    | "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_FORBIDDEN"
    | "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_DUPLICATE"
    | "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_TYPE_MISMATCH"
    | "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE"
    | "SOURCE_CAMPAIGN_DEPLOYMENT_TILE_DUPLICATE"
    | "SOURCE_CAMPAIGN_DEPLOYMENT_OUT_OF_BOUNDS"
    | "SOURCE_CAMPAIGN_DEPLOYMENT_UNOCCUPIED"
    | "SOURCE_CAMPAIGN_DEPLOYMENT_EMPTY"
    | "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE"
    | "SOURCE_CAMPAIGN_SPECIFICITY_EMPTY"
    | "SOURCE_CAMPAIGN_STAT_DELTA_ZERO"
    | "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE"
    | "SOURCE_CAMPAIGN_ARRIVAL_MEMBER"
    | "SOURCE_CAMPAIGN_OBJECTIVE_RESULT_UNKNOWN"
    | "SOURCE_CAMPAIGN_STAGE_UNDECIDED"
    | "SOURCE_OBJECTIVE_ROUNDS_MISMATCH"
    | "SOURCE_DIALOGUE_SPEAKER_CAST_TWICE"
    | "SOURCE_DIALOGUE_CAST_SPEAKS_NO_LINE";
  readonly sourcePath: ProjectPath;
  readonly instancePath: string;
  readonly message: string;
}

export interface SourceAnalysis {
  readonly definitions: readonly IndexedDefinition[];
  readonly diagnostics: readonly SourceDiagnostic[];
  readonly indexDiagnostics: readonly IndexDiagnostic[];
}

const validate =
  generatedSourceProjectValidator as ValidateFunction<SourceProject>;

const stableIdPattern = "^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$";

type DeltaStat = keyof StatDeltaBlock;

interface StatDeltaBound {
  readonly minimum: number;
  readonly maximum: number;
  readonly whenOmitted: number;
}

// As much of a source schema document as reading one field's bounds needs.
interface SchemaField {
  readonly $ref?: string;
  readonly minimum?: number;
  readonly maximum?: number;
}

interface SchemaDocument {
  readonly $id?: string;
  readonly $defs?: Record<
    string,
    SchemaField & { readonly properties?: Record<string, SchemaField> }
  >;
}

// What a stat line that never mentioned a stat holds of it, and the one floor
// the stat block's own is not the truth about.
//
// `whenOmitted` is the block's omission rule: the four stats the block requires
// never reach theirs, and carry their own floor for want of anything truer to
// carry. Movement's floor is one rather than the schema's nought, and a reader
// comparing the two will notice: the content compiler refuses a *class* whose
// movement is nought outright, so nought is not a thing an author may make a
// class.
const statDeltaFloors: Readonly<
  Record<DeltaStat, { readonly minimum?: number; readonly whenOmitted: number }>
> = {
  health: { whenOmitted: 1 },
  movement: { minimum: 1, whenOmitted: 1 },
  strength: { whenOmitted: 0 },
  defense: { whenOmitted: 0 },
  resistance: { whenOmitted: 0 },
  skill: { whenOmitted: 0 },
  luck: { whenOmitted: 0 },
  evasion: { whenOmitted: 0 },
  magic: { whenOmitted: 0 },
  actionPoints: { whenOmitted: 1 },
  speed: { whenOmitted: 1 }
};

// Where an authored delta may land, per stat. The bounds are the ones that
// stat's own statBlock field admits and nothing else: an author may make a
// character anything they could have made a class, and nothing they could not.
// That is one rule rather than eleven, and it is read out of the schema rather
// than written out again here, so the three analyzers that hold this rule
// cannot come to disagree about it. `tools/source_schema/validate.mjs` reads
// the same block the same way, and `tools/source_schema/test.mjs` checks the
// block itself against the engine constant the rules refuse a unit for
// passing.
export const statDeltaBounds: Readonly<Record<DeltaStat, StatDeltaBound>> =
  (() => {
    const common = (sourceV1Schemas as readonly SchemaDocument[]).find(
      (schema) => schema.$id?.endsWith("/common.schema.json")
    );
    const block = common?.$defs?.statBlock?.properties ?? {};
    const entries = Object.entries(statDeltaFloors).map(([stat, floor]) => {
      const field = block[stat] ?? {};
      // A field may state its bounds itself or share them through a `$ref`
      // into this document's `$defs`, which is how the four stats that stand
      // in the damage arithmetic say they stop where the rules stop.
      const shared = field.$ref === undefined
        ? undefined
        : common?.$defs?.[field.$ref.slice(field.$ref.lastIndexOf("/") + 1)];
      return [stat, {
        minimum: floor.minimum ?? field.minimum ?? shared?.minimum ?? 0,
        maximum: field.maximum ?? shared?.maximum ?? 0,
        whenOmitted: floor.whenOmitted
      }];
    });
    return Object.fromEntries(entries) as Record<DeltaStat, StatDeltaBound>;
  })();

const typeHints: Readonly<Record<string, string>> = {
  integer: "a whole number",
  number: "a number",
  boolean: "true or false",
  string: "text",
  array: "a list",
  object: "an object"
};

/**
 * Restates an ajv failure as what to do rather than what failed. The raw
 * message is kept as a fallback for keywords with no better phrasing.
 */
function friendlySchemaMessage(error: ErrorObject): string {
  const params = error.params as Record<string, unknown>;
  switch (error.keyword) {
    case "required":
      return `missing required field '${String(params.missingProperty)}': ` +
        "fill it in and save again";
    case "additionalProperties":
      return `unknown field '${String(params.additionalProperty)}': ` +
        "remove it";
    case "type":
      return `this value must be ${
        typeHints[String(params.type)] ?? String(params.type)
      }`;
    case "enum":
      return `choose one of: ${
        (params.allowedValues as unknown[]).map(String).join(", ")
      }`;
    case "const":
      return `this value must be exactly '${String(params.allowedValue)}'`;
    case "pattern":
      return params.pattern === stableIdPattern
        ? "identifiers use lowercase letters, digits, and separators " +
          "(. _ -) and start with a letter, like 'iron_knight'"
        : `this value must match the pattern ${String(params.pattern)}`;
    case "minimum":
    case "maximum":
      return `this number must be ${
        error.keyword === "minimum" ? "at least" : "at most"
      } ${String(params.limit)}`;
    case "minLength":
      return params.limit === 1
        ? "this text must not be empty"
        : `this text needs at least ${String(params.limit)} characters`;
    case "maxLength":
      return `this text is too long: at most ${String(params.limit)} ` +
        "characters fit";
    case "uniqueItems":
      return "list entries must not repeat";
    default:
      return error.message ?? "schema validation failed";
  }
}

function schemaDiagnostic(
  sourcePath: ProjectPath,
  error: ErrorObject
): SourceDiagnostic {
  return {
    severity: "error",
    code: "SOURCE_SCHEMA_INVALID",
    sourcePath,
    instancePath: error.instancePath || "/",
    message: friendlySchemaMessage(error)
  };
}

function definition(
  sourcePath: ProjectPath,
  category: IndexedDefinition["category"],
  sourceKey: string,
  displayName: string,
  semanticPath: string,
  references: IndexedDefinition["references"] = []
): IndexedDefinition {
  return {
    category,
    sourceKey,
    displayName,
    sourcePath,
    semanticPath,
    // Which source version this definition was read at. Asked rather than
    // written out: the analyzer only ever sees a project at the version this
    // build writes, because anything older is brought up before it gets here
    // and anything newer never opens.
    schemaVersion: CURRENT_SOURCE_VERSION,
    references
  };
}

interface ScriptBindingSource {
  readonly scriptBindings?: readonly ScriptBinding[];
}

type CampaignCondition = NonNullable<CampaignTransition["when"]>;

// The two outcomes an objective can have. The schema types a condition's
// `result` as a free identifier because it was written before the runtime had
// a vocabulary to name; it has one now, satisfied or failed, spelled here as
// the source spells them, and a third word is a transition no build can
// evaluate. One list, held here, in `tools/source_schema/validate.mjs` and in
// the native reader, and the conformance suite pins the three level.
const objectiveResults: readonly string[] = ["victory", "defeat"];

// One walk of a condition tree, in authored order, reporting everything a
// condition can be wrong about. Both callers need the same order, because the
// analyzers must agree diagnostic for diagnostic, so there is one traversal
// and not two: a caller that wants only the references passes nothing for the
// other hand.
function walkCondition(
  condition: CampaignCondition,
  semanticPath: string,
  onReference: (
    reference: IndexedDefinition["references"][number]
  ) => void,
  onUnknownResult: (instancePath: string, result: string) => void = () => {}
): void {
  switch (condition.kind) {
    case "all":
    case "any":
      (condition.conditions ?? []).forEach((child, index) =>
        walkCondition(
          child,
          `${semanticPath}/conditions/${index}`,
          onReference,
          onUnknownResult
        )
      );
      return;
    case "not":
      walkCondition(
        condition.condition,
        `${semanticPath}/condition`,
        onReference,
        onUnknownResult
      );
      return;
    case "objectiveResult":
      onReference({
        category: "objective",
        sourceKey: condition.objectiveId,
        semanticPath: `${semanticPath}/objectiveId`
      });
      if (!objectiveResults.includes(condition.result)) {
        onUnknownResult(`${semanticPath}/result`, condition.result);
      }
      return;
    case "inventoryAtLeast":
      onReference({
        category: "item",
        sourceKey: condition.itemId,
        semanticPath: `${semanticPath}/itemId`
      });
      return;
    case "worldFlagEquals":
      return;
  }
}

function conditionReferences(
  condition: CampaignCondition,
  semanticPath: string
): IndexedDefinition["references"] {
  const references: IndexedDefinition["references"][number][] = [];
  walkCondition(condition, semanticPath, (reference) => {
    references.push(reference);
  });
  return references;
}

function campaignFlowReferences(
  flow: CampaignFlow | undefined,
  semanticPath: string
): IndexedDefinition["references"] {
  if (flow === undefined) return [];
  return flow.nodes.flatMap((node, nodeIndex) => {
    const nodePath = `${semanticPath}/nodes/${nodeIndex}`;
    return [
      ...(node.mapId === undefined ? [] : [{
        category: "map" as const,
        sourceKey: node.mapId,
        semanticPath: `${nodePath}/mapId`
      }]),
      ...(node.placements ?? []).map((placement, index) => ({
        category: "unit_type" as const,
        sourceKey: placement.unitTypeId,
        semanticPath: `${nodePath}/placements/${index}/unitTypeId`
      })),
      // A recruit is a member of the company like any other, so what they are
      // is a reference like any other: deleting the unit type a recruit is
      // made of must be refused where deleting a founding member's is.
      ...(node.recruits ?? []).map((member, index) => ({
        category: "unit_type" as const,
        sourceKey: member.unitTypeId,
        semanticPath: `${nodePath}/recruits/${index}/unitTypeId`
      })),
      // What a node hands the company names an item the same way a member
      // names a unit type: deleting that item must be refused here, and
      // renaming it must rewrite the grant.
      ...(node.grants ?? []).map((grant, index) => ({
        category: "item" as const,
        sourceKey: grant.itemId,
        semanticPath: `${nodePath}/grants/${index}/itemId`
      })),
      ...(node.objectiveIds ?? []).map((sourceKey, index) => ({
        category: "objective" as const,
        sourceKey,
        semanticPath: `${nodePath}/objectiveIds/${index}`
      })),
      ...(node.dialogueIds ?? []).map((sourceKey, index) => ({
        category: "dialogue" as const,
        sourceKey,
        semanticPath: `${nodePath}/dialogueIds/${index}`
      })),
      ...node.transitions.flatMap((transition, transitionIndex) =>
        transition.when === undefined ? [] : conditionReferences(
          transition.when,
          `${nodePath}/transitions/${transitionIndex}/when`
        )
      )
    ];
  });
}

function scriptReferences(
  item: ScriptBindingSource,
  semanticPath: string
): IndexedDefinition["references"] {
  return (item.scriptBindings ?? []).flatMap((binding, bindingIndex) =>
    binding.parameters.flatMap((parameter, parameterIndex) =>
      parameter.value.kind === "contentRef"
        ? [{
            category: parameter.value.category,
            sourceKey: parameter.value.sourceKey,
            semanticPath:
              `${semanticPath}/scriptBindings/${bindingIndex}/parameters/${parameterIndex}/value/sourceKey`
          }]
        : []
    )
  );
}

export function analyzeSourceProject(
  sourcePath: ProjectPath,
  text: string
): SourceAnalysis {
  let candidate: unknown;
  try {
    candidate = JSON.parse(text);
  } catch (error) {
    return {
      definitions: [],
      diagnostics: [{
        severity: "error",
        code: "SOURCE_JSON_INVALID",
        sourcePath,
        instancePath: "/",
        message: error instanceof Error ? error.message : String(error)
      }],
      indexDiagnostics: []
    };
  }

  if (!validate(candidate)) {
    return {
      definitions: [],
      diagnostics: (validate.errors ?? []).map((error) =>
        schemaDiagnostic(sourcePath, error)
      ),
      indexDiagnostics: []
    };
  }

  const project = candidate;
  const definitions: IndexedDefinition[] = [];
  (project.weaponTypes ?? []).forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "weapon_type",
      item.id,
      item.name,
      `/weaponTypes/${index}`
    )
  ));
  (project.itemTypes ?? []).forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "item_type",
      item.id,
      item.name,
      `/itemTypes/${index}`
    )
  ));
  (project.factions ?? []).forEach((item, index) => definitions.push(
    definition(sourcePath, "faction", item.id, item.name, `/factions/${index}`)
  ));
  (project.abilities ?? []).forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "ability",
      item.id,
      item.name,
      `/abilities/${index}`,
      scriptReferences(item, `/abilities/${index}`)
    )
  ));
  (project.objectives ?? []).forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "objective",
      item.id,
      item.name,
      `/objectives/${index}`,
      scriptReferences(item, `/objectives/${index}`)
    )
  ));
  (project.dialogues ?? []).forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "dialogue",
      item.id,
      item.name,
      `/dialogues/${index}`,
      [
        // Who the scene casts, as outbound references, so that renaming a unit
        // type reaches the scenes its character speaks in rather than leaving
        // a cast pointing at a name nothing answers to any more.
        ...(item.cast ?? []).map((entry, entryIndex) => ({
          category: "unit_type" as const,
          sourceKey: entry.unitTypeId,
          semanticPath: `/dialogues/${index}/cast/${entryIndex}/unitTypeId`
        })),
        ...scriptReferences(item, `/dialogues/${index}`)
      ]
    )
  ));
  (project.campaigns ?? []).forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "campaign",
      item.id,
      item.name,
      `/campaigns/${index}`,
      [
        ...scriptReferences(item, `/campaigns/${index}`),
        ...(item.roster ?? []).map((member, memberIndex) => ({
          category: "unit_type" as const,
          sourceKey: member.unitTypeId,
          semanticPath: `/campaigns/${index}/roster/${memberIndex}/unitTypeId`
        })),
        ...(item.startingStore ?? []).map((grant, grantIndex) => ({
          category: "item" as const,
          sourceKey: grant.itemId,
          semanticPath: `/campaigns/${index}/startingStore/${grantIndex}/itemId`
        })),
        ...campaignFlowReferences(
          item.flow,
          `/campaigns/${index}/flow`
        ),
        ...(item.objectiveIds ?? []).map((sourceKey, objectiveIndex) => ({
          category: "objective" as const,
          sourceKey,
          semanticPath: `/campaigns/${index}/objectiveIds/${objectiveIndex}`
        })),
        ...(item.dialogueIds ?? []).map((sourceKey, dialogueIndex) => ({
          category: "dialogue" as const,
          sourceKey,
          semanticPath: `/campaigns/${index}/dialogueIds/${dialogueIndex}`
        }))
      ]
    )
  ));
  project.classes.forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "class",
      item.id,
      item.name,
      `/classes/${index}`,
      (item.allowedWeaponTypeIds ?? []).map((sourceKey, typeIndex) => ({
        category: "weapon_type",
        sourceKey,
        semanticPath: `/classes/${index}/allowedWeaponTypeIds/${typeIndex}`
      }))
    )
  ));
  project.weapons.forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "weapon",
      item.id,
      item.name,
      `/weapons/${index}`,
      item.weaponTypeId === undefined ? [] : [{
        category: "weapon_type",
        sourceKey: item.weaponTypeId,
        semanticPath: `/weapons/${index}/weaponTypeId`
      }]
    )
  ));
  project.items.forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "item",
      item.id,
      item.name,
      `/items/${index}`,
      item.itemTypeId === undefined ? [] : [{
        category: "item_type",
        sourceKey: item.itemTypeId,
        semanticPath: `/items/${index}/itemTypeId`
      }]
    )
  ));
  project.maps.forEach((item, index) => definitions.push(
    definition(sourcePath, "map", item.id, item.name, `/maps/${index}`)
  ));
  project.unitTypes.forEach((item, index) => definitions.push(
    definition(
      sourcePath,
      "unit_type",
      item.id,
      item.name,
      `/unitTypes/${index}`,
      [
        {
          category: "class",
          sourceKey: item.classId,
          semanticPath: `/unitTypes/${index}/classId`
        },
        ...(item.factionId === undefined ? [] : [{
          category: "faction" as const,
          sourceKey: item.factionId,
          semanticPath: `/unitTypes/${index}/factionId`
        }]),
        ...(item.abilityIds ?? []).map((sourceKey, abilityIndex) => ({
          category: "ability" as const,
          sourceKey,
          semanticPath: `/unitTypes/${index}/abilityIds/${abilityIndex}`
        })),
        ...(item.startingWeaponIds ?? []).map((sourceKey, weaponIndex) => ({
          category: "weapon" as const,
          sourceKey,
          semanticPath: `/unitTypes/${index}/startingWeaponIds/${weaponIndex}`
        })),
        ...(item.startingItemIds ?? []).map((sourceKey, itemIndex) => ({
          category: "item" as const,
          sourceKey,
          semanticPath: `/unitTypes/${index}/startingItemIds/${itemIndex}`
        })),
        ...(item.dropItemId === undefined ? [] : [{
          category: "item" as const,
          sourceKey: item.dropItemId,
          semanticPath: `/unitTypes/${index}/dropItemId`
        }])
      ]
    )
  ));

  const index = new ProjectIndex();
  index.updateDocument(sourcePath, definitions);
  const diagnostics: SourceDiagnostic[] = [];
  const identities = new Set<string>();
  const collections = [
    ["classes", "class", project.classes],
    ["weaponTypes", "weapon_type", project.weaponTypes ?? []],
    ["itemTypes", "item_type", project.itemTypes ?? []],
    ["unitTypes", "unit_type", project.unitTypes],
    ["weapons", "weapon", project.weapons],
    ["items", "item", project.items],
    ["maps", "map", project.maps],
    ["factions", "faction", project.factions ?? []],
    ["abilities", "ability", project.abilities ?? []],
    ["objectives", "objective", project.objectives ?? []],
    ["campaigns", "campaign", project.campaigns ?? []],
    ["dialogues", "dialogue", project.dialogues ?? []]
  ] as const;
  for (const [property, category, items] of collections) {
    items.forEach((item, itemIndex) => {
      const identity = `${category}:${item.id}`;
      if (identities.has(identity)) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_ID_DUPLICATE",
          sourcePath,
          instancePath: `/${property}/${itemIndex}/id`,
          message: `duplicate ${category} identity '${item.id}'`
        });
      } else {
        identities.add(identity);
      }
    });
  }
  const requireReference = (
    category: DefinitionCategory,
    sourceKey: string,
    instancePath: string
  ) => {
    if (!identities.has(`${category}:${sourceKey}`)) {
      diagnostics.push({
        severity: "error",
        code: "SOURCE_REF_MISSING",
        sourcePath,
        instancePath,
        message: `missing ${category} reference '${sourceKey}'`
      });
    }
  };
  // What a company is handed, in one list: the store it is founded with, or
  // what one node puts in that store. Each identity is named once, two entries
  // for one item being an author saying the same thing twice with two different
  // answers about how many, and each must name an item the project defines,
  // exactly as a member's unit type must.
  const requireItemGrants = (
    grants: readonly CampaignItemGrant[] | undefined,
    listPath: string,
    owner: string
  ) => {
    const granted = new Set<string>();
    (grants ?? []).forEach((grant, grantIndex) => {
      const instancePath = `${listPath}/${grantIndex}/itemId`;
      if (granted.has(grant.itemId)) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
          sourcePath,
          instancePath,
          message: `${owner} names item '${grant.itemId}' twice`
        });
        return;
      }
      granted.add(grant.itemId);
      requireReference("item", grant.itemId, instancePath);
    });
  };
  // The stat line an authored delta lands on. A unit type keeps no line of its
  // own, being a class with a name and a kit, so the base is the class's,
  // reached through the type the member names. Built once, because a company is
  // read member by member and every one of them asks.
  const unitTypesById = new Map(
    project.unitTypes.map((unitType) => [unitType.id, unitType])
  );
  const classesById = new Map(
    project.classes.map((sourceClass) => [sourceClass.id, sourceClass])
  );
  // What an author made of one character, over and above the class they are.
  // Three ways of saying nothing, all refused: a specificity that states the
  // object and then holds nothing in it, which claims a character is specific
  // without saying how; a delta of zero, because an author who wrote a number
  // meant to change something and omitting the stat is the only way to say
  // nothing; and a delta that lands the stat outside what that stat's own
  // statBlock field admits.
  //
  // Only the bounds need to know who the character is, so only the bounds are
  // skipped when nothing says: an unresolved unit type, or an unresolved class
  // beneath it, is already reported where it is named, and a bad reference must
  // never reach an author as a bad delta. The other two are true of any
  // character, so they are said either way.
  const requireMemberSpecificity = (
    member: CampaignRosterMember,
    memberPath: string
  ) => {
    const specificity = member.specificity;
    if (specificity === undefined) {
      return;
    }
    const stats = specificity.stats ?? {};
    if (
      specificity.rangeBonus === undefined &&
      Object.keys(stats).length === 0
    ) {
      diagnostics.push({
        severity: "error",
        code: "SOURCE_CAMPAIGN_SPECIFICITY_EMPTY",
        sourcePath,
        instancePath: `${memberPath}/specificity`,
        message:
          `member '${member.id}' is stated to be specific and says nothing ` +
          "about how"
      });
    }
    const unitType = unitTypesById.get(member.unitTypeId);
    const baseStats = unitType === undefined
      ? undefined
      : classesById.get(unitType.classId)?.baseStats;
    // Only the eleven the block names can be here: the schema closed the
    // object before any of this ran.
    for (const [stat, delta] of Object.entries(stats) as [
      DeltaStat,
      number
    ][]) {
      const instancePath = `${memberPath}/specificity/stats/${stat}`;
      if (delta === 0) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_CAMPAIGN_STAT_DELTA_ZERO",
          sourcePath,
          instancePath,
          message:
            `member '${member.id}' adjusts ${stat} by zero, which says ` +
            "nothing a stat left out does not say"
        });
        continue;
      }
      if (baseStats === undefined) {
        continue;
      }
      const bounds = statDeltaBounds[stat];
      const base = baseStats[stat] ?? bounds.whenOmitted;
      const landed = base + delta;
      if (landed < bounds.minimum || landed > bounds.maximum) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
          sourcePath,
          instancePath,
          message:
            `member '${member.id}' takes ${stat} from ${base} by ` +
            `${delta > 0 ? "+" : ""}${delta} to ${landed}, outside the ` +
            `${bounds.minimum} to ${bounds.maximum} that ${stat} admits`
        });
      }
    }
  };
  project.unitTypes.forEach((unitType, unitIndex) => {
    requireReference("class", unitType.classId, `/unitTypes/${unitIndex}/classId`);
    if (unitType.factionId !== undefined) {
      requireReference(
        "faction",
        unitType.factionId,
        `/unitTypes/${unitIndex}/factionId`
      );
    }
    (unitType.abilityIds ?? []).forEach((abilityId, abilityIndex) =>
      requireReference(
        "ability",
        abilityId,
        `/unitTypes/${unitIndex}/abilityIds/${abilityIndex}`
      )
    );
    (unitType.startingWeaponIds ?? []).forEach((weaponId, weaponIndex) =>
      requireReference(
        "weapon",
        weaponId,
        `/unitTypes/${unitIndex}/startingWeaponIds/${weaponIndex}`
      )
    );
    (unitType.startingItemIds ?? []).forEach((itemId, itemIndex) =>
      requireReference(
        "item",
        itemId,
        `/unitTypes/${unitIndex}/startingItemIds/${itemIndex}`
      )
    );
    // A drop is authored as a pair or not at all, in both directions. Neither
    // half means anything alone, so the analyzer names the half that is there
    // rather than leaving an author to wonder why the picket never drops.
    if (unitType.dropItemId !== undefined) {
      requireReference(
        "item",
        unitType.dropItemId,
        `/unitTypes/${unitIndex}/dropItemId`
      );
      if (unitType.dropChance === undefined) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_DROP_INCOMPLETE",
          sourcePath,
          instancePath: `/unitTypes/${unitIndex}/dropItemId`,
          message:
            `unit type '${unitType.id}' drops '${unitType.dropItemId}' with ` +
            "no dropChance"
        });
      }
    } else if (unitType.dropChance !== undefined) {
      diagnostics.push({
        severity: "error",
        code: "SOURCE_DROP_INCOMPLETE",
        sourcePath,
        instancePath: `/unitTypes/${unitIndex}/dropChance`,
        message:
          `unit type '${unitType.id}' has a dropChance and no dropItemId`
      });
    }
  });
  project.classes.forEach((sourceClass, classIndex) => {
    (sourceClass.allowedWeaponTypeIds ?? []).forEach((typeId, typeIndex) =>
      requireReference(
        "weapon_type",
        typeId,
        `/classes/${classIndex}/allowedWeaponTypeIds/${typeIndex}`
      )
    );
  });
  project.weapons.forEach((weapon, weaponIndex) => {
    if (weapon.weaponTypeId !== undefined) {
      requireReference(
        "weapon_type",
        weapon.weaponTypeId,
        `/weapons/${weaponIndex}/weaponTypeId`
      );
    }
  });
  project.items.forEach((item, itemIndex) => {
    if (item.itemTypeId !== undefined) {
      requireReference(
        "item_type",
        item.itemTypeId,
        `/items/${itemIndex}/itemTypeId`
      );
    }
  });
  // Who a scene says its speakers are. Three things the schema cannot say:
  // that the unit type cast is one this project declares, that one speaker is
  // not answered twice, and that a cast entry actually speaks. The last is the
  // one an author meets most: it is what renaming a speaker leaves behind,
  // and its symptom on screen is the old drawing rather than a complaint.
  (project.dialogues ?? []).forEach((dialogue, dialogueIndex) => {
    const cast = dialogue.cast ?? [];
    const spoken = new Set((dialogue.lines ?? []).map((line) => line.speaker));
    const seen = new Set<string>();
    cast.forEach((entry, entryIndex) => {
      const entryPath = `/dialogues/${dialogueIndex}/cast/${entryIndex}`;
      requireReference("unit_type", entry.unitTypeId, `${entryPath}/unitTypeId`);
      if (seen.has(entry.speaker)) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_DIALOGUE_SPEAKER_CAST_TWICE",
          sourcePath,
          instancePath: `${entryPath}/speaker`,
          message: `'${entry.speaker}' is cast twice in this scene`
        });
      }
      seen.add(entry.speaker);
      if (!spoken.has(entry.speaker)) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_DIALOGUE_CAST_SPEAKS_NO_LINE",
          sourcePath,
          instancePath: `${entryPath}/speaker`,
          message: `'${entry.speaker}' speaks no line in this scene`
        });
      }
    });
  });
  (project.campaigns ?? []).forEach((campaign, campaignIndex) => {
    (campaign.objectiveIds ?? []).forEach((objectiveId, objectiveIndex) =>
      requireReference(
        "objective",
        objectiveId,
        `/campaigns/${campaignIndex}/objectiveIds/${objectiveIndex}`
      )
    );
    (campaign.dialogueIds ?? []).forEach((dialogueId, dialogueIndex) =>
      requireReference(
        "dialogue",
        dialogueId,
        `/campaigns/${campaignIndex}/dialogueIds/${dialogueIndex}`
      )
    );

    const flow = campaign.flow;
    const flowPath = `/campaigns/${campaignIndex}/flow`;

    // Who this campaign can ever hold, and what each of them is. The founding
    // members and every node's recruits share one namespace, because a
    // placement names a member without saying where that member joined, and
    // the whole company has to be known before any board is read.
    const memberUnitTypes = new Map<string, string>();
    const admitMember = (
      member: CampaignRosterMember,
      memberPath: string
    ) => {
      if (memberUnitTypes.has(member.id)) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_CAMPAIGN_MEMBER_ID_DUPLICATE",
          sourcePath,
          instancePath: `${memberPath}/id`,
          message:
            `duplicate campaign member identity '${member.id}' in campaign '${campaign.id}'`
        });
        return;
      }
      memberUnitTypes.set(member.id, member.unitTypeId);
      requireReference(
        "unit_type",
        member.unitTypeId,
        `${memberPath}/unitTypeId`
      );
      requireMemberSpecificity(member, memberPath);
    };
    (campaign.roster ?? []).forEach((member, memberIndex) =>
      admitMember(member, `/campaigns/${campaignIndex}/roster/${memberIndex}`)
    );
    // A campaign nobody can be founded from: no roster at all, or every member
    // it holds joining at some later node. Both leave a company somebody would
    // have to invent at play time, and nothing invents one.
    if ((campaign.roster ?? []).length === 0) {
      diagnostics.push({
        severity: "error",
        code: "SOURCE_CAMPAIGN_ROSTER_EMPTY",
        sourcePath,
        instancePath: `/campaigns/${campaignIndex}/roster`,
        message:
          `campaign '${campaign.id}' has no founding member to be played by`
      });
    }
    (flow?.nodes ?? []).forEach((node, nodeIndex) =>
      (node.recruits ?? []).forEach((member, memberIndex) =>
        admitMember(
          member,
          `${flowPath}/nodes/${nodeIndex}/recruits/${memberIndex}`
        )
      )
    );
    requireItemGrants(
      campaign.startingStore,
      `/campaigns/${campaignIndex}/startingStore`,
      `the founding store of campaign '${campaign.id}'`
    );

    if (flow === undefined) return;
    const nodeIds = new Set<string>();
    flow.nodes.forEach((node, nodeIndex) => {
      if (nodeIds.has(node.id)) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_CAMPAIGN_NODE_ID_DUPLICATE",
          sourcePath,
          instancePath: `${flowPath}/nodes/${nodeIndex}/id`,
          message: `duplicate campaign node identity '${node.id}'`
        });
      } else {
        nodeIds.add(node.id);
      }
    });
    if (!nodeIds.has(flow.entryNodeId)) {
      diagnostics.push({
        severity: "error",
        code: "SOURCE_CAMPAIGN_ENTRY_MISSING",
        sourcePath,
        instancePath: `${flowPath}/entryNodeId`,
        message: `missing campaign entry node '${flow.entryNodeId}'`
      });
    }

    flow.nodes.forEach((node, nodeIndex) => {
      const nodePath = `${flowPath}/nodes/${nodeIndex}`;
      const map = node.mapId === undefined
        ? undefined
        : project.maps.find((candidate) => candidate.id === node.mapId);
      if (node.mapId !== undefined) {
        requireReference("map", node.mapId, `${nodePath}/mapId`);
      }
      const placementIds = new Set<string>();
      const occupiedTiles = new Map<string, string>();
      // One board, one appearance each: a member standing on two tiles of the
      // same encounter would be two units claiming one character's history.
      const fielded = new Set<string>();
      (node.placements ?? []).forEach((placement, placementIndex) => {
        const placementPath = `${nodePath}/placements/${placementIndex}`;
        if (placementIds.has(placement.id)) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_PLACEMENT_ID_DUPLICATE",
            sourcePath,
            instancePath: `${placementPath}/id`,
            message:
              `duplicate placement identity '${placement.id}' in node '${node.id}'`
          });
        } else {
          placementIds.add(placement.id);
        }
        requireReference(
          "unit_type",
          placement.unitTypeId,
          `${placementPath}/unitTypeId`
        );
        // Who stands here, in both directions. The first side is the company,
        // so every tile of it is one named member the campaign holds; the
        // second side is whoever the campaign is fighting, and naming a member
        // there would field one of the player's own characters as an enemy.
        if (placement.side === "second") {
          if (placement.memberId !== undefined) {
            diagnostics.push({
              severity: "error",
              code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_FORBIDDEN",
              sourcePath,
              instancePath: `${placementPath}/memberId`,
              message:
                `placement '${placement.id}' stands on the second side and ` +
                `names member '${placement.memberId}'`
            });
          }
        } else if (placement.memberId === undefined) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_MISSING",
            sourcePath,
            instancePath: placementPath,
            message:
              `placement '${placement.id}' stands on the first side and names ` +
              "no member"
          });
        } else {
          const memberUnitTypeId = memberUnitTypes.get(placement.memberId);
          if (memberUnitTypeId === undefined) {
            diagnostics.push({
              severity: "error",
              code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_UNKNOWN",
              sourcePath,
              instancePath: `${placementPath}/memberId`,
              message:
                `placement '${placement.id}' names member ` +
                `'${placement.memberId}', whom campaign '${campaign.id}' ` +
                "never holds"
            });
          } else {
            if (fielded.has(placement.memberId)) {
              diagnostics.push({
                severity: "error",
                code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_DUPLICATE",
                sourcePath,
                instancePath: `${placementPath}/memberId`,
                message:
                  `member '${placement.memberId}' already stands on node ` +
                  `'${node.id}'`
              });
            } else {
              fielded.add(placement.memberId);
            }
            if (memberUnitTypeId !== placement.unitTypeId) {
              diagnostics.push({
                severity: "error",
                code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_TYPE_MISMATCH",
                sourcePath,
                instancePath: `${placementPath}/unitTypeId`,
                message:
                  `placement '${placement.id}' fields member ` +
                  `'${placement.memberId}' as '${placement.unitTypeId}', ` +
                  `who is '${memberUnitTypeId}'`
              });
            }
          }
        }
        // A placement that arrives is a reinforcement: it is not on the board
        // when the battle opens, so it shares no tile with the opening
        // arrangement and its own tile is a request rather than a claim. Where
        // that tile is held when its round comes, the engine stands it on the
        // nearest one it could stand on instead.
        const arrives = placement.arrival !== undefined;
        if (arrives && placement.memberId !== undefined) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_ARRIVAL_MEMBER",
            sourcePath,
            instancePath: `${placementPath}/arrival`,
            message:
              `placement '${placement.id}' fields member ` +
              `'${placement.memberId}' and cannot also arrive`
          });
        }
        const tileKey = arrives ? null : `${placement.x},${placement.y}`;
        const occupant =
          tileKey === null ? undefined : occupiedTiles.get(tileKey);
        if (occupant !== undefined) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_PLACEMENT_TILE_OCCUPIED",
            sourcePath,
            instancePath: `${placementPath}/x`,
            message:
              `placement '${placement.id}' overlaps '${occupant}' at (${tileKey})`
          });
        } else if (tileKey !== null) {
          occupiedTiles.set(tileKey, placement.id);
        }
        if (map && (
          placement.x >= map.width ||
          placement.y >= map.height
        )) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_PLACEMENT_OUT_OF_BOUNDS",
            sourcePath,
            instancePath: placement.x >= map.width
              ? `${placementPath}/x`
              : `${placementPath}/y`,
            message:
              `placement '${placement.id}' is outside map '${map.id}' (${map.width}x${map.height})`
          });
        }
      });
      // What the encounter says about the player's own troops: the region they
      // are arranged in, how many of them may take the field, or both. Who
      // falls out of the placements above, which is why this runs after them: a
      // region no first-side placement stands inside is a region nobody can be
      // arranged in, and a cap no company can reach is a cap that never refuses
      // anything.
      //
      // What terrain a region tile asks of whoever stands on it is checked by
      // the content compiler and not here, exactly as a placement's own terrain
      // is: this analyzer does not resolve a unit type through its class to its
      // crossings, and inventing half of that rule would be worse than leaving
      // the whole of it in one place.
      const zone = node.deployment;
      if (zone !== undefined) {
        const zonePath = `${nodePath}/deployment`;
        const zoneTiles = new Set<string>();
        let arrangeable = false;
        (zone.tiles ?? []).forEach((tile, tileIndex) => {
          const tilePath = `${zonePath}/tiles/${tileIndex}`;
          const tileKey = `${tile.x},${tile.y}`;
          if (zoneTiles.has(tileKey)) {
            diagnostics.push({
              severity: "error",
              code: "SOURCE_CAMPAIGN_DEPLOYMENT_TILE_DUPLICATE",
              sourcePath,
              instancePath: `${tilePath}/x`,
              message:
                `deployment zone '${zone.id}' names (${tileKey}) twice`
            });
          } else {
            zoneTiles.add(tileKey);
          }
          if (map && (tile.x >= map.width || tile.y >= map.height)) {
            diagnostics.push({
              severity: "error",
              code: "SOURCE_CAMPAIGN_DEPLOYMENT_OUT_OF_BOUNDS",
              sourcePath,
              instancePath: tile.x >= map.width
                ? `${tilePath}/x`
                : `${tilePath}/y`,
              message:
                `deployment zone '${zone.id}' reaches outside map ` +
                `'${map.id}' (${map.width}x${map.height})`
            });
          }
        });
        // How many of the company this board could ever field: one per
        // first-side placement, because a member takes the field by standing on
        // one.
        let fieldable = 0;
        (node.placements ?? []).forEach((placement) => {
          if (placement.side !== "first") return;
          fieldable += 1;
          if (zoneTiles.has(`${placement.x},${placement.y}`)) {
            arrangeable = true;
          }
        });
        // A region nobody stands in is only a question where a region was
        // stated: a deployment that states a cap alone arranges nobody
        // anywhere, by design.
        if (zone.tiles !== undefined && !arrangeable) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_DEPLOYMENT_UNOCCUPIED",
            sourcePath,
            instancePath: `${zonePath}/id`,
            message:
              `deployment zone '${zone.id}' holds no first-side placement, ` +
              "so nobody can be arranged in it"
          });
        }
        // A deployment says at least one of the two things a deployment is for.
        // This is where that is refused, at the deployment's own path and in a
        // sentence, beside every other thing that can be wrong with a
        // deployment. A keyword failure on the shape of the object would name
        // neither the deployment nor what it was missing.
        if (zone.tiles === undefined && zone.capacity === undefined) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_DEPLOYMENT_EMPTY",
            sourcePath,
            instancePath: `${zonePath}/id`,
            message:
              `deployment '${zone.id}' states neither a region to arrange in ` +
              "nor a cap on how many may take the field"
          });
        }
        // A cap at or above the number of first-side placements can never
        // refuse anybody, because the board has nobody else to field. An author
        // who wrote one meant a smaller number.
        if (zone.capacity !== undefined && zone.capacity >= fieldable) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE",
            sourcePath,
            instancePath: `${zonePath}/capacity`,
            message:
              `deployment '${zone.id}' caps the field at ${zone.capacity}, ` +
              `which node '${node.id}' never reaches: it authors ` +
              `${fieldable} first-side placement${fieldable === 1 ? "" : "s"}`
          });
        }
      }
      // A Stage nothing decides cannot be opened, which is a harder fact than
      // it sounds. The package format writes the count of a board's objectives
      // before anything else and every client refuses a board declaring none,
      // so a campaign that reaches this node stops there and reports a board it
      // could not decode. The compiler refuses to emit one; this says so before
      // an author has built a ROM and met it on a console.
      if (node.kind === "encounter" &&
          (node.objectiveIds ?? []).length === 0) {
        diagnostics.push({
          severity: "error",
          code: "SOURCE_CAMPAIGN_STAGE_UNDECIDED",
          sourcePath,
          instancePath: `${nodePath}/objectiveIds`,
          message:
            `Stage '${node.id}' states no way to be won or lost, so it ` +
            "cannot be played: a campaign that reaches it stops there"
        });
      }
      (node.objectiveIds ?? []).forEach((objectiveId, objectiveIndex) =>
        requireReference(
          "objective",
          objectiveId,
          `${nodePath}/objectiveIds/${objectiveIndex}`
        )
      );
      (node.dialogueIds ?? []).forEach((dialogueId, dialogueIndex) =>
        requireReference(
          "dialogue",
          dialogueId,
          `${nodePath}/dialogueIds/${dialogueIndex}`
        )
      );
      requireItemGrants(
        node.grants,
        `${nodePath}/grants`,
        `node '${node.id}'`
      );

      const transitionIds = new Set<string>();
      const transitionPath = `${flowPath}/nodes/${nodeIndex}/transitions`;
      const priorities = new Set<number>();
      let fallbackSeen = false;
      node.transitions.forEach((transition, transitionIndex) => {
        const itemPath = `${transitionPath}/${transitionIndex}`;
        if (transitionIds.has(transition.id)) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_TRANSITION_ID_DUPLICATE",
            sourcePath,
            instancePath: `${itemPath}/id`,
            message:
              `duplicate transition identity '${transition.id}' in node '${node.id}'`
          });
        } else {
          transitionIds.add(transition.id);
        }
        if (!nodeIds.has(transition.targetNodeId)) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_TRANSITION_TARGET_MISSING",
            sourcePath,
            instancePath: `${itemPath}/targetNodeId`,
            message: `missing campaign transition target '${transition.targetNodeId}'`
          });
        }
        if (priorities.has(transition.priority)) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
            sourcePath,
            instancePath: `${itemPath}/priority`,
            message:
              `duplicate transition priority '${transition.priority}' in node '${node.id}'`
          });
        } else {
          priorities.add(transition.priority);
        }
        if (transition.when === undefined) {
          if (fallbackSeen) {
            diagnostics.push({
              severity: "error",
              code: "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
              sourcePath,
              instancePath: itemPath,
              message: `node '${node.id}' has more than one unconditional fallback`
            });
          }
          fallbackSeen = true;
        } else {
          walkCondition(
            transition.when,
            `${itemPath}/when`,
            (reference) =>
              requireReference(
                reference.category,
                reference.sourceKey,
                reference.semanticPath
              ),
            (instancePath, result) =>
              diagnostics.push({
                severity: "error",
                code: "SOURCE_CAMPAIGN_OBJECTIVE_RESULT_UNKNOWN",
                sourcePath,
                instancePath,
                message:
                  `objective result '${result}' is not 'victory' or 'defeat'`
              })
          );
        }
      });
    });

    if (nodeIds.has(flow.entryNodeId)) {
      const reachable = new Set<string>();
      const pending = [flow.entryNodeId];
      while (pending.length > 0) {
        const nodeId = pending.pop()!;
        if (reachable.has(nodeId)) continue;
        reachable.add(nodeId);
        const node = flow.nodes.find((candidate) => candidate.id === nodeId);
        node?.transitions.forEach((transition) => {
          if (nodeIds.has(transition.targetNodeId)) {
            pending.push(transition.targetNodeId);
          }
        });
      }
      flow.nodes.forEach((node, nodeIndex) => {
        if (!reachable.has(node.id)) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_CAMPAIGN_NODE_UNREACHABLE",
            sourcePath,
            instancePath: `${flowPath}/nodes/${nodeIndex}/id`,
            message:
              `campaign node '${node.id}' is unreachable from entry '${flow.entryNodeId}'`
          });
        }
      });
    }
  });
  const scriptCollections = [
    ["abilities", project.abilities ?? []],
    ["objectives", project.objectives ?? []],
    ["campaigns", project.campaigns ?? []],
    ["dialogues", project.dialogues ?? []]
  ] as const;
  for (const [property, items] of scriptCollections) {
    items.forEach((item, itemIndex) => {
      const scriptSource = item as ScriptBindingSource;
      const slots = new Set<string>();
      (scriptSource.scriptBindings ?? []).forEach((binding, bindingIndex) => {
        const bindingPath = `/${property}/${itemIndex}/scriptBindings/${bindingIndex}`;
        if (slots.has(binding.slot)) {
          diagnostics.push({
            severity: "error",
            code: "SOURCE_SCRIPT_SLOT_DUPLICATE",
            sourcePath,
            instancePath: `${bindingPath}/slot`,
            message: `duplicate script binding slot '${binding.slot}'`
          });
        } else {
          slots.add(binding.slot);
        }

        const parameterNames = new Set<string>();
        binding.parameters.forEach((parameter, parameterIndex) => {
          if (parameterNames.has(parameter.name)) {
            diagnostics.push({
              severity: "error",
              code: "SOURCE_SCRIPT_PARAMETER_DUPLICATE",
              sourcePath,
              instancePath: `${bindingPath}/parameters/${parameterIndex}/name`,
              message: `duplicate script parameter name '${parameter.name}'`
            });
          } else {
            parameterNames.add(parameter.name);
          }
          if (parameter.value.kind === "contentRef") {
            requireReference(
              parameter.value.category,
              parameter.value.sourceKey,
              `${bindingPath}/parameters/${parameterIndex}/value/sourceKey`
            );
          }
        });
      });
    });
  }
  // The kind and the count are one authored fact. See the command-line
  // analyzer, which says the same thing at the same path.
  (project.objectives ?? []).forEach((objective, objectiveIndex) => {
    const counts = objective.kind === "surviveRounds";
    if (counts === (objective.rounds !== undefined)) return;
    diagnostics.push({
      severity: "error",
      code: "SOURCE_OBJECTIVE_ROUNDS_MISMATCH",
      sourcePath,
      instancePath: counts
        ? `/objectives/${objectiveIndex}/kind`
        : `/objectives/${objectiveIndex}/rounds`,
      message: counts
        ? `objective '${objective.id}' survives rounds and states no count`
        : `objective '${objective.id}' states a round count no kind but ` +
          "surviveRounds reads"
    });
  });
  project.maps.forEach((map, mapIndex) => {
    if (map.terrain.length !== map.width * map.height) {
      diagnostics.push({
        severity: "error",
        code: "SOURCE_MAP_SHAPE_INVALID",
        sourcePath,
        instancePath: `/maps/${mapIndex}/terrain`,
        message:
          `terrain has ${map.terrain.length} cells; expected ${map.width * map.height}`
      });
    }
  });

  return {
    definitions,
    diagnostics,
    indexDiagnostics: index.diagnostics()
  };
}
