# Console interface font

The PlayStation uses Spleen 6×12 by Frederic Cambus under the included
BSD 2-Clause license. Its fixed bitmap cells keep small text clear and prevent
letters from clipping or bleeding into their neighbours.

Source: https://github.com/fcambus/spleen, version 2.2.0,
commit `0493c34e22791824767c618fed42c434b477662c`.
The original BDF and license are retained here. Regenerate the atlases
with `python3 tools/fonts/render-console-font.py`; ordinary exports use the
committed PNG and need no installed fonts or downloads.

The original `console-6x12.png` keeps its 64 ASCII cells. PlayStation uses
`console-latin-6x12.png`: the same cells plus 30 accented Latin capital letters
and Spanish inverted punctuation. Lowercase letters map to their matching
capitals, preserving accents. Its dedicated white/transparent 4-bit palette
keeps the 64×144 atlas smaller in PS1 memory than the previous ASCII atlas.
