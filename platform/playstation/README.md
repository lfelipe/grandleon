# PlayStation

Grandleon can export a game as a PlayStation BIN/CUE image. It uses the same
campaign, combat rules, localization, and saves as the Godot player and the
Nintendo 64 version. The PlayStation presentation runs at 320×240 through the
project's single Mipsync-based renderer.

## Export and play

Open **Finish & Export** in the Grandleon creator and choose **PlayStation**.
The optional PlayStation builder must be installed first; once installed, it
works offline. Export creates `game.bin` and `game.cue`.

Keep both files in the same directory and open `game.cue` in a compatible
emulator or FPGA system. The image intentionally has no Sony licence sector,
so it does not boot on an unmodified retail PlayStation.

The campaign decides whether **New Game** starts one player, two players, or
offers a choice. A fixed one-player or two-player campaign starts directly;
only a campaign authored with a choice shows the player-count screen. Ready
formations go straight to the battle, while an authored deployment area opens
the board deployment step.

## Controls

- D-pad: move the cursor or menu selection.
- Cross: confirm, select a character, or choose a destination.
- Circle: cancel or go back.
- Triangle: open the selected character's action menu.
- Start: open the battle menu or begin an authored deployment.

Each controller has its own cursor in two-player play, and each character can
only be commanded by its authored owner. **Settings** can change language,
close combat, reduced motion, and the FPS display while the game is running.
FPS begins off.

If an author lets players name a character, the game shows a controller
keyboard when that character joins. Use the D-pad and Cross to type, Circle to
erase, Start to save, or Triangle to keep the current name.

## What is included

Every exported disc contains project-specific stages, team-coloured character
art, textured tactical terrain, campaign flow, memory-card saves, localized
text, conversations, deployment, reinforcements, objectives, tactical actions,
and the optional close-combat view. Large maps scroll in both directions, and
authored hidden areas reveal when a character enters them.

There is no alternate or fallback PlayStation renderer. An export fails with a
clear error if the Mipsync build cannot be produced or exceeds the PlayStation
memory budgets.

## Contributor checks

The exact project exporter and emulator launch checks live in
[`mipsync`](mipsync/README.md). They build both a direct-entry one-player game
and the full five-stage one/two-player Steel and Cedar campaign, then boot the
generated discs through the pinned emulator.

For the portable console limits and current 0.5 parity work, see
[Playing on Nintendo 64 and PlayStation](../../docs/CONSOLE_RENDERER.md).
Contributor setup is in [Setting up](../../docs/SETUP.md).
