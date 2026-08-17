// The shape of the Stage editor, measured rather than described.
//
// This surface was one column 442 px wide and some four thousand pixels tall,
// and an author met it by scrolling. Three things made it that, and each of
// them is pinned below, because a layout is the kind of thing that grows back
// one section at a time and nothing else in this suite would notice.
//
// The numbers are ceilings with room in them, not photographs. What they are
// defending is the shape: a palette that is a strip, a panel that sits beside
// the board it describes, and a fold that costs nothing to open.
import { test, expect, type Page } from "@playwright/test";

async function openTheFordlight(page: Page) {
  await page.goto("/");
  if ((await page.locator("#start").count()) === 0) {
    await page.getByRole("button", { name: "Start screen" }).click();
  }
  await page.locator('input[name="start-sample"][value="tarnholt"]').check();
  await page.getByRole("button", { name: "Open this example" }).click();
  await page
    .locator('nav[aria-label="Project"]')
    .getByRole("button", { name: "Stages" })
    .click();
  await page.getByRole("button", { name: "Open The Fordlight Crossing" }).click();
  await expect(page.locator(".placement-editor").first()).toBeVisible();
}

test("the palette is a strip, however many characters it offers", async ({
  page
}) => {
  await page.setViewportSize({ width: 1600, height: 900 });
  await openTheFordlight(page);

  // Tarnholt offers a dozen characters and this campaign's own besides. Wrapped
  // into a block they stood eight tall before the board began, so the first
  // thing this editor showed was a list and the board was under the fold.
  const palette = await page.locator(".palette-units").first().boundingBox();
  expect(palette).not.toBeNull();
  expect(palette!.height).toBeLessThan(90);
});

test("the panel about a character sits beside the board, and costs no height",
  async ({ page }) => {
    await page.setViewportSize({ width: 1600, height: 900 });
    await openTheFordlight(page);

    const stage = page.locator(".stage-editor").first();
    const closed = (await stage.boundingBox())!.height;

    // Pressing somebody opens the panel about them. It used to open below the
    // board, which is a panel an author scrolls away from the thing it
    // describes, and it made the page taller every time.
    await page.locator(".placed-unit").first().click();
    const panel = page.locator(".placement-fields.selected");
    await expect(panel).toBeVisible();

    const board = (await page.locator(".placement-grid-wrap").first().boundingBox())!;
    const box = (await panel.boundingBox())!;
    expect(box.x).toBeGreaterThan(board.x + board.width - 5);
    expect((await stage.boundingBox())!.height).toBe(closed);
  });

test("a Stage is shorter than the screen is tall, several times over",
  async ({ page }) => {
    await page.setViewportSize({ width: 1600, height: 900 });
    await openTheFordlight(page);

    // The Fordlight is the campaign's busiest Stage on this count: eight
    // characters, scenes either side, and a campaign carrying seven ways to
    // win. A ceiling with room in it, so ordinary authoring does not trip it
    // and another four-thousand-pixel column cannot arrive unnoticed.
    const stage = (await page.locator(".stage-editor").first().boundingBox())!;
    expect(stage.height).toBeLessThan(4600);

    // The conditions this Stage has not taken are sentences and ticks. Seven
    // objectives drawing seven forms was over two thousand pixels of it.
    const conditions = (await page.locator(".win-conditions").boundingBox())!;
    expect(conditions.height).toBeLessThan(1400);
  });
