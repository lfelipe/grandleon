// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// One test for a whole class of defect, against the checked-in Tarnholt
// campaign.
//
// The controls below are not a list of surfaces worth testing one by one. They
// are every free-text and number control in the editor that was bound at the
// stored record and committed only when the browser fired `change`, and each
// one was measured losing keystrokes in this browser before it was fixed. What
// they share is what is tested: while a field is being typed into, and before
// it is left, the editor must know the words are not in the project.
//
// That is the whole check, and it is the one that catches this. Whether the
// words then reach storage is what a Save is for, and the roads a Save takes
// are covered beneath, including the one where the author never leaves the
// field.

function watchForFailures(page: Page): string[] {
  const failures: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") failures.push(`console: ${message.text()}`);
  });
  page.on("pageerror", (error) => failures.push(`pageerror: ${error.message}`));
  return failures;
}

const status = (page: Page) => page.locator(".project-status");
const save = (page: Page) =>
  page.getByRole("button", { name: "Save", exact: true });

async function openSample(page: Page) {
  page.on("dialog", (dialog) => void dialog.accept());
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Grandleon Editor" }))
    .toBeVisible();
  await page.getByRole("button", { name: "Open this example" }).click();
}

/** A saved, clean starting point, so "unsaved" later can only be the typing. */
async function settle(page: Page) {
  if (await save(page).isEnabled()) await save(page).click();
  await expect(status(page)).toContainText("Saved locally");
}

/**
 * Types into one control without leaving it, and insists the editor noticed.
 *
 * The header is where an author reads whether their work is in the project, and
 * it is the same signal the close-the-tab guard and `flushDrafts` are driven
 * by: a control the header cannot see is a control a Save cannot find.
 */
async function typingIsWorkInProgress(
  page: Page,
  selector: string,
  typed: string
) {
  await settle(page);
  const control = page.locator(selector);
  await expect(control, selector).toHaveCount(1);
  const before = await control.inputValue();
  await control.click();
  await control.press("ControlOrMeta+a");
  await control.pressSequentially(typed);
  await expect(control, selector).toBeFocused();
  await expect(status(page), selector).toContainText("Unsaved changes");
  // Put back what was there, so the next control is measured against the same
  // clean project rather than against whatever the last one left behind. Some
  // of these identify a character to the rest of the game, and a Stage that
  // fields somebody who has been renamed out from under it is a project that
  // will not save at all.
  await control.fill(before);
  await control.blur();
}

test("a record's notes are the project's before the field is left",
  async ({ page }) => {
    const failures = watchForFailures(page);
    await openSample(page);
    // Every record in the game carries `notes` through this one form, so a
    // character stands in for classes, weapons, items, factions, abilities,
    // objectives and maps alike.
    await page.locator(".project-sections button", { hasText: "Characters" })
      .click();
    await page.locator(".records-fold > summary").click();
    await page.locator(".record-list li button").first().click();
    await page.locator(".advanced-fields > summary").first().click();

    await typingIsWorkInProgress(page, "#field-notes", "Fights beside the ford.");

    // And a Save that never moves the focus persists what is on screen. The
    // click is dispatched rather than performed for exactly that reason: a
    // pointer press would leave the field first, which is the one road that
    // already worked. A crash, a closed tab and a keyboard-driven save are the
    // same case.
    await page.locator("#field-notes").click();
    await page.locator("#field-notes").press("ControlOrMeta+a");
    await page.locator("#field-notes").pressSequentially("Holds the north bank.");
    await save(page).dispatchEvent("click");
    await expect(status(page)).toContainText("Saved in this browser");

    await page.reload();
    await page.locator(".project-sections button", { hasText: "Characters" })
      .click();
    await page.locator(".records-fold > summary").click();
    await page.locator(".record-list li button").first().click();
    await page.locator(".advanced-fields > summary").first().click();
    await expect(page.locator("#field-notes"))
      .toHaveValue("Holds the north bank.");

    expect(failures).toEqual([]);
  });

test("what a campaign's own page is typed into is work in progress",
  async ({ page }) => {
    const failures = watchForFailures(page);
    await openSample(page);
    await page.locator(".project-sections button", { hasText: "Flow" }).click();

    await typingIsWorkInProgress(page, "#flow-campaign-name", "The Long Retreat");

    // The company and the store beside it. A member's identifier is how a
    // Stage names them and their differences are numbers, and all of them
    // reached the project only when the field was left.
    if (await page.locator("#roster-0-name").count() === 0) {
      await page.getByRole("button", { name: "Add member" }).click();
    }
    await typingIsWorkInProgress(page, "#roster-0-name", "The ferryman");
    await typingIsWorkInProgress(page, "#roster-0-id", "ferryman");
    await typingIsWorkInProgress(page, "#roster-0-notes", "Knows the crossing.");
    await page.locator(".roster-member details > summary").first().click();
    await typingIsWorkInProgress(page, "#roster-0-stat-health", "4");
    await typingIsWorkInProgress(page, "#roster-0-range-bonus", "2");

    if (await page.locator("#starting-store-0-quantity").count() === 0) {
      await page.getByRole("button", { name: "Add starting stock" }).click();
    }
    await typingIsWorkInProgress(page, "#starting-store-0-quantity", "12");
    await typingIsWorkInProgress(page, "#starting-store-0-notes", "For the road.");

    // The road itself, behind the fold under the graph.
    await page.locator(".flow-detail > summary").click();
    await typingIsWorkInProgress(page, "#campaign-node-notes", "The gates shut.");
    await typingIsWorkInProgress(page, "#campaign-node-id", "theford");
    await typingIsWorkInProgress(page, "#transition-0-notes", "Always taken.");

    expect(failures).toEqual([]);
  });

test("what a Stage is typed into is work in progress", async ({ page }) => {
  const failures = watchForFailures(page);
  await openSample(page);
  await page.locator(".project-sections button", { hasText: "Stages" }).click();
  await page.locator(".record-list ul button").first().click();
  await expect(page.locator("#stage-name")).toBeVisible();

  await typingIsWorkInProgress(page, "#stage-name", "The Ford at Dusk");
  // The note about where your own side stands exists only once the Stage has a
  // region to write it on, and the capacity is what makes one.
  if (await page.locator("#stage-deployment-notes").count() === 0) {
    await page.locator("#stage-deployment-capacity").fill("6");
    await page.locator("#stage-deployment-capacity").blur();
  }
  await typingIsWorkInProgress(page, "#stage-deployment-notes", "Behind the wall.");
  await typingIsWorkInProgress(page, "#stage-deployment-capacity", "8");

  // And a Save from the keyboard, with the words still in the field, persists
  // them: the Stage editor is several components deep and every one of them
  // has to be on the road `flushDrafts` walks.
  const name = page.locator("#stage-name");
  await name.click();
  await name.press("ControlOrMeta+a");
  await name.pressSequentially("The Ford at Dawn");
  await save(page).dispatchEvent("click");
  await expect(status(page)).toContainText("Saved in this browser");

  await page.reload();
  await page.locator(".project-sections button", { hasText: "Stages" }).click();
  await page.locator(".record-list ul button").first().click();
  await expect(page.locator("#stage-name")).toHaveValue("The Ford at Dawn");

  expect(failures).toEqual([]);
});

test("a redraw never takes back a number an author is still typing",
  async ({ page }) => {
    const failures = watchForFailures(page);
    await openSample(page);
    await page.locator(".project-sections button", { hasText: "Flow" }).click();
    if (await page.locator("#roster-0-name").count() === 0) {
      await page.getByRole("button", { name: "Add member" }).click();
    }
    await settle(page);

    await page.locator(".roster-member details > summary").first().click();
    const bonus = page.locator("#roster-0-range-bonus");
    await bonus.click();
    await bonus.press("ControlOrMeta+a");
    await bonus.pressSequentially("12");

    // Something else writes the campaign while the number is half said, here
    // the store beside the company, which is the same record. A control bound
    // straight at the stored value writes the stored number back over the typed
    // one, with no input event to say so and no change event on the way out.
    await page.getByRole("button", { name: "Add starting stock" })
      .dispatchEvent("click");
    await expect(page.locator(".save-status"))
      .toContainText("Saved the campaign's starting store");

    await expect(bonus).toHaveValue("12");

    expect(failures).toEqual([]);
  });
