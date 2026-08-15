// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// What the shipped editor does with a game file made by another Grandleon.
//
// Browser-only for the reason the file itself is: this is the road where a real
// `File` goes through a real `<input type="file">` into the production bundle,
// with the production Content Security Policy over it and the real IndexedDB
// draft underneath. A happy-dom suite can hand the document model some bytes;
// it cannot show that an author picking a file off their disk meets a sentence
// rather than a stack trace.
//
// The build under test ships no migration steps, because `1.0.0` is the first
// version of the source format and there is nothing to come from. So what a
// real browser can be held to here is the half that does not need one: a game
// is *placed* before it is validated, and a game this build will not open is
// refused by name, in words an author can act on, with nothing opened and
// nothing written. The chain itself, dialog to agreement to upgrade to play,
// is walked in `src/domain/source-migration-chain.test.ts`, which can hand the
// editor steps to walk.

function watchForFailures(page: Page): string[] {
  const failures: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") failures.push(`console: ${message.text()}`);
  });
  page.on("pageerror", (error) => failures.push(`pageerror: ${error.message}`));
  return failures;
}

/** A game file whose only unusual property is which Grandleon it claims. */
function gameWrittenAt(version: string): string {
  return `${JSON.stringify({
    schemaVersion: version,
    packageId: "3f2504e0-4f89-41d3-9a0c-0305e82c3301",
    gameId: "elsewhere.game",
    title: "Made Elsewhere",
    contentRevision: "0.1.0",
    classes: [],
    unitTypes: [],
    weapons: [],
    items: [],
    maps: []
  }, null, 2)}\n`;
}

/** Hand the editor a file, the way the file chooser does. */
async function openFile(page: Page, name: string, version: string) {
  await page.locator('input[type="file"]').setInputFiles({
    name,
    mimeType: "application/json",
    buffer: Buffer.from(gameWrittenAt(version), "utf8")
  });
}

test.beforeEach(async ({ page }) => {
  page.on("dialog", (dialog) => void dialog.accept());
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Grandleon Editor" }))
    .toBeVisible();
});

test("refuses a game made with a newer Grandleon, by name", async ({ page }) => {
  const failures = watchForFailures(page);

  await openFile(page, "from-the-future.json", "2.0.0");

  const question = page.getByTestId("version-question");
  await expect(question).toBeVisible();
  // The version it was made with, said out loud. An author holding a file that
  // will not open needs to know whether to update their tools or their file,
  // and only this sentence can tell them which.
  await expect(question).toContainText("from-the-future.json was made with");
  await expect(question).toContainText("2.0.0");
  await expect(question).toContainText("newer Grandleon");
  await expect(question).toContainText("1.0.0");

  // Refused, not offered. Opening what this build can see of a newer game means
  // dropping the rest, and the author would find out at the save that did it.
  await expect(
    question.getByRole("button", { name: "Bring it up to date" })
  ).toHaveCount(0);

  // Nothing was opened: the workspace and its rail are not there.
  await expect(page.locator('nav[aria-label="Project"]')).toBeHidden();

  await question.getByRole("button", { name: "Cancel" }).click();
  await expect(question).toBeHidden();
  await expect(page.locator(".project-status")).toContainText(
    "Nothing was changed"
  );

  expect(failures).toEqual([]);
});

test("says where the chain stopped for a game it cannot bring up", async ({
  page
}) => {
  const failures = watchForFailures(page);

  await openFile(page, "from-before.json", "0.9.0");

  const question = page.getByTestId("version-question");
  await expect(question).toBeVisible();
  await expect(question).toContainText("0.9.0");
  // This build has no step out of 0.9.0, and the honest answer names the
  // version it could not leave rather than reporting a schema mismatch.
  await expect(question).toContainText("no way to bring a game up from 0.9.0");
  await expect(question).toContainText("Nothing was changed");
  await expect(
    question.getByRole("button", { name: "Bring it up to date" })
  ).toHaveCount(0);

  expect(failures).toEqual([]);
});

test("does not claim a version for a file that names none", async ({ page }) => {
  const failures = watchForFailures(page);

  await openFile(page, "who-knows.json", "not a version");

  const question = page.getByTestId("version-question");
  await expect(question).toBeVisible();
  await expect(question).toContainText("who-knows.json cannot be opened");
  await expect(question).toContainText("does not say which Grandleon made it");
  // Not called out of date, because it is not: a file nothing can place is a
  // different problem from a file made by an older tool, and saying the wrong
  // one sends the author looking for an upgrade that would not help.
  await expect(question).not.toContainText("brought up to");

  expect(failures).toEqual([]);
});

test("opens a game made with this Grandleon without asking", async ({
  page
}) => {
  // The other side of the same road, and the one that would catch a version
  // check that had become a gate on everything: an ordinary current file must
  // go straight in.
  const failures = watchForFailures(page);

  await openFile(page, "ordinary.json", "1.0.0");

  await expect(page.getByTestId("version-question")).toHaveCount(0);
  await expect(page.locator('nav[aria-label="Project"]')).toBeVisible();
  await expect(page.locator(".project-status")).toContainText("ordinary.json");

  expect(failures).toEqual([]);
});
