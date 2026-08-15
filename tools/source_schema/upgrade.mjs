// SPDX-License-Identifier: MIT

// Bring a project file up to the version this checkout writes.
//
// The editor asks first and upgrades the open game, leaving the file alone
// until the author saves. This is the same registry with the other answer: a
// project on disk, rewritten in place, for a person holding a directory of
// games rather than one open in a browser. Both roads run the same steps in the
// same order, because there is one registry and `migration.mjs` is it.
//
//   node tools/source_schema/upgrade.mjs games/demo/source/project.json
//   node tools/source_schema/upgrade.mjs --check <project.json>
//
// `--check` reports and writes nothing, which is what a script wants: exit 0
// when the file is current, 1 when it is not, 2 when it cannot be.

import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

import {
  CURRENT_SOURCE_VERSION,
  planUpgrade,
  projectVersion,
  sourceMigrations,
  upgradeProject
} from "./migration.mjs";

/** What a refusal means, said once, for the CLI and nothing else. */
function refusalMessage(result, filename) {
  const found = result.from === "" ? "no version at all" : result.from;
  switch (result.refusal) {
    case "unreadable_version":
      return `${filename}: this is not a Grandleon project. It declares ${found}`;
    case "downgrade_refused":
      return `${filename}: made with a newer Grandleon (${result.from}); this `
        + `one writes ${result.to}. Upgrade the tools rather than the file: `
        + "going backwards can only be done by throwing something away.";
    case "missing_step":
      return `${filename}: made with Grandleon ${result.from}, and there is no `
        + `way up from ${result.stoppedAt}. Nothing was changed.`;
    case "step_limit_exceeded":
      return `${filename}: the chain out of ${result.from} is longer than this `
        + "tool will walk. Nothing was changed.";
    case "step_failed":
      return `${filename}: bringing it up from ${result.stoppedAt} failed: `
        + `${result.detail}. Nothing was changed.`;
    default:
      return `${filename}: refused (${result.refusal}). Nothing was changed.`;
  }
}

function main(argv) {
  const check = argv.includes("--check");
  const filename = argv.find((one) => !one.startsWith("--"));
  if (!filename) {
    console.error("usage: node upgrade.mjs [--check] <project.json>");
    return 2;
  }

  let project;
  try {
    project = JSON.parse(fs.readFileSync(filename, "utf8"));
  } catch (error) {
    console.error(`${filename}: ${error instanceof Error ? error.message : error}`);
    return 2;
  }

  const registry = sourceMigrations();
  const declared = projectVersion(project);
  if (declared === CURRENT_SOURCE_VERSION) {
    console.log(`${filename}: already made with Grandleon ${CURRENT_SOURCE_VERSION}`);
    return 0;
  }

  if (check) {
    const plan = planUpgrade(registry, declared);
    if (!plan.ok) {
      console.error(refusalMessage(plan, filename));
      return 2;
    }
    console.log(
      `${filename}: made with Grandleon ${plan.from}, needs ${plan.to}`
    );
    for (const step of plan.steps) console.log(`  - ${step.changed}`);
    return 1;
  }

  const result = upgradeProject(registry, project);
  if (!result.ok) {
    console.error(refusalMessage(result, filename));
    return 2;
  }

  // Written with the same shape the editor writes: two-space indent and a
  // trailing newline, so upgrading a file and saving it from the editor produce
  // the same bytes and a review shows the migration rather than the formatter.
  fs.writeFileSync(filename, `${JSON.stringify(result.project, null, 2)}\n`);
  console.log(`${filename}: brought up from ${result.from} to ${result.to}`);
  for (const sentence of result.changed) console.log(`  - ${sentence}`);
  return 0;
}

if (import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  process.exitCode = main(process.argv.slice(2));
}

export { main, refusalMessage };
