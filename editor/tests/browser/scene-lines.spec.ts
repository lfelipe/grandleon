// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// What an author types into a scene's lines, against the checked-in Tarnholt
// campaign. The question these ask is not "does the list editor save", which
// it does, but "when": a control whose work only reaches the project on an event
// nobody promises is a control that loses work, and the loss is silent because
// the field goes on showing the words that are already gone.

function watchForFailures(page: Page): string[] {
  const failures: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") failures.push(`console: ${message.text()}`);
  });
  page.on("pageerror", (error) => failures.push(`pageerror: ${error.message}`));
  return failures;
}

const status = (page: Page) => page.locator(".project-status");

/** Opens the sample, its interlude scene, and saves, so the project is clean. */
async function openSavedScene(page: Page) {
  page.on("dialog", (dialog) => void dialog.accept());
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Grandleon Editor" }))
    .toBeVisible();
  await page.getByRole("button", { name: "Open this example" }).click();
  await page.locator(".project-sections button", { hasText: "Scenes" }).click();
  await page.locator(".record-list button", { hasText: "Between the Rivers" })
    .click();
  // A saved, clean starting point, so that "unsaved changes" later can only be
  // the typing this test does.
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(status(page)).toContainText("Saved locally");
}

test("a scene line's keystrokes belong to the project before the field is left",
  async ({ page }) => {
    const failures = watchForFailures(page);
    await openSavedScene(page);

    const said = page.locator("#dialogue-line-0-text");
    const before = await said.inputValue();
    await said.click();
    await said.press("ControlOrMeta+a");
    await said.pressSequentially("The ford runs shallow in autumn.");

    // Still in the field. Every other text control in this editor, the record
    // form's own fields among them, reports work in progress from the first
    // keystroke, and the header is where an author reads it. A scene line that
    // does not is a scene line whose words the close-the-tab guard does not
    // know about.
    await expect(said).toBeFocused();
    await expect(status(page)).toContainText("Unsaved changes");

    // And a Save that does not move the focus must persist what is on screen.
    // The click is dispatched rather than performed for exactly that reason:
    // a pointer press would leave the field first, which is the one road that
    // already worked, and `flushDrafts` exists precisely for the roads that do
    // not. A crash, a closed tab and a keyboard-driven save are the same case.
    await page.getByRole("button", { name: "Save", exact: true })
      .dispatchEvent("click");
    await expect(status(page)).toContainText("Saved in this browser");

    await page.reload();
    await page.locator(".project-sections button", { hasText: "Scenes" }).click();
    await page.locator(".record-list button", { hasText: "Between the Rivers" })
      .click();
    const reloaded = page.locator("#dialogue-line-0-text");
    await expect(reloaded).toHaveValue("The ford runs shallow in autumn.");
    expect(await reloaded.inputValue()).not.toBe(before);

    expect(failures).toEqual([]);
  });

test("a speaker's name is the project's before the field is left",
  async ({ page }) => {
    const failures = watchForFailures(page);
    await openSavedScene(page);

    // The same control, the same disease: "Who speaks" is free text beside
    // "What they say" and joins a line to its cast entry by exact string.
    const speaker = page.locator("#dialogue-line-0-speaker");
    await speaker.click();
    await speaker.press("ControlOrMeta+a");
    await speaker.pressSequentially("The ferryman");

    await expect(speaker).toBeFocused();
    await expect(status(page)).toContainText("Unsaved changes");

    await page.getByRole("button", { name: "Save", exact: true })
      .dispatchEvent("click");
    await expect(status(page)).toContainText("Saved in this browser");

    await page.reload();
    await page.locator(".project-sections button", { hasText: "Scenes" }).click();
    await page.locator(".record-list button", { hasText: "Between the Rivers" })
      .click();
    await expect(page.locator("#dialogue-line-0-speaker"))
      .toHaveValue("The ferryman");

    expect(failures).toEqual([]);
  });

test("leaving a scene line and reloading keeps what was typed", async ({ page }) => {
  const failures = watchForFailures(page);
  await openSavedScene(page);

  // The road that already worked, kept so it goes on working: type, leave the
  // field, save, come back to it.
  const said = page.locator("#dialogue-line-1-text");
  await said.click();
  await said.press("ControlOrMeta+a");
  await said.pressSequentially("Sealed by a Warden of Kesh.");
  await said.blur();
  await expect(page.locator(".save-status")).toContainText("Saved scene lines");

  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(status(page)).toContainText("Saved in this browser");

  await page.reload();
  await page.locator(".project-sections button", { hasText: "Scenes" }).click();
  await page.locator(".record-list button", { hasText: "Between the Rivers" })
    .click();
  await expect(page.locator("#dialogue-line-1-text"))
    .toHaveValue("Sealed by a Warden of Kesh.");

  expect(failures).toEqual([]);
});

test("a re-render never takes back a line the author is still typing",
  async ({ page }) => {
    const failures = watchForFailures(page);
    await openSavedScene(page);

    const said = page.locator("#dialogue-line-0-text");
    await said.click();
    await said.press("ControlOrMeta+a");
    await said.pressSequentially("A good one, and a true one.");

    // Something else saves the same scene while this field is mid-word: the
    // cast editor naming a speaker, an undo, another surface writing the
    // record. The list is drawn again, and a control bound straight at the
    // stored line writes the stored words back over the typed ones, without
    // an input event to say so and without a change event on the way out.
    await page.locator(".dialogue-cast")
      .getByRole("button", { name: "Remove speaker 2" }).dispatchEvent("click");
    await expect(page.locator(".save-status")).toContainText("Saved who speaks");

    await expect(said).toHaveValue("A good one, and a true one.");

    expect(failures).toEqual([]);
  });
