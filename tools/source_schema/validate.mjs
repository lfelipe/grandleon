// SPDX-License-Identifier: MIT
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import Ajv2020 from "ajv/dist/2020.js";
import sourceMap from "json-source-map";

const directory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(directory, "../..");
const schemaDirectory = path.join(repositoryRoot, "schemas/source/v1");

const maximumSourceBytes = 32 * 1024 * 1024;

// What a stat line that never mentioned a stat holds of it, and the one floor
// the stat block's own is not the truth about.
//
// `whenOmitted` is the block's omission rule: the four stats the block requires
// never reach theirs, and carry their own floor for want of anything truer to
// carry. Movement's floor is one rather than the schema's nought, and a reader
// comparing the two will notice: the content compiler refuses a *class* whose
// movement is nought outright, so nought is not a thing an author may make a
// class.
const statDeltaFloors = {
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

// One stat block field's own bounds. A field either states them or shares them
// through a `$ref` into the same document's `$defs`, which is how the stats
// that stand in the damage arithmetic say they stop where the rules stop.
function fieldBounds(common, field) {
  const referenced = typeof field.$ref === "string"
    ? common.$defs[field.$ref.slice(field.$ref.lastIndexOf("/") + 1)]
    : {};
  return {
    minimum: field.minimum ?? referenced.minimum,
    maximum: field.maximum ?? referenced.maximum
  };
}

// Where an authored delta may land, per stat. The bounds are the ones that
// stat's own statBlock field admits and nothing else: an author may make a
// character anything they could have made a class, and nothing they could not.
// That is one rule rather than eleven, and it is read out of the schema rather
// than written out again here, so the two cannot come to disagree.
export const statDeltaBounds = (() => {
  const common = readSchema(path.join(schemaDirectory, "common.schema.json"));
  const block = common.$defs.statBlock.properties;
  return Object.fromEntries(
    Object.entries(statDeltaFloors).map(([stat, floor]) => {
      const bounds = fieldBounds(common, block[stat]);
      return [stat, { ...bounds, ...floor }];
    })
  );
})();

function diagnostic(filename, code, instancePath, message, location) {
  return {
    severity: "error",
    code,
    sourcePath: filename,
    instancePath,
    line: location ? location.line + 1 : 1,
    column: location ? location.column + 1 : 1,
    message
  };
}

function readDocument(filename) {
  const stat = fs.statSync(filename);
  if (stat.size > maximumSourceBytes) {
    return {
      diagnostics: [
        diagnostic(
          filename,
          "SOURCE_LIMIT_EXCEEDED",
          "/",
          `document is ${stat.size} bytes; limit is ${maximumSourceBytes} bytes`
        )
      ]
    };
  }

  const text = fs.readFileSync(filename, "utf8");
  try {
    const parsed = sourceMap.parse(text);
    return { data: parsed.data, pointers: parsed.pointers, diagnostics: [] };
  } catch (error) {
    const match = /position\s+(\d+)/i.exec(String(error));
    const position = match ? Number(match[1]) : 0;
    const prefix = text.slice(0, position);
    const lines = prefix.split("\n");
    return {
      diagnostics: [
        diagnostic(
          filename,
          "SOURCE_JSON_INVALID",
          "/",
          String(error),
          { line: lines.length - 1, column: lines.at(-1)?.length ?? 0 }
        )
      ]
    };
  }
}

function readSchema(filename) {
  return JSON.parse(fs.readFileSync(filename, "utf8"));
}

function schemaFiles() {
  return fs.readdirSync(schemaDirectory)
    .filter((name) => name.endsWith(".schema.json"))
    .sort()
    .map((name) => path.join(schemaDirectory, name));
}

export function validateProject(filename) {
  const ajv = new Ajv2020({
    allErrors: true,
    strict: true,
    verbose: false
  });

  for (const schemaFile of schemaFiles()) {
    ajv.addSchema(readSchema(schemaFile));
  }

  const document = readDocument(filename);
  if (!document.data) {
    return document.diagnostics;
  }
  const project = document.data;
  const validate = ajv.getSchema("https://grandleon.dev/schemas/source/v1/project.schema.json");
  if (!validate) {
    throw new Error("project schema was not registered");
  }

  const diagnostics = [];
  if (!validate(project)) {
    for (const error of validate.errors ?? []) {
      const instancePath = error.instancePath || "/";
      const pointer = document.pointers[instancePath];
      diagnostics.push(
        diagnostic(
          filename,
          "SOURCE_SCHEMA_INVALID",
          instancePath,
          error.message ?? "schema validation failed",
          pointer?.value ?? pointer?.key
        )
      );
    }
  } else {
    const definitions = new Map();
    const collections = [
      ["classes", "class"],
      ["weaponTypes", "weapon_type"],
      ["itemTypes", "item_type"],
      ["unitTypes", "unit_type"],
      ["weapons", "weapon"],
      ["items", "item"],
      ["maps", "map"],
      ["factions", "faction"],
      ["abilities", "ability"],
      ["objectives", "objective"],
      ["campaigns", "campaign"],
      ["dialogues", "dialogue"]
    ];

    for (const [property, category] of collections) {
      for (const [index, definition] of (project[property] ?? []).entries()) {
        const key = `${category}:${definition.id}`;
        if (definitions.has(key)) {
          const instancePath = `/${property}/${index}/id`;
          const pointer = document.pointers[instancePath];
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_ID_DUPLICATE",
              instancePath,
              `duplicate ${category} identity '${definition.id}'`,
              pointer?.value
            )
          );
        } else {
          definitions.set(key, true);
        }
      }
    }

    function requireReference(category, id, instancePath) {
      if (!definitions.has(`${category}:${id}`)) {
        const pointer = document.pointers[instancePath];
        diagnostics.push(
          diagnostic(
            filename,
            "SOURCE_REF_MISSING",
            instancePath,
            `missing ${category} reference '${id}'`,
            pointer?.value
          )
        );
      }
    }

    function validateCampaignCondition(condition, instancePath) {
      if (condition.kind === "all" || condition.kind === "any") {
        for (const [index, child] of condition.conditions.entries()) {
          validateCampaignCondition(child, `${instancePath}/conditions/${index}`);
        }
      } else if (condition.kind === "not") {
        validateCampaignCondition(condition.condition, `${instancePath}/condition`);
      } else if (condition.kind === "objectiveResult") {
        requireReference(
          "objective",
          condition.objectiveId,
          `${instancePath}/objectiveId`
        );
        // The two outcomes an objective can have. The schema types `result` as
        // a free identifier because it was written before the runtime had a
        // vocabulary to name; it has one now: `campaign::ObjectiveOutcome` is
        // satisfied or failed, and nothing else. A third word is a transition
        // no build can ever evaluate. Said here so the two analyzers and the
        // compiler agree on one list rather than two.
        if (
          condition.result !== "victory" &&
          condition.result !== "defeat"
        ) {
          const resultPath = `${instancePath}/result`;
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_OBJECTIVE_RESULT_UNKNOWN",
              resultPath,
              `objective result '${condition.result}' is not 'victory' or ` +
                "'defeat'",
              document.pointers[resultPath]?.value
            )
          );
        }
      } else if (condition.kind === "inventoryAtLeast") {
        requireReference("item", condition.itemId, `${instancePath}/itemId`);
      }
    }

    // What a company is handed, in one list: the store it is founded with, or
    // what one node puts in that store. Each identity is named once, and each
    // must name an item the project defines, exactly as a member's unit type
    // must. Two entries for one item are an author saying the same thing twice
    // with two different answers about how many.
    function validateItemGrants(grants, listPath, owner) {
      const granted = new Set();
      for (const [index, grant] of (grants ?? []).entries()) {
        const instancePath = `${listPath}/${index}/itemId`;
        if (granted.has(grant.itemId)) {
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE",
              instancePath,
              `${owner} names item '${grant.itemId}' twice`,
              document.pointers[instancePath]?.value
            )
          );
          continue;
        }
        granted.add(grant.itemId);
        requireReference("item", grant.itemId, instancePath);
      }
    }

    // The stat line an authored delta lands on. A unit type keeps no line of
    // its own (it is a class with a name and a kit), so the base is the
    // class's, reached through the type the member names. Built once, because a
    // company is read member by member and every one of them asks.
    const unitTypesById = new Map(
      (project.unitTypes ?? []).map((unitType) => [unitType.id, unitType])
    );
    const classesById = new Map(
      (project.classes ?? []).map(
        (sourceClass) => [sourceClass.id, sourceClass]
      )
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
    // skipped when nothing says: an unresolved unit type, or an unresolved
    // class beneath it, is already reported where it is named, and a bad
    // reference must never reach an author as a bad delta. The other two are
    // true of any character, so they are said either way.
    function validateMemberSpecificity(member, memberPath) {
      const specificity = member.specificity;
      if (specificity === undefined) {
        return;
      }
      const stats = specificity.stats ?? {};
      if (
        specificity.rangeBonus === undefined &&
        Object.keys(stats).length === 0
      ) {
        const instancePath = `${memberPath}/specificity`;
        diagnostics.push(
          diagnostic(
            filename,
            "SOURCE_CAMPAIGN_SPECIFICITY_EMPTY",
            instancePath,
            `member '${member.id}' is stated to be specific and says nothing ` +
              "about how",
            document.pointers[instancePath]?.value
          )
        );
      }
      const unitType = unitTypesById.get(member.unitTypeId);
      const baseStats = unitType === undefined
        ? undefined
        : classesById.get(unitType.classId)?.baseStats;
      // Only the eleven the block names can be here: the schema closed the
      // object before any of this ran.
      for (const [stat, delta] of Object.entries(stats)) {
        const instancePath = `${memberPath}/specificity/stats/${stat}`;
        if (delta === 0) {
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_STAT_DELTA_ZERO",
              instancePath,
              `member '${member.id}' adjusts ${stat} by zero, which says ` +
                "nothing a stat left out does not say",
              document.pointers[instancePath]?.value
            )
          );
          continue;
        }
        if (baseStats === undefined) {
          continue;
        }
        const bounds = statDeltaBounds[stat];
        const base = baseStats[stat] ?? bounds.whenOmitted;
        const landed = base + delta;
        if (landed < bounds.minimum || landed > bounds.maximum) {
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE",
              instancePath,
              `member '${member.id}' takes ${stat} from ${base} by ` +
                `${delta > 0 ? "+" : ""}${delta} to ${landed}, outside the ` +
                `${bounds.minimum} to ${bounds.maximum} that ${stat} admits`,
              document.pointers[instancePath]?.value
            )
          );
        }
      }
    }

    function validateCampaignRoster(campaign, campaignIndex) {
      // Who this campaign can ever hold, and what each of them is. The
      // founding members and every node's recruits share one namespace,
      // because a placement names a member without saying where that member
      // joined, and the whole company has to be known before any board is
      // read.
      const memberUnitTypes = new Map();
      const admitMember = (member, memberPath) => {
        if (memberUnitTypes.has(member.id)) {
          const instancePath = `${memberPath}/id`;
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_MEMBER_ID_DUPLICATE",
              instancePath,
              `duplicate campaign member identity '${member.id}' in campaign '${campaign.id}'`,
              document.pointers[instancePath]?.value
            )
          );
          return;
        }
        memberUnitTypes.set(member.id, member.unitTypeId);
        requireReference(
          "unit_type",
          member.unitTypeId,
          `${memberPath}/unitTypeId`
        );
        validateMemberSpecificity(member, memberPath);
      };

      for (const [memberIndex, member] of (campaign.roster ?? []).entries()) {
        admitMember(
          member,
          `/campaigns/${campaignIndex}/roster/${memberIndex}`
        );
      }
      // A campaign nobody can be founded from: no roster at all, or every
      // member it holds joining at some later node. Both leave a company
      // somebody would have to invent at play time, and nothing invents one.
      if ((campaign.roster ?? []).length === 0) {
        const instancePath = `/campaigns/${campaignIndex}/roster`;
        diagnostics.push(
          diagnostic(
            filename,
            "SOURCE_CAMPAIGN_ROSTER_EMPTY",
            instancePath,
            `campaign '${campaign.id}' has no founding member to be played by`,
            document.pointers[instancePath]?.value
          )
        );
      }
      for (const [nodeIndex, node] of (campaign.flow?.nodes ?? []).entries()) {
        for (const [memberIndex, member] of (node.recruits ?? []).entries()) {
          admitMember(
            member,
            `/campaigns/${campaignIndex}/flow/nodes/${nodeIndex}` +
            `/recruits/${memberIndex}`
          );
        }
      }
      return memberUnitTypes;
    }

    function validateCampaignFlow(campaign, campaignIndex, memberUnitTypes) {
      if (!campaign.flow) {
        return;
      }

      const flow = campaign.flow;
      const flowPath = `/campaigns/${campaignIndex}/flow`;
      const nodesById = new Map();
      for (const [nodeIndex, node] of flow.nodes.entries()) {
        const idPath = `${flowPath}/nodes/${nodeIndex}/id`;
        if (nodesById.has(node.id)) {
          const pointer = document.pointers[idPath];
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_NODE_ID_DUPLICATE",
              idPath,
              `duplicate campaign node identity '${node.id}'`,
              pointer?.value
            )
          );
        } else {
          nodesById.set(node.id, nodeIndex);
        }
      }

      if (!nodesById.has(flow.entryNodeId)) {
        const instancePath = `${flowPath}/entryNodeId`;
        const pointer = document.pointers[instancePath];
        diagnostics.push(
          diagnostic(
            filename,
            "SOURCE_CAMPAIGN_ENTRY_MISSING",
            instancePath,
            `missing campaign entry node '${flow.entryNodeId}'`,
            pointer?.value
          )
        );
      }

      for (const [nodeIndex, node] of flow.nodes.entries()) {
        const nodePath = `${flowPath}/nodes/${nodeIndex}`;
        const map = node.mapId === undefined
          ? undefined
          : (project.maps ?? []).find((candidate) => candidate.id === node.mapId);
        if (node.mapId !== undefined) {
          requireReference("map", node.mapId, `${nodePath}/mapId`);
        }
        const placementIds = new Set();
        const occupiedTiles = new Map();
        // One board, one appearance each: a member standing on two tiles of
        // the same encounter would be two units claiming one character's
        // history.
        const fielded = new Set();
        for (const [placementIndex, placement] of (node.placements ?? []).entries()) {
          const placementPath = `${nodePath}/placements/${placementIndex}`;
          if (placementIds.has(placement.id)) {
            const instancePath = `${placementPath}/id`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_PLACEMENT_ID_DUPLICATE",
                instancePath,
                `duplicate placement identity '${placement.id}' in node '${node.id}'`,
                pointer?.value
              )
            );
          } else {
            placementIds.add(placement.id);
          }
          requireReference(
            "unit_type",
            placement.unitTypeId,
            `${placementPath}/unitTypeId`
          );
          // Who stands here, in both directions. The first side is the
          // company, so every tile of it is one named member the campaign
          // holds; the second side is whoever the campaign is fighting, and
          // naming a member there would field one of the player's own
          // characters as an enemy.
          if (placement.side === "second") {
            if (placement.memberId !== undefined) {
              const instancePath = `${placementPath}/memberId`;
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_FORBIDDEN",
                  instancePath,
                  `placement '${placement.id}' stands on the second side and names member '${placement.memberId}'`,
                  document.pointers[instancePath]?.value
                )
              );
            }
          } else if (placement.memberId === undefined) {
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_MISSING",
                placementPath,
                `placement '${placement.id}' stands on the first side and names no member`,
                document.pointers[placementPath]?.value
              )
            );
          } else if (!memberUnitTypes.has(placement.memberId)) {
            const instancePath = `${placementPath}/memberId`;
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_UNKNOWN",
                instancePath,
                `placement '${placement.id}' names member '${placement.memberId}', whom campaign '${campaign.id}' never holds`,
                document.pointers[instancePath]?.value
              )
            );
          } else {
            const instancePath = `${placementPath}/memberId`;
            if (fielded.has(placement.memberId)) {
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_DUPLICATE",
                  instancePath,
                  `member '${placement.memberId}' already stands on node '${node.id}'`,
                  document.pointers[instancePath]?.value
                )
              );
            } else {
              fielded.add(placement.memberId);
            }
            const memberUnitTypeId = memberUnitTypes.get(placement.memberId);
            if (memberUnitTypeId !== placement.unitTypeId) {
              const typePath = `${placementPath}/unitTypeId`;
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_TYPE_MISMATCH",
                  typePath,
                  `placement '${placement.id}' fields member '${placement.memberId}' as '${placement.unitTypeId}', who is '${memberUnitTypeId}'`,
                  document.pointers[typePath]?.value
                )
              );
            }
          }
          // A placement that arrives is a reinforcement: it is not on the
          // board when the battle opens, so it shares no tile with the opening
          // arrangement and its own tile is a request rather than a claim.
          // Where that tile is held when its round comes, the engine stands it
          // on the nearest one it could stand on instead.
          const arrives = placement.arrival !== undefined;
          if (arrives && placement.memberId !== undefined) {
            const instancePath = `${placementPath}/arrival`;
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_ARRIVAL_MEMBER",
                instancePath,
                `placement '${placement.id}' fields member '${placement.memberId}' and cannot also arrive`,
                document.pointers[instancePath]?.value
              )
            );
          }
          const tileKey = arrives ? null : `${placement.x},${placement.y}`;
          const occupant = tileKey === null
            ? undefined
            : occupiedTiles.get(tileKey);
          if (occupant !== undefined) {
            const instancePath = `${placementPath}/x`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_PLACEMENT_TILE_OCCUPIED",
                instancePath,
                `placement '${placement.id}' overlaps '${occupant}' at (${tileKey})`,
                pointer?.value
              )
            );
          } else if (tileKey !== null) {
            occupiedTiles.set(tileKey, placement.id);
          }
          if (map && (
            placement.x >= map.width ||
            placement.y >= map.height
          )) {
            const instancePath = placement.x >= map.width
              ? `${placementPath}/x`
              : `${placementPath}/y`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_PLACEMENT_OUT_OF_BOUNDS",
                instancePath,
                `placement '${placement.id}' is outside map '${map.id}' (${map.width}x${map.height})`,
                pointer?.value
              )
            );
          }
        }
        // What the encounter says about the player's own troops: the region
        // they are arranged in, how many of them may take the field, or both.
        // Who falls out of the placements above, which is why this runs after
        // them: a region no first-side placement stands inside is a region
        // nobody can be arranged in, and a cap no company can reach is a cap
        // that never refuses anything.
        //
        // What terrain a region tile asks of whoever stands on it is checked by
        // the content compiler and not here, exactly as a placement's own
        // terrain is: this analyzer does not resolve a unit type through its
        // class to its crossings, and inventing half of that rule would be
        // worse than leaving the whole of it in one place.
        if (node.deployment !== undefined) {
          const zone = node.deployment;
          const zonePath = `${nodePath}/deployment`;
          const zoneTiles = new Set();
          let arrangeable = false;
          for (const [tileIndex, tile] of (zone.tiles ?? []).entries()) {
            const tilePath = `${zonePath}/tiles/${tileIndex}`;
            const tileKey = `${tile.x},${tile.y}`;
            if (zoneTiles.has(tileKey)) {
              const instancePath = `${tilePath}/x`;
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_CAMPAIGN_DEPLOYMENT_TILE_DUPLICATE",
                  instancePath,
                  `deployment zone '${zone.id}' names (${tileKey}) twice`,
                  document.pointers[instancePath]?.value
                )
              );
            } else {
              zoneTiles.add(tileKey);
            }
            if (map && (tile.x >= map.width || tile.y >= map.height)) {
              const instancePath = tile.x >= map.width
                ? `${tilePath}/x`
                : `${tilePath}/y`;
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_CAMPAIGN_DEPLOYMENT_OUT_OF_BOUNDS",
                  instancePath,
                  `deployment zone '${zone.id}' reaches outside map ` +
                    `'${map.id}' (${map.width}x${map.height})`,
                  document.pointers[instancePath]?.value
                )
              );
            }
          }
          // How many of the company this board could ever field: one per
          // first-side placement, because a member takes the field by standing
          // on one.
          let fieldable = 0;
          for (const placement of node.placements ?? []) {
            if (placement.side !== "first") continue;
            fieldable += 1;
            if (zoneTiles.has(`${placement.x},${placement.y}`)) {
              arrangeable = true;
            }
          }
          // A region nobody stands in is only a question where a region was
          // stated: a deployment that states a cap alone arranges nobody
          // anywhere, by design.
          if (zone.tiles !== undefined && !arrangeable) {
            const instancePath = `${zonePath}/id`;
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_DEPLOYMENT_UNOCCUPIED",
                instancePath,
                `deployment zone '${zone.id}' holds no first-side ` +
                  "placement, so nobody can be arranged in it",
                document.pointers[instancePath]?.value
              )
            );
          }
          // A deployment says at least one of the two things a deployment is
          // for. This is where that is refused, at the deployment's own path
          // and in a sentence, beside every other thing that can be wrong with
          // a deployment. A keyword failure on the shape of the object would
          // name neither the deployment nor what it was missing.
          if (
            typeof zone === "object" &&
            typeof zone.id === "string" &&
            zone.tiles === undefined &&
            zone.capacity === undefined
          ) {
            const instancePath = `${zonePath}/id`;
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_DEPLOYMENT_EMPTY",
                instancePath,
                `deployment '${zone.id}' states neither a region to arrange ` +
                  "in nor a cap on how many may take the field",
                document.pointers[instancePath]?.value
              )
            );
          }
          // A cap at or above the number of first-side placements can never
          // refuse anybody, because the board has nobody else to field. An
          // author who wrote one meant a smaller number.
          if (zone.capacity !== undefined && zone.capacity >= fieldable) {
            const instancePath = `${zonePath}/capacity`;
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE",
                instancePath,
                `deployment '${zone.id}' caps the field at ${zone.capacity}, ` +
                  `which node '${node.id}' never reaches: it authors ` +
                  `${fieldable} first-side placement` +
                  `${fieldable === 1 ? "" : "s"}`,
                document.pointers[instancePath]?.value
              )
            );
          }
        }
        // A Stage nothing decides cannot be opened at all. The package format
        // writes the count of a board's objectives before anything else and
        // every runtime refuses a board declaring none, so a campaign that
        // reaches such a node stops there and reports a board it could not
        // decode. The compiler refuses to emit one; this refuses it earlier,
        // where an author can still be told which Stage it is.
        if (node.kind === "encounter" && (node.objectiveIds ?? []).length === 0) {
          const instancePath = `${nodePath}/objectiveIds`;
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_CAMPAIGN_STAGE_UNDECIDED",
              instancePath,
              `Stage '${node.id}' states no way to be won or lost, so it ` +
                "cannot be played: a campaign that reaches it stops there",
              document.pointers[instancePath]?.value
            )
          );
        }
        for (const [index, objectiveId] of (node.objectiveIds ?? []).entries()) {
          requireReference(
            "objective",
            objectiveId,
            `${nodePath}/objectiveIds/${index}`
          );
        }
        for (const [index, dialogueId] of (node.dialogueIds ?? []).entries()) {
          requireReference(
            "dialogue",
            dialogueId,
            `${nodePath}/dialogueIds/${index}`
          );
        }
        validateItemGrants(
          node.grants,
          `${nodePath}/grants`,
          `node '${node.id}'`
        );

        const transitionIds = new Set();
        const priorities = new Set();
        let fallbackSeen = false;
        for (const [transitionIndex, transition] of node.transitions.entries()) {
          const transitionPath = `${nodePath}/transitions/${transitionIndex}`;
          if (transitionIds.has(transition.id)) {
            const instancePath = `${transitionPath}/id`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_TRANSITION_ID_DUPLICATE",
                instancePath,
                `duplicate transition identity '${transition.id}' in node '${node.id}'`,
                pointer?.value
              )
            );
          } else {
            transitionIds.add(transition.id);
          }

          if (!nodesById.has(transition.targetNodeId)) {
            const instancePath = `${transitionPath}/targetNodeId`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_TRANSITION_TARGET_MISSING",
                instancePath,
                `missing campaign transition target '${transition.targetNodeId}'`,
                pointer?.value
              )
            );
          }

          if (priorities.has(transition.priority)) {
            const instancePath = `${transitionPath}/priority`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
                instancePath,
                `duplicate transition priority '${transition.priority}' in node '${node.id}'`,
                pointer?.value
              )
            );
          } else {
            priorities.add(transition.priority);
          }

          if (transition.when === undefined) {
            if (fallbackSeen) {
              const instancePath = transitionPath;
              const pointer = document.pointers[instancePath];
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
                  instancePath,
                  `node '${node.id}' has more than one unconditional fallback`,
                  pointer?.value
                )
              );
            }
            fallbackSeen = true;
          } else {
            validateCampaignCondition(transition.when, `${transitionPath}/when`);
          }
        }
      }

      if (nodesById.has(flow.entryNodeId)) {
        const reachable = new Set();
        const pending = [flow.entryNodeId];
        while (pending.length > 0) {
          const id = pending.pop();
          if (reachable.has(id)) {
            continue;
          }
          reachable.add(id);
          const node = flow.nodes[nodesById.get(id)];
          for (let index = node.transitions.length - 1; index >= 0; index -= 1) {
            const target = node.transitions[index].targetNodeId;
            if (nodesById.has(target) && !reachable.has(target)) {
              pending.push(target);
            }
          }
        }
        for (const [nodeIndex, node] of flow.nodes.entries()) {
          if (!reachable.has(node.id)) {
            const instancePath = `${flowPath}/nodes/${nodeIndex}/id`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_CAMPAIGN_NODE_UNREACHABLE",
                instancePath,
                `campaign node '${node.id}' is unreachable from entry '${flow.entryNodeId}'`,
                pointer?.value
              )
            );
          }
        }
      }
    }

    for (const [index, unitType] of (project.unitTypes ?? []).entries()) {
      requireReference("class", unitType.classId, `/unitTypes/${index}/classId`);
      if (unitType.factionId !== undefined) {
        requireReference(
          "faction",
          unitType.factionId,
          `/unitTypes/${index}/factionId`
        );
      }
      for (const [abilityIndex, abilityId] of (
        unitType.abilityIds ?? []
      ).entries()) {
        requireReference(
          "ability",
          abilityId,
          `/unitTypes/${index}/abilityIds/${abilityIndex}`
        );
      }
      for (const [weaponIndex, weaponId] of (unitType.startingWeaponIds ?? []).entries()) {
        requireReference(
          "weapon",
          weaponId,
          `/unitTypes/${index}/startingWeaponIds/${weaponIndex}`
        );
      }
      for (const [itemIndex, itemId] of (unitType.startingItemIds ?? []).entries()) {
        requireReference(
          "item",
          itemId,
          `/unitTypes/${index}/startingItemIds/${itemIndex}`
        );
      }
      // A drop is authored as a pair or not at all, in both directions: a
      // chance with nothing to leave is a roll whose outcome was never written
      // down, and something to leave with no chance of leaving it is an
      // outcome nothing reaches. The schema cannot say "these two together"
      // without a conditional nobody would read, so it is said here, where the
      // diagnostic can point at the half that is missing.
      if (unitType.dropItemId !== undefined) {
        requireReference(
          "item",
          unitType.dropItemId,
          `/unitTypes/${index}/dropItemId`
        );
        if (unitType.dropChance === undefined) {
          const instancePath = `/unitTypes/${index}/dropItemId`;
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_DROP_INCOMPLETE",
              instancePath,
              `unit type '${unitType.id}' drops '${unitType.dropItemId}' with no dropChance`,
              document.pointers[instancePath]?.value
            )
          );
        }
      } else if (unitType.dropChance !== undefined) {
        const instancePath = `/unitTypes/${index}/dropChance`;
        diagnostics.push(
          diagnostic(
            filename,
            "SOURCE_DROP_INCOMPLETE",
            instancePath,
            `unit type '${unitType.id}' has a dropChance and no dropItemId`,
            document.pointers[instancePath]?.value
          )
        );
      }
    }

    for (const [index, sourceClass] of (project.classes ?? []).entries()) {
      for (const [typeIndex, typeId] of (
        sourceClass.allowedWeaponTypeIds ?? []
      ).entries()) {
        requireReference(
          "weapon_type",
          typeId,
          `/classes/${index}/allowedWeaponTypeIds/${typeIndex}`
        );
      }
    }

    for (const [index, weapon] of (project.weapons ?? []).entries()) {
      if (weapon.weaponTypeId !== undefined) {
        requireReference(
          "weapon_type",
          weapon.weaponTypeId,
          `/weapons/${index}/weaponTypeId`
        );
      }
    }

    for (const [index, item] of (project.items ?? []).entries()) {
      if (item.itemTypeId !== undefined) {
        requireReference(
          "item_type",
          item.itemTypeId,
          `/items/${index}/itemTypeId`
        );
      }
    }

    // Who a scene says its speakers are. The same three checks the browser
    // analyzer makes, on the same paths: the unit type cast must be one this
    // project declares, one speaker must not be answered twice, and a cast
    // entry that speaks no line is named rather than left to show itself as
    // the wrong drawing.
    for (const [index, dialogue] of (project.dialogues ?? []).entries()) {
      const spoken = new Set((dialogue.lines ?? []).map((line) => line.speaker));
      const seen = new Set();
      for (const [entryIndex, entry] of (dialogue.cast ?? []).entries()) {
        const entryPath = `/dialogues/${index}/cast/${entryIndex}`;
        requireReference("unit_type", entry.unitTypeId, `${entryPath}/unitTypeId`);
        const speakerPath = `${entryPath}/speaker`;
        if (seen.has(entry.speaker)) {
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_DIALOGUE_SPEAKER_CAST_TWICE",
              speakerPath,
              `'${entry.speaker}' is cast twice in this scene`,
              document.pointers[speakerPath]?.value
            )
          );
        }
        seen.add(entry.speaker);
        if (!spoken.has(entry.speaker)) {
          diagnostics.push(
            diagnostic(
              filename,
              "SOURCE_DIALOGUE_CAST_SPEAKS_NO_LINE",
              speakerPath,
              `'${entry.speaker}' speaks no line in this scene`,
              document.pointers[speakerPath]?.value
            )
          );
        }
      }
    }

    for (const [index, campaign] of (project.campaigns ?? []).entries()) {
      for (const [objectiveIndex, objectiveId] of (
        campaign.objectiveIds ?? []
      ).entries()) {
        requireReference(
          "objective",
          objectiveId,
          `/campaigns/${index}/objectiveIds/${objectiveIndex}`
        );
      }
      for (const [dialogueIndex, dialogueId] of (
        campaign.dialogueIds ?? []
      ).entries()) {
        requireReference(
          "dialogue",
          dialogueId,
          `/campaigns/${index}/dialogueIds/${dialogueIndex}`
        );
      }
      const memberUnitTypes = validateCampaignRoster(campaign, index);
      validateItemGrants(
        campaign.startingStore,
        `/campaigns/${index}/startingStore`,
        `the founding store of campaign '${campaign.id}'`
      );
      validateCampaignFlow(campaign, index, memberUnitTypes);
    }

    for (const [property] of collections) {
      for (const [recordIndex, record] of (project[property] ?? []).entries()) {
        const slots = new Set();
        for (const [bindingIndex, binding] of (
          record.scriptBindings ?? []
        ).entries()) {
          const bindingPath =
            `/${property}/${recordIndex}/scriptBindings/${bindingIndex}`;
          if (slots.has(binding.slot)) {
            const instancePath = `${bindingPath}/slot`;
            const pointer = document.pointers[instancePath];
            diagnostics.push(
              diagnostic(
                filename,
                "SOURCE_SCRIPT_SLOT_DUPLICATE",
                instancePath,
                `duplicate script binding slot '${binding.slot}'`,
                pointer?.value
              )
            );
          } else {
            slots.add(binding.slot);
          }

          const parameterNames = new Set();
          for (const [parameterIndex, parameter] of binding.parameters.entries()) {
            const parameterPath =
              `${bindingPath}/parameters/${parameterIndex}`;
            if (parameterNames.has(parameter.name)) {
              const instancePath = `${parameterPath}/name`;
              const pointer = document.pointers[instancePath];
              diagnostics.push(
                diagnostic(
                  filename,
                  "SOURCE_SCRIPT_PARAMETER_DUPLICATE",
                  instancePath,
                  `duplicate script parameter name '${parameter.name}'`,
                  pointer?.value
                )
              );
            } else {
              parameterNames.add(parameter.name);
            }

            if (parameter.value.kind === "contentRef") {
              requireReference(
                parameter.value.category,
                parameter.value.sourceKey,
                `${parameterPath}/value/sourceKey`
              );
            }
          }
        }
      }
    }

    // The kind and the count are one authored fact: a surviveRounds objective
    // with no count is a battle already over before it opened, and a count on a
    // kind that could never read one is a number nothing will consult. Said out
    // loud rather than defaulted or dropped, the way the compiler says it.
    for (const [index, objective] of (project.objectives ?? []).entries()) {
      const counts = objective.kind === "surviveRounds";
      if (counts === (objective.rounds !== undefined)) continue;
      const instancePath = counts
        ? `/objectives/${index}/kind`
        : `/objectives/${index}/rounds`;
      diagnostics.push(
        diagnostic(
          filename,
          "SOURCE_OBJECTIVE_ROUNDS_MISMATCH",
          instancePath,
          counts
            ? `objective '${objective.id}' survives rounds and states no count`
            : `objective '${objective.id}' states a round count no kind but surviveRounds reads`,
          document.pointers[instancePath]?.value
        )
      );
    }

    for (const [index, map] of (project.maps ?? []).entries()) {
      if (map.terrain.length !== map.width * map.height) {
        const instancePath = `/maps/${index}/terrain`;
        const pointer = document.pointers[instancePath];
        diagnostics.push(
          diagnostic(
            filename,
            "SOURCE_MAP_SHAPE_INVALID",
            instancePath,
            `terrain has ${map.terrain.length} cells; expected ${map.width * map.height}`,
            pointer?.value
          )
        );
      }
    }
  }

  return diagnostics;
}

function main() {
  const filename = process.argv[2];
  if (!filename) {
    console.error("usage: node validate.mjs <project.json>");
    process.exitCode = 2;
    return;
  }

  const resolved = path.resolve(filename);
  const diagnostics = validateProject(resolved);
  for (const diagnostic of diagnostics) {
    // `file:line:column:` first and unbroken, because that prefix is what an
    // editor jumps from. The instance path follows as part of the sentence
    // rather than running into the column number.
    console.error(
      `${diagnostic.sourcePath}:${diagnostic.line}:${diagnostic.column}: ` +
      `${diagnostic.code}: ${diagnostic.instancePath}: ` +
      `${diagnostic.message}`
    );
  }
  process.exitCode = diagnostics.some((item) => item.severity === "error") ? 1 : 0;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main();
}
