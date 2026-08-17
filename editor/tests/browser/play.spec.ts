// SPDX-License-Identifier: MIT
import { expect, test, type Page } from "@playwright/test";

// Asserted natively in games/demo/src/play_demo.cpp, reached there by compiling
// games/demo/source/project.json into a package and applying this same command
// sequence. Seeing the identical value in a browser is the whole point of this
// suite: it proves the deployed WebAssembly build is the authoritative engine
// and not a lookalike.
// Padded to sixteen digits, which is how the editor renders it.
const nativeDemoCompletedHash = "673e5a59765c94c5";

/** Records anything the page reports as broken, including CSP violations. */
function watchForFailures(page: Page): string[] {
  const failures: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") failures.push(`console: ${message.text()}`);
  });
  page.on("pageerror", (error) => failures.push(`pageerror: ${error.message}`));
  return failures;
}

/**
 * Loads a bundled sample by id. The demo is not the default sample.
 *
 * The examples live on the start screen, which the editor opens on only when
 * it has nothing stored, since after a reload it goes straight back to the
 * draft, so this asks for the way in when it is not already up.
 */
async function loadSample(page: Page, id: string) {
  if (await page.locator("#start").count() === 0) {
    await page.getByRole("button", { name: "Start screen" }).click();
  }
  await page.locator(`input[name="start-sample"][value="${id}"]`).check();
  await page.getByRole("button", { name: "Open this example" }).click();
}

/**
 * Opens the browser playtest, which lives under Diagnostics.
 *
 * It lives there rather than on the page an author lands on, where it would be
 * one more thing competing with the game's own settings. It is a smaller game
 * than ▶ Play, being one Stage, a mode switch and no story, and the one thing
 * it has that Play has not is the engine's own fingerprint of authoritative state,
 * which is the thing this suite is here to read.
 */
async function openPlaytest(page: Page) {
  await page.locator('nav[aria-label="Project"]')
    .getByRole("button", { name: /Diagnostics/ }).click();
  await page.getByRole("button", { name: "Run the Stage" }).click();
}

function cell(page: Page, x: number, y: number) {
  return page.locator(`[aria-label^="Position ${x}, ${y},"]`);
}

test.beforeEach(async ({ page }) => {
  // Loading a sample over the unsaved initial draft asks for confirmation.
  page.on("dialog", (dialog) => void dialog.accept());
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Grandleon Editor" })).toBeVisible();
});

test("loads the WebAssembly engine under the production CSP", async ({ page }) => {
  const failures = watchForFailures(page);

  await loadSample(page, "demo");
  await openPlaytest(page);

  // The board only renders once the engine instantiated and accepted the
  // Stage. Under a CSP without 'wasm-unsafe-eval' this never appears.
  await expect(page.locator('[role="gridcell"]')).toHaveCount(24);
  await expect(page.locator("[data-canonical-hash]")).toHaveText(/^[0-9a-f]{16}$/);
  await expect(page.getByText("Could not load the game engine")).toHaveCount(0);

  expect(
    failures.filter((failure) => /Content Security Policy|WebAssembly|CompileError/i.test(failure))
  ).toEqual([]);
  expect(failures).toEqual([]);
});

test("playing the demo reproduces the native canonical hash", async ({ page }) => {
  const failures = watchForFailures(page);

  await loadSample(page, "demo");
  await openPlaytest(page);
  await expect(page.locator('[role="gridcell"]')).toHaveCount(24);

  const attack = page.getByRole("button", { name: "Attack", exact: true });

  // The same four commands the native reference playthrough applies, and the
  // first two are one turn: the rider walks onto the bridge and strikes
  // without the picket being handed the board in between, which is what its
  // second action point is for.
  await cell(page, 0, 1).click();
  await cell(page, 1, 1).click();

  await cell(page, 1, 1).click();
  await attack.click();
  await cell(page, 2, 1).click();

  await cell(page, 2, 1).click();
  await attack.click();
  await cell(page, 1, 1).click();

  await cell(page, 1, 1).click();
  await attack.click();
  await cell(page, 2, 1).click();

  await expect(page.locator(".playtest-status")).toContainText("Your side won");
  await expect(page.locator("[data-canonical-hash]")).toHaveText(
    nativeDemoCompletedHash
  );
  expect(failures).toEqual([]);
});

test("Play opens a full-screen surface that finishes a Stage", async ({ page }) => {
  const failures = watchForFailures(page);

  await loadSample(page, "demo");
  await page.getByRole("button", { name: "▶ Play" }).click();

  const play = page.getByRole("dialog");
  await expect(play).toBeVisible();
  // The company stands before the board, so the surface opens on it.
  await play.getByRole("button", { name: "To the Stage" }).click();
  await expect(play.locator('[role="gridcell"]')).toHaveCount(24);
  await expect(play.getByRole("button", { name: /Back to editing/ })).toBeVisible();

  const playCell = (x: number, y: number) =>
    play.locator(`[aria-label^="Position ${x}, ${y},"]`);

  // The player steers the first side only. The second side acts on its own
  // from the behaviour authored on each placement, so nobody drives red here.
  // The walk does not end the rider's turn, so it arrives unhurt and still
  // holding the point it will strike with.
  await playCell(0, 1).click();
  await playCell(1, 1).click();
  await expect(playCell(1, 1)).toHaveAttribute("aria-label", /HP 7 of 7/);

  for (let round = 0; round < 6; round += 1) {
    if (await play.getByText(/wins!/).count()) break;
    await playCell(1, 1).click();
    await playCell(2, 1).click();
  }
  await expect(play.getByText(/wins!/)).toBeVisible();

  await play.getByRole("button", { name: /Back to editing/ }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  expect(failures).toEqual([]);
});

test("Play runs the authored campaign: scenes from the engine, then the Stage", async ({ page }) => {
  const failures = watchForFailures(page);

  // The default sample's campaign opens on three story nodes. Their text must
  // come through the deployed WebAssembly module's dialogue loader before any
  // board exists.
  await loadSample(page, "tarnholt");
  await page.getByRole("button", { name: "▶ Play" }).click();

  const play = page.getByRole("dialog");
  await expect(play).toBeVisible();
  const headline = play.locator("#play-headline");
  await expect(headline).toHaveText("Prologue");
  await expect(play.getByText("Then they want the valley.")).toBeVisible();
  await expect(play.locator('[role="gridcell"]')).toHaveCount(0);

  for (const scene of ["The Valley", "Waking the Guard"]) {
    await play.getByRole("button", { name: "Continue" }).click();
    await expect(headline).toHaveText(scene);
  }
  await play.getByRole("button", { name: "Continue" }).click();

  // The company stands between the last scene and the board, as it stands
  // before every board a kept campaign fights.
  await expect(headline).toHaveText("Before The Fordlight Crossing");
  await play.getByRole("button", { name: "To the Stage" }).click();

  // Fordlight Crossing is 32 x 8: the Stage the campaign cursor named. Play
  // draws every cell of it, having a page to draw on rather than a console
  // screen, which is why this board scrolls on both consoles and not here.
  await expect(play.locator('[role="gridcell"]')).toHaveCount(256);
  await expect(headline).toHaveText("Your turn. Pick someone.");

  await play.getByRole("button", { name: /Back to editing/ }).click();
  expect(failures).toEqual([]);
});

test("Play keeps the campaign: a rider lost for good is off the next board", async ({
  page
}) => {
  const failures = watchForFailures(page);

  // The demo's second campaign, The Muster Road, is the content that has every
  // campaign rule in it: two riders the player keeps, a growth block, an item
  // that drops, and a second map that still lists the rider who dies on the
  // first. Everything asserted below is derived inside the deployed WebAssembly
  // module by the same C++ the terminal client runs.
  await loadSample(page, "demo");
  await page.getByRole("button", { name: "▶ Play" }).click();

  const play = page.getByRole("dialog");
  await expect(play).toBeVisible();
  await play.locator(".play-campaign select").selectOption("muster_road");

  const headline = play.locator("#play-headline");
  const playCell = (x: number, y: number) =>
    play.locator(`[aria-label^="Position ${x}, ${y},"]`);

  // The company, before the first board. Both riders are going, and each is
  // holding the draught the founding put in their hands.
  await expect(headline).toHaveText("Before The Skirmish at the Crossing");
  await expect(play.locator(".play-manage")).toContainText("Vanguard Rilla");
  await expect(play.locator(".play-manage")).toContainText("1 × Field Tonic");
  await play.getByRole("button", { name: "To the Stage" }).click();

  // Three on the board: both riders, and the picket on the far bank.
  await expect(play.locator("g.unit")).toHaveCount(3);
  await expect(play.locator(".play-excluded")).toHaveCount(0);

  // The crossing opens on the region the content authors, in a real browser:
  // a click on a rider lights the western bank, a click on a lit tile stands
  // it there, and the fighting begins because the player says so. The vanguard
  // is moved off the tile the author put it on and back onto it, so the fight
  // below is the fight the rest of this test pins.
  //
  // Everything below names the rider rather than her character type, and that
  // is the point of asking: the board is arranged by a player who knows these
  // people, and `Vanguard Rilla` is who they know. A prompt reading
  // `Where should Dawn Guard 1 stand?` would be the deployment screen calling
  // her something the log three screens later does not.
  const standingAt = (x: number, y: number) =>
    play.locator(
      `[aria-label^="Position ${x}, ${y}, "][aria-label*="Vanguard Rilla"]`
    );
  await expect(headline).toHaveText("Deployment. Pick someone.");
  await playCell(0, 1).click();
  await expect(headline).toHaveText("Where should Vanguard Rilla stand?");
  await playCell(0, 2).click();
  await expect(standingAt(0, 2)).toHaveCount(1);
  await expect(standingAt(0, 1)).toHaveCount(0);
  await playCell(0, 1).click();
  await expect(standingAt(0, 1)).toHaveCount(1);
  await play.getByRole("button", { name: "Begin the fighting" }).click();
  await expect(headline).toHaveText("Your turn. Pick someone.");

  // The crossing. The outrider trades with the picket and both are left on one;
  // the outrider holds and the picket's next swing fells it; the vanguard then
  // rides onto the emptied tile and finishes the picket in a single turn.
  await playCell(2, 1).click();
  await playCell(3, 1).click();
  // The outrider stands still with the picket on one, and the swing that comes
  // back is the one it does not survive.
  await playCell(2, 1).click();
  await play.locator(".play-wait").click();
  // Then the vanguard rides onto the tile its companion fell from and strikes
  // in the same turn, which is what the second action point buys.
  await playCell(0, 1).click();
  await playCell(2, 1).click();
  await playCell(3, 1).click();
  await expect(play.getByText(/wins!/)).toBeVisible();

  // The aftermath: the permadeath, the level and what it granted, the drop,
  // who the crossing brought in, and where the campaign went. Not one of these
  // numbers exists in TypeScript, and not one of these names is derived from a
  // unit type: they are what the campaign's author called these people.
  await play.getByRole("button", { name: "Continue" }).click();
  await expect(headline).toHaveText("After The Skirmish at the Crossing");
  await expect(
    play.getByText("Outrider Bevan died, and will not come back.")
  ).toBeVisible();
  await expect(play.getByText("Vanguard Rilla earned 60.")).toBeVisible();
  await expect(
    play.getByText(/Vanguard Rilla reached level 2/)
  ).toBeVisible();
  await expect(
    play.getByText("Torvald the Ferryman joined the company.")
  ).toBeVisible();
  // The two owners a campaign keeps, on screen. Nobody drank in this run and
  // the picket kept its own, being authored to leave a tonic three times in
  // five with the draw off this battle's seeded drop stream not coming up, so
  // the stores hold exactly the one the fallen rider was still carrying, a
  // fallen member leaving their kit behind, while the survivors hold their own.
  // The pick-up line is asserted absent, because a screen claiming something
  // fell is as wrong as one hiding that it did.
  await expect(play.getByText(/Picked up/)).toHaveCount(0);
  await expect(play.getByText("In the stores: 1 × Field Tonic.")).toBeVisible();
  await expect(
    play.getByText("Vanguard Rilla: 1 × Field Tonic")
  ).toBeVisible();
  await expect(play.getByText("Next: The Watch on the Road.")).toBeVisible();

  // And then the aftermath acts. Continue opens the company, where the draught
  // the crossing left the army is a thing a player can do something with.
  await play.getByRole("button", { name: "Continue" }).click();
  await expect(headline).toHaveText("Before The Watch on the Road");
  await expect(play.locator(".play-manage-store")).toContainText(
    "1 × Field Tonic"
  );

  // The tonic the crossing left the company goes into the survivor's hand. The
  // gesture is a committed campaign fact before the screen redraws: there is no
  // Apply, and the campaign is written to its slot as it is made.
  await play.locator(".play-manage-give").first().click();
  await expect(play.locator(".play-manage-members")).toContainText(
    "2 × Field Tonic"
  );

  // And the ferryman is left behind, which is the player choosing who takes the
  // field. The engine leaves him off exactly as it leaves off the dead.
  await play.locator(".play-manage-bench").last().click();
  await expect(play.locator(".play-manage")).toContainText("staying behind");

  // The next board comes through the roster. The authored map lists four; the
  // campaign fields two, the survivor and the picket, and the surface says
  // who is missing, without saying which of them chose it.
  await play.getByRole("button", { name: "To the Stage" }).click();
  await expect(headline).toHaveText("Your turn. Pick someone.");
  await expect(play.locator("g.unit")).toHaveCount(2);
  await expect(play.locator(".play-excluded")).toContainText("Outrider Bevan");
  await expect(play.locator(".play-excluded")).toContainText(
    "Torvald the Ferryman"
  );

  // The survivor carries what the first Stage taught them: the author wrote
  // seven health, and the rider who crossed the river has eight.
  await expect(playCell(0, 1)).toHaveAttribute("aria-label", /HP 8 of 8/);

  await play.getByRole("button", { name: /Back to editing/ }).click();
  expect(failures).toEqual([]);
});

/**
 * Fights the crossing on the demo's Muster Road, exactly as the campaign test
 * above fights it, and stops on the aftermath.
 *
 * The point of the two tests below is what happens to the campaign *after*
 * this, so the fight itself is shared rather than restated.
 */
async function fightTheCrossing(page: Page) {
  await loadSample(page, "demo");
  await page.getByRole("button", { name: "▶ Play" }).click();
  const play = page.getByRole("dialog");
  await expect(play).toBeVisible();
  await play.locator(".play-campaign select").selectOption("muster_road");

  const headline = play.locator("#play-headline");
  const playCell = (x: number, y: number) =>
    play.locator(`[aria-label^="Position ${x}, ${y},"]`);
  await expect(headline).toHaveText("Before The Skirmish at the Crossing");
  await play.getByRole("button", { name: "To the Stage" }).click();
  await play.getByRole("button", { name: "Begin the fighting" }).click();
  await expect(headline).toHaveText("Your turn. Pick someone.");

  await playCell(2, 1).click();
  await playCell(3, 1).click();
  // The outrider stands still with the picket on one, and the swing that comes
  // back is the one it does not survive.
  await playCell(2, 1).click();
  await play.locator(".play-wait").click();
  // Then the vanguard rides onto the tile its companion fell from and strikes
  // in the same turn, which is what the second action point buys.
  await playCell(0, 1).click();
  await playCell(2, 1).click();
  await playCell(3, 1).click();
  await expect(play.getByText(/wins!/)).toBeVisible();

  // The commit. This is the save the browser has to be holding afterwards: the
  // rider is buried, the survivor is levelled, the picket's tonic is in the
  // stores, and the ferryman has joined.
  await play.getByRole("button", { name: "Continue" }).click();
  await expect(headline).toHaveText("After The Skirmish at the Crossing");
  await expect(
    play.getByText("Outrider Bevan died, and will not come back.")
  ).toBeVisible();
  return play;
}

/** Reloads the page and puts the same game back in front of the editor. */
async function reloadWith(page: Page, sample: string) {
  await page.reload();
  await expect(page.getByRole("heading", { name: "Grandleon Editor" })).toBeVisible();
  await loadSample(page, sample);
}

test("a playtest campaign is still there after the page reloads", async ({
  page
}) => {
  const failures = watchForFailures(page);

  await fightTheCrossing(page);

  // The page goes away. Everything the WebAssembly module was holding goes
  // with it: the campaign session, its roster, and the slot device it saved
  // into. What is left is what the browser kept.
  await reloadWith(page, "demo");
  await page.getByRole("button", { name: "▶ Play" }).click();
  const play = page.getByRole("dialog");
  await play.locator(".play-campaign select").selectOption("muster_road");
  const headline = play.locator("#play-headline");

  // The offer exists because there is something to pick up, and the press
  // that would throw it away says so on itself.
  await expect(play.locator(".play-restart")).toHaveAttribute(
    "title", "Replaces the campaign this browser is keeping."
  );
  await play.getByRole("button", { name: "Pick up where I left off" }).click();

  // The campaign is where it was left: standing before the second map, with
  // the stores holding the one tonic the crossing left the company.
  await expect(headline).toHaveText("Before The Watch on the Road");
  await expect(play.locator(".play-manage-store")).toContainText(
    "1 × Field Tonic"
  );
  await expect(play.locator(".play-manage")).toContainText(
    "Torvald the Ferryman"
  );

  // And the dead stay dead across the reload. The authored map lists the rider
  // the crossing buried; the roster refuses to field them and says so.
  await play.getByRole("button", { name: "To the Stage" }).click();
  await expect(headline).toHaveText("Your turn. Pick someone.");
  await expect(play.locator(".play-excluded")).toContainText("Outrider Bevan");
  // The survivor still carries what the first Stage taught them.
  await expect(
    play.locator('[aria-label^="Position 0, 1,"]')
  ).toHaveAttribute("aria-label", /HP 8 of 8/);

  await play.getByRole("button", { name: /Back to editing/ }).click();
  expect(failures).toEqual([]);
});

test("a kept campaign whose content moved is refused by name", async ({
  page
}) => {
  const failures = watchForFailures(page);

  await fightTheCrossing(page);
  await reloadWith(page, "demo");

  // The author edits the game and says so, which is what a content revision is
  // for. The kept campaign was written against the revision before this one and
  // no migration is registered for the step.
  // Which revision the game is, is a game-wide setting, so it is raised on the
  // game settings page.
  await page.locator(".project-sections button", { hasText: "Game" })
    .click();
  // Behind Advanced: nobody is asked for a revision before a game exists, and
  // raising one is exactly the deliberate act that fold is for.
  await page.locator(".game-settings details.advanced-fields > summary").click();
  await page.getByLabel("Content revision").fill("0.1.1");
  await page.getByRole("button", { name: "Save game settings" }).click();

  await page.getByRole("button", { name: "▶ Play" }).click();
  const play = page.getByRole("dialog");
  await play.locator(".play-campaign select").selectOption("muster_road");
  await play.getByRole("button", { name: "Pick up where I left off" }).click();

  // Refused, in the migration registry's own word, with the way forward in the
  // same sentence, rather than silently founding a fresh campaign and letting
  // the author wonder where theirs went.
  const refused = play.locator(".play-refused");
  await expect(refused).toContainText("missing_step");
  await expect(refused).toContainText("Start fresh to replace it");
  // A founded campaign is standing there to play, and it is not presented as
  // the resumed one.
  await expect(play.locator("#play-headline")).toHaveText(
    "Before The Skirmish at the Crossing"
  );

  // Start fresh replaces what would not load, and the offer goes with it.
  await play.getByRole("button", { name: "Start fresh" }).click();
  await expect(play.locator(".play-refused")).toHaveCount(0);
  await expect(play.locator(".play-resume")).toHaveCount(0);

  await play.getByRole("button", { name: /Back to editing/ }).click();
  expect(failures).toEqual([]);
});

test("joins two Stages by dragging, and plays the road that makes", async ({
  page
}) => {
  const failures = watchForFailures(page);
  // A campaign's shape is a wide thing and the graph is drawn at the width it
  // is given. Four stops need more room than the default window has.
  await page.setViewportSize({ width: 1500, height: 900 });

  // The Bridge at Dawn is one fight and an ending. This gives it a second
  // fight, joins the first to it by dragging, and then plays the campaign to
  // see the engine arrive where the drag said it would.
  await loadSample(page, "demo");
  const rail = page.locator('nav[aria-label="Project"]');

  // A second fight on the same ground. One map can be fought over as many
  // times as you like, and the control that means "one more here" says so.
  await rail.getByRole("button", { name: "Stages" }).click();
  await page.getByRole("button", { name: "Add another Stage on this ground" })
    .click();
  const surface = page.locator(".stage-editor");
  await expect(surface).toContainText("A Stage fought on Dawn Bridge");

  // Somebody on each side of it, put down by pressing the board. A Stage
  // nobody stands on is not one the campaign can play through.
  // Both on the grass of row two: Dawn Bridge is water and mountain elsewhere,
  // and the engine refuses a board that stands somebody where they could never
  // have walked.
  await surface.locator('[data-palette="unit:dawn_guard_unit"]').click();
  await surface.getByRole("button", { name: "Your side" }).click();
  await surface.locator('[data-cell="6"]').click();
  await surface.locator('[data-palette="unit:river_watch_unit"]').click();
  await surface.getByRole("button", { name: "The enemy" }).click();
  await surface.locator('[data-cell="10"]').click();
  await expect(surface.locator(".placed-unit")).toHaveCount(2);
  // And something that ends it. An objective is a shared record, so the way
  // the demo's first fight is won is the way this one is won too.
  await surface.getByLabel("Defeat all opponents").check();

  // The graph. The road runs through the ending the demo used to stop at, and
  // the new Stage hangs off the far side of it.
  await rail.getByRole("button", { name: "Flow" }).click();
  const graph = page.locator(".flow-graph-panel");
  await expect(graph.locator('[data-stop="bridge_encounter"]')).toBeVisible();
  await expect(graph.locator('[data-stop="stage_dawn_bridge"]')).toBeVisible();
  await expect(graph.locator('[data-way-out="bridge_encounter/finish_demo"]'))
    .toContainText("The Road Opens");

  // The gesture the owner asked for: a Stage's output, put on another Stage.
  // Nothing is typed and no identifier is chosen: the road is joined by
  // picking the way out up and dropping it where it should go.
  await graph.locator('[data-way-out="bridge_encounter/finish_demo"]').dragTo(
    graph.locator('[data-stop="stage_dawn_bridge"]')
  );
  await expect(graph.locator('[data-way-out="bridge_encounter/finish_demo"]'))
    .toContainText("Stage at Dawn Bridge");
  // Said as the author's own act, which is what the undo entry is called too.
  await expect(page.locator(".save-status"))
    .toContainText("Send A Friendly Rivalry on to Stage at Dawn Bridge");

  // And now the engine walks it. The first fight is the demo's own, fought the
  // same way the test above fights it; what comes after it is the thing the
  // drag decided, and it comes out of the deployed WebAssembly module rather
  // than out of this file.
  await page.getByRole("button", { name: "▶ Play" }).click();
  const play = page.getByRole("dialog");
  await expect(play).toBeVisible();
  const headline = play.locator("#play-headline");
  await play.getByRole("button", { name: "To the Stage" }).click();

  const playCell = (x: number, y: number) =>
    play.locator(`[aria-label^="Position ${x}, ${y},"]`);
  await playCell(0, 1).click();
  await playCell(1, 1).click();
  for (let round = 0; round < 6; round += 1) {
    if (await play.getByText(/wins!/).count()) break;
    await playCell(1, 1).click();
    await playCell(2, 1).click();
  }
  await expect(play.getByText(/wins!/)).toBeVisible();

  // The Bridge at Dawn used to stop here. It does not any more, and where it
  // goes instead is named by the engine, out of the road the drag rewrote.
  await play.getByRole("button", { name: "Continue" }).click();
  await expect(headline).toHaveText("After A Friendly Rivalry");
  await expect(play.getByText("Next: Stage at Dawn Bridge.")).toBeVisible();

  // And it arrives. The company stands before the second board, and then on
  // it: two characters, which is what this test put there and what nothing
  // else in the demo has.
  await play.getByRole("button", { name: "Continue" }).click();
  await expect(headline).toHaveText("Before Stage at Dawn Bridge");
  await play.getByRole("button", { name: "To the Stage" }).click();
  await expect(play.locator('[role="gridcell"]')).toHaveCount(24);
  await expect(play.locator("g.unit")).toHaveCount(2);

  await play.getByRole("button", { name: /Back to editing/ }).click();
  expect(failures).toEqual([]);
});

test("opens a map in the map editor without the error boundary", async ({ page }) => {
  const failures = watchForFailures(page);

  // The default sample, exactly as a fresh visitor gets it. Handing the map
  // editor a Vue reactive proxy throws structuredClone into the error
  // boundary, and only a real browser ever shows it.
  await loadSample(page, "tarnholt");
  await page.locator(".project-sections button", { hasText: "Maps" }).click();
  await page.locator(".record-list button", { hasText: "Fordlight Crossing" })
    .click();

  await expect(page.locator(".terrain-grid")).toBeVisible();
  await expect(
    page.getByText("The workspace could not be displayed")
  ).toHaveCount(0);
  expect(failures).toEqual([]);
});
