// SPDX-License-Identifier: MIT

// What happens when the project file on disk is older than the editor opening
// it.
//
// The save format solved this problem first and argued it out in
// `engine/campaign/include/grandleon/campaign/migration.hpp`. This is the same
// registry for the authoring source, and it keeps that module's four rules
// rather than inventing a second philosophy for the same question:
//
// * **Version by version, never in a leap.** A step goes from one version to
//   the next and no further. A project at `1.0.0` reaching an editor at `1.2.0`
//   runs 1.0.0 -> 1.1.0 and then 1.1.0 -> 1.2.0, each separately registered
//   with its own test. One function per (old, new) pair is quadratic in the
//   number of versions and gives the pair nobody thought to write no diagnostic
//   at all.
// * **A gap is `missing_step`, and it is named.** `planUpgrade` walks the chain
//   one version at a time and stops at the first version with no step out of
//   it, saying which version that was. Never a silent skip, and never running
//   the far end of the chain against the near end's fields.
// * **Downgrade is refused by name.** A project written by a newer editor knows
//   things this one does not, and the only honest transform from a newer format
//   to an older one is a lossy one. `downgrade_refused` says so instead of
//   dropping the fields this build cannot see.
// * **Old project in, new project out.** No step mutates anything. A step is
//   handed a deep copy and returns the next version's project; the caller's
//   original is untouched whatever happens, so a chain that fails at its third
//   step leaves no half-upgraded game anywhere.
//
// ## Where the current version lives
//
// Here, and it is derived rather than declared: a version exists because a step
// arrives at it. `SourceMigrationRegistry.versions()` is `FIRST_SOURCE_VERSION`
// followed by every step's destination, in order, and `current()` is the last
// of them. That is what makes the sentence below true rather than merely
// intended: you cannot add a version without adding the step that reaches it,
// and you cannot add a step without saying what changed.
//
// Every other site that spells the version out (the schema's `const`, the
// native compiler's refusal, the project the editor makes for a new game) is
// pinned to this one by `test.mjs`, which the gate runs. JSON Schema and C++
// cannot import a JavaScript module, so the copies exist; what must not exist
// is a copy that has drifted.
//
// ## Why here and not in the engine
//
// The editor's load path is pure TypeScript and reaches no WebAssembly: a
// project is parsed, validated by the generated Ajv validator, and handed to a
// session, none of which waits for the simulation module to instantiate. A
// migration in C++ would be unavailable at exactly the moment it is needed. The
// native compiler keeps refusing anything that is not current, and names what
// it found, what it wants, and how to bring the file up.

/** The first version of the authoring source format. */
export const FIRST_SOURCE_VERSION = "1.0.0";

// A chain no longer than this. Far above any plausible format history, for the
// same reason the save registry's limit is: the number exists to make a hostile
// input cheap, not to express a policy about how often a format may move.
export const MAXIMUM_MIGRATION_STEPS = 64;

/**
 * Why an upgrade did not happen. Written into diagnostics and read by tests, so
 * append only.
 */
export const MigrationRefusal = Object.freeze({
  /** The text where a version belongs is not one. */
  unreadable_version: "unreadable_version",
  /** The project is newer than this build. Refused rather than attempted. */
  downgrade_refused: "downgrade_refused",
  /** The chain has a hole: no registered step leaves the version named. */
  missing_step: "missing_step",
  /** A registered step ran and threw. Nothing was kept. */
  step_failed: "step_failed",
  /** A chain longer than `MAXIMUM_MIGRATION_STEPS`. */
  step_limit_exceeded: "step_limit_exceeded"
});

/**
 * @typedef {keyof typeof MigrationRefusal} MigrationRefusalName
 *
 * @typedef {{ readonly schemaVersion?: string } & Record<string, unknown>}
 *   VersionedProject
 *
 * @typedef {object} SourceMigration
 * @property {string} from
 * @property {string} to
 * @property {string} changed
 * @property {(project: VersionedProject) => VersionedProject} apply
 *
 * @typedef {object} UpgradePlanned
 * @property {true} ok
 * @property {string} from
 * @property {string} to
 * @property {readonly SourceMigration[]} steps
 *
 * @typedef {object} UpgradeRefused
 * @property {false} ok
 * @property {MigrationRefusalName} refusal
 * @property {string} from
 * @property {string} to
 * @property {string} [stoppedAt]
 * @property {string} [detail]
 *
 * @typedef {UpgradePlanned | UpgradeRefused} UpgradePlan
 *
 * @typedef {object} UpgradeDone
 * @property {true} ok
 * @property {string} from
 * @property {string} to
 * @property {VersionedProject} project
 * @property {readonly SourceMigration[]} applied
 * @property {readonly string[]} changed
 *
 * @typedef {UpgradeRefused & { project: undefined, changed: readonly string[] }}
 *   UpgradeStopped
 *
 * @typedef {UpgradeDone | UpgradeStopped} UpgradeResult
 */

const VERSION_PATTERN = /^(\d+)\.(\d+)\.(\d+)$/;

/**
 * The three numbers, or undefined when the text is not a version.
 * @param {unknown} text
 * @returns {[number, number, number] | undefined}
 */
export function parseVersion(text) {
  if (typeof text !== "string") return undefined;
  const match = VERSION_PATTERN.exec(text);
  if (!match) return undefined;
  return [Number(match[1]), Number(match[2]), Number(match[3])];
}

/**
 * Negative, zero or positive, as `left` sorts before, with, or after `right`.
 * @param {string} left
 * @param {string} right
 * @returns {number}
 */
export function compareVersions(left, right) {
  const one = parseVersion(left);
  const other = parseVersion(right);
  if (!one || !other) throw new TypeError("compareVersions needs two versions");
  for (let index = 0; index < 3; index += 1) {
    if (one[index] !== other[index]) return one[index] - other[index];
  }
  return 0;
}

/**
 * One step, and the sentence that goes with it.
 *
 * `changed` is not documentation. It is the line an author reads in the dialog
 * that asks whether to bring their game up to date, so it is written for
 * somebody who has never read the source format: "turn order moved onto the
 * Stage", not "encounter.turnOrder added". A step without one cannot be built,
 * because a dialog that lists nothing is a dialog that asks the author to
 * consent to something nobody described. It is the same rule that stops a
 * thirteenth collection existing without a word for one of it.
 *
 * `to` is only required to be later than `from`, not to be its numeric
 * successor, and that is not a weaker rule than the save registry's: it is the
 * same rule read against strings instead of integers. A version exists because
 * a step arrives at it, so a step from `1.0.0` to `3.0.0` skips nothing: there
 * was never a `2.0.0` for it to skip. What is forbidden is a second step out of
 * `1.0.0`, and that is refused by name.
 *
 * @param {SourceMigration} step
 * @returns {SourceMigration}
 */
export function defineMigration({ from, to, changed, apply }) {
  if (!parseVersion(from)) {
    throw new TypeError(
      `a migration's 'from' must be a version, not ${JSON.stringify(from)}`
    );
  }
  if (!parseVersion(to)) {
    throw new TypeError(
      `a migration's 'to' must be a version, not ${JSON.stringify(to)}`
    );
  }
  if (compareVersions(from, to) >= 0) {
    throw new TypeError(`a migration must go forwards; ${from} -> ${to} does not`);
  }
  if (typeof changed !== "string" || changed.trim() === "") {
    throw new TypeError(
      `the ${from} -> ${to} migration must say what changed, in a sentence an `
        + "author can read"
    );
  }
  if (typeof apply !== "function") {
    throw new TypeError(`the ${from} -> ${to} migration must have something to apply`);
  }
  return Object.freeze({ from, to, changed: changed.trim(), apply });
}

/**
 * Every step this build knows.
 *
 * Registration is by the version a step leaves, and registering two steps out
 * of one version is refused rather than resolved: two functions claiming one
 * version is an ambiguity nobody could settle later, and the second one
 * silently winning is the worst of the three possible answers.
 */
export class SourceMigrationRegistry {
  /** @type {SourceMigration[]} */
  #steps = [];

  /**
   * Returns this registry, so a chain can be built in one expression.
   * @param {SourceMigration} step
   * @returns {SourceMigrationRegistry}
   */
  add(step) {
    const one = defineMigration(step);
    if (this.#steps.some((held) => held.from === one.from)) {
      throw new Error(`a migration out of ${one.from} is already registered`);
    }
    this.#steps.push(one);
    return this;
  }

  /**
   * The step out of `version`, or undefined when the chain stops there.
   * @param {string} version
   * @returns {SourceMigration | undefined}
   */
  find(version) {
    return this.#steps.find((step) => step.from === version);
  }

  /**
   * Every version a step arrives at, and the one they all start from, oldest
   * first.
   *
   * Sorted rather than taken in registration order: a step is registered by the
   * version it leaves, and nothing about writing `add` calls in one order makes
   * that the order of the format's history. Sorting means the last entry is the
   * newest whatever order the file is written in, and a chain with a hole in it
   * still reports its real end, which is what lets `planUpgrade` find the hole
   * rather than stop short of it and call the project current.
   *
   * @returns {string[]}
   */
  versions() {
    const reached = this.#steps.map((step) => step.to);
    const all = [FIRST_SOURCE_VERSION, ...reached];
    return [...new Set(all)].sort(compareVersions);
  }

  /**
   * The version this build writes. The last one a step arrives at.
   * @returns {string}
   */
  current() {
    const versions = this.versions();
    return /** @type {string} */ (versions[versions.length - 1]);
  }

  /** @returns {number} */
  get size() {
    return this.#steps.length;
  }
}

/**
 * The steps that lead from `from` to this build's current version, or the
 * reason there are none. Pure: it consults the registry and runs nothing.
 *
 * Exposed rather than kept private because "can this editor open that file?" is
 * a question worth answering before offering to, and because the chain-walking
 * rule is worth testing on its own. A two-step path, a hole in the middle of
 * one, and a backwards one are three different answers and none of them needs a
 * project to demonstrate.
 *
 * @param {string} from
 * @returns {UpgradePlan}
 */
export function planUpgrade(registry, from) {
  const current = registry.current();
  if (!parseVersion(from)) {
    return {
      ok: false,
      refusal: MigrationRefusal.unreadable_version,
      from,
      to: current
    };
  }
  const order = compareVersions(from, current);
  if (order > 0) {
    return {
      ok: false,
      refusal: MigrationRefusal.downgrade_refused,
      from,
      to: current
    };
  }
  if (order === 0) return { ok: true, from, to: current, steps: [] };

  const steps = [];
  let at = from;
  while (compareVersions(at, current) < 0) {
    if (steps.length >= MAXIMUM_MIGRATION_STEPS) {
      return {
        ok: false,
        refusal: MigrationRefusal.step_limit_exceeded,
        from,
        to: current,
        stoppedAt: at
      };
    }
    const step = registry.find(at);
    if (!step) {
      return {
        ok: false,
        refusal: MigrationRefusal.missing_step,
        from,
        to: current,
        stoppedAt: at
      };
    }
    steps.push(step);
    at = step.to;
  }
  return { ok: true, from, to: current, steps };
}

/**
 * What each step of a plan will tell the author, in the order they run.
 * @param {UpgradePlan} plan
 * @returns {readonly string[]}
 */
export function planChanges(plan) {
  return plan.ok ? plan.steps.map((step) => step.changed) : [];
}

/**
 * The version a project declares. Empty when it declares nothing readable,
 * which every refusal below treats the same way as text that is not a version:
 * an unversioned file is not an old file, it is a file nothing can place.
 *
 * @param {unknown} project
 * @returns {string}
 */
export function projectVersion(project) {
  const declared = project !== null && typeof project === "object"
    ? /** @type {Record<string, unknown>} */ (project).schemaVersion
    : undefined;
  return typeof declared === "string" ? declared : "";
}

/**
 * Bring `project` up to this build's version, or say why not.
 *
 * `project` is read and never touched. Each step is handed a deep copy of the
 * candidate before it and returns the next one, so a chain that refuses at its
 * third step leaves the caller holding exactly what they passed in. On success
 * the returned project declares the new version: a step does not have to
 * remember to write it, and could not be trusted to.
 *
 * @param {SourceMigrationRegistry} registry
 * @param {VersionedProject} project
 * @returns {UpgradeResult}
 */
export function upgradeProject(registry, project) {
  const plan = planUpgrade(registry, projectVersion(project));
  if (!plan.ok) return { ...plan, project: undefined, changed: [] };

  let candidate = structuredClone(project);
  const applied = [];
  for (const step of plan.steps) {
    let next;
    try {
      next = step.apply(structuredClone(candidate));
    } catch (error) {
      return {
        ok: false,
        refusal: MigrationRefusal.step_failed,
        from: plan.from,
        to: plan.to,
        stoppedAt: step.from,
        detail: error instanceof Error ? error.message : String(error),
        project: undefined,
        changed: []
      };
    }
    if (next === null || typeof next !== "object" || Array.isArray(next)) {
      return {
        ok: false,
        refusal: MigrationRefusal.step_failed,
        from: plan.from,
        to: plan.to,
        stoppedAt: step.from,
        detail: `the ${step.from} -> ${step.to} migration did not return a project`,
        project: undefined,
        changed: []
      };
    }
    candidate = next;
    candidate.schemaVersion = step.to;
    applied.push(step);
  }
  return {
    ok: true,
    from: plan.from,
    to: plan.to,
    project: candidate,
    applied,
    changed: applied.map((step) => step.changed)
  };
}

/**
 * Every step this build ships.
 *
 * There are none. `1.0.0` is the first version of the source format, so there
 * is nothing to come from. The seam is here, tested, and waiting for the first
 * schema change to register the step that answers for it. `CONTRIBUTING.md`
 * says what that costs.
 *
 * @returns {SourceMigrationRegistry}
 */
export function sourceMigrations() {
  return new SourceMigrationRegistry();
}

/** The version this build writes into a new project and demands of an old one. */
export const CURRENT_SOURCE_VERSION = sourceMigrations().current();
