# Grandleon audio

The shared score uses strings, horns, flute and orchestral percussion. `Cedar
Overture` is the quieter exploration theme; `Banners Afar` is more adventurous.
`Willow Village` accompanies peaceful scenes, `Stone and Shadow` brings quieter
tension, `Drums at the Gate` accompanies a finale, and `First Light` offers relief.
Music and sound effects have separate volume controls.

Combat effects cover blades, bows, heavy physical strikes, magic and healing.
The close-up presentations play launch and contact sounds with their animation
beats, including misses and counterattacks. Assigning custom files to individual
weapons and spells in the editor is planned for a later release.

To tweak just the weapon and magic effects, edit `combat_cues()` in the render
script and run `python3 tools/audio/render-score.py --combat-effects-only`.
This path uses no downloaded samples. Blade uses bright steel; bow uses a
low string twang and a separate arrow wake. Magic layers rising crystal tones;
healing uses ascending bells with a soft, warm halo.

To change the compositions or instrumentation, edit
[`render-score.py`](../../tools/audio/render-score.py), then run it with Python,
NumPy and ffmpeg installed. It downloads a small, pinned selection of instrument
recordings into your local cache and regenerates the audio. Normal game builds
use the committed renders and need no downloads.

The JSON files describe the rendered notes. `music/` holds compressed WAV masters
for console builds; the Godot project contains Ogg music and WAV effects.

The compositions and renderer use Grandleon's MIT license. Instrument recordings
come from [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) under
CC0. See [provenance.json](provenance.json) for source revisions, checksums and
credits, and [the CC0 license](VSCO-2-CE-LICENSE.txt) for the full terms.
