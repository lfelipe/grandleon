// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { sourceV1Schemas } from "../generated/source-v1-schemas";
import {
  bringUpToDate,
  CURRENT_SOURCE_VERSION,
  projectAge
} from "./source-migration";
import { createSourceProject } from "./source-project-document";

// This build ships no migration steps: 1.0.0 is the first version of the source
// format, so there is nothing to come from. What that leaves worth checking
// here is everything around the steps: the version this editor writes, the way
// a game from another version is placed, and the sentences an author is shown.
// `source-migration-chain.test.ts` is the other half, and it runs a chain.

describe("the version this Grandleon writes", () => {
  it("is what a new game declares", () => {
    // The pin `tools/source_schema/test.mjs` names. Everything that cannot
    // import the registry, the schema's const, the native compiler and the two
    // shipped games, is checked there against the same constant; this is the
    // one site that can import it, so it is checked by running it.
    expect(createSourceProject().schemaVersion).toBe(CURRENT_SOURCE_VERSION);
  });

  it("is what the bundled schema demands", () => {
    // The schema the editor validates against is generated from
    // `schemas/source/v1/`, so this is the same fact the Node guard pins,
    // asserted against the copy that actually decides whether a file opens.
    const schemas = sourceV1Schemas as readonly {
      $id?: string;
      properties?: { schemaVersion?: { const?: string } };
    }[];
    const project = schemas.find(
      (schema) => schema.$id?.endsWith("/project.schema.json")
    );
    expect(project?.properties?.schemaVersion?.const)
      .toBe(CURRENT_SOURCE_VERSION);
  });

  it("opens a game made with it without asking anything", () => {
    expect(projectAge(createSourceProject())).toEqual({ kind: "current" });
  });
});

describe("a game made with another Grandleon", () => {
  it("is refused by name when it is newer than this one", () => {
    const age = projectAge({ ...createSourceProject(), schemaVersion: "2.0.0" });
    expect(age.kind).toBe("refused");
    if (age.kind !== "refused") return;
    expect(age.madeWith).toBe("2.0.0");
    // Named, and named as the newer thing it is. Silently opening what this
    // build can see of it would drop whatever 2.0.0 added, and the author would
    // find out at the save that threw it away.
    expect(age.sentence).toContain("2.0.0");
    expect(age.sentence).toContain("newer Grandleon");
    expect(age.sentence).toContain(CURRENT_SOURCE_VERSION);
  });

  it("says where the chain stopped when there is no way up from it", () => {
    const age = projectAge({ ...createSourceProject(), schemaVersion: "0.7.0" });
    expect(age.kind).toBe("refused");
    if (age.kind !== "refused") return;
    // This build has no step out of 0.7.0, having no steps at all, and the
    // honest answer names the version it could not leave rather than reporting
    // a schema mismatch nobody can act on.
    expect(age.sentence).toContain("0.7.0");
    expect(age.sentence).toContain("no way to bring a game up");
  });

  it("is not called out of date when it never said what made it", () => {
    for (const written of [
      {},
      { schemaVersion: 3 },
      { schemaVersion: "1.0" },
      { schemaVersion: "not a version" }
    ]) {
      const age = projectAge(written);
      expect(age.kind).toBe("refused");
      if (age.kind !== "refused") continue;
      expect(age.sentence).toContain("does not say which Grandleon made it");
      // And it names no version, because there is none to name. Handing the
      // heading whatever text sat in the field produces "made with Grandleon
      // not a version", which reads as though the file had answered.
      expect(age.madeWith).toBe("");
    }
  });

  it("is refused before anything is run, and nothing comes back", () => {
    const written = { ...createSourceProject(), schemaVersion: "2.0.0" };
    const attempt = bringUpToDate(written);
    expect(attempt.ok).toBe(false);
    expect(written.schemaVersion).toBe("2.0.0");
  });
});

describe("what an author is told", () => {
  // The editor's surface has a vocabulary of its own and the format keeps its
  // own words underneath. A refusal is read by somebody making a game, so it
  // may not need the format's vocabulary to parse: a sentence naming a schema,
  // a migration or a constant is a sentence written for the wrong person.
  const formatWords = [
    "schemaVersion",
    "schema",
    "migration",
    "migrate",
    "registry",
    "const",
    "encounter",
    "JSON",
    "ajv"
  ];

  it("never needs the format's vocabulary to parse", () => {
    const sentences = ["2.0.0", "0.7.0", "1.0", "9.9.9"].map((version) => {
      const age = projectAge({ schemaVersion: version });
      return age.kind === "refused" ? age.sentence : "";
    });
    expect(sentences.every((one) => one.length > 0)).toBe(true);
    for (const sentence of sentences) {
      for (const word of formatWords) {
        expect(sentence.toLowerCase()).not.toContain(word.toLowerCase());
      }
    }
  });
});
