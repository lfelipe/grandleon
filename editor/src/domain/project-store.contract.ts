// SPDX-License-Identifier: MIT
import { expect } from "vitest";
import type { ProjectStore } from "./project-store";

export async function exerciseProjectStoreContract(
  create: () => ProjectStore
) {
  const store = create();
  expect(await store.read("missing.json")).toBeUndefined();

  const input = new Uint8Array([1, 2, 3]);
  const first = await store.write("content/unit.json", input);
  input[0] = 99;
  expect([...first.bytes]).toEqual([1, 2, 3]);
  expect([...(await store.read("content/unit.json"))!.bytes]).toEqual([1, 2, 3]);

  const second = await store.write(
    "content/unit.json",
    new Uint8Array([4]),
    { expectedRevision: first.revision }
  );
  expect(second.revision).toBeGreaterThan(first.revision);

  await expect(
    store.write("content/unit.json", new Uint8Array(), {
      expectedRevision: first.revision
    })
  ).rejects.toMatchObject({ code: "REVISION_CONFLICT" });

  await store.write("manifest.json", new Uint8Array([5]));
  expect((await store.list()).map((file) => file.path)).toEqual([
    "content/unit.json",
    "manifest.json"
  ]);
  expect((await store.list("content")).map((file) => file.path)).toEqual([
    "content/unit.json"
  ]);

  const snapshot = await store.snapshot();
  expect(snapshot.files).toHaveLength(2);
  await store.delete("content/unit.json", { expectedRevision: second.revision });
  expect(await store.read("content/unit.json")).toBeUndefined();
  expect((await store.snapshot()).revision).toBeGreaterThan(snapshot.revision);

  for (const invalid of ["", "/absolute", "../escape", "a/../b", "a\\b"]) {
    await expect(store.read(invalid)).rejects.toMatchObject({
      code: "INVALID_PATH"
    });
  }
}
