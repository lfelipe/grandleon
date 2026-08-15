// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// The game settings page in a real browser, against the checked-in Tarnholt
// campaign. The point of the page is a navigation and a consequence: reach it,
// change the setting that shapes every Stage, and read back which Stages it
// reaches. Neither is observable without a browser that actually renders
// the section switch.

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

test("changes the game's turn order and says which Stages it reaches", async ({
  page
}) => {
  const failures = watchForFailures(page);

  await page.locator(".project-sections button", { hasText: "Game" })
    .click();
  const settings = page.locator(".game-settings");
  await expect(settings).toBeVisible();

  // Tarnholt states `sideBlocks` for the whole game and no board overrides it,
  // so the page opens on that choice with all six Stages following it and the
  // override list gone. That is the shape the setting is for: one answer, in
  // one place, reaching every board.
  const order = page.locator("#field-defaultTurnOrder");
  await expect(order).toHaveValue("sideBlocks");
  await expect(settings).toContainText(
    "Stages are ordered All of one side, then all of the other, in any order you pick"
  );
  await expect(settings).toContainText("6 Stages follow this setting.");
  await expect(settings.locator(".turn-order-overrides li")).toHaveCount(0);
  await expect(settings).toContainText(
    "No Stage overrides it, so changing it above changes them all."
  );

  await order.selectOption("initiative");
  await settings.getByRole("button", { name: "Save game settings" }).click();
  await expect(page.getByText("Saved game settings")).toBeVisible();
  await expect(settings).toContainText(
    "Stages are ordered Everyone mixed together, fastest first"
  );

  // A Stage that states nothing offers to keep following the game, and names
  // the order it would take rather than pretending to be alternating. The
  // order is on the Stage, so it is read where the Stage is set up.
  await page.locator(".project-sections button", { hasText: "Stages" })
    .click();
  await page.locator(".record-list button", { hasText: "The Fordlight Crossing" })
    .click();
  const boardOrder = page.locator("#stage-turn-order");
  await expect(boardOrder).toHaveValue("");
  await expect(boardOrder.locator("option").first()).toContainText(
    "Follow the game setting (Everyone mixed together, fastest first)"
  );

  // Stating an order here is an override, and the settings page says so
  // without the board having changed.
  await boardOrder.selectOption("sideBlocks");
  await page.locator(".project-sections button", { hasText: "Game" })
    .click();
  await expect(settings).toContainText("5 Stages follow this setting.");
  await expect(settings).toContainText(
    "One Stage chose its own order and keeps it"
  );
  await expect(settings.locator(".turn-order-overrides li")).toHaveCount(1);
  await expect(settings.locator(".turn-order-overrides li").first()).toContainText(
    "All of one side, then all of the other, in any order you pick"
  );

  // And changing the setting again leaves that board exactly where it is.
  await page.locator("#field-defaultTurnOrder").selectOption("alternating");
  await settings.getByRole("button", { name: "Save game settings" }).click();
  await expect(settings.locator(".turn-order-overrides li")).toHaveCount(1);
  await expect(settings.locator(".turn-order-overrides li").first()).toContainText(
    "All of one side, then all of the other, in any order you pick"
  );

  expect(failures).toEqual([]);
});

test("states what a fall costs, and keeps the testing aid off that list", async ({
  page
}) => {
  const failures = watchForFailures(page);

  await page.locator(".project-sections button", { hasText: "Game" })
    .click();
  const settings = page.locator(".game-settings");
  await expect(settings).toBeVisible();

  // Tarnholt states no rule, so the page opens on the empty choice, which is
  // the permanent loss every campaign meant before the setting existed.
  const loss = page.locator("#field-characterLoss");
  await expect(loss).toHaveValue("");
  await expect(loss.locator("option").nth(2)).toContainText(
    "A character who falls is carried off, and rejoins the company after the Stage"
  );

  await loss.selectOption("recoverable");
  await settings.getByRole("button", { name: "Save game settings" }).click();
  await expect(page.getByText("Saved game settings")).toBeVisible();
  await expect(loss).toHaveValue("recoverable");

  // The testing aid is on this page and nowhere near that menu: behind its own
  // closed lid, with its own save and its warning on the control itself.
  // Choosing between the two fates a fallen character can meet must never be a
  // place to find it, and neither must reading down the page.
  await expect(loss.locator("option")).toHaveCount(3);
  const testing = settings.locator("details.testing-aids");
  await expect(testing.locator("#testing-aids-title")).toHaveText("Testing");
  await expect(page.locator("#field-invulnerableForTesting")).toBeHidden();
  await testing.locator("#testing-aids-title").click();
  await expect(testing).toContainText("for debugging purposes");
  await expect(testing).toContainText("written into the file you export");

  // The control for a behaviour nothing implements yet is drawn, and says so.
  const skip = page.locator("#skip-to-next-map");
  await expect(skip).toBeDisabled();
  await expect(skip).not.toBeChecked();
  await expect(testing).toContainText("Player can directly skip to next map");
  await expect(testing).toContainText("Not implemented");

  const invulnerable = page.locator("#field-invulnerableForTesting");
  await expect(invulnerable).not.toBeChecked();
  await invulnerable.check();
  await testing.getByRole("button", { name: "Save testing aids" }).click();
  await expect(invulnerable).toBeChecked();

  // And it comes back off, because a switch that could only be turned on would
  // hand somebody a game nobody can lose.
  await invulnerable.uncheck();
  await testing.getByRole("button", { name: "Save testing aids" }).click();
  await expect(invulnerable).not.toBeChecked();

  expect(failures).toEqual([]);
});
