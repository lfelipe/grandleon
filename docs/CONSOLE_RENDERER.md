# Playing on Nintendo 64 and PlayStation

The bundled **Steel and Cedar** campaign uses the same maps, units and rules
on PC, Android, Nintendo 64 and PlayStation. The console builds play locally
with one or two controllers.

## Start a game

- **Nintendo 64**. Open the `.z64` ROM in a compatible emulator, FPGA system
  or flash cartridge. The game uses SRAM saves.
- **PlayStation**. Keep the `.bin` and `.cue` together and open the `.cue` in a
  compatible emulator or FPGA system. Use a memory card to save. The disc
  image has no Sony licence sector and needs compatible hardware.

Choose **New Game**, then **1 Player** or **2 Players** if the campaign offers
both. A formation screen appears when a battle needs a deployment choice.
**Help** on the title screen shows the controls.

## Controls and settings

| Action | Nintendo 64 | PlayStation |
| --- | --- | --- |
| Move the cursor | D-pad or left stick | D-pad or left stick |
| Confirm | A | Cross |
| Go back | B | Circle |
| Open the game menu | Start | Start |

Select a unit to see its movement range. Attack highlights legal enemies and
shows health, damage, hit chance and possible counterattacks before you commit.
Confirm skips the opening stage sweep. Toggle **Battle Close-ups** and adjust
**Music** and **Sound Effects** independently in **Settings**. You can enable
the FPS counter from the in-game menu.

Games that include translations offer **Language** in Settings. **Steel and
Cedar** ships with English, Spanish and Brazilian Portuguese. When a campaign
lets you name a character, the controller keyboard accepts up to 18 visible
characters. **Start** saves, **Default** restores the authored name, and
**C-Up** on Nintendo 64 or **Triangle** on PlayStation keeps the current name.

## Export your campaign

In the desktop editor, open **Finish & Export**. Install the optional builder
for each console you want to target. Nintendo 64 produces a `.z64` ROM.
PlayStation produces a `.bin` and matching `.cue`.

- [Set up the optional console builders](SETUP.md#console-builds)
- [See the map and hardware limits while creating a game](CREATING_A_GAME.md#share-a-playable-game)

Nintendo 64 uses Pyrite/Tiny3D. PlayStation uses Mipsync. Both use art adapted
from the Godot player and target 30 FPS. PlayStation can still scroll slowly
on large boards, particularly around dense castle scenery. Test on your
intended hardware before sharing a campaign.
