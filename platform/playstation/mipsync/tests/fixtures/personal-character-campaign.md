# Personal-character acceptance campaign

`personal-character-campaign.json` is ordinary source content, not a modified
engine or generated debug fixture. It has three 5×5 battles and normal story,
name, objective, campaign, and save pages. The same source supports 1P and 2P;
Willow and Rhea belong to controller two in 2P.

Scholar has the **Mage** role and explicit Mage artwork, two personal affinities
(`blade`, `focus`), and a personal sword. The Mage class still permits only focus
weapons, and its archetype kit still contains a focus. This proves the personal
kit is independent of the role without relaxing enemy/archetype validation.
Perman has `permanent`, Willow has `withdraw`, and Rhea has `loseBattle`.
Scholar also withdraws. The game's fallback is recoverable.

## Build and run on PlayStation

Use the normal release toolchain/environment:

```sh
PSN00BSDK_ROOT=/path/to/sdk MIPSYNC_SOURCE=/path/to/mipsync \
  platform/playstation/mipsync/scripts/build-project.sh \
  platform/playstation/mipsync/tests/fixtures/personal-character-campaign.json \
  out/personal-character06/acceptance-ps1

platform/playstation/mipsync/tests/run-personal-character-probe.sh \
  out/personal-character06/acceptance-ps1 \
  out/personal-character06/acceptance-probe
```

Both output directories must initially be empty. The probe uses the pinned
PCSX-Redux image already used by other acceptance probes. Set
`GRANDLEON_PERSONAL_PLAYERS_LIST=1` for a single-mode diagnostic run.
The default runs both modes, each with its own memory card and three independent
emulator processes:

1. **new:** New Game, select players, keep Scholar's authored name, read the
   introduction, and fight **I. Casualties**. Wait with all three heroes. The
   normal enemy policy kills Perman and Willow. Scholar moves to `(3,2)` and
   attacks an adjacent raider; the remaining raider dies to Scholar's legal
   counterattack. Enter **II. Required Hero**, check the roster, and quit.
2. **loss:** Continue from that card in a fresh process. Check the roster again,
   then wait with all three surviving heroes. The guard defeats Rhea while
   Scholar and Willow remain alive. Read the authored defeat scene and enter
   **III. Return**, then quit.
3. **reload:** Continue again in a fresh process. Check the recovery roster and
   Scholar's personal sword without performing another battle action.

## Exact assertions

Before launching the emulator:

- Every packaged stage EXE must byte-match its linked output; record the ISO and
  per-EXE SHA-256 values.
- Each stage must pass the normal RAM gate, retaining at least 512 KiB gameplay
  heap, 64 KiB audio reserve, and 32 KiB stack reserve.
- Exported node, encounter, member, placement, and sword IDs must match the
  fixture's canonical source keys. All four defeat policies and Scholar's two
  affinity IDs/personal sword must match the authored source.
- Target struct offsets come from the actual ELF's DWARF. Missing or ambiguous
  symbols/layouts are errors. Private names may have GCC's `.lto_priv.N` suffix.

While playing:

- Scholar always has exactly the personal sword, including after both process
  restarts. A Mage focus fallback is a failure.
- First battle opens with five units. Both ordinary casualties reach zero
  health through actual enemy attacks; Scholar lives and the battle remains
  ongoing until the enemies are defeated.
- Second battle opens with four units despite five authored placements: Perman
  is excluded, Willow returns at full health, and Rhea is present at full health
  with the required-hero rule. The other living heroes lack that rule.
- Rhea's actual defeat immediately produces second-side victory and fails the
  first-side authored goal while Scholar and Willow still live. The normal
  defeat story must appear before recovery.
- Recovery opens with four units: Perman remains absent, Willow and Rhea return
  at full health, and Scholar retains the sword. A fresh-process Continue must
  produce the same roster and kit.
- 2P actions must be accepted from controller two for its assigned heroes.
- New and loss runs must report successful card saves; both Continue runs must
  report that a saved campaign exists. Any hash, campaign, or card failure fails
  the probe. The engine's authored canonical-hash check remains active.

The only injected values are ordinary controller button masks at the production
runtime input boundary. Snapshot and UI observations are read-only. The probe
never calls a game function, writes RAM, changes health/objectives/save data,
presses debug completion, or patches executable bytes. Captures and logs are
written beside the memory cards.

## Godot and Nintendo 64

Import the same JSON into Godot's creator and launch the campaign through the
normal player. Choose 1P or 2P, follow the three phases above, and reopen through
Continue at each checkpoint. Use the between-battle checkpoint for comparison;
Godot also supports battle-action recovery, which is separate from the roster
persistence checked here. The two ordinary casualties are beside their raiders;
Scholar's destination `(3,2)` is between the two raiders.

Build an isolated normal N64 ROM from exactly the same source:

```sh
GRANDLEON_N64_BUILD_DIR=out/personal-character06/n64 \
  platform/nintendo64/scripts/build-n64.sh --project \
  platform/playstation/mipsync/tests/fixtures/personal-character-campaign.json
```

Follow the same normal menus/actions and retain the emulator's SRAM between
processes. In 2P, use controller two for Willow and Rhea. The shared package,
class/kit resolution, simulation, and campaign outcomes are the same; the PS1
probe additionally verifies its generated stage data and independent bridge.
Gold, shops, class progression, and temporary allied extras are outside this
fixture.
