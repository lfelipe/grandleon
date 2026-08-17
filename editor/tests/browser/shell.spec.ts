// SPDX-License-Identifier: MIT
import { expect, test } from "@playwright/test";

// The application shell in a real browser: the derived logo assets actually
// resolve and decode, and the left navigation only offers anchors that exist.
// happy-dom neither fetches images nor resolves fragment navigation, so both
// are only observable here.

test.beforeEach(async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Grandleon Editor" })).toBeVisible();
});

test("serves the header logo and the favicon", async ({ page }) => {
  const logo = page.locator("header .app-logo");
  await expect(logo).toBeVisible();
  // A broken image element is still "visible"; decoding is the real signal.
  const width = await logo.evaluate(
    (image) => (image as HTMLImageElement).naturalWidth
  );
  expect(width).toBeGreaterThan(0);
  await expect(logo).toHaveAttribute("alt", "");

  const favicon = await page.request.get("/favicon.png");
  expect(favicon.status()).toBe(200);
  expect(favicon.headers()["content-type"]).toContain("image/png");
});

test("the editor opens on the way in", async ({ page }) => {
  // Nothing stored, so the first thing an author meets is the question rather
  // than an empty workspace.
  await expect(page.locator("#start")).toBeVisible();
  await expect(page.getByRole("button", { name: "Start a new game" }))
    .toBeVisible();
  // The rail belongs to an open game and is not on screen before there is one.
  await expect(page.locator('nav[aria-label="Project"]')).toBeHidden();
});

test("jumps from a reported problem to the record it is about", async ({ page }) => {
  // Over a production build, so the analysis that raises the problem runs in
  // its real worker rather than in a stub.
  await page.getByRole("button", { name: "Start a new game" }).click();
  const rail = page.locator('nav[aria-label="Project"]');
  await rail.getByRole("button", { name: "Characters" }).click();
  // A character with no class to point at: the reference the analyzer refuses.
  // Made in the record columns, which stand behind a fold on this page: the
  // roster and the wizard lead it, and the wizard cannot make a broken one.
  await page.locator(".records-fold > summary").click();
  await page.getByRole("button", { name: "Create character" }).click();
  await page.getByRole("button", { name: "Validate" }).click();

  const diagnostics = rail.getByRole("button", { name: /Diagnostics/ });
  await expect(diagnostics).toContainText("problems found");
  await diagnostics.click();
  await page.getByRole("button", { name: /^Go to / }).first().click();

  await expect(rail.getByRole("button", { name: "Characters" }))
    .toHaveAttribute("aria-current", "page");
  // The jump promised a record, so it opened the fold the record is behind.
  await expect(page.locator(".records-fold")).toHaveAttribute("open", "");
  await expect(page.locator("#field-name")).toHaveValue("New Character");
});

test("every rail entry opens the section it names", async ({ page }) => {
  await page.getByRole("button", { name: "Start a new game" }).click();
  const rail = page.locator('nav[aria-label="Project"]');
  await expect(rail).toBeVisible();
  const entries = rail.locator("button");
  const count = await entries.count();
  expect(count).toBe(8);
  for (let index = 0; index < count; index += 1) {
    const entry = entries.nth(index);
    const label = (await entry.textContent())!.trim().split("\n")[0]!.trim();
    await entry.click();
    await expect(page.locator("#content-title"), label).toHaveText(label);
    await expect(entry, label).toHaveAttribute("aria-current", "page");
    await expect(
      rail.locator('button[aria-current="page"]'), label
    ).toHaveCount(1);
  }
});

test("a new project lands on configuration and nothing else", async ({ page }) => {
  await page.getByRole("button", { name: "Start a new game" }).click();
  await expect(page.locator("#content-title")).toHaveText("Game");
  // What an author is asked: what the game is called, and the three choices
  // that shape every Stage in it. Five controls, and every one of them is a
  // decision somebody can make before anything else exists.
  await expect(page.locator("#field-title")).toBeVisible();
  await expect(page.locator("#field-defaultTurnOrder")).toBeVisible();
  await expect(page.locator("#field-characterLoss")).toBeVisible();
  await expect(page.locator("#field-characterStyleId")).toBeVisible();
  await expect(page.locator("#field-themeId")).toBeVisible();
  // The two names for a machine are on the page and not in front of it. Both
  // are already answered, the id following the title, so an author meets them
  // only by opening the fold, and the fold says what it holds.
  const fold = page.locator(".game-settings details.advanced-fields");
  await expect(fold).toHaveCount(1);
  await expect(page.locator("#field-gameId")).toBeHidden();
  await expect(page.locator("#field-contentRevision")).toBeHidden();
  await fold.getByText("Advanced").click();
  await expect(page.locator("#field-gameId")).toBeVisible();
  await expect(page.locator("#field-contentRevision")).toBeVisible();
  await expect(fold).toContainText(
    "The name the file carries, and the content revision."
  );
  // And nothing else at all. The testing aids are on this page too, and they
  // are folded rather than absent. See the settings suite, which opens them.
  await expect(page.locator(".playtest-panel")).toHaveCount(0);
  await expect(page.locator("#field-notes")).toHaveCount(0);
  await expect(page.locator("#field-packageId")).toHaveCount(0);
  await expect(page.locator(".next-step")).toHaveCount(0);
});

test("Characters shows the characters, and the wizard makes another", async ({
  page
}) => {
  await page.getByRole("button", { name: "Start a new game" }).click();
  const rail = page.locator('nav[aria-label="Project"]');
  await rail.getByRole("button", { name: "Characters" }).click();

  // Nothing yet, so the page says what the one button is for.
  await expect(page.getByRole("heading", { name: "Your characters" }))
    .toBeVisible();
  await expect(page.getByText("No characters yet.")).toBeVisible();

  await page.getByRole("button", { name: "New character" }).click();
  const steps = page.locator(".wizard-steps li");
  await expect(steps).toHaveCount(3);
  await expect(steps.nth(0)).toHaveAttribute("aria-current", "step");
  // The heading takes focus, so a screen reader is told where it has arrived.
  await expect(page.locator("#character-wizard-title")).toBeFocused();

  await page.locator('input[name="character-wizard-side"][value="the_enemy"]')
    .check();
  await page.getByRole("button", { name: "Next" }).click();
  await expect(steps.nth(1)).toHaveAttribute("aria-current", "step");
  await page.getByRole("radio", { name: /Archer/ }).click();
  await page.getByRole("button", { name: "Next" }).click();
  await expect(steps.nth(2)).toHaveAttribute("aria-current", "step");
  await page.locator("#character-wizard-name").fill("Wren");
  await page.getByRole("button", { name: "Make them" }).click();

  // A card, drawn in the enemy's colour, saying nothing depends on them yet.
  const card = page.locator(".character-card");
  await expect(card).toHaveCount(1);
  await expect(card).toContainText("Wren");
  await expect(card).toContainText("An enemy");
  await expect(card).toContainText("Wren is not in any Stage yet.");
  await expect(card.locator("img")).toHaveAttribute("src", /archer_red/);

  // And everything it made is an ordinary record in its own collection,
  // behind the fold, where an author goes to change one rather than to make
  // their first.
  await page.locator(".records-fold > summary").click();
  const categories = page.locator(".content-categories");
  await categories.getByRole("button", { name: /^Factions/ }).click();
  await expect(page.locator(".record-list")).toContainText("The enemy");
  await categories.getByRole("button", { name: /^Classes/ }).click();
  // Named for the archetype Wren belongs to, not for Wren: a class is what
  // several characters share, and the next archer joins this one.
  await expect(page.locator(".record-list")).toContainText("Archer class");
});


test("draws a map in one section, sets a Stage up in another, and plays it",
  async ({ page }) => {
    // The split, walked end to end in a real browser. Two questions, two
    // entries on the rail: Maps is ground, Stages is the fight on it. An
    // author who has never read the source format goes from an empty project
    // to a playable Stage without meeting the format's word for one, or the
    // editor's own older word, anywhere on the way.
    const forbidden = /\b(encounter|battle)/i;
    const readable = async () => (await page.locator("body").innerText());

    await page.getByRole("button", { name: "Start a new game" }).click();
    const rail = page.locator('nav[aria-label="Project"]');
    expect(await readable()).not.toMatch(forbidden);

    // Two characters: one of each side, so a Stage can be fought.
    for (const [name, side] of [["Wren", "your_side"], ["Bandit", "the_enemy"]]) {
      await rail.getByRole("button", { name: "Characters" }).click();
      await page.getByRole("button", { name: "New character" }).click();
      await page.locator(`input[name="character-wizard-side"][value="${side}"]`)
        .check();
      await page.getByRole("button", { name: "Next" }).click();
      await page.getByRole("radio", { name: /Archer/ }).click();
      await page.getByRole("button", { name: "Next" }).click();
      await page.locator("#character-wizard-name").fill(name!);
      await page.getByRole("button", { name: "Make them" }).click();
    }
    expect(await readable()).not.toMatch(forbidden);

    // Ground. Maps draws terrain and nothing else: no board, no objectives,
    // no verb that writes a fight.
    await rail.getByRole("button", { name: "Maps" }).click();
    await page.getByRole("button", { name: "Create map" }).click();
    await page.locator(".record-list").getByRole("button", { name: /New Map/ })
      .click();
    await expect(page.locator(".terrain-grid")).toBeVisible();
    // What the consoles make of this ground, beside the fields that size it.
    // An author cannot see a telly from here and the answer is exact.
    const consoleFit = page.getByTestId("console-fit");
    await expect(consoleFit).toBeVisible();
    await expect(consoleFit).toContainText("Drawn whole on both consoles");
    // And it follows the field rather than the saved map: a size still being
    // typed is the size worth answering about.
    await page.locator("#map-width").fill("40");
    await expect(consoleFit).toContainText("Scrolls on both consoles");
    await expect(consoleFit).toContainText("21×14");
    // Guidance and not a refusal. Nothing is disabled, and it is not an alert.
    await expect(page.getByRole("button", { name: "Apply resize" }))
      .toBeEnabled();
    await expect(page.locator('[data-testid="console-fit"][role="alert"]'))
      .toHaveCount(0);
    await page.locator("#map-width").fill("3");
    await expect(consoleFit).toContainText("Drawn whole on both consoles");
    // A map with no Stage on it is a normal thing to have, not a warning.
    await expect(page.locator(".map-stages"))
      .toContainText("No Stage uses this ground yet");
    await expect(page.locator(".stage-editor")).toHaveCount(0);
    expect(await readable()).not.toMatch(forbidden);

    // The fight. One press makes the Stage and everything it needs to exist.
    await rail.getByRole("button", { name: "Stages" }).click();
    await expect(page.locator("#stage-ground")).toHaveValue("new_map");
    await page.getByRole("button", { name: "Make the Stage" }).click();
    await expect(page.locator(".save-status"))
      .toContainText("a campaign that opens on a Stage at New Map");
    const stage = page.locator(".stage-editor");
    await expect(stage).toContainText("A Stage fought on New Map");
    expect(await readable()).not.toMatch(forbidden);

    // Somebody on each side, put down by pressing tiles. Neither press picks a
    // side: both characters were made on a side and carry the faction that
    // says so, and a board that asked again would be asking the author to
    // repeat themselves and letting the two answers disagree.
    await stage.locator('[data-unit-type="wren"]').click();
    await expect(stage.locator(".palette-sides")).toHaveCount(0);
    await expect(stage).toContainText("Wren always fights for your side");
    await stage.locator('[data-cell="0"]').click();
    await stage.locator('[data-unit-type="bandit"]').click();
    await expect(stage.locator(".palette-sides")).toHaveCount(0);
    await stage.locator('[data-cell="9"]').click();
    await expect(stage.locator(".placed-unit")).toHaveCount(2);
    expect(await readable()).not.toMatch(forbidden);

    // And it plays. A fresh campaign has no scene to sit through, so Play opens
    // on the company and the board is one press away.
    await page.getByRole("button", { name: /Play/ }).click();
    const play = page.locator(".play-mode");
    await expect(play).toBeVisible();
    const toTheStage = play.getByRole("button", { name: "To the Stage" });
    await expect(toTheStage).toBeVisible({ timeout: 30_000 });
    expect(await readable()).not.toMatch(forbidden);
    await toTheStage.click();
    // 8 x 6, the size a fresh map starts at: the Stage the author just made,
    // running under the engine.
    await expect(play.locator('[role="gridcell"]')).toHaveCount(48);
    expect(await readable()).not.toMatch(forbidden);

    await play.getByRole("button", { name: /Back to editing/ }).click();
  });

// One press, for an author filling a board to try something out rather than
// making somebody in particular. What comes out has to be an ordinary
// character: the same four records, on a side, drawn as its own role.
test("makes a whole character in one press", async ({ page }) => {
  await page.getByRole("button", { name: "Start a new game" }).click();
  const rail = page.locator('nav[aria-label="Project"]');
  await rail.getByRole("button", { name: "Characters" }).click();
  await page.getByRole("button", { name: "New character" }).click();

  // No question answered, and the wizard still on its first step.
  await expect(page.locator(".wizard-steps li").nth(0))
    .toHaveAttribute("aria-current", "step");
  await page.getByTestId("wizard-random").click();

  const card = page.locator(".character-card");
  await expect(card).toHaveCount(1);
  // On one of the two sides, never on neither: somebody who is on no side is
  // the one thing an author filling a board cannot put down.
  await expect(card).toContainText(/One of yours|An enemy/);
  // And drawn as a real figure in a real colour, which is the trap this
  // feature was warned about: the picture follows the class, not the name.
  await expect(card.locator("img")).toHaveAttribute(
    "src",
    /\/(knight|archer|mage|stormcaller|healer|commander|rogue|beast)_(blue|red)\.png$/
  );

  // Pressing it again makes a second person rather than the same one twice.
  await page.getByRole("button", { name: "New character" }).click();
  await page.getByTestId("wizard-random").click();
  await expect(card).toHaveCount(2);
  const names = await page.locator(".character-card strong").allInnerTexts();
  expect(new Set(names).size).toBe(2);
});

test("makes a Stage on chosen ground, and fills it from the board",
  async ({ page }) => {
    // The whole of what the Stages section is for, in a real browser: ground
    // drawn under Maps, a Stage made from it in one press, and characters put
    // on the board by pressing tiles: a road that is otherwise six correct
    // guesses through a section this author never opens.
    await page.getByRole("button", { name: "Start a new game" }).click();
    const rail = page.locator('nav[aria-label="Project"]');

    // One character to put down.
    await rail.getByRole("button", { name: "Characters" }).click();
    await page.getByRole("button", { name: "New character" }).click();
    await page.locator('input[name="character-wizard-side"][value="the_enemy"]')
      .check();
    await page.getByRole("button", { name: "Next" }).click();
    await page.getByRole("radio", { name: /Archer/ }).click();
    await page.getByRole("button", { name: "Next" }).click();
    await page.locator("#character-wizard-name").fill("Wren");
    await page.getByRole("button", { name: "Make them" }).click();

    // Ground to fight over.
    await rail.getByRole("button", { name: "Maps" }).click();
    await page.getByRole("button", { name: "Create map" }).click();
    await page.locator(".record-list").getByRole("button", { name: /New Map/ })
      .click();

    // The map signposts where a Stage is made and carries the ground across,
    // so the author is not asked to choose it a second time.
    await page.getByRole("button", { name: "Make a Stage on this ground" })
      .click();
    await expect(page.locator("#content-title")).toHaveText("Stages");
    await expect(page.locator("#stage-ground")).toHaveValue("new_map");

    // One press, and everything the Stage needs to exist comes with it.
    await page.getByRole("button", { name: "Make the Stage" }).click();
    await expect(page.locator(".save-status"))
      .toContainText("a campaign that opens on a Stage at New Map");
    const surface = page.locator(".stage-editor");
    await expect(surface).toContainText("A Stage fought on New Map");

    // The board draws the character the game will draw, in its own colour, and
    // on the side the character itself already answered for.
    await surface.locator('[data-unit-type="wren"]').click();
    await surface.locator('[data-cell="9"]').click();
    const placed = surface.locator(".placed-unit");
    await expect(placed).toHaveCount(1);
    await expect(placed).toHaveAttribute("src", /archer_red/);
    await expect(surface.locator(".placement-notice"))
      .toContainText("Put Wren on column 2, row 2");

    // And the board is a grid a keyboard can cross: one tab stop, arrow keys,
    // and Enter puts somebody down where the focus is.
    const cells = surface.locator('[role="gridcell"]');
    await expect(surface.locator('[role="row"]')).toHaveCount(6);
    await expect(cells).toHaveCount(48);
    await cells.first().focus();
    await page.keyboard.press("ArrowRight");
    await page.keyboard.press("ArrowDown");
    await expect(cells.nth(9)).toBeFocused();
    await page.keyboard.press("ArrowRight");
    await page.keyboard.press("Enter");
    await expect(surface.locator(".placed-unit")).toHaveCount(2);

    // What is said after belongs to what the Stage leads to, and is named by
    // the node that owns it rather than offered as a field here.
    const after = surface.locator(".stage-after");
    await expect(after).toContainText("After New Map");
    await expect(after).toContainText("says nothing.");
    await expect(after.locator("input")).toHaveCount(0);
  });

test("puts three bandits down without a bandit having been made first",
  async ({ page }) => {
    // The owner's complaint, end to end, in a real browser: a game with nobody
    // in it, and "put a bandit here" answered by a bandit standing on the tile
    // rather than by a trip to Characters to author a weapon type, a weapon, a
    // class and a character before coming back. Three of them, because three
    // bandits are three placements of one Bandit, and then one person, who is
    // not.
    await page.getByRole("button", { name: "Start a new game" }).click();
    const rail = page.locator('nav[aria-label="Project"]');

    // Ground, and only ground. Nobody has been authored at all.
    await rail.getByRole("button", { name: "Maps" }).click();
    await page.getByRole("button", { name: "Create map" }).click();
    await page.locator(".record-list").getByRole("button", { name: /New Map/ })
      .click();
    await page.getByRole("button", { name: "Make a Stage on this ground" })
      .click();
    await page.getByRole("button", { name: "Make the Stage" }).click();
    const stage = page.locator(".stage-editor");
    await expect(stage).toContainText("A Stage fought on New Map");

    // The palette offers the characters this game has not got, so the first
    // act on an empty board is putting somebody on it.
    await expect(stage.locator(".palette-new").first()).toBeVisible();
    await stage.locator('[data-palette="new:medieval_rogue"]').click();
    await stage.locator("#palette-new-name").fill("Bandit");
    await stage.getByRole("button", { name: "The enemy", exact: true }).click();
    await stage.locator('[data-cell="9"]').click();

    // One press made them and stood them there, and the palette is holding
    // them now, so the next two presses put down two more of the same Bandit
    // rather than making two more Bandits.
    await expect(stage.locator('[data-palette="unit:bandit"]'))
      .toHaveAttribute("aria-checked", "true");
    await expect(stage.locator(".placed-unit")).toHaveCount(1);
    await stage.locator('[data-cell="10"]').click();
    await stage.locator('[data-cell="11"]').click();
    await expect(stage.locator(".placed-unit")).toHaveCount(3);

    // Somebody of the author's own, made the same way. Your side is fought by
    // the company, so she joins it, which is what makes her a person rather
    // than a kind.
    await stage.locator('[data-palette="new:medieval_commander"]').click();
    await stage.locator("#palette-new-name").fill("Wren");
    await stage.getByRole("button", { name: "Your side", exact: true }).click();
    await stage.locator('[data-cell="0"]').click();
    await expect(stage.locator(".placed-unit")).toHaveCount(4);
    await expect(page.locator(".save-status")).toContainText("Wren joined it");

    // Everything the four presses wrote is an ordinary record, and the
    // Characters page reads who is who off the game rather than off a field
    // anybody ticked: Wren marches with a company, the bandits do not.
    await rail.getByRole("button", { name: "Characters" }).click();
    const roster = page.locator(".character-roster");
    await expect(roster).toContainText("Wren marches with a campaign's company");
    await expect(roster).toContainText(
      "Bandit stands in one Stage, 3 times in all"
    );

    // And it plays: the board the author just filled, running under the engine.
    await page.getByRole("button", { name: /Play/ }).click();
    const play = page.locator(".play-mode");
    const toTheStage = play.getByRole("button", { name: "To the Stage" });
    await expect(toTheStage).toBeVisible({ timeout: 30_000 });
    await toTheStage.click();
    await expect(play.locator('[role="gridcell"]')).toHaveCount(48);
    // Four characters on the running board: the three bandits and Wren.
    await expect(play.locator("g.unit")).toHaveCount(4);
    await play.getByRole("button", { name: /Back to editing/ }).click();
  });

// Taking somebody off the board with the same gesture that moves them, in a
// real browser: happy-dom has no drag-and-drop, so the component suite has to
// synthesise the events and this is the only place the real one is exercised.
test("takes a character off the board by dragging them off it", async ({
  page
}) => {
  await page.getByRole("button", { name: "Start a new game" }).click();
  const rail = page.locator('nav[aria-label="Project"]');
  await rail.getByRole("button", { name: "Maps" }).click();
  await page.getByRole("button", { name: "Create map" }).click();
  await page.locator(".record-list").getByRole("button", { name: /New Map/ })
    .click();
  await page.getByRole("button", { name: "Make a Stage on this ground" }).click();
  await page.getByRole("button", { name: "Make the Stage" }).click();
  const stage = page.locator(".stage-editor");

  await stage.locator('[data-palette="new:medieval_rogue"]').click();
  await stage.locator("#palette-new-name").fill("Bandit");
  await stage.locator('[data-cell="0"]').click();
  await expect(stage.locator(".placed-unit")).toHaveCount(1);

  // The strip says what the gesture is before anybody makes it.
  const bin = stage.getByTestId("placement-bin");
  await expect(bin).toBeVisible();
  await expect(bin).toContainText("Drag somebody off the board");

  await stage.locator(".placed-unit").first().dragTo(bin);
  await expect(stage.locator(".placed-unit")).toHaveCount(0);
  // And it says who it took, because a drag is easy to make by accident.
  await expect(stage.locator(".placement-notice")).toContainText("off the board");
});

test("will not stand one person on the same board twice", async ({ page }) => {
  // A member of the company is one person, and a person is in one place. The
  // editor is never told which characters those are: `characterIsOnePerson`
  // reads it off the campaign that holds them.
  await page.getByRole("button", { name: "Start a new game" }).click();
  const rail = page.locator('nav[aria-label="Project"]');

  // Somebody on no side yet, so the board is the thing that decides where she
  // stands and the side picker is genuinely still a question.
  await rail.getByRole("button", { name: "Characters" }).click();
  await page.getByRole("button", { name: "New character" }).click();
  await page.locator('input[name="character-wizard-side"][value=""]').check();
  await page.getByRole("button", { name: "Next" }).click();
  await page.getByRole("radio", { name: /Commander/ }).click();
  await page.getByRole("button", { name: "Next" }).click();
  await page.locator("#character-wizard-name").fill("Kesh");
  await page.getByRole("button", { name: "Make them" }).click();

  await rail.getByRole("button", { name: "Maps" }).click();
  await page.getByRole("button", { name: "Create map" }).click();
  await page.locator(".record-list").getByRole("button", { name: /New Map/ })
    .click();
  await page.getByRole("button", { name: "Make a Stage on this ground" }).click();
  await page.getByRole("button", { name: "Make the Stage" }).click();
  const stage = page.locator(".stage-editor");

  // Standing her on the author's own side puts her in the company, which is
  // the whole of what makes her somebody rather than something.
  await stage.locator('[data-unit-type="kesh"]').click();
  await stage.getByRole("button", { name: "Your side", exact: true }).click();
  await stage.locator('[data-cell="0"]').click();
  await expect(stage.locator(".placed-unit")).toHaveCount(1);

  // A placement on the opposing side names nobody, so a second Kesh there
  // would be Kesh in two places at once. The palette greys her out and says
  // why rather than stamping a copy.
  await stage.getByRole("button", { name: "The enemy", exact: true }).click();
  const entry = stage.locator('[data-palette="unit:kesh"]');
  await expect(entry).toHaveAttribute("aria-disabled", "true");
  await stage.locator('[data-cell="9"]').click();
  await expect(stage.locator(".placed-unit")).toHaveCount(1);
  await expect(stage.locator(".placement-notice"))
    .toContainText("Kesh already stands on this board");
});


// The ROM export, seen from a real browser with no build service behind the
// proxy: the state an author who has never started the service will meet.
//
// That state is arranged rather than assumed. This used to read whatever was on
// 4699, the port a person's own ROM service listens on, and it is often up
// because running it is how the editor is used — so this test went red on
// exactly the machines where a ROM *can* be built here, which is a failure of
// the suite and not of the editor. `playwright.config.ts` points the proxy at a
// port of this suite's own and `tests/browser/global-setup.ts` refuses to start
// if anything answers there.
//
// Two things are only observable here. The Content-Security-Policy is real in
// Chromium and nowhere else, so this is the only place that can prove the
// request the editor makes is one `connect-src 'self'` allows; a fetch to a
// named localhost origin would be blocked and the page would report it
// differently. And `vite preview` answers the unproxied path with HTML rather
// than refusing the connection, so this is the only place the editor's
// handling of *that* particular shape of nothing is exercised.
test("offers the Nintendo 64 ROM, and says why it cannot build one here",
  async ({ page }) => {
    const violations: string[] = [];
    page.on("console", (message) => {
      if (/Content Security Policy/i.test(message.text())) {
        violations.push(message.text());
      }
    });

    await page.getByRole("button", { name: "Start a new game" }).click();
    const button = page.getByTestId("download-n64-rom");
    await expect(button).toBeVisible();
    // Present and disabled with a reason, rather than absent or failing on a
    // press.
    await expect(button).toBeDisabled();
    await expect(page.getByTestId("rom-status")).toContainText(
      "ROM build service is not running"
    );
    // And the editor got that answer without the policy stopping it.
    expect(violations).toEqual([]);
  });
