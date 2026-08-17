// SPDX-License-Identifier: MIT
// @vitest-environment node
import { describe, expect, it } from "vitest";
import { romRefusalCodes, romTargets } from "./rom-service";

// The service is plain JavaScript and ships no declarations, so it is loaded
// the way node loads it, through a computed specifier that keeps the type
// checker out of a module it cannot read, and this file asks for the node environment
// so that `import.meta.url` is the file URL that resolution needs.
interface ServiceConsole {
  id: string;
  refusals: Readonly<Record<string, string>>;
}

async function serviceConsoles(): Promise<Record<string, ServiceConsole>> {
  const loaded = await import(
    new URL("../../../tools/rom_service/serve.mjs", import.meta.url).href
  ) as { consoles: Record<string, ServiceConsole> };
  return loaded.consoles;
}

// Every code any console can refuse with. The union rather than one console's
// table, because the two do not refuse the same set: only the PlayStation can
// say a project draws characters in more than one way.
async function serviceRefusalCodes(): Promise<Set<string>> {
  const codes = new Set<string>();
  for (const target of Object.values(await serviceConsoles())) {
    for (const code of Object.keys(target.refusals)) codes.add(code);
  }
  return codes;
}

describe("ROM refusal codes", () => {
  it("has words for every refusal the service can make", async () => {
    // The union's stated purpose is that a code the editor has no words for is
    // a type error rather than an empty message in front of an author, and the
    // only thing that can hold it to that is the service's own table.
    const refusals = await serviceRefusalCodes();
    const declared = new Set<string>(romRefusalCodes);

    expect([...refusals].filter((code) => !declared.has(code))).toEqual([]);
  });

  it("declares nothing the service and this side do not raise", async () => {
    // Two codes belong to this side alone: a build the service is holding but
    // has produced nothing for, and a service that answered nothing at all.
    // Everything else has to be a refusal the service actually makes, so the
    // list stays a list of real ones.
    const refusals = await serviceRefusalCodes();

    expect(
      romRefusalCodes.filter((code) =>
        !refusals.has(code) &&
        code !== "rom_build_unfinished" &&
        code !== "rom_service_unreachable"
      )
    ).toEqual([]);
  });

  it("addresses every console the service answers for", async () => {
    // The editor's list of consoles and the service's are the same list, or a
    // button here asks a path that is not there and a console there is one no
    // author can reach. Compared by the path segment, because that is the only
    // thing the two sides actually exchange.
    const served = await serviceConsoles();
    expect(
      Object.values(romTargets).map((target) => target.route).sort()
    ).toEqual(Object.keys(served).sort());
    for (const [route, target] of Object.entries(served)) {
      const addressed = Object.values(romTargets).find(
        (candidate) => candidate.route === route
      );
      expect(addressed, `nothing here addresses /api/${route}`).toBeDefined();
      // And they mean the same console by it: the service names the image it
      // builds, and the button says the same word to the author.
      expect(target.id).toContain(
        addressed!.platform.toLowerCase().replace(/[^a-z0-9]/g, "")
      );
    }
  });
});
