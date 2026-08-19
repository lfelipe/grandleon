# SPDX-License-Identifier: MIT
"""The roster somebody wrote down, as part tables, keyed by figure as well.

Every other module in this package *draws* its figures: a commission is Python
that names its boxes, and the reason is that a table of integers a person typed
is the thing this game's rules can actually be checked against. This module
holds the same kind of table, arrived at a different way — authored against each
role's own committed sprite by the experiment in ``~/src/3d-test`` — and kept as
JSON under ``tools/placeholder_art/units/`` rather than as Python.

JSON rather than seven more ``*.py`` modules, for one reason: these are meant to
keep changing. The generator that produced them is checked in beside them and
will be run again, and a regenerated Python module is a diff nobody can read
against a hand-written one they must not clobber. Keeping the authored roster in
data leaves :data:`COMMISSIONS` — the drawn figures — untouched and obviously
authoritative, and makes "regenerate the roster" a change to files that only
ever held generated content.

**The figure axis.** :data:`COMMISSIONS` is keyed ``style -> archetype``, 56
figures, because when it was written nothing selected a body. The sprites grew a
second figure (:mod:`..figures`), and this roster has one for every role: 112
tables, ``style -> archetype -> figure``. So the key here carries the figure the
commissions' key does not, and :func:`..parts_for` grew a defaulted ``figure``
parameter to reach it. A caller that names no figure asks for the first one and
gets exactly what it got before this module existed.

Nothing here is drawn by default. :func:`load` is a read, and
:func:`..roster` is the context manager that puts these in front of the
commissioned tables for the length of one build — the same shape, and for the
same reason, as :func:`..provided`.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, Tuple

from .rules import Part

#: Where the authored tables live, relative to this package. Beside the
#: generator that writes them rather than inside the package, because they are
#: content a person edits and re-exports, not code this package imports.
ROSTER_DIRECTORY = Path(__file__).resolve().parents[2] / "units"

#: The keys a unit file must carry. Checked rather than assumed: a file missing
#: its ``figure`` would otherwise load as some other figure's table and be
#: caught, if at all, as a puzzling mesh in a roster page.
_REQUIRED = ("style", "archetype", "figure", "parts")

#: The keys one part must carry. The same six coordinates, ramp and rung that
#: :class:`~.rules.Part` holds, and the ``name`` its diagnostics quote.
_PART_FIELDS = ("x0", "x1", "y0", "y1", "z0", "z1", "ramp", "rung")


class RosterError(ValueError):
    """A unit file that cannot be read as a part table, naming the file.

    A distinct type because the roster is data that a person and a generator
    both write, so "this file is malformed" is an ordinary outcome that a
    caller may want to report per unit rather than a bug in this package.
    """


def _part(entry: Mapping[str, object], where: str, index: int) -> Part:
    missing = [field for field in _PART_FIELDS if field not in entry]
    if missing:
        raise RosterError(
            f"{where}: part {index} is missing {', '.join(missing)}")
    values = {}
    for field in _PART_FIELDS:
        raw = entry[field]
        # Integers, and not "integers after rounding": these coordinates are
        # emitted into a console header as whole units, and a float here would
        # mean the file and the ROM disagree about where a box is.
        if isinstance(raw, bool) or not isinstance(raw, int):
            raise RosterError(
                f"{where}: part {index} has {field}={raw!r}, which is not a "
                f"whole number")
        values[field] = raw
    return Part(name=str(entry.get("name", f"part {index + 1}")), **values)


def read(path: Path) -> Tuple[Tuple[str, str, str], Tuple[Part, ...]]:
    """One unit file, as the key it claims and the parts it holds.

    The key comes from **inside** the file rather than from its name, and then
    the name is checked against it. Either alone would be a way for a renamed
    file to quietly become a different unit; together they cannot disagree
    without saying so.
    """
    try:
        document = json.loads(path.read_text())
    except ValueError as error:
        raise RosterError(f"{path.name}: not readable as JSON — {error}")
    if not isinstance(document, dict):
        raise RosterError(f"{path.name}: holds {type(document).__name__}, "
                          f"not an object")
    missing = [key for key in _REQUIRED if key not in document]
    if missing:
        raise RosterError(f"{path.name}: missing {', '.join(missing)}")

    style = str(document["style"])
    archetype = str(document["archetype"])
    figure = str(document["figure"])
    expected = f"{archetype}.{figure}.json"
    if path.name != expected or path.parent.name != style:
        raise RosterError(
            f"{path.name}: the file says it is {style}/{archetype}/{figure}, "
            f"which belongs at {style}/{expected}")

    entries = document["parts"]
    if not isinstance(entries, list) or not entries:
        raise RosterError(f"{path.name}: holds no parts")
    where = f"{style}/{archetype}/{figure}"
    parts = tuple(_part(entry, where, index)
                  for index, entry in enumerate(entries))
    return (style, archetype, figure), parts


def load(directory: Optional[Path] = None
         ) -> Dict[Tuple[str, str, str], Tuple[Part, ...]]:
    """Every authored unit, keyed ``(style, archetype, figure)``.

    Reads whatever is on disk and does not check it against the mesh rules:
    that is :func:`..rules.check_commission`'s job and it needs a style's
    measured silhouettes, which this module has no business fetching. A table
    that loads is well-formed, not necessarily legal.

    An absent directory is an empty roster rather than an error, because a
    checkout that has not exported one is a complete checkout: the commissioned
    figures are what this repository has always drawn.
    """
    root = ROSTER_DIRECTORY if directory is None else directory
    if not root.is_dir():
        return {}
    loaded: Dict[Tuple[str, str, str], Tuple[Part, ...]] = {}
    for path in sorted(root.glob("*/*.json")):
        key, parts = read(path)
        if key in loaded:
            raise RosterError(f"{path.name}: {'/'.join(key)} is declared twice")
        loaded[key] = parts
    return loaded


def for_figure(figure: str, directory: Optional[Path] = None
               ) -> Dict[Tuple[str, str], Tuple[Part, ...]]:
    """The authored roster for one figure, keyed the way a commission is.

    The shape :func:`..provided` accepts, so one figure of this roster can
    stand in for the commissioned tables without anything downstream learning
    that a figure axis exists.
    """
    return {(style, archetype): parts
            for (style, archetype, held), parts in load(directory).items()
            if held == figure}


def styles_covered(roster: Mapping[Tuple[str, str, str], Tuple[Part, ...]]
                   ) -> List[str]:
    """The style names a roster holds at least one unit for, in file order."""
    seen: List[str] = []
    for style, _archetype, _figure in roster:
        if style not in seen:
            seen.append(style)
    return seen
