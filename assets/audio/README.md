# Grandleon audio

The shared score uses strings, horns, flute and orchestral percussion. `Cedar
Overture` is the quieter exploration theme; `Banners Afar` is more adventurous.
`Willow Village` accompanies peaceful scenes, `Stone and Shadow` brings quieter
tension, `Drums at the Gate` accompanies a finale, and `First Light` offers relief.
Music and sound effects have separate volume controls.

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
