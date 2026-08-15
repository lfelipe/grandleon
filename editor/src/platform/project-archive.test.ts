// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { ProjectSnapshot } from "../domain/project-store";
import {
  exportProjectArchive,
  readProjectArchive
} from "./project-archive";

const encoder = new TextEncoder();

function snapshot(paths: readonly string[]): ProjectSnapshot {
  return {
    revision: paths.length,
    files: paths.map((path, index) => ({
      path,
      bytes: encoder.encode(`content:${path}`),
      revision: index + 1
    }))
  };
}

function replaceAscii(archive: Uint8Array, from: string, to: string): Uint8Array {
  expect(to.length).toBe(from.length);
  const copy = archive.slice();
  const source = encoder.encode(from);
  const replacement = encoder.encode(to);
  for (let index = 0; index <= copy.length - source.length; ++index) {
    if (source.every((byte, offset) => copy[index + offset] === byte)) {
      copy.set(replacement, index);
      index += source.length - 1;
    }
  }
  return copy;
}

describe("project ZIP archives", () => {
  it("exports deterministic canonical ZIP bytes and round trips", () => {
    const first = exportProjectArchive(snapshot(["z.json", "content/a.json"]));
    const second = exportProjectArchive(snapshot(["content/a.json", "z.json"]));
    expect(first).toEqual(second);
    const imported = readProjectArchive(first);
    expect(imported.files.map((file) => file.path)).toEqual([
      "content/a.json",
      "z.json"
    ]);
    expect(new TextDecoder().decode(imported.files[0]!.bytes)).toBe(
      "content:content/a.json"
    );
  });

  it("rejects traversal paths before exposing any files", () => {
    const hostile = replaceAscii(
      exportProjectArchive(snapshot(["safe"])),
      "safe",
      "../x"
    );
    expect(() => readProjectArchive(hostile)).toThrowError(
      expect.objectContaining({ code: "ARCHIVE_INVALID" })
    );
  });

  it("rejects duplicate canonical paths", () => {
    const duplicate = replaceAscii(
      exportProjectArchive(snapshot(["one.json", "two.json"])),
      "two.json",
      "one.json"
    );
    expect(() => readProjectArchive(duplicate)).toThrowError(
      expect.objectContaining({ code: "ARCHIVE_DUPLICATE_PATH" })
    );
  });

  it("checks expansion budgets before copying entry data", () => {
    const archive = exportProjectArchive(snapshot(["large.json"]));
    expect(() => readProjectArchive(archive, {
      maximumArchiveBytes: archive.length,
      maximumEntries: 10,
      maximumEntryBytes: 2,
      maximumTotalBytes: 2,
      maximumPathBytes: 64
    })).toThrowError(expect.objectContaining({ code: "ARCHIVE_LIMIT_EXCEEDED" }));
  });

  it("rejects content corruption", () => {
    const archive = exportProjectArchive(snapshot(["file.json"]));
    const marker = encoder.encode("content:file.json");
    const offset = archive.findIndex((_, index) =>
      marker.every((byte, part) => archive[index + part] === byte)
    );
    expect(offset).toBeGreaterThan(0);
    const corrupt = archive.slice();
    corrupt[offset] = corrupt[offset]! ^ 0xff;
    expect(() => readProjectArchive(corrupt)).toThrowError(
      expect.objectContaining({ code: "ARCHIVE_INTEGRITY_FAILED" })
    );
  });
});
