// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { beforeEach, describe, expect, it, vi } from "vitest";

// The registry with steps in it.
//
// This build ships none, 1.0.0 being the first version of the source format, so
// the only way to walk a chain is to hand the editor one, and that is what this
// does: `sourceMigrations` answers with the example steps for the length of
// this file, and everything downstream of it runs unchanged.
//
// The chain ends at the version this build actually writes, which is what makes
// the last assertion here possible: the game that comes out of it is a real
// game, so it can be validated, opened, and played rather than merely counted.
//
// Two ordering rules are keeping this file working and both are easy to undo by
// accident. The steps come through `vi.hoisted` rather than an ordinary import,
// because the replacement is hoisted above every import and a factory closing
// over an import binding depends on which import happens to be written first.
// And `migration-example.mjs` imports nothing at all: a factory that reached
// for a module which itself imports the module being replaced would wait for a
// module waiting for the factory, and the run would never start.
const example = await vi.hoisted(
  async () => import("../../../tools/source_schema/migration-example.mjs")
);
const {
  EXAMPLE_OLDEST, EXAMPLE_STEPS, FIRST_CHANGE, SECOND_CHANGE, THIRD_CHANGE
} = example;

vi.mock("../../../tools/source_schema/migration.mjs", async (importOriginal) => {
  const actual = await importOriginal<
    typeof import("../../../tools/source_schema/migration.mjs")
  >();
  return {
    ...actual,
    sourceMigrations: () => example.EXAMPLE_STEPS.reduce(
      (registry, step) => registry.add(step),
      new actual.SourceMigrationRegistry()
    )
  };
});

import App from "../App.vue";
import { createSampleProject } from "../sample-projects";
import { MemoryProjectStore } from "./memory-project-store";
import { CURRENT_SOURCE_VERSION, projectAge } from "./source-migration";
import {
  bringProjectUpToDate,
  encodeSourceProject,
  sourceProjectPath
} from "./source-project-document";
import { RomService } from "../platform/rom-service";

/**
 * The demo, as it would have been written two versions ago: no season, no
 * turn order, and a version this build has never issued. Everything else is the
 * shipped game, so what the chain produces can be held to the same standard as
 * a game somebody actually made.
 */
function gameFromTwoVersionsAgo(): Record<string, unknown> {
  const written = JSON.parse(JSON.stringify(createSampleProject("demo")));
  delete written.themeId;
  delete written.defaultTurnOrder;
  written.schemaVersion = EXAMPLE_OLDEST;
  return written as Record<string, unknown>;
}

const absentRomService = () => new RomService({
  fetch: (async () => {
    throw new TypeError("Failed to fetch");
  }) as unknown as typeof globalThis.fetch
});

const settle = () => new Promise((resolve) => setTimeout(resolve, 0));

function commandButton(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim() === text
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

beforeEach(() => {
  window.confirm = vi.fn(() => true) as unknown as typeof window.confirm;
});

describe("a game two versions out of date", () => {
  it("is placed rather than rejected, and says what bringing it up would do", () => {
    const age = projectAge(gameFromTwoVersionsAgo());
    expect(age.kind).toBe("behind");
    if (age.kind !== "behind") return;
    expect(age.madeWith).toBe(EXAMPLE_OLDEST);
    expect(age.needs).toBe(CURRENT_SOURCE_VERSION);
    // Every step's sentence, in the order the steps run. This is the list the
    // dialog puts in front of the author, and its order is the order things
    // would happen in: a step that runs second cannot be described first.
    expect(age.changed).toEqual([FIRST_CHANGE, SECOND_CHANGE, THIRD_CHANGE]);
  });

  it("becomes a game this build can open, and the original is untouched", () => {
    const written = gameFromTwoVersionsAgo();
    const before = JSON.stringify(written);

    const upgraded = bringProjectUpToDate(written);
    expect(upgraded.ok).toBe(true);
    if (!upgraded.ok) return;

    expect(upgraded.project.schemaVersion).toBe(CURRENT_SOURCE_VERSION);
    expect(upgraded.project.themeId).toBe("temperate");
    expect(upgraded.project.defaultTurnOrder).toBe("sideBlocks");
    expect(upgraded.changed).toEqual([FIRST_CHANGE, SECOND_CHANGE, THIRD_CHANGE]);
    // The result is not merely different, it is a game: it goes through the
    // same gate every save goes through. A step that produced something the
    // editor could not read would otherwise be discovered at the author's next
    // save, by which point the game they opened is gone.
    expect(JSON.stringify(written)).toBe(before);
  });
});

describe("the editor's answer to a stored game that is out of date", () => {
  it("asks, brings it up to date, and opens a game that plays", async () => {
    const store = new MemoryProjectStore("out-of-date");
    await store.write(
      sourceProjectPath,
      // Written past the encoder's type, because this is a game from a version
      // whose shape the current types cannot describe, which is the whole
      // situation being tested.
      encodeSourceProject(gameFromTwoVersionsAgo() as never)
    );

    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp(App, {
      projectStore: store,
      romService: absentRomService()
    });
    app.mount(host);
    await settle();

    // Nothing was opened and nothing was written. The author is asked first.
    const question = host.querySelector<HTMLElement>(
      '[data-testid="version-question"]'
    );
    expect(question).not.toBeNull();
    expect(question?.textContent).toContain(`Grandleon ${EXAMPLE_OLDEST}`);
    expect(question?.textContent)
      .toContain(`brought up to ${CURRENT_SOURCE_VERSION}`);
    expect(question?.textContent).toContain("What changed");
    const listed = [...question!.querySelectorAll(".version-changes li")]
      .map((item) => item.textContent?.trim());
    expect(listed).toEqual([FIRST_CHANGE, SECOND_CHANGE, THIRD_CHANGE]);
    expect(host.querySelector("#workspace .content-workspace")).toBeNull();

    commandButton(host, "Bring it up to date").click();
    await settle();

    // The game is open and unsaved: the upgrade is to what is on screen, and
    // the file on disk still says what it always said.
    expect(host.querySelector('[data-testid="version-question"]')).toBeNull();
    expect(host.textContent).toContain("Unsaved changes");
    expect(host.textContent).toContain("brought up to date");
    const onDisk = JSON.parse(
      new TextDecoder().decode((await store.read(sourceProjectPath))!.bytes)
    );
    expect(onDisk.schemaVersion).toBe(EXAMPLE_OLDEST);

    // And it plays. A game that opens but cannot be played would be a migration
    // that produced a document rather than a game.
    commandButton(host, "▶ Play").click();
    await settle();
    expect(host.querySelector(".play-mode")).not.toBeNull();

    app.unmount();
  });

  it("changes nothing at all when the author says no", async () => {
    const store = new MemoryProjectStore("declined");
    const written = gameFromTwoVersionsAgo();
    await store.write(sourceProjectPath, encodeSourceProject(written as never));
    const before = (await store.read(sourceProjectPath))!.bytes;

    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp(App, {
      projectStore: store,
      romService: absentRomService()
    });
    app.mount(host);
    await settle();

    commandButton(host, "Cancel").click();
    await settle();

    expect(host.querySelector('[data-testid="version-question"]')).toBeNull();
    expect(host.textContent).toContain("Nothing was changed");
    expect((await store.read(sourceProjectPath))!.bytes).toEqual(before);

    // And declining does not quietly mean losing it. The game is still stored,
    // still not open, and the next save would go over the top of it, so a copy
    // goes somewhere nothing else writes, exactly as the recovery road does it.
    const kept = (await store.list("recovered")).map((file) => file.bytes);
    expect(kept).toEqual([before]);
    expect(host.textContent).toContain("nothing you do next can write over it");

    app.unmount();
  });

  it("does not set a file the author still has aside", async () => {
    // The other road into the same question. An imported file is on the
    // author's disk and stays there, so copying it into the project would be
    // filing away something nobody can lose.
    const store = new MemoryProjectStore("declined-import");
    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp(App, {
      projectStore: store,
      romService: absentRomService()
    });
    app.mount(host);
    await settle();

    const input = host.querySelector<HTMLInputElement>('input[type="file"]')!;
    const bytes = encodeSourceProject(gameFromTwoVersionsAgo() as never);
    Object.defineProperty(input, "files", {
      value: [new File([bytes as BlobPart], "from-a-disk.json")],
      configurable: true
    });
    input.dispatchEvent(new Event("change"));
    await settle();

    expect(host.querySelector('[data-testid="version-question"]')?.textContent)
      .toContain("from-a-disk.json");
    commandButton(host, "Cancel").click();
    await settle();

    expect(host.textContent).toContain("Nothing was changed");
    expect(await store.list("recovered")).toEqual([]);

    app.unmount();
  });
});
