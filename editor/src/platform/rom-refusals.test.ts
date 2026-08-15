// SPDX-License-Identifier: MIT
// @vitest-environment node
import { describe, expect, it } from "vitest";
import { romRefusalCodes } from "./rom-service";

// The service is plain JavaScript and ships no declarations, so it is loaded
// the way node loads it, through a computed specifier that keeps the type
// checker out of a module it cannot read, and this file asks for the node environment
// so that `import.meta.url` is the file URL that resolution needs.
async function serviceRefusals(): Promise<Readonly<Record<string, string>>> {
  const loaded = await import(
    new URL("../../../tools/rom_service/serve.mjs", import.meta.url).href
  ) as { refusals: Readonly<Record<string, string>> };
  return loaded.refusals;
}

describe("ROM refusal codes", () => {
  it("has words for every refusal the service can make", async () => {
    // The union's stated purpose is that a code the editor has no words for is
    // a type error rather than an empty message in front of an author, and the
    // only thing that can hold it to that is the service's own table.
    const refusals = await serviceRefusals();
    const declared = new Set<string>(romRefusalCodes);

    expect(Object.keys(refusals).filter((code) => !declared.has(code)))
      .toEqual([]);
  });

  it("declares nothing the service and this side do not raise", async () => {
    // Two codes belong to this side alone: a build the service is holding but
    // has produced nothing for, and a service that answered nothing at all.
    // Everything else has to be a refusal the service actually makes, so the
    // list stays a list of real ones.
    const refusals = await serviceRefusals();

    expect(
      romRefusalCodes.filter((code) =>
        !(code in refusals) &&
        code !== "rom_build_unfinished" &&
        code !== "rom_service_unreachable"
      )
    ).toEqual([]);
  });
});
