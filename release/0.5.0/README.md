# Grandleon 0.5

Grandleon combines a tactical RPG creator and player in one Godot application,
with exports for Nintendo 64 and PlayStation. This release focuses on making
and translating campaigns and on bringing the console players through their
engine transition.

## Steel and Cedar

This demo currently serves as a feature and translation test. A polished,
fully designed campaign is planned before 1.0.

The shared demo follows a false treaty signal across five battles: Bell
Orchard, Cedar Deepwood, Cedar Watch, Gate Village, and Twin Gate Keep.

Choose **1 Player** for Rowan and Tomas or **2 Players** to add Akari and Ren.
Co-op has separate formations, routes, and opponents, with independent local
controls. Both variants include a twenty-round defense and a castle interior
that becomes visible as the party enters. Godot and both consoles play the
same authored campaign and tactical rules.

English, Spanish, and Brazilian Portuguese translations are included. The
language selector appears for games with authored translations; missing or
outdated copy falls back to the current original. Music and Sound Effects have
separate saved sliders. Combat close-ups show enemy attacks and counters;
legal attack targets and HP forecasts help you plan before confirming.

## Create and translate

Choose **Create / Edit Games** to make maps, armies, objectives, conversations,
and campaign outcomes, then play the draft or export it. **Translations**
supports manual editing, missing/outdated filters, .PO import/export, and an
isolated **Play Language** preview. See [Creating a game](../../docs/CREATING_A_GAME.md)
and [Contributing translations](../../TRANSLATING.md).

Back up editable projects before upgrading. The editor and project format are
still evolving; keep milestone copies even when an older project imports
successfully.

## Run the builds

- **Linux PC:** extract the application bundle and run `launch-pc.sh`. It
  includes the creator and Steel and Cedar with a 1P/2P choice on the home screen.
- **Nintendo 64:** open the supplied `.z64` in a compatible emulator or FPGA
  system, or use a compatible flash cartridge. Saves use SRAM.
- **PlayStation:** keep the supplied `.bin` and `.cue` together and open the
  `.cue`. The image has no Sony licence sector and does not boot on an
  unmodified retail PlayStation.

Console builders are optional separate downloads for exporting your own games.
See [console controls and exports](../../docs/CONSOLE_RENDERER.md).

## Current limits

Nintendo 64 uses Pyrite/Tiny3D and PlayStation uses Mipsync. Each console has
one supported renderer. Both consoles support the shared campaign, local co-op, saves, settings,
translations and castle exploration. Presentation quality still differs
between machines.

The console target is 30 fps. PlayStation scrolling and castle scenes can
still judder or miss that target; frame-pacing improvements are deferred beyond
0.5. Real-hardware controls, listening, full 1P/2P playthroughs, remain part of release acceptance.

Release packaging is being prepared. This directory currently contains release
notes, not the final downloadable builds or their checksums. Earlier
[0.4 console review files](../0.4.0/README.md) contain Sevenfold Gate and do not
represent the 0.5 demo.

Grandleon uses the [MIT License](../../LICENSE); bundled dependencies retain
their own notices in [NOTICE](../../NOTICE).
