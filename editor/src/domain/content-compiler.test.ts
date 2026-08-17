// SPDX-License-Identifier: MIT
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { resolve } from "node:path";
import { beforeAll, describe, expect, it } from "vitest";
import { compileProject, initEncounterEngine } from "./encounter-simulation";

// The shipped campaign, read from the repository rather than fixtured, because
// what this file has to prove is that the browser compiles *the* project the
// toolchain compiles; a fixture would only prove it compiles something. Vitest
// runs from `editor/`, which is the one directory this path is relative to.
const tarnholtSource = resolve(
  process.cwd(),
  "../games/tarnholt/source/project.json"
);
const demoSource = resolve(process.cwd(), "../games/demo/source/project.json");

// What `grandleon_content_compile games/tarnholt/source/project.json` writes,
// measured on the host. Restated here rather than derived so that a change to
// either compiler has to be a deliberate change to this number.
// A figure that moves is content, not compiler: naming who speaks in each of
// the campaign's seventeen scenes is worth about four hundred bytes of the
// total below, and widening the Fordlight from ten columns to thirty-two is
// worth about seventeen hundred more, because a map's terrain is a byte a cell.
// The host and WebAssembly compilers agree on it either way.
const tarnholtPackageBytes = 20344;
const tarnholtPackageMd5 = "d22f8c0ccf094aefd8faa0a2966c1c35";

beforeAll(async () => {
  await initEncounterEngine();
});

describe("the content compiler through the WebAssembly boundary", () => {
  it("compiles the shipped campaign to the package the host compiler writes", () => {
    const result = compileProject(readFileSync(tarnholtSource, "utf8"));
    expect(result.compiled).toBe(true);
    if (!result.compiled) return;
    expect(result.package.byteLength).toBe(tarnholtPackageBytes);
    expect(createHash("md5").update(result.package).digest("hex")).toBe(
      tarnholtPackageMd5
    );
  });

  it("answers with the identities the compiler assigned", () => {
    const result = compileProject(readFileSync(tarnholtSource, "utf8"));
    expect(result.compiled).toBe(true);
    if (!result.compiled) return;
    expect(result.encounterIds.length).toBeGreaterThan(0);
    expect(result.campaignIds.length).toBeGreaterThan(0);
    for (const id of [...result.encounterIds, ...result.campaignIds]) {
      expect(id).toBeGreaterThan(0n);
    }
    // Every identity is distinct: two boards sharing one is a project a
    // cartridge could not name its opening board in.
    const ids = new Set(result.encounterIds.map((id) => id.toString(16)));
    expect(ids.size).toBe(result.encounterIds.length);
  });

  it("compiles the demo, which names no style, in the first style", () => {
    const result = compileProject(readFileSync(demoSource, "utf8"));
    expect(result.compiled).toBe(true);
    if (!result.compiled) return;
    expect(result.characterStyle).toBe(0);
  });

  it("does not leave the previous package behind on a later call", () => {
    const first = compileProject(readFileSync(tarnholtSource, "utf8"));
    const held = first.compiled ? first.package : new Uint8Array();
    const copy = Uint8Array.from(held);
    compileProject(readFileSync(demoSource, "utf8"));
    expect(held).toEqual(copy);
  });

  it("refuses text that is not a project, in the parser's own words", () => {
    const result = compileProject("{}");
    expect(result.compiled).toBe(false);
    if (result.compiled) return;
    expect(result.stage).toBe("source");
    expect(result.diagnostics.length).toBeGreaterThan(0);
    for (const diagnostic of result.diagnostics) {
      expect(diagnostic.code).not.toBe("");
    }
  });

  it("refuses a project whose references do not resolve", () => {
    const project = JSON.parse(
      readFileSync(tarnholtSource, "utf8")
    ) as Record<string, unknown>;
    const unitTypes = project.unitTypes as { classId: string }[];
    const first = unitTypes[0];
    if (!first) throw new Error("the shipped project holds no unit types");
    first.classId = "no_such_class";
    const result = compileProject(JSON.stringify(project));
    expect(result.compiled).toBe(false);
    if (result.compiled) return;
    expect(result.stage).toBe("content");
    expect(result.diagnostics.map((one) => one.code)).toContain(
      "missing_reference"
    );
  });

  it("refuses source larger than the compiler's buffer rather than truncating", () => {
    const huge = " ".repeat(2 * 1024 * 1024);
    expect(() => compileProject(huge)).toThrow(/accepts/);
  });
});
