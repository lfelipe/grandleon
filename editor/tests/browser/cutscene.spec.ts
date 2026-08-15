// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// The cutscene editor in a real browser, against the checked-in Tarnholt
// campaign: its story nodes carry authored dialogue sequences, so this
// exercises real content rather than a synthetic fixture.

function watchForFailures(page: Page): string[] {
  const failures: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") failures.push(`console: ${message.text()}`);
  });
  page.on("pageerror", (error) => failures.push(`pageerror: ${error.message}`));
  return failures;
}

test.beforeEach(async ({ page }) => {
  // Loading a sample over the unsaved initial draft asks for confirmation.
  page.on("dialog", (dialog) => void dialog.accept());
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Grandleon Editor" })).toBeVisible();
  // Tarnholt is the default sample.
  await page.getByRole("button", { name: "Open this example" }).click();
});

test("edits and previews a story node's cutscene", async ({ page }) => {
  const failures = watchForFailures(page);

  // Flow opens on the campaign this project has: no category to choose and no
  // record to select first. The graph leads the page; the words-and-forms half
  // under it, where a story node's scenes are edited, is one press behind a
  // fold that names what is inside it.
  await page.locator(".project-sections button", { hasText: "Flow" })
    .click();
  await page.locator(".flow-detail > summary").click();

  // The entry node is the prologue story node; its authored scene is listed
  // by name, not as a comma-separated identifier.
  const cutscene = page.locator(".cutscene-editor");
  await expect(cutscene).toContainText("Prologue");
  await expect(cutscene).toContainText("3 lines");

  await cutscene.getByRole("button", { name: "Edit lines" }).click();
  const firstSpeaker = page.locator("#cutscene-0-line-0-speaker");
  await expect(firstSpeaker).toHaveValue("Runner");

  await firstSpeaker.fill("Mirea of Tarnholt");
  await firstSpeaker.blur();
  await expect(page.getByText("Saved scenes")).toBeVisible();

  // The preview plays the edited line the way the game presents it.
  await cutscene.getByRole("button", { name: "Preview" }).click();
  const stage = page.locator(".dialogue-preview-stage");
  await expect(stage).toContainText("Mirea of Tarnholt");
  await expect(stage).toContainText("Line 1 of 3");
  await stage.getByRole("button", { name: "Next" }).click();
  await expect(stage).toContainText("Line 2 of 3");

  expect(failures).toEqual([]);
});

test("sets what a scene is drawn against, from the rail", async ({ page }) => {
  const failures = watchForFailures(page);

  await page.locator(".project-sections button", { hasText: "Flow" }).click();
  await page.locator(".flow-detail > summary").click();

  const cutscene = page.locator(".cutscene-editor");
  // Labelled and reachable by its label, which is what a keyboard and a screen
  // reader both need; the campaign ships this scene set against a throne hall.
  const select = cutscene.getByLabel("Set against").first();
  await expect(select).toHaveValue("throne_hall");
  await expect(cutscene).toContainText("A hall by lamplight");

  await select.selectOption("open_sea");
  await expect(page.getByText("Saved scenes")).toBeVisible();
  await expect(cutscene).toContainText("Night water under a night sky");

  // The preview draws what the scene now names.
  await cutscene.getByRole("button", { name: "Preview" }).click();
  const stage = page.locator(".dialogue-preview-stage");
  await expect(stage).toContainText("Set against Open sea");

  // Cleared is absent, not empty: the help text says what a scene with no
  // backdrop is, which is what an author needs to know before choosing one.
  await stage.getByRole("button", { name: "Close preview" }).click();
  await select.selectOption("");
  await expect(cutscene).toContainText("Drawn on a plain screen.");

  expect(failures).toEqual([]);
});

test("edits a scene record's lines as a list", async ({ page }) => {
  const failures = watchForFailures(page);

  // Scenes are their own place: what is said is neither ground, nor a fight,
  // nor the shape of a campaign, and it is one press from the rail.
  await page.locator(".project-sections button", { hasText: "Scenes" })
    .click();
  await page.locator(".record-list button", { hasText: "Between the Rivers" })
    .click();

  // No raw JSON textarea for lines; the list editor stands in its place.
  await expect(page.locator("#field-lines")).toHaveCount(0);
  const lines = page.locator(".dialogue-record-lines");
  await expect(lines.locator("fieldset.dialogue-line")).toHaveCount(3);

  await lines.getByRole("button", { name: "Add a line" }).click();
  await expect(page.getByText("Saved scene lines")).toBeVisible();
  await expect(lines.locator("fieldset.dialogue-line")).toHaveCount(4);
  // The previous speaker carries forward, so the new line starts sayable.
  await expect(page.locator("#dialogue-line-3-speaker")).toHaveValue(
    "Captain Mirea"
  );

  expect(failures).toEqual([]);
});
