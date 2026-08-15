// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { MemoryProjectStore } from "./memory-project-store";
import { ProjectStoreError } from "./project-store";
import {
  createSourceProject,
  encodeSourceProject,
  gameIdFollowingTitle,
  gameIdFromTitle,
  SourceProjectDocument,
  sourceProjectPath,
  UnwritableProjectError
} from "./source-project-document";
import { isStableId } from "./source-form-model";
import { exportProjectArchive } from "../platform/project-archive";

type Loaded = Awaited<ReturnType<SourceProjectDocument["load"]>>;

function readable(loaded: Loaded) {
  if (!loaded || "unreadable" in loaded || "otherVersion" in loaded) {
    throw new Error("expected a readable stored project");
  }
  return loaded;
}

type Imported = Awaited<ReturnType<SourceProjectDocument["import"]>>;

/** A file that turned out to be a game this Grandleon can open as it stands. */
function opened(imported: Imported) {
  if ("otherVersion" in imported) {
    throw new Error("expected a game made with this version");
  }
  return imported;
}

describe("the name the file carries", () => {
  it("is made out of the name a player reads", () => {
    expect(gameIdFromTitle("The Salt Road")).toBe("the_salt_road");
    expect(gameIdFromTitle("Nine Months Of Work")).toBe("nine_months_of_work");
    // A new project is already following its own title, so the very first
    // rename has a tie to follow rather than an unrelated id to leave alone.
    const fresh = createSourceProject();
    expect(fresh.gameId).toBe(gameIdFromTitle(fresh.title));
  });

  it("is always something the format can hold", () => {
    // Every one of these breaks the identifier rule some other way: leading
    // digit, punctuation only, and a title longer than the format's ceiling.
    for (const title of [
      "1066",
      "!!!",
      "こんにちは",
      "A".repeat(160),
      "The Long March ".repeat(12)
    ]) {
      expect(isStableId(gameIdFromTitle(title))).toBe(true);
    }
  });

  it("follows a rename while nobody has written an id of their own", () => {
    expect(gameIdFollowingTitle(
      { title: "Untitled Game", gameId: "untitled_game" },
      { title: "The Salt Road", gameId: "untitled_game" }
    )).toBe("the_salt_road");
  });

  it("leaves an id its author chose alone, this rename and every later one",
    () => {
      // Chosen in the same save as the rename.
      expect(gameIdFollowingTitle(
        { title: "Untitled Game", gameId: "untitled_game" },
        { title: "The Salt Road", gameId: "saltroad" }
      )).toBe("saltroad");
      // And chosen earlier: a second rename must not quietly take it back.
      // This is the whole reason the tie is inferred from the two names rather
      // than trusted to a flag: an author who typed an id once keeps it.
      expect(gameIdFollowingTitle(
        { title: "The Salt Road", gameId: "saltroad" },
        { title: "The Salt Road, Part Two", gameId: "saltroad" }
      )).toBe("saltroad");
    });

  it("does not repoint a kept campaign, because no slot is made of it", () => {
    // The load-bearing half of deriving safely. A saved campaign is filed
    // under the package identity, which a rename never touches; if that were
    // ever made of `gameId` instead, renaming a game would orphan its saves
    // and this derivation would have to go.
    const before = createSourceProject();
    const after = {
      ...before,
      title: "The Salt Road",
      gameId: gameIdFollowingTitle(before, {
        title: "The Salt Road",
        gameId: before.gameId
      })
    };
    expect(after.gameId).not.toBe(before.gameId);
    expect(after.packageId).toBe(before.packageId);
  });
});

describe("SourceProjectDocument", () => {
  it("saves canonical JSON and reopens the same project", async () => {
    const store = new MemoryProjectStore("document");
    const document = new SourceProjectDocument(store);
    const project = { ...createSourceProject(), title: "Vertical Slice" };

    const saved = await document.save(project);
    const reopened = await document.load();

    expect(reopened).toEqual(saved);
    const text = new TextDecoder().decode(
      (await store.read(sourceProjectPath))!.bytes
    );
    expect(text.endsWith("\n")).toBe(true);
    expect(JSON.parse(text).title).toBe("Vertical Slice");
  });

  it("writes nothing rather than a project it could not read back", async () => {
    // The gesture behind this: an author types the game's name into the field
    // labelled "Game id *". Every control that should have refused it is
    // bypassed on one road or another, so the write itself is where it stops,
    // and the stored game has to come through untouched, because a save that
    // half-happened is the whole of the damage.
    const store = new MemoryProjectStore("gate");
    const document = new SourceProjectDocument(store);
    const kept = { ...createSourceProject(), title: "Nine Months Of Work" };
    await document.save(kept);
    const before = (await store.read(sourceProjectPath))!;

    const attempt = document.save(
      { ...kept, gameId: "The Tarnholt Line" },
      before.revision
    );
    await expect(attempt).rejects.toBeInstanceOf(UnwritableProjectError);
    await expect(attempt).rejects.toThrow("/gameId");

    const after = (await store.read(sourceProjectPath))!;
    expect(after.bytes).toEqual(before.bytes);
    expect(after.revision).toBe(before.revision);
    expect(readable(await document.load()).project.title)
      .toBe("Nine Months Of Work");
  });

  it("names every problem that would stop the project reopening", async () => {
    const document = new SourceProjectDocument(new MemoryProjectStore("many"));
    const attempt = document.save({
      ...createSourceProject(),
      gameId: "Not An Id",
      contentRevision: "1.0"
    });
    await expect(attempt).rejects.toMatchObject({
      problems: [
        expect.objectContaining({ instancePath: "/gameId" }),
        expect.objectContaining({ instancePath: "/contentRevision" })
      ]
    });
  });

  it("still saves a project whose problems are not structural", async () => {
    // A reference naming nothing is a problem in a project that opens, and an
    // author halfway through building one has several. Refusing to store work
    // in progress would be the same defect wearing the other coat.
    const store = new MemoryProjectStore("in-progress");
    const document = new SourceProjectDocument(store);
    const halfBuilt = {
      ...createSourceProject(),
      classes: [{
        id: "knight",
        name: "Knight",
        baseStats: { health: 20, movement: 4, strength: 5, defense: 5 }
      }],
      unitTypes: [{ id: "dawn", name: "Dawn", classId: "nobody_at_all" }]
    };

    await document.save(halfBuilt);
    const reopened = readable(await document.load());
    expect(reopened.project.unitTypes[0]!.classId).toBe("nobody_at_all");
    // And the analyzer still says so, where diagnostics belong.
    expect(document.analyze(halfBuilt).diagnostics.map((problem) => problem.code))
      .toContain("SOURCE_REF_MISSING");
  });

  it("preserves revision conflicts rather than overwriting another edit", async () => {
    const store = new MemoryProjectStore("conflict");
    const document = new SourceProjectDocument(store);
    const first = await document.save(createSourceProject());
    await store.write(sourceProjectPath, encodeSourceProject({
      ...createSourceProject(),
      title: "Other tab"
    }), { expectedRevision: first.fileRevision! });

    await expect(document.save({
      ...first.project,
      title: "This tab"
    }, first.fileRevision)).rejects.toMatchObject({
      code: "REVISION_CONFLICT"
    } satisfies Partial<ProjectStoreError>);
  });

  it("round-trips a portable archive into another project store", async () => {
    const source = new SourceProjectDocument(new MemoryProjectStore("source"));
    const fixture = { ...createSourceProject(), title: "Archive Fixture" };
    await source.save(fixture);
    const archive = source.exportSnapshot(await source.store.snapshot(), fixture);

    const target = new SourceProjectDocument(new MemoryProjectStore("target"));
    const imported = opened(await target.import(archive));

    expect(imported.project.title).toBe("Archive Fixture");
    // Import only reads; the archive reaches the store when the author saves.
    expect(await target.load()).toBeUndefined();
    await target.save(imported.project, imported.fileRevision);
    expect(readable(await target.load()).project).toEqual(imported.project);
  });

  it("importing an archive never overwrites the stored draft by itself", async () => {
    const store = new MemoryProjectStore("look-dont-touch");
    const document = new SourceProjectDocument(store);
    const kept = await document.save({ ...createSourceProject(), title: "Keep Me" });

    const other = new SourceProjectDocument(new MemoryProjectStore("other"));
    const visitor = { ...createSourceProject(), title: "Visitor" };
    await other.save(visitor);
    const archive = other.exportSnapshot(await other.store.snapshot(), visitor);

    const imported = opened(await document.import(archive));
    expect(imported.project.title).toBe("Visitor");
    expect(imported.fileRevision).toBe(kept.fileRevision);
    // Opening a zip just to look must not destroy the only saved copy.
    expect(readable(await document.load()).project.title).toBe("Keep Me");

    await document.save(imported.project, imported.fileRevision);
    expect(readable(await document.load()).project.title).toBe("Visitor");
  });

  it("takes back the bare project.json it hands out for recovery", async () => {
    // The recovery banner downloads `grandleon-recovered-draft.json`. A draft
    // the editor gives out and cannot take back is the author's work in a
    // format only the author can read.
    const document = new SourceProjectDocument(new MemoryProjectStore("bare"));
    const bytes = encodeSourceProject({
      ...createSourceProject(),
      title: "Repaired By Hand"
    });

    const imported = opened(await document.import(bytes));

    expect(imported.project.title).toBe("Repaired By Hand");
  });

  it("reads a stored archive an ordinary zip tool would make", async () => {
    // Every ordinary tool leaves the UTF-8 name flag clear, because an ASCII
    // name needs no declaration. Only the editor's own exporter sets it, so
    // demanding it made the editor the sole producer of a readable archive.
    const source = new SourceProjectDocument(new MemoryProjectStore("flagless"));
    const project = { ...createSourceProject(), title: "Made Elsewhere" };
    await source.save(project);
    const archive = source.exportSnapshot(
      await source.store.snapshot(),
      project
    );
    const asciiFlagged = archive.slice();
    // The general purpose flags, in the local header and in its directory
    // entry, cleared exactly as `zip -0 -X` leaves them.
    const view = new DataView(
      asciiFlagged.buffer,
      asciiFlagged.byteOffset,
      asciiFlagged.byteLength
    );
    const centralOffset = view.getUint32(asciiFlagged.length - 22 + 16, true);
    view.setUint16(6, 0, true);
    view.setUint16(centralOffset + 8, 0, true);

    const document = new SourceProjectDocument(new MemoryProjectStore("target"));
    expect(opened(await document.import(asciiFlagged)).project.title)
      .toBe("Made Elsewhere");
  });

  it("exports what is on screen rather than what the store holds", async () => {
    // The one road out that must never be closed. When the store and the
    // screen disagree, on a save refused or another tab holding the file, an
    // archive of the stored copy would hand the author somebody else's work.
    const store = new MemoryProjectStore("export-overlay");
    const document = new SourceProjectDocument(store);
    await document.save({ ...createSourceProject(), title: "What Was Stored" });

    const archive = document.exportSnapshot(await store.snapshot(), {
      ...createSourceProject(),
      title: "What Is On Screen"
    });

    const target = new SourceProjectDocument(new MemoryProjectStore("reader"));
    expect(opened(await target.import(archive)).project.title)
      .toBe("What Is On Screen");
  });

  it("hands back the raw bytes of a stored draft that no longer loads", async () => {
    const store = new MemoryProjectStore("recovery");
    const bytes = new TextEncoder().encode('{"schemaVersion": "1.0.0", "title"');
    await store.write(sourceProjectPath, bytes);

    const loaded = await new SourceProjectDocument(store).load();
    if (!loaded || !("unreadable" in loaded)) {
      throw new Error("expected the unreadable draft to be kept");
    }
    expect(loaded.bytes).toEqual(bytes);
    expect(loaded.reason).not.toBe("");
    expect(loaded.fileRevision).toBeGreaterThan(0);
  });

  it("rejects an invalid candidate before changing the current project", async () => {
    const store = new MemoryProjectStore("safe-import");
    const document = new SourceProjectDocument(store);
    await document.save({ ...createSourceProject(), title: "Keep Me" });
    const invalidArchive = exportProjectArchive({
      revision: 1,
      files: [{
        path: sourceProjectPath,
        bytes: new TextEncoder().encode("{"),
        revision: 1
      }]
    });

    await expect(document.import(invalidArchive)).rejects.toThrow(
      "project.json is invalid"
    );
    expect(readable(await document.load()).project.title).toBe("Keep Me");
  });
});
