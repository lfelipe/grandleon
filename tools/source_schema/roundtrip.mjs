// SPDX-License-Identifier: MIT
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

export function canonicalize(value) {
  if (Array.isArray(value)) {
    return value.map(canonicalize);
  }
  if (value !== null && typeof value === "object") {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .map((key) => [key, canonicalize(value[key])])
    );
  }
  return value;
}

export function serializeCanonical(value) {
  return `${JSON.stringify(canonicalize(value), null, 2)}\n`;
}

function main() {
  const filename = process.argv[2];
  if (!filename) {
    console.error("usage: node roundtrip.mjs <project.json>");
    process.exitCode = 2;
    return;
  }

  const source = JSON.parse(fs.readFileSync(filename, "utf8"));
  process.stdout.write(serializeCanonical(source));
}

if (import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main();
}
