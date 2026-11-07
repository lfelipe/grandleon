# Creating a game

Grandleon's PC application includes both the player and the campaign creator.
A new campaign starts with a small playable chapter, so the normal workflow is
simple: make a change, test it, and keep building from what works.

This page is a visual tour of the 0.4 creator, not a permanent reference
manual. The editor will change substantially before 1.0, when the project will
publish a complete step-by-step tutorial.

## Start with something playable

Choose **Create / Edit Games**, then **New Campaign**. Give the campaign a
title and choose **Create & Open First Map**.

![Creating a campaign, opening its outline, and reaching the first map](screenshots/native-guide/01-start-a-campaign.gif)

The starter includes two heroes, opponents, a short conversation, a battle
goal, local co-op ownership, and an ending. You can play it immediately or
replace each part at your own pace.

## Shape the first battle

The map screen is where you paint terrain and arrange the encounter. **Brush**,
**Fill**, and **Rectangle** cover the common painting jobs; **Undo** and
**Redo** make it safe to experiment.

![Painting terrain, undoing a change, opening battle setup, and choosing the look](screenshots/native-guide/02-shape-a-battle.gif)

From the same screen you can:

- place, move, and remove allied and opposing units;
- assign allied units to Player 1 or Player 2;
- add opponents that appear only in a two-player game;
- choose deployment areas and battle goals;
- add a conversation that starts when a unit is approached; and
- choose the terrain theme, character style, and faction colours.

The [artwork and animation roster](ARTWORK.md) shows every included style and
unit. Grandleon currently uses its built-in art library; importing a custom art
pack is not part of the 0.4 creator.

## Write the story

Open **Story** to choose a speaker, portrait, backdrop, and dialogue. Add lines
for the common path; less frequent scene and ordering actions are under
**More**.

![Writing dialogue, adding a scene, and opening the additional story actions](screenshots/native-guide/03-write-the-story.gif)

Story text is saved with the project and shown consistently on PC, Nintendo
64, and PlayStation. A dedicated translation step is planned for 0.5.

## Add chapters and branches

**Continue Creating** opens the campaign outline. It presents the story,
battles, endings, and alternate routes in play order.

![Reviewing the campaign outline and adding a branching outcome](screenshots/native-guide/04-build-the-campaign.gif)

Use **Add Next Chapter** for a linear campaign. Use **Outcomes** when a battle
should lead somewhere different because of a win, loss, item, or conversation.
New chapters begin as playable copies of the last working setup, which is much
faster than rebuilding every placement and goal.

## Grow the army

**Grow the Army** manages heroes, recruits, roles, gear, abilities, growth,
rewards, and campaign supplies. A new role can start from an existing one and
then be tuned independently.

![Creating and tuning a role, setting growth, and editing campaign supplies](screenshots/native-guide/05-grow-the-army.gif)

Visible names can change without breaking campaign progress. Keep roles and
gear small at first; add new choices when a chapter gives the player a reason
to use them.

## Test and export

Use **Test Campaign** often. It plays the current draft in the normal Godot
player without replacing progress from an installed campaign.

![Testing the playable draft and opening Finish and Export](screenshots/native-guide/06-test-and-export.gif)

**Finish & Export** can produce:

- an editable `.grandleon.zip` project;
- a standalone Linux player build;
- a Nintendo 64 `.z64` ROM when the optional builder is installed; and
- a PlayStation `.bin` and `.cue` pair when its optional builder is installed.

Console builders are separate downloads. Once installed, they work offline
and do not require a source checkout. The PlayStation image does not include a
Sony licence sector, so it does not boot on an unmodified retail console.

## Keep projects safe

Export a `.grandleon.zip` at useful milestones and keep copies outside the
application's project library. Projects made with Grandleon 0.2 or 0.3 can be
imported into 0.4; source is migrated before it is opened.

> We still plan to do heavy work on the editor throughout the next releases,
> and we will update the schema version with those changes. Existing projects
> should import through the migration path, but until the editor reaches a
> stable format they may need occasional adjustments. Back up your game at
> multiple stages before upgrading, and report any import or migration issue
> you find.

## Refresh this visual guide

The animations above come from scripted interactions with the real Godot
creator, not mockups. Rebuild them from a development checkout with:

```sh
platform/godot/scripts/capture-creator-guide.sh
```

Pass `--player-shell path/to/pc-bundle` to show the installed Linux export
target as available in the final animation.
