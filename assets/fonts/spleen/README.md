# Console interface font

The PlayStation uses Spleen 6×12 by Frederic Cambus under the included
BSD 2-Clause license. Its fixed bitmap cells keep small text clear and prevent
letters from clipping or bleeding into their neighbours.

Source: https://github.com/fcambus/spleen, version 2.2.0,
commit `0493c34e22791824767c618fed42c434b477662c`.
The original BDF and license are retained here. Regenerate the ASCII atlas
with `python3 tools/fonts/render-console-font.py`; ordinary exports use the
committed PNG and need no installed fonts or downloads.
