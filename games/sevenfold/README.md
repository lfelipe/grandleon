# Sevenfold Gate

Sevenfold Gate is a short, deliberately plain showcase campaign. Its seven
Stages use the medieval, sci-fi, mythical, nature, sengoku, undead and pirate
character styles in order. Every Stage fields all eight archetypes, split four
against four, with two male and two female drawings on each side.

The maps are not palette swaps of one layout. They cover all thirteen terrain
kinds across a crossroads, staggered ruins, ridge switchbacks, marsh islands,
bamboo courtyards, a broken grave road and a coastal landing. The generator
checks that every starting tile remains connected despite the water and ridges.

The same campaign supports one or two local players. In one-player mode all
four Gatewardens belong to Player 1. In two-player mode the four are divided
evenly between Players 1 and 2, and one additional opponent enters the board.

## Nintendo 64 showcase ROM

Build the persistent campaign ROM with the debug Stage picker enabled:

```sh
platform/nintendo64/scripts/build-n64.sh \
  --project games/sevenfold/source/project.json \
  --targets grandleon_n64_campaign \
  --stage-picker
```

The title flow asks for one or two players. During a battle, press Start and
choose **Go to another Stage** to jump directly to any style. A jump records no
victories for skipped Stages; that is intentional for this showcase.

`generate_project.py` keeps the regular 56-unit project readable and
repeatable. Run it after changing the style, archetype, placement or terrain
tables, then compile `source/project.json` normally.

## Nintendo 64 3D roster ROM

The companion 3D ROM makes every planned N64 model easy to review:

```sh
platform/nintendo64/scripts/build-sevenfold-3d.sh
platform/nintendo64/scripts/check-sevenfold-3d.sh
```

It contains 112 procedural low-poly characters: seven styles, eight
archetypes, and male and female variants. Each page shows all eight archetypes
at a readable board distance, and alternates red and blue team colours. The
check captures all fourteen style/body pages and verifies each stays above the
30 fps target with depth enabled.

This is a roster and performance ROM, not yet the 3D version of the campaign.
The playable Sevenfold ROM above uses the normal sprite renderer. The two ROMs
share the same seven terrain layouts so that the experiment can grow into a
campaign renderer without inventing a second showcase.
