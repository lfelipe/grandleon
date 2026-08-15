// SPDX-License-Identifier: MIT
/**
 * Whether the game an author just opened was made with this Grandleon, and what
 * to tell them when it was not.
 *
 * The registry itself lives in `tools/source_schema/migration.mjs`, shared with
 * the command-line tool that upgrades a file on disk, so that both roads run
 * the same steps in the same order. This module is the editor's half: it turns
 * a refusal into a sentence for somebody who has never read the source format,
 * and it is the only place in the editor that does.
 *
 * Nothing here says "schema", "migration" or "version constant". An author has
 * a game that was made with one Grandleon and is being opened in another, and
 * that is the whole of what they need to know.
 */
import {
  CURRENT_SOURCE_VERSION,
  parseVersion,
  planChanges,
  planUpgrade,
  projectVersion,
  sourceMigrations,
  upgradeProject
} from "../../../tools/source_schema/migration.mjs";
import type { SourceProject } from "../generated/source-v1";

export { CURRENT_SOURCE_VERSION };

/** A game made with this Grandleon. Nothing to do. */
export interface ProjectIsCurrent {
  readonly kind: "current";
}

/**
 * A game made with an older Grandleon that this one can bring up.
 *
 * `changed` is the list the author is shown before anything happens: one
 * sentence per step, in the order the steps run. It is never empty: a step
 * cannot exist without a sentence, so a chain with something to do always has
 * something to say about it.
 */
export interface ProjectIsBehind {
  readonly kind: "behind";
  readonly madeWith: string;
  readonly needs: string;
  readonly changed: readonly string[];
}

/** A game this Grandleon will not open, and why, in one sentence. */
export interface ProjectIsRefused {
  readonly kind: "refused";
  readonly madeWith: string;
  readonly sentence: string;
}

export type ProjectAge = ProjectIsCurrent | ProjectIsBehind | ProjectIsRefused;

/** What a refusal means, for somebody making a game rather than reading one. */
function refusalSentence(
  refusal: string,
  from: string,
  to: string,
  stoppedAt: string | undefined
): string {
  switch (refusal) {
    case "downgrade_refused":
      return `This game was made with a newer Grandleon (${from}). This one `
        + `makes games at ${to}. Update Grandleon to open it. Opening it here `
        + "could only be done by throwing away the parts this version does not "
        + "know about.";
    case "missing_step":
      return `This game was made with Grandleon ${from}, and this one has no `
        + `way to bring a game up from ${stoppedAt ?? from}. Nothing was `
        + "changed.";
    case "step_limit_exceeded":
      return `This game claims to be from Grandleon ${from}, which is too far `
        + "away to be real. Nothing was changed.";
    case "step_failed":
      return `Bringing this game up from ${stoppedAt ?? from} did not work, so `
        + "nothing was changed. The file is exactly as it was.";
    case "unreadable_version":
    default:
      // The default and `unreadable_version` are one answer on purpose. A
      // refusal this module has no sentence for is a refusal nobody has placed,
      // and "we cannot tell what this is" is the honest thing to say about a
      // file nobody has placed.
      return "This file does not say which Grandleon made it, so there is no "
        + "way to tell what it is.";
  }
}

/**
 * How old the game in `parsed` is, and what can be done about it.
 *
 * Asked before the schema is: the schema describes the version this build
 * writes, so a game from any other version fails it, and "must be equal to
 * constant" is not an answer anybody can act on. Placing the game first is what
 * turns that into an offer.
 */
export function projectAge(parsed: unknown): ProjectAge {
  const declared = projectVersion(parsed);
  if (declared === CURRENT_SOURCE_VERSION) return { kind: "current" };

  const plan = planUpgrade(sourceMigrations(), declared);
  if (!plan.ok) {
    return {
      kind: "refused",
      // Empty unless the file named a version. Text where a version belongs is
      // not a version the editor may repeat back as one: "made with Grandleon
      // not a version" is a sentence that reads as though the file said
      // something, and it did not.
      madeWith: parseVersion(declared) ? declared : "",
      sentence: refusalSentence(plan.refusal, plan.from, plan.to, plan.stoppedAt)
    };
  }
  return {
    kind: "behind",
    madeWith: plan.from,
    needs: plan.to,
    changed: planChanges(plan)
  };
}

/** A game brought up to date, or the sentence explaining why it was not. */
export type UpgradeOutcome =
  | {
    readonly ok: true;
    readonly project: SourceProject;
    readonly changed: readonly string[];
  }
  | { readonly ok: false; readonly sentence: string };

/**
 * Run the steps.
 *
 * The project handed in is read and never touched, and the one handed back is a
 * new object, so an upgrade that refuses half way through leaves the author
 * holding exactly the game they opened. Nothing on disk moves either way; this
 * upgrades what is on screen, and the author's save is what commits it.
 */
export function bringUpToDate(parsed: unknown): UpgradeOutcome {
  const result = upgradeProject(sourceMigrations(), parsed as never);
  if (!result.ok) {
    return {
      ok: false,
      sentence: refusalSentence(
        result.refusal,
        result.from,
        result.to,
        result.stoppedAt
      )
    };
  }
  return {
    ok: true,
    project: result.project as unknown as SourceProject,
    changed: result.changed
  };
}
