// SPDX-License-Identifier: MIT
import type {
  ScriptBinding,
  SourceAbility,
  SourceCampaign,
  SourceClass,
  SourceDialogue,
  SourceFaction,
  SourceItem,
  SourceItemType,
  SourceMap,
  SourceObjective,
  SourceProject,
  SourceUnitType,
  SourceWeapon,
  SourceWeaponType
} from "../generated/source-v1";
import { COLLECTION_WORD } from "./author-words";

export interface SourceCollections {
  readonly classes: SourceClass;
  readonly weaponTypes: SourceWeaponType;
  readonly itemTypes: SourceItemType;
  readonly unitTypes: SourceUnitType;
  readonly weapons: SourceWeapon;
  readonly items: SourceItem;
  readonly maps: SourceMap;
  readonly factions: SourceFaction;
  readonly abilities: SourceAbility;
  readonly objectives: SourceObjective;
  readonly campaigns: SourceCampaign;
  readonly dialogues: SourceDialogue;
}

export type SourceCollectionName = keyof SourceCollections;
export type SourceRecord<C extends SourceCollectionName> = SourceCollections[C];

export interface SourceTransaction {
  readonly label: string;
  readonly affectedPaths: readonly string[];
}

/**
 * One write inside a larger act.
 *
 * The union is built by mapping over the collections rather than written out
 * once with a free `C`, so a `create` carries the record type its own
 * collection holds and an `update` gets a draft of that same type. A step
 * naming `unitTypes` and carrying a map cannot be written down.
 */
export type SourceEdit = {
  [C in SourceCollectionName]:
    | { readonly kind: "create"; readonly collection: C; readonly record: SourceRecord<C> }
    | {
      readonly kind: "update";
      readonly collection: C;
      readonly id: string;
      readonly update: (draft: SourceRecord<C>) => void;
    }
}[SourceCollectionName];

export class SourceProjectEditError extends Error {
  constructor(
    readonly code:
      | "RECORD_NOT_FOUND"
      | "DUPLICATE_ID"
      | "DELETE_REFERENCED"
      | "INVALID_PROJECT_FIELD",
    message: string,
    readonly affectedPaths: readonly string[] = []
  ) {
    super(message);
    this.name = "SourceProjectEditError";
  }
}

interface HistoryEntry {
  readonly transaction: SourceTransaction;
  readonly before: SourceProject;
  readonly after: SourceProject;
}

// The words an author reads live in `author-words.ts`, which is the one place
// the editor's vocabulary is written down. They name records in transaction
// labels and error messages here; the stored collection names stay schema-true.
const collectionCategory = COLLECTION_WORD;

const referenceCategory: Record<SourceCollectionName, string> = {
  classes: "class",
  unitTypes: "unit_type",
  weaponTypes: "weapon_type",
  weapons: "weapon",
  itemTypes: "item_type",
  items: "item",
  maps: "map",
  factions: "faction",
  abilities: "ability",
  objectives: "objective",
  campaigns: "campaign",
  dialogues: "dialogue"
};

function copy(project: SourceProject): SourceProject {
  return structuredClone(project);
}

function recordPath(collection: SourceCollectionName, index: number): string {
  return `/${collection}/${index}`;
}

/**
 * Where a record sits in a project, or a refusal naming it.
 *
 * Asked of a project rather than of the session's own, because a grouped act
 * builds up a draft and every step after the first has to be judged against
 * what the steps before it wrote: a second step naming a record the first one
 * created must find it, and a second `create` of one identifier must not.
 */
function indexOfRecord(
  project: SourceProject,
  collection: SourceCollectionName,
  id: string
): number {
  const index = records(project, collection).findIndex(
    (record) => record.id === id
  );
  if (index < 0) {
    throw new SourceProjectEditError(
      "RECORD_NOT_FOUND",
      `${collectionCategory[collection]} '${id}' was not found`
    );
  }
  return index;
}

function assertAvailableId(
  project: SourceProject,
  collection: SourceCollectionName,
  id: string
) {
  if (records(project, collection).some((record) => record.id === id)) {
    throw new SourceProjectEditError(
      "DUPLICATE_ID",
      `${collectionCategory[collection]} '${id}' already exists`
    );
  }
}

function records<C extends SourceCollectionName>(
  project: SourceProject,
  collection: C
): SourceRecord<C>[] {
  return (project[collection] ?? []) as SourceRecord<C>[];
}

function writableRecords<C extends SourceCollectionName>(
  project: SourceProject,
  collection: C
): SourceRecord<C>[] {
  const existing = records(project, collection);
  if (project[collection] === undefined) {
    Object.assign(project, { [collection]: existing });
  }
  return existing;
}

function scriptBindings(project: SourceProject): readonly {
  readonly ownerPath: string;
  readonly bindings: ScriptBinding[];
}[] {
  return ([
    ["abilities", project.abilities ?? []],
    ["objectives", project.objectives ?? []],
    ["campaigns", project.campaigns ?? []],
    ["dialogues", project.dialogues ?? []]
  ] as const).flatMap(([collection, owners]) =>
    owners.flatMap((owner, ownerIndex) =>
      owner.scriptBindings
        ? [{
          ownerPath: `/${collection}/${ownerIndex}`,
          bindings: owner.scriptBindings
        }]
        : []
    )
  );
}

type CrossReferenceCollection =
  | "maps"
  | "unitTypes"
  | "objectives"
  | "dialogues"
  | "items";

interface CampaignConditionShape {
  readonly kind: string;
  conditions?: CampaignConditionShape[];
  condition?: CampaignConditionShape;
  objectiveId?: string;
  itemId?: string;
}

// A member of the company, wherever they are authored. The member id itself is
// campaign-scoped rather than a project definition, so what a rename or a
// delete has to follow here is the unit type the member is made of.
interface CampaignMemberShape {
  unitTypeId: string;
}

// A quantity of one item put into the company's store, wherever it is authored:
// the campaign's founding stock and a node's grant are the same record. The
// quantity is a number rather than a reference, so what a rename or a delete
// has to follow here is the item the grant names.
interface CampaignItemGrantShape {
  itemId: string;
}

interface CampaignFlowShape {
  nodes: {
    mapId?: string;
    placements?: { unitTypeId: string; memberId?: string }[];
    objectiveIds?: string[];
    dialogueIds?: string[];
    recruits?: CampaignMemberShape[];
    grants?: CampaignItemGrantShape[];
    transitions: { when?: CampaignConditionShape }[];
  }[];
}

/**
 * Every reference the collection-by-collection rules above do not carry: the
 * ones authored inside a campaign's flow, and the cast of a scene.
 *
 * Rename, delete and the preview of a rename all walk this one visitor, so a
 * reference added here is followed by all three at once. That matters most for
 * the cast, which is the only place a unit type is named from a record of
 * another kind: a cast this visitor did not carry would let a character be
 * deleted out from under a scene that speaks for them, and let a rename leave
 * the scene naming somebody nothing answers to.
 */
function visitCrossReferences(
  project: SourceProject,
  visit: (
    collection: CrossReferenceCollection,
    id: string,
    path: string,
    replace: (nextId: string) => void
  ) => void
) {
  (project.dialogues ?? []).forEach((dialogue, dialogueIndex) => {
    (dialogue.cast ?? []).forEach((entry, entryIndex) => {
      visit(
        "unitTypes",
        entry.unitTypeId,
        `/dialogues/${dialogueIndex}/cast/${entryIndex}/unitTypeId`,
        (nextId) => {
          entry.unitTypeId = nextId;
        }
      );
    });
  });
  const visitCondition = (
    condition: CampaignConditionShape,
    path: string
  ): void => {
    if (condition.kind === "objectiveResult" && condition.objectiveId) {
      visit("objectives", condition.objectiveId, `${path}/objectiveId`, (nextId) => {
        condition.objectiveId = nextId;
      });
    }
    if (condition.kind === "inventoryAtLeast" && condition.itemId) {
      visit("items", condition.itemId, `${path}/itemId`, (nextId) => {
        condition.itemId = nextId;
      });
    }
    (condition.conditions ?? []).forEach((child, index) => {
      visitCondition(child, `${path}/conditions/${index}`);
    });
    if (condition.condition) {
      visitCondition(condition.condition, `${path}/condition`);
    }
  };

  (project.campaigns ?? []).forEach((campaign, campaignIndex) => {
    const roster =
      (campaign as SourceCampaign & { roster?: CampaignMemberShape[] }).roster;
    // The founding company is authored on the campaign rather than in the
    // flow, but it is the same reference: a placement fielding a member must
    // still agree with the member's unit type after a rename, and a unit type
    // the roster is made of must not be deleted out from under it.
    (roster ?? []).forEach((member, memberIndex) => {
      visit(
        "unitTypes",
        member.unitTypeId,
        `/campaigns/${campaignIndex}/roster/${memberIndex}/unitTypeId`,
        (nextId) => {
          member.unitTypeId = nextId;
        }
      );
    });
    // The stock a campaign is founded with names items the same way a node's
    // grant does, and for the same reason it must be followed: deleting an item
    // a campaign already owns would found a company holding nothing under a
    // name nothing answers to.
    const startingStore = (campaign as SourceCampaign & {
      startingStore?: CampaignItemGrantShape[];
    }).startingStore;
    (startingStore ?? []).forEach((grant, grantIndex) => {
      visit(
        "items",
        grant.itemId,
        `/campaigns/${campaignIndex}/startingStore/${grantIndex}/itemId`,
        (nextId) => {
          grant.itemId = nextId;
        }
      );
    });
    const flow = (campaign as SourceCampaign & { flow?: CampaignFlowShape }).flow;
    flow?.nodes.forEach((node, nodeIndex) => {
      const nodePath = `/campaigns/${campaignIndex}/flow/nodes/${nodeIndex}`;
      if (node.mapId) {
        visit("maps", node.mapId, `${nodePath}/mapId`, (nextId) => {
          node.mapId = nextId;
        });
      }
      (node.placements ?? []).forEach((placement, placementIndex) => {
        visit(
          "unitTypes",
          placement.unitTypeId,
          `${nodePath}/placements/${placementIndex}/unitTypeId`,
          (nextId) => {
            placement.unitTypeId = nextId;
          }
        );
      });
      (node.recruits ?? []).forEach((member, memberIndex) => {
        visit(
          "unitTypes",
          member.unitTypeId,
          `${nodePath}/recruits/${memberIndex}/unitTypeId`,
          (nextId) => {
            member.unitTypeId = nextId;
          }
        );
      });
      (node.grants ?? []).forEach((grant, grantIndex) => {
        visit(
          "items",
          grant.itemId,
          `${nodePath}/grants/${grantIndex}/itemId`,
          (nextId) => {
            grant.itemId = nextId;
          }
        );
      });
      (node.objectiveIds ?? []).forEach((objectiveId, objectiveIndex) => {
        visit(
          "objectives",
          objectiveId,
          `${nodePath}/objectiveIds/${objectiveIndex}`,
          (nextId) => {
            node.objectiveIds![objectiveIndex] = nextId;
          }
        );
      });
      (node.dialogueIds ?? []).forEach((dialogueId, dialogueIndex) => {
        visit(
          "dialogues",
          dialogueId,
          `${nodePath}/dialogueIds/${dialogueIndex}`,
          (nextId) => {
            node.dialogueIds![dialogueIndex] = nextId;
          }
        );
      });
      node.transitions.forEach((transition, transitionIndex) => {
        if (transition.when) {
          visitCondition(
            transition.when,
            `${nodePath}/transitions/${transitionIndex}/when`
          );
        }
      });
    });
  });
}

export class SourceProjectSession {
  #project: SourceProject;
  readonly #undo: HistoryEntry[] = [];
  readonly #redo: HistoryEntry[] = [];

  constructor(project: SourceProject) {
    this.#project = copy(project);
  }

  snapshot(): SourceProject {
    return copy(this.#project);
  }

  canUndo(): boolean {
    return this.#undo.length > 0;
  }

  canRedo(): boolean {
    return this.#redo.length > 0;
  }

  updateMetadata(
    // Explicit `undefined` is meaningful here: it clears an optional field,
    // which is not the same as leaving it out of the change.
    changes: {
      [Field in
        | "gameId"
        | "title"
        | "contentRevision"
        | "themeId"
        | "characterStyleId"
        | "characterFigureId"
        | "characterGeometry"
        | "defaultTurnOrder"
        | "characterLoss"
        | "invulnerableForTesting"
        | "notes"
        | "extensions"
      ]?: SourceProject[Field] | undefined
    }
  ): SourceTransaction {
    // The runtime half of the list above, and it has to be kept level with it
    // by hand: the type is erased before a caller's object gets here, so a
    // field named in one and not the other is a field the settings page can
    // render, report as saved, and never store. Every project-level control the
    // editor offers belongs in both.
    const allowed = new Set([
      "gameId",
      "title",
      "contentRevision",
      "themeId",
      "characterStyleId",
      "characterFigureId",
      "characterGeometry",
      "defaultTurnOrder",
      "characterLoss",
      "invulnerableForTesting",
      "notes",
      "extensions"
    ]);
    const affectedPaths = Object.keys(changes).map((field) => {
      if (!allowed.has(field)) {
        throw new SourceProjectEditError(
          "INVALID_PROJECT_FIELD",
          `project field '${field}' is not editable`
        );
      }
      return `/${field}`;
    });
    return this.#commit("Edit project metadata", affectedPaths, (project) => {
      for (const [field, value] of Object.entries(changes)) {
        // An optional field cleared in the form is an absent field, not a
        // present one holding nothing: the schema would reject the latter.
        const fields = project as unknown as Record<string, unknown>;
        if (value === undefined) delete fields[field];
        else fields[field] = value;
      }
    });
  }

  create<C extends SourceCollectionName>(
    collection: C,
    record: SourceRecord<C>
  ): SourceTransaction {
    this.#assertAvailableId(collection, record.id);
    const index = records(this.#project, collection).length;
    return this.#commit(
      `Create ${collectionCategory[collection]} '${record.id}'`,
      [recordPath(collection, index)],
      (project) => {
        writableRecords(project, collection).push(structuredClone(record));
      }
    );
  }

  /**
   * Several writes as **one act**, undone and redone in one press.
   *
   * An author's act and the records it takes are not the same count. Putting a
   * bandit on a board is one thing a person did, and it can write a weapon
   * type, a weapon, a class, a character and the Stage that places them:
   * five records for one decision. Committed one at a time, that is five undo
   * entries an author has to press through to get back to where they were, and
   * a throw partway leaves the first three standing with nothing referring to
   * them. Neither is a thing the author asked for.
   *
   * So the steps are validated and applied against a draft, and the draft
   * replaces the project only once every step has landed. A step that
   * refuses, on an identifier already taken or a record that is not there,
   * throws before anything is stored, and the project is exactly as it was.
   *
   * Steps see each other: a step may update a record an earlier step created,
   * and two steps may not create the same identifier. That is the whole reason
   * this is not a loop over `create` and `update` at the call site.
   *
   * `rename` and `delete` are deliberately not steps. Both walk every
   * cross-reference in the project to decide what they touch, and both are
   * already single acts an author performs on purpose; grouping them would buy
   * nothing and would make the affected paths a guess.
   */
  transact(label: string, edits: readonly SourceEdit[]): SourceTransaction {
    return this.#commitWith(label, (project) => {
      const affectedPaths: string[] = [];
      for (const edit of edits) {
        if (edit.kind === "create") {
          assertAvailableId(project, edit.collection, edit.record.id);
          const index = records(project, edit.collection).length;
          writableRecords(project, edit.collection).push(
            structuredClone(edit.record) as never
          );
          affectedPaths.push(recordPath(edit.collection, index));
          continue;
        }
        const index = indexOfRecord(project, edit.collection, edit.id);
        const draft = structuredClone(
          records(project, edit.collection)[index]
        ) as SourceRecord<typeof edit.collection>;
        (edit.update as (value: typeof draft) => void)(draft);
        if (draft.id !== edit.id) {
          throw new SourceProjectEditError(
            "INVALID_PROJECT_FIELD",
            "use rename() to change a stable identifier",
            [`${recordPath(edit.collection, index)}/id`]
          );
        }
        writableRecords(project, edit.collection)[index] = draft as never;
        affectedPaths.push(recordPath(edit.collection, index));
      }
      return affectedPaths;
    });
  }

  update<C extends SourceCollectionName>(
    collection: C,
    id: string,
    update: (draft: SourceRecord<C>) => void
  ): SourceTransaction {
    const index = this.#findIndex(collection, id);
    const draft = structuredClone(
      records(this.#project, collection)[index]
    ) as SourceRecord<C>;
    update(draft);
    if (draft.id !== id) {
      throw new SourceProjectEditError(
        "INVALID_PROJECT_FIELD",
        "use rename() to change a stable identifier",
        [`${recordPath(collection, index)}/id`]
      );
    }
    return this.#commit(
      `Edit ${collectionCategory[collection]} '${id}'`,
      [recordPath(collection, index)],
      (project) => {
        writableRecords(project, collection)[index] = draft;
      }
    );
  }

  rename<C extends SourceCollectionName>(
    collection: C,
    id: string,
    nextId: string
  ): SourceTransaction {
    const affectedPaths = this.previewRename(collection, id, nextId);
    return this.#commit(
      `Rename ${collectionCategory[collection]} '${id}' to '${nextId}'`,
      affectedPaths,
      (project) => {
        const index = records(project, collection)
          .findIndex((record) => record.id === id);
        writableRecords(project, collection)[index]!.id = nextId;
        if (collection === "classes") {
          project.unitTypes.forEach((unitType) => {
            if (unitType.classId === id) unitType.classId = nextId;
          });
        }
        if (collection === "weapons") {
          project.unitTypes.forEach((unitType) => {
            if (unitType.startingWeaponIds) {
              unitType.startingWeaponIds = unitType.startingWeaponIds.map(
                (weaponId) => weaponId === id ? nextId : weaponId
              );
            }
          });
        }
        if (collection === "weaponTypes") {
          project.classes.forEach((sourceClass) => {
            if (sourceClass.allowedWeaponTypeIds) {
              sourceClass.allowedWeaponTypeIds =
                sourceClass.allowedWeaponTypeIds.map(
                  (typeId) => typeId === id ? nextId : typeId
                );
            }
          });
          project.weapons.forEach((weapon) => {
            if (weapon.weaponTypeId === id) weapon.weaponTypeId = nextId;
          });
        }
        if (collection === "itemTypes") {
          project.items.forEach((item) => {
            if (item.itemTypeId === id) item.itemTypeId = nextId;
          });
        }
        if (collection === "items") {
          project.unitTypes.forEach((unitType) => {
            if (unitType.startingItemIds) {
              unitType.startingItemIds = unitType.startingItemIds.map(
                (itemId) => itemId === id ? nextId : itemId
              );
            }
            if (unitType.dropItemId === id) unitType.dropItemId = nextId;
          });
        }
        if (collection === "factions") {
          project.unitTypes.forEach((unitType) => {
            if (unitType.factionId === id) unitType.factionId = nextId;
          });
        }
        if (collection === "abilities") {
          project.unitTypes.forEach((unitType) => {
            if (unitType.abilityIds) {
              unitType.abilityIds = unitType.abilityIds.map(
                (abilityId) => abilityId === id ? nextId : abilityId
              );
            }
          });
        }
        if (collection === "objectives") {
          (project.campaigns ?? []).forEach((campaign) => {
            if (campaign.objectiveIds) {
              campaign.objectiveIds = campaign.objectiveIds.map(
                (objectiveId) => objectiveId === id ? nextId : objectiveId
              );
            }
          });
        }
        if (collection === "dialogues") {
          (project.campaigns ?? []).forEach((campaign) => {
            if (campaign.dialogueIds) {
              campaign.dialogueIds = campaign.dialogueIds.map(
                (dialogueId) => dialogueId === id ? nextId : dialogueId
              );
            }
          });
        }
        visitCrossReferences(
          project,
          (referenceCollection, sourceId, _path, replace) => {
            if (referenceCollection === collection && sourceId === id) {
              replace(nextId);
            }
          }
        );
        for (const owner of scriptBindings(project)) {
          owner.bindings.forEach((binding) => {
            binding.parameters.forEach((parameter) => {
              if (
                parameter.value.kind === "contentRef" &&
                parameter.value.category === referenceCategory[collection] &&
                parameter.value.sourceKey === id
              ) {
                parameter.value.sourceKey = nextId;
              }
            });
          });
        }
      }
    );
  }

  previewRename<C extends SourceCollectionName>(
    collection: C,
    id: string,
    nextId: string
  ): readonly string[] {
    const index = this.#findIndex(collection, id);
    this.#assertAvailableId(collection, nextId);
    const affectedPaths = [`${recordPath(collection, index)}/id`];

    if (collection === "classes") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        if (unitType.classId === id) {
          affectedPaths.push(`/unitTypes/${unitIndex}/classId`);
        }
      });
    }
    if (collection === "weapons") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        (unitType.startingWeaponIds ?? []).forEach((weaponId, weaponIndex) => {
          if (weaponId === id) {
            affectedPaths.push(
              `/unitTypes/${unitIndex}/startingWeaponIds/${weaponIndex}`
            );
          }
        });
      });
    }
    if (collection === "weaponTypes") {
      this.#project.classes.forEach((sourceClass, classIndex) => {
        (sourceClass.allowedWeaponTypeIds ?? []).forEach((typeId, typeIndex) => {
          if (typeId === id) {
            affectedPaths.push(
              `/classes/${classIndex}/allowedWeaponTypeIds/${typeIndex}`
            );
          }
        });
      });
      this.#project.weapons.forEach((weapon, weaponIndex) => {
        if (weapon.weaponTypeId === id) {
          affectedPaths.push(`/weapons/${weaponIndex}/weaponTypeId`);
        }
      });
    }
    if (collection === "itemTypes") {
      this.#project.items.forEach((item, itemIndex) => {
        if (item.itemTypeId === id) {
          affectedPaths.push(`/items/${itemIndex}/itemTypeId`);
        }
      });
    }
    if (collection === "items") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        (unitType.startingItemIds ?? []).forEach((itemId, itemIndex) => {
          if (itemId === id) {
            affectedPaths.push(
              `/unitTypes/${unitIndex}/startingItemIds/${itemIndex}`
            );
          }
        });
        if (unitType.dropItemId === id) {
          affectedPaths.push(`/unitTypes/${unitIndex}/dropItemId`);
        }
      });
    }
    if (collection === "factions") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        if (unitType.factionId === id) {
          affectedPaths.push(`/unitTypes/${unitIndex}/factionId`);
        }
      });
    }
    if (collection === "abilities") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        (unitType.abilityIds ?? []).forEach((abilityId, abilityIndex) => {
          if (abilityId === id) {
            affectedPaths.push(
              `/unitTypes/${unitIndex}/abilityIds/${abilityIndex}`
            );
          }
        });
      });
    }
    if (collection === "objectives") {
      (this.#project.campaigns ?? []).forEach((campaign, campaignIndex) => {
        (campaign.objectiveIds ?? []).forEach((objectiveId, objectiveIndex) => {
          if (objectiveId === id) {
            affectedPaths.push(
              `/campaigns/${campaignIndex}/objectiveIds/${objectiveIndex}`
            );
          }
        });
      });
    }
    if (collection === "dialogues") {
      (this.#project.campaigns ?? []).forEach((campaign, campaignIndex) => {
        (campaign.dialogueIds ?? []).forEach((dialogueId, dialogueIndex) => {
          if (dialogueId === id) {
            affectedPaths.push(
              `/campaigns/${campaignIndex}/dialogueIds/${dialogueIndex}`
            );
          }
        });
      });
    }
    visitCrossReferences(
      this.#project,
      (referenceCollection, sourceId, path) => {
        if (referenceCollection === collection && sourceId === id) {
          affectedPaths.push(path);
        }
      }
    );
    for (const owner of scriptBindings(this.#project)) {
      owner.bindings.forEach((binding, bindingIndex) => {
        binding.parameters.forEach((parameter, parameterIndex) => {
          if (
            parameter.value.kind === "contentRef" &&
            parameter.value.category === referenceCategory[collection] &&
            parameter.value.sourceKey === id
          ) {
            affectedPaths.push(
              `${owner.ownerPath}/scriptBindings/${bindingIndex}` +
              `/parameters/${parameterIndex}/value/sourceKey`
            );
          }
        });
      });
    }

    return affectedPaths;
  }

  delete<C extends SourceCollectionName>(
    collection: C,
    id: string
  ): SourceTransaction {
    const index = this.#findIndex(collection, id);
    const inbound = this.#inboundPaths(collection, id);
    if (inbound.length > 0) {
      throw new SourceProjectEditError(
        "DELETE_REFERENCED",
        `cannot delete ${collectionCategory[collection]} '${id}' while it is referenced`,
        inbound
      );
    }
    return this.#commit(
      `Delete ${collectionCategory[collection]} '${id}'`,
      [recordPath(collection, index)],
      (project) => {
        writableRecords(project, collection).splice(index, 1);
      }
    );
  }

  undo(): SourceTransaction | undefined {
    const entry = this.#undo.pop();
    if (!entry) return undefined;
    this.#project = copy(entry.before);
    this.#redo.push(entry);
    return entry.transaction;
  }

  redo(): SourceTransaction | undefined {
    const entry = this.#redo.pop();
    if (!entry) return undefined;
    this.#project = copy(entry.after);
    this.#undo.push(entry);
    return entry.transaction;
  }

  #commit(
    label: string,
    affectedPaths: readonly string[],
    mutate: (project: SourceProject) => void
  ): SourceTransaction {
    return this.#commitWith(label, (project) => {
      mutate(project);
      return affectedPaths;
    });
  }

  /**
   * One act, whatever it takes to write it.
   *
   * The draft is mutated and only assigned once it is whole, so a `mutate` that
   * throws halfway leaves the project exactly as it was and pushes nothing onto
   * the undo stack. That is what lets `transact` below refuse a bad step
   * without leaving the records the good steps already wrote behind it.
   */
  #commitWith(
    label: string,
    mutate: (project: SourceProject) => readonly string[]
  ): SourceTransaction {
    const before = copy(this.#project);
    const after = copy(this.#project);
    const affectedPaths = mutate(after);
    const transaction = {
      label,
      affectedPaths: [...new Set(affectedPaths)]
    };
    this.#project = after;
    this.#undo.push({ transaction, before, after: copy(after) });
    this.#redo.length = 0;
    return transaction;
  }

  #findIndex<C extends SourceCollectionName>(collection: C, id: string): number {
    return indexOfRecord(this.#project, collection, id);
  }

  #assertAvailableId(collection: SourceCollectionName, id: string) {
    assertAvailableId(this.#project, collection, id);
  }

  #inboundPaths(collection: SourceCollectionName, id: string): string[] {
    const inbound: string[] = [];
    if (collection === "classes") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        if (unitType.classId === id) inbound.push(`/unitTypes/${unitIndex}/classId`);
      });
    }
    if (collection === "weapons") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        (unitType.startingWeaponIds ?? []).forEach((weaponId, weaponIndex) => {
          if (weaponId === id) {
            inbound.push(
              `/unitTypes/${unitIndex}/startingWeaponIds/${weaponIndex}`
            );
          }
        });
      });
    }
    if (collection === "weaponTypes") {
      this.#project.classes.forEach((sourceClass, classIndex) => {
        (sourceClass.allowedWeaponTypeIds ?? []).forEach((typeId, typeIndex) => {
          if (typeId === id) {
            inbound.push(
              `/classes/${classIndex}/allowedWeaponTypeIds/${typeIndex}`
            );
          }
        });
      });
      this.#project.weapons.forEach((weapon, weaponIndex) => {
        if (weapon.weaponTypeId === id) {
          inbound.push(`/weapons/${weaponIndex}/weaponTypeId`);
        }
      });
    }
    if (collection === "itemTypes") {
      this.#project.items.forEach((item, itemIndex) => {
        if (item.itemTypeId === id) {
          inbound.push(`/items/${itemIndex}/itemTypeId`);
        }
      });
    }
    if (collection === "items") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        (unitType.startingItemIds ?? []).forEach((itemId, itemIndex) => {
          if (itemId === id) {
            inbound.push(
              `/unitTypes/${unitIndex}/startingItemIds/${itemIndex}`
            );
          }
        });
        if (unitType.dropItemId === id) {
          inbound.push(`/unitTypes/${unitIndex}/dropItemId`);
        }
      });
    }
    if (collection === "factions") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        if (unitType.factionId === id) {
          inbound.push(`/unitTypes/${unitIndex}/factionId`);
        }
      });
    }
    if (collection === "abilities") {
      this.#project.unitTypes.forEach((unitType, unitIndex) => {
        (unitType.abilityIds ?? []).forEach((abilityId, abilityIndex) => {
          if (abilityId === id) {
            inbound.push(`/unitTypes/${unitIndex}/abilityIds/${abilityIndex}`);
          }
        });
      });
    }
    if (collection === "objectives") {
      (this.#project.campaigns ?? []).forEach((campaign, campaignIndex) => {
        (campaign.objectiveIds ?? []).forEach((objectiveId, objectiveIndex) => {
          if (objectiveId === id) {
            inbound.push(
              `/campaigns/${campaignIndex}/objectiveIds/${objectiveIndex}`
            );
          }
        });
      });
    }
    if (collection === "dialogues") {
      (this.#project.campaigns ?? []).forEach((campaign, campaignIndex) => {
        (campaign.dialogueIds ?? []).forEach((dialogueId, dialogueIndex) => {
          if (dialogueId === id) {
            inbound.push(
              `/campaigns/${campaignIndex}/dialogueIds/${dialogueIndex}`
            );
          }
        });
      });
    }
    visitCrossReferences(
      this.#project,
      (referenceCollection, sourceId, path) => {
        if (referenceCollection === collection && sourceId === id) {
          inbound.push(path);
        }
      }
    );
    for (const owner of scriptBindings(this.#project)) {
      owner.bindings.forEach((binding, bindingIndex) => {
        binding.parameters.forEach((parameter, parameterIndex) => {
          if (
            parameter.value.kind === "contentRef" &&
            parameter.value.category === referenceCategory[collection] &&
            parameter.value.sourceKey === id
          ) {
            inbound.push(
              `${owner.ownerPath}/scriptBindings/${bindingIndex}` +
              `/parameters/${parameterIndex}/value/sourceKey`
            );
          }
        });
      });
    }
    return inbound;
  }
}
