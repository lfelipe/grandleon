// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import maintainedDemoSource from "../../games/demo/source/project.json";
import { createDemoProject } from "./sample-projects";

describe("maintained demo project", () => {
  it("is sourced directly from the canonical native-demo input", () => {
    const loaded = createDemoProject();
    expect(loaded).toEqual(maintainedDemoSource);
    expect((loaded.factions ?? []).map((faction) => faction.name)).toEqual([
      "Dawn Guard",
      "River Watch"
    ]);

    loaded.title = "Working copy";
    expect(createDemoProject().title).toBe("The Bridge at Dawn");
  });
});
