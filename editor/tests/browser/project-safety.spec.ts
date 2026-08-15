// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// The roads that could destroy an author's game, driven end to end in a real
// browser and across a reload.
//
// The whole point of these is that they are not observable anywhere else. The
// unit suite is green while every one of them is reachable: the guards that
// look like they would refuse are HTML constraint validation on controls that
// are in no form or are never submitted, and the damage only becomes damage on
// the next open, which needs a real IndexedDB and a real page load.

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
  await expect(page.getByRole("heading", { name: "Grandleon Editor" }))
    .toBeVisible();
  await page.getByRole("button", { name: "Open this example" }).click();
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.getByLabel("Project status")).toContainText("Saved in this browser");
});

test("a renamed character keeps the game openable", async ({ page }) => {
  const failures = watchForFailures(page);

  await page.locator(".project-sections button", { hasText: "Characters" })
    .click();
  // The record columns are behind a fold on this page; the roster leads it.
  await page.locator(".records-fold > summary").click();
  await page.locator(".content-categories button", { hasText: "Characters" })
    .click();
  await page.locator(".record-list ul button").first().click();

  await page.getByRole("group", { name: "Rename stable identifier" })
    .or(page.locator(".rename-record"))
    .first()
    .getByText("Rename stable identifier")
    .click();
  const identifier = page.locator("#rename-id");
  await identifier.fill("My Best Knight!!");
  await page.getByRole("button", { name: "Preview rename" }).click();

  // Refused where the mistake is made, rather than rewritten across a dozen
  // records and discovered on the next open.
  await expect(page.locator(".save-status"))
    .toContainText("is not an identifier this format can hold");
  await expect(page.getByRole("button", { name: "Confirm atomic rename" }))
    .toHaveCount(0);

  // A legal one still goes through, so the refusal is a rule and not a wall.
  await identifier.fill("my_best_knight");
  await page.getByRole("button", { name: "Preview rename" }).click();
  await expect(page.locator(".save-status"))
    .toContainText("Rename will update");
  await page.getByRole("button", { name: "Confirm atomic rename" }).click();
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.getByLabel("Project status"))
    .toContainText("Saved in this browser");

  // The reload is the assertion: this is where a project written with a
  // forbidden identifier stopped opening.
  await page.reload();
  await expect(page.getByLabel("Project status"))
    .toContainText("Recovered local browser draft");
  await expect(page.locator(".draft-recovery")).toHaveCount(0);
  expect(failures).toEqual([]);
});

test("prose typed into the game identifier never reaches the stored game",
  async ({ page }) => {
    const failures = watchForFailures(page);

    await page.locator(".project-sections button", { hasText: "Game" }).click();
    const settings = page.locator(".game-settings");
    await expect(settings).toBeVisible();
    // Behind the Advanced fold, because nobody is asked for it: it follows the
    // title. Where it is found changed; what it refuses did not, and the
    // refusal is still raised at the field and reported on the page.
    const fold = settings.locator("details.advanced-fields");
    await fold.getByText("Advanced").click();
    const stored = await page.locator("#field-gameId").inputValue();

    // A plausible thing to write in a box called "Game": the game's name.
    await page.locator("#field-gameId").fill("The Tarnholt Line");
    await settings.getByRole("button", { name: "Save game settings" }).click();
    await expect(settings.locator(".field-error"))
      .toContainText("Nothing was saved.");

    await page.getByRole("button", { name: "Save", exact: true }).click();
    await page.reload();
    await expect(page.getByLabel("Project status"))
      .toContainText("Recovered local browser draft");
    await expect(page.locator(".draft-recovery")).toHaveCount(0);
    await page.locator(".project-sections button", { hasText: "Game" }).click();
    await settings.locator("details.advanced-fields").getByText("Advanced")
      .click();
    await expect(page.locator("#field-gameId")).toHaveValue(stored);
    expect(failures).toEqual([]);
  });

test("a required field cleared is refused rather than stored empty",
  async ({ page }) => {
    const failures = watchForFailures(page);

    await page.locator(".project-sections button", { hasText: "Characters" })
      .click();
    await page.locator(".records-fold > summary").click();
    await page.locator(".content-categories button", { hasText: "Characters" })
      .click();
    await page.locator(".record-list ul button").first().click();
    const name = page.locator("#field-name");
    const stored = await name.inputValue();
    await name.fill("");

    // Leaving the section is one of the roads that commits a pending draft
    // without ever raising a submit event, which is how a cleared required
    // field would otherwise reach the file.
    await page.locator(".project-sections button", { hasText: "Maps" }).click();
    await expect(page.locator(".save-status"))
      .toContainText("Fix the problems shown in the open editor first");

    await name.fill(stored);
    await page.locator(".project-sections button", { hasText: "Maps" }).click();
    await expect(page.locator("#content-title")).toHaveText("Maps");
    expect(failures).toEqual([]);
  });

test("Play plays the numbers on screen", async ({ page }) => {
  // Save, Export, the ROM button and every section change commit an open
  // form's pending edits, and Play has to for the same reason: an author who
  // raises a weapon's power and presses Play would otherwise watch a Stage
  // fought with the stored number.
  const failures = watchForFailures(page);

  await page.locator(".project-sections button", { hasText: "Weapons & items" })
    .click();
  await page.locator(".content-categories button", { hasText: "Weapons" })
    .first()
    .click();
  await page.locator(".record-list ul button").first().click();
  const power = page.locator("#field-power");
  await expect(power).toBeVisible();
  const weapon = await page.locator("#field-name").inputValue();
  await power.fill("99");
  await expect(power).toHaveValue("99");

  // No Save of any kind between the edit and the press.
  await page.getByRole("button", { name: "▶ Play" }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page.getByRole("button", { name: "Back to editing" }).click();

  // The draft was committed on the way in, so the project the editor holds now
  // carries it, which is what Play was handed.
  await expect(page.locator(".save-status")).toContainText(`Saved ${weapon}`);
  await expect(page.locator("#field-power")).toHaveValue("99");
  await expect(page.getByLabel("Project status"))
    .toContainText("Unsaved changes");
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.getByLabel("Project status"))
    .toContainText("Saved in this browser");
  await page.reload();
  await page.locator(".project-sections button", { hasText: "Weapons & items" })
    .click();
  await page.locator(".content-categories button", { hasText: "Weapons" })
    .first()
    .click();
  await page.locator(".record-list ul button").first().click();
  await expect(page.locator("#field-name")).toHaveValue(weapon);
  await expect(page.locator("#field-power")).toHaveValue("99");
  expect(failures).toEqual([]);
});
