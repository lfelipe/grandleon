// SPDX-License-Identifier: MIT
import {
  analyzeSourceProject,
  type SourceAnalysis,
  type SourceDiagnostic
} from "../analysis/source-analysis";
import type { SourceProject } from "../generated/source-v1";
import { NEW_PROJECT_TURN_ORDER } from "./game-settings";
import {
  bringUpToDate,
  CURRENT_SOURCE_VERSION,
  projectAge,
  type ProjectIsBehind,
  type ProjectIsRefused,
  type UpgradeOutcome
} from "./source-migration";
import type { ProjectSnapshot, ProjectStore } from "./project-store";
import {
  exportProjectArchive,
  readProjectArchive
} from "../platform/project-archive";
import { identifierFromName } from "./source-form-model";

/** What a game is called before its author has called it anything. */
const NEW_PROJECT_TITLE = "Untitled Game";

export const sourceProjectPath = "project.json";

/**
 * The name the file carries, made out of the name a player reads.
 *
 * `gameId` is question two of the whole product and it means nothing before a
 * game exists, so nobody is asked it: the title answers it. A first author who
 * never opens Advanced still exports `the_salt_road.z64` rather than
 * `new.game.z64`.
 *
 * `unnamed_game` is what a title with no letters or digits in it falls back to.
 * It is a fallback and not a default: the title is required, so reaching it
 * takes a title made entirely of punctuation.
 */
export function gameIdFromTitle(title: string): string {
  return identifierFromName(title, "unnamed_game");
}

/**
 * The `gameId` a renamed game should carry.
 *
 * Deriving must not surprise, and the surprise to avoid is an author who chose
 * an id under Advanced finding it silently rewritten the next time they touch
 * the title. So the tie holds only while it is still visibly a tie: an id that
 * is exactly what the old title derived was following the title and keeps
 * following it, and an id that is anything else was chosen and is left alone.
 * No flag is stored for this. The two names are the whole of the evidence,
 * which is what keeps the rule true for a project written by another build.
 *
 * Nothing else in the editor is made of `gameId`. It reaches no package byte,
 * no content hash and no save slot, a kept campaign being filed under
 * `packageId` (`campaign-slot-store.ts`), so a rename here repoints one thing, the
 * name an export downloads under, and repointing that is the whole point.
 */
export function gameIdFollowingTitle(
  previous: { readonly title: string; readonly gameId: string },
  next: { readonly title: string; readonly gameId: string }
): string {
  if (next.gameId !== previous.gameId) return next.gameId;
  if (previous.gameId !== gameIdFromTitle(previous.title)) return previous.gameId;
  return gameIdFromTitle(next.title);
}

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

/**
 * A project with nothing in it, and the one setting a new game is not left to
 * guess at.
 *
 * The turn order is written rather than omitted. An omitted one means
 * alternating, and alternating is how a first battle comes to look broken: a
 * side moves one character and the turn changes hands. See
 * `NEW_PROJECT_TURN_ORDER`, which is where that reasoning lives; here it is
 * only stated in the file, which is what makes it an ordinary setting the
 * author can change rather than a behaviour they have to discover.
 */
export function createSourceProject(): SourceProject {
  return {
    // The version this build writes, asked for rather than restated. The
    // registry in `tools/source_schema` is where it lives, and
    // `tools/source_schema/test.mjs` holds the schema, the native compiler and
    // this line to it.
    schemaVersion: CURRENT_SOURCE_VERSION as SourceProject["schemaVersion"],
    packageId: globalThis.crypto?.randomUUID?.() ??
      "00000000-0000-4000-8000-000000000001",
    // Derived from the title beside it rather than written, so the one rule
    // that keeps the two names together holds from the project's first
    // moment: a game nobody has renamed yet already files under its own name.
    gameId: gameIdFromTitle(NEW_PROJECT_TITLE),
    title: NEW_PROJECT_TITLE,
    contentRevision: "0.1.0",
    defaultTurnOrder: NEW_PROJECT_TURN_ORDER,
    weaponTypes: [],
    itemTypes: [],
    classes: [],
    unitTypes: [],
    weapons: [],
    items: [],
    maps: [],
    factions: [],
    abilities: [],
    objectives: [],
    campaigns: [],
    dialogues: []
  };
}

export function encodeSourceProject(project: SourceProject): Uint8Array {
  return encoder.encode(`${JSON.stringify(project, null, 2)}\n`);
}

/**
 * The two problems that decide whether a project file can be read back at all:
 * text that is not JSON, and JSON the source schema refuses. Everything else
 * the analyzer reports is a problem *in* a project that still opens: a
 * reference naming nothing, a company with no founding member, a terrain grid
 * of the wrong length. Those belong on the diagnostics page rather than in the way of an
 * author saving what they have so far.
 *
 * This set is what makes the write gate and the read gate one rule: the editor
 * refuses to write exactly what it would refuse to read, so a save can never
 * produce a file the next open cannot.
 */
const unreadableCodes: ReadonlySet<SourceDiagnostic["code"]> = new Set([
  "SOURCE_JSON_INVALID",
  "SOURCE_SCHEMA_INVALID"
]);

/** What stops this text being a project, in the order the analyzer found it. */
export function unreadableProblems(text: string): readonly SourceDiagnostic[] {
  return analyzeSourceProject(sourceProjectPath, text).diagnostics
    .filter((diagnostic) => unreadableCodes.has(diagnostic.code));
}

/** One problem, named by where it is and what to do about it. */
export function describeProblem(problem: SourceDiagnostic): string {
  const where = problem.instancePath === "/" ? "the project" : problem.instancePath;
  return `${where}: ${problem.message}`;
}

function describeProblems(problems: readonly SourceDiagnostic[]): string {
  const first = describeProblem(problems[0]!);
  return problems.length === 1
    ? first
    : `${first} (and ${problems.length - 1} more problem${
      problems.length === 2 ? "" : "s"})`;
}

/**
 * A project the editor declined to write, because writing it would have
 * replaced the author's stored game with a file the editor cannot open again.
 * Nothing was written when this is thrown: the stored draft is exactly as it
 * was.
 */
export class UnwritableProjectError extends Error {
  constructor(readonly problems: readonly SourceDiagnostic[]) {
    super(describeProblems(problems));
    this.name = "UnwritableProjectError";
  }
}

export function decodeSourceProject(bytes: Uint8Array): SourceProject {
  const text = decoder.decode(bytes);
  const problems = unreadableProblems(text);
  if (problems.length > 0) {
    throw new Error(`project.json is invalid: ${describeProblems(problems)}`);
  }
  return JSON.parse(text) as SourceProject;
}

/**
 * A game that opened, or a game from another Grandleon.
 *
 * The second is not a problem with the file. It is a file this build was never
 * going to be able to read as it stands, which is a different thing and gets a
 * different answer. It carries the project exactly as written, because bringing
 * it up to date is the author's decision and the fields the steps need are in
 * there.
 */
export type OpenedSourceProject =
  | { readonly kind: "project"; readonly project: SourceProject }
  | {
    readonly kind: "otherVersion";
    readonly written: unknown;
    readonly age: ProjectIsBehind | ProjectIsRefused;
  };

/**
 * Read bytes as a game, placing it before validating it.
 *
 * The order matters and is the whole reason this exists beside
 * `decodeSourceProject`. The bundled schema describes the version this build
 * writes, so a game from any other version fails it, and what the author would
 * be shown is `/schemaVersion must be equal to constant`: true, useless, and
 * indistinguishable from a corrupt file. Asking how old the game is first turns
 * the same fact into an offer to bring it up, or into a sentence naming the
 * Grandleon that made it.
 */
export function openSourceProject(bytes: Uint8Array): OpenedSourceProject {
  const text = decoder.decode(bytes);
  let written: unknown;
  try {
    written = JSON.parse(text);
  } catch {
    // Not JSON at all. The analyzer says where, in its own words.
    throw new Error(
      `project.json is invalid: ${describeProblems(unreadableProblems(text))}`
    );
  }
  const age = projectAge(written);
  if (age.kind !== "current") return { kind: "otherVersion", written, age };
  return { kind: "project", project: decodeSourceProject(bytes) };
}

/**
 * Bring a game up to this Grandleon, and prove it opens before handing it back.
 *
 * The second half is not belt and braces. A step is a function somebody wrote,
 * and one that produces a game the editor cannot read would otherwise be found
 * at the author's next save, which is the one moment there is no good answer,
 * because by then the game they opened is gone. Checking here means a step that
 * gets it wrong costs a sentence and nothing else.
 */
export function bringProjectUpToDate(written: unknown): UpgradeOutcome {
  const upgraded = bringUpToDate(written);
  if (!upgraded.ok) return upgraded;
  const problems = unreadableProblems(
    decoder.decode(encodeSourceProject(upgraded.project))
  );
  if (problems.length > 0) {
    return {
      ok: false,
      sentence: "Bringing this game up to date produced something this "
        + `Grandleon cannot open: ${describeProblems(problems)}. Nothing was `
        + "changed."
    };
  }
  return upgraded;
}

export interface LoadedSourceProject {
  readonly project: SourceProject;
  readonly fileRevision: number | undefined;
}

/**
 * A stored or opened game from another Grandleon, held until the author says
 * what to do about it. Nothing has been written and nothing has been replaced.
 */
export interface OtherVersionSourceProject {
  readonly otherVersion: true;
  readonly written: unknown;
  readonly age: ProjectIsBehind | ProjectIsRefused;
  readonly fileRevision: number | undefined;
  /**
   * The bytes exactly as they were read, carried for the same reason
   * `UnreadableSourceProject` carries them: when these came out of the store
   * they are the author's only copy, and an author who declines to bring their
   * game up has not agreed to lose it to the next thing they save.
   */
  readonly bytes: Uint8Array;
}

/**
 * A stored draft that no longer decodes or validates. The raw bytes are the
 * author's only copy of their work, so they are handed back for download and
 * repair instead of being replaced by a blank project.
 */
export interface UnreadableSourceProject {
  readonly unreadable: true;
  readonly bytes: Uint8Array;
  readonly fileRevision: number;
  readonly reason: string;
}

/**
 * Whether these bytes are a ZIP rather than a bare project.
 *
 * The local file header signature is the whole test: a JSON project begins with
 * a brace, and an archive begins with "PK\x03\x04". Guessing from a filename
 * would be guessing from something the author can rename.
 */
function isZipArchive(bytes: Uint8Array): boolean {
  return bytes.length >= 4 &&
    bytes[0] === 0x50 && bytes[1] === 0x4b &&
    bytes[2] === 0x03 && bytes[3] === 0x04;
}

function projectEntry(snapshot: ProjectSnapshot): Uint8Array {
  const source = snapshot.files.find((file) => file.path === sourceProjectPath);
  if (!source) {
    throw new Error(`archive does not contain '${sourceProjectPath}'`);
  }
  return source.bytes;
}

export class SourceProjectDocument {
  constructor(readonly store: ProjectStore) {}

  async load(): Promise<
    | LoadedSourceProject
    | OtherVersionSourceProject
    | UnreadableSourceProject
    | undefined
  > {
    const file = await this.store.read(sourceProjectPath);
    if (!file) return undefined;
    try {
      const opened = openSourceProject(file.bytes);
      if (opened.kind === "otherVersion") {
        return {
          otherVersion: true,
          written: opened.written,
          age: opened.age,
          fileRevision: file.revision,
          bytes: file.bytes
        };
      }
      return { project: opened.project, fileRevision: file.revision };
    } catch (error) {
      return {
        unreadable: true,
        bytes: file.bytes,
        fileRevision: file.revision,
        reason: error instanceof Error ? error.message : String(error)
      };
    }
  }

  /**
   * Writes the project, or writes nothing at all.
   *
   * The gate is here rather than on any one editing surface because a surface
   * can be bypassed and this cannot: every road that persists a project runs
   * through this method, so an identifier no schema admits, a required field
   * cleared, a version string that is prose: whatever the control it was typed
   * into failed to catch is caught once, before the author's stored game is
   * replaced by a file the editor could not open again.
   */
  async save(
    project: SourceProject,
    expectedRevision?: number
  ): Promise<LoadedSourceProject> {
    const bytes = encodeSourceProject(project);
    const problems = unreadableProblems(decoder.decode(bytes));
    if (problems.length > 0) throw new UnwritableProjectError(problems);
    const file = await this.store.write(
      sourceProjectPath,
      bytes,
      expectedRevision === undefined ? {} : { expectedRevision }
    );
    return { project, fileRevision: file.revision };
  }

  /**
   * Puts a stored draft the editor cannot open somewhere it will survive.
   *
   * The bytes are the author's only copy, and the recovery banner's other
   * button replaces them with whatever project is currently open, which, on
   * the road that gets an author here, is a blank one. Keeping a copy under a
   * name nothing else writes is what stops that road ending in a lost game: the
   * file travels in every exported archive and can be opened straight back in.
   * Returns where it was put.
   */
  async rescue(bytes: Uint8Array, revision: number): Promise<string> {
    const path = `recovered/project-${revision}.json`;
    await this.store.write(path, bytes);
    return path;
  }

  analyze(project: SourceProject): SourceAnalysis {
    return analyzeSourceProject(
      sourceProjectPath,
      decoder.decode(encodeSourceProject(project))
    );
  }

  /**
   * Reads a project into memory without touching the stored draft. Opening a
   * file just to look must not destroy the only saved copy; the caller's
   * explicit save commits it, using the returned revision.
   *
   * Either shape the editor hands out is taken back: the portable archive from
   * Export, and the bare `project.json` the recovery banner downloads. A
   * recovered draft that could only be downloaded and never opened again would
   * be a copy of the author's work in a format only the author could read.
   */
  async import(
    file: Uint8Array
  ): Promise<LoadedSourceProject | OtherVersionSourceProject> {
    const opened = openSourceProject(
      isZipArchive(file) ? projectEntry(readProjectArchive(file)) : file
    );
    const current = await this.store.read(sourceProjectPath);
    if (opened.kind === "otherVersion") {
      return {
        otherVersion: true,
        written: opened.written,
        age: opened.age,
        fileRevision: current?.revision,
        // The file the author picked, which they still have. Carried only so
        // that both roads hand back the same shape.
        bytes: file
      };
    }
    return { project: opened.project, fileRevision: current?.revision };
  }

  /**
   * The portable archive.
   *
   * The project passed alongside the snapshot is the one on screen, and it
   * overrides whatever the store holds under the same name. That matters
   * exactly when the store and the screen disagree, on a save refused or
   * another tab holding the file, because those are the moments an author most needs a
   * way to get their work out, and an archive of the *stored* copy would hand
   * them somebody else's.
   */
  exportSnapshot(snapshot: ProjectSnapshot, project: SourceProject): Uint8Array {
    const current = {
      path: sourceProjectPath,
      bytes: encodeSourceProject(project),
      revision: snapshot.revision
    };
    return exportProjectArchive({
      revision: snapshot.revision,
      files: [
        ...snapshot.files.filter((file) => file.path !== sourceProjectPath),
        current
      ]
    });
  }
}
