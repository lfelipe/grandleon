# SPDX-License-Identifier: MIT
"""Character meshes: the roster as solids, in every style that has one.

Sprites are drawn in :mod:`..characters` and its sibling style modules; meshes
are built here, and the two are drawings of the same archetype rather than two
things. That is the whole reason this package lives beside those: mesh source
belongs next to the code that already draws these figures. The connection
between them is load-bearing, because **a mesh archetype targets its own
sprite's silhouette**, which is rule 4 of :mod:`.rules`. A mesh that lived
anywhere else would be a drawing held to a number nobody could regenerate.

How this package is laid out
----------------------------
A mesh is keyed by **style and archetype**, and the two levels live in
different places on purpose:

* :mod:`.rules` says what a mesh *is* and holds every rule one is checked
  against. It is the same file for every style, because the rules are
  properties of the camera and of the console rather than of a genre.
* One module a style holds that style's art, one for every style in the
  library: :mod:`.medieval`, :mod:`.scifi`, :mod:`.mythical`, :mod:`.nature`,
  :mod:`.sengoku`, :mod:`.undead`, :mod:`.pirates`. A style with no mesh
  commission has **no module**, which is what "the commission is a subset
  of the library" looks like on disk.
* :data:`COMMISSIONS` below is the registry those modules are reached through,
  and it is the only thing the rest of the generator reads.

This is the shape the sprite side already has, and it is the same argument:
:mod:`..characters` draws ``medieval`` and :mod:`..scifi`, :mod:`..mythical`,
:mod:`..nature` draw the others. A style is a different *drawing* of the
same archetype, so two styles' meshes have nothing to share but the rules: a
`nature` badger-knight is not a `medieval` armoured one recoloured, and rule 4
would not even catch the substitution, because it holds each figure to its own
style's sprite.

Adding a style's mesh commission
--------------------------------
1. Write ``meshes/<style>.py``. It imports nothing but :mod:`.rules` and
   declares exactly two names: ``STYLE = "<style>"``, and ``MESHES``, an
   ``archetype -> tuple of Part`` mapping. Copy :mod:`.medieval`'s shape; the
   tables carry their own reasoning beside them and a new commission is
   expected to as well.
2. Import it below and add it to :data:`_COMMISSIONED`, in
   :data:`..styles.STYLES` order.

Nothing else changes. The generated header, the glTF export, the round trip
that reads it back, the roster page and the console's own style seam all reach
a mesh through :func:`parts_for` and were style-aware before any of this was
written.

Two things a new commission is allowed to be, and one it is not:

* It may cover **some** archetypes and not others. :func:`parts_for` returns
  ``None`` for the rest and the header emits a null row with a part count of
  zero, which is a complete style with an incomplete commission.
* It may be **absent entirely**. Most styles are, and the roster page renders
  "no mesh commissioned" for them without being told which.
* It may **not** name an archetype the roster does not hold, reuse another
  style's figures, or break a rule to fit. :func:`check` refuses all three,
  and the refusal is the point.
"""

from __future__ import annotations

import contextlib
from typing import Dict, Iterator, List, Mapping, Optional, Sequence, Tuple

from .. import figures, styles
from . import (authored, medieval, mythical, nature, pirates, rules, scifi,
               sengoku, undead)
from .rules import (FACES_PER_PART, MESH_WORLD_HEIGHT, PART_FIELDS,
                    PITCH_DEGREES, RAMP_COUNT, RAMP_FACTION, RAMP_NEUTRAL,
                    RUNG_COUNT, TRIANGLE_BAND, TRIANGLES_PER_PART, UNIT_WORLD,
                    VALUES_PER_PART, VERTICES_PER_PART, WIDTH_TOLERANCE, Part,
                    Silhouette, authored_width, depth_key, target_width)

__all__ = [
    "COMMISSIONS", "FACES_PER_PART", "MESH_WORLD_HEIGHT", "PART_FIELDS",
    "PITCH_DEGREES", "RAMP_COUNT", "RAMP_FACTION", "RAMP_NEUTRAL",
    "RUNG_COUNT", "TRIANGLE_BAND", "TRIANGLES_PER_PART", "UNIT_WORLD",
    "VALUES_PER_PART", "VERTICES_PER_PART", "WIDTH_TOLERANCE", "Part",
    "Silhouette", "authored", "authored_width", "check", "commission",
    "commissioned_styles", "depth_key", "has_meshes", "parts_for", "provided",
    "roster", "rules", "target_width",
]

#: Every style module that holds a commission, in :data:`..styles.STYLES`
#: order. One line a style, and the module names the style it is for, so the
#: registry below cannot bind a table to the wrong sprites.
_COMMISSIONED = (medieval, scifi, mythical, nature, sengoku, undead, pirates)

#: The commissioned meshes, keyed by **style name** and then by archetype name.
#: This is the only mapping the rest of the generator reads; reach a figure
#: through :func:`parts_for` rather than indexing it, so that an uncommissioned
#: style is an answer rather than a :class:`KeyError`.
COMMISSIONS: Mapping[str, Mapping[str, Tuple[Part, ...]]] = {
    module.STYLE: module.MESHES for module in _COMMISSIONED
}

# The registry, checked at import: a mesh table bound to a style that does not
# exist, or written down out of the library's own order, is a mistake that
# would otherwise surface as a missing header rather than as a message.
assert len(COMMISSIONS) == len(_COMMISSIONED), (
    "two mesh modules declare the same style")
for _name in COMMISSIONS:
    assert _name in styles.STYLES_BY_NAME, (
        f"meshes are commissioned for style '{_name}', which the character "
        f"library does not hold")
assert list(COMMISSIONS) == [entry.name for entry in styles.STYLES
                             if entry.name in COMMISSIONS], (
    "the mesh commissions are registered out of the library's style order")


#: Figures somebody else authored, standing in for commissioned ones for the
#: length of one build. Keyed ``(style name, archetype)`` and empty except
#: inside :func:`provided`, which is the only thing that writes it.
_PROVIDED: Dict[Tuple[str, str], Tuple[Part, ...]] = {}


#: The authored roster, keyed ``(style, archetype, figure)`` and empty except
#: inside :func:`roster`. Separate from :data:`_PROVIDED` because the two answer
#: different questions: a *provided* model is one author replacing one figure,
#: and the roster is a whole second commission that a build either draws or does
#: not. Keeping them apart is what lets a provided model still win over the
#: roster, which is the precedence an author expects.
_ROSTER: Dict[Tuple[str, str, str], Tuple[Part, ...]] = {}

#: The figure a caller that names none is asking for. Read from the library
#: rather than spelled here, so this cannot drift from the menu order every
#: client indexes by.
_DEFAULT_FIGURE = figures.DEFAULT_FIGURE.name


@contextlib.contextmanager
def roster(units: Optional[Mapping[Tuple[str, str, str],
                                   Tuple[Part, ...]]] = None) -> Iterator[None]:
    """Draw the authored roster (:mod:`.authored`) for the length of one build.

    The seam the 3D build is selected through. Inside it, :func:`parts_for`
    answers with an authored table wherever the roster holds one, including for
    the **second figure**, which :data:`COMMISSIONS` has none of; outside it,
    every lookup misses and the commissioned figures answer exactly as they
    always have. That is the whole of what "default to the 3D build" costs the
    rest of this package: one context manager, and no consumer told anything.

    A context manager rather than a flag threaded through the header emitters,
    the glTF export and the roster page, for the reason :func:`provided` is one:
    those consumers already reach a mesh through :func:`parts_for`, and a
    build-wide choice of drawing is the shape :func:`..terrain.rendering`
    established.

    ``units`` defaults to whatever :func:`.authored.load` finds on disk. Passing
    a mapping is for tests and for a caller that has already read and filtered
    the roster, so this need not read the disk twice.

    Unlike :func:`provided` this does **not** refuse a unit the library holds no
    commission for, because the roster is not a replacement: it is a second
    drawing of the same 56 roles, and its second figure is by definition a
    figure no commission covers. It does refuse a style or archetype the
    character library does not hold, which would be a table bound to nothing.
    """
    units = authored.load() if units is None else units
    for style_name, archetype, figure in units:
        assert style_name in styles.STYLES_BY_NAME, (
            f"the authored roster holds {style_name}/{archetype}, and "
            f"{style_name} is not a style this library draws")
        assert archetype in styles.STYLES_BY_NAME[style_name].archetypes, (
            f"the authored roster holds {style_name}/{archetype}, which that "
            f"style has no such role in")
        assert figure in figures.FIGURES_BY_NAME, (
            f"the authored roster holds {style_name}/{archetype} as figure "
            f"'{figure}', which is not one this library draws")
    previous = dict(_ROSTER)
    _ROSTER.update(units)
    try:
        yield
    finally:
        _ROSTER.clear()
        _ROSTER.update(previous)


@contextlib.contextmanager
def provided(models: Mapping[Tuple[str, str], Tuple[Part, ...]]
             ) -> Iterator[None]:
    """Stand ``models`` in for the commissioned figures they name.

    The mesh half of the seam ``build.substitute`` is for sprites, and it is
    exactly as shallow on purpose: a provided model enters as a **part list of
    integers**, in front of everything, so the generated console header, the
    glTF this package exports, the round trip that reads it back and the roster
    page all reach it through :func:`parts_for` by the route they already
    reached the commissioned one. Nothing downstream learns that anything was
    replaced, and nothing downstream had to be told.

    A context manager rather than an argument threaded through nine call sites,
    which is the shape :func:`..terrain.rendering` already gives a build-wide
    choice of drawing. An empty mapping is exactly the build this repository has
    always run: the dictionary below is empty, every lookup misses it, and
    :data:`COMMISSIONS` answers as it always did.

    ``models`` may only name a style and archetype the library already
    commissions. Accepting one it does not would be *adding* a figure rather
    than replacing one, which is a different thing wanting rules of its own.
    :mod:`..provided` refuses it at the door, and this asserts it rather than
    discovering it two hundred lines later as a header row nobody expected.
    """
    for style_name, archetype in models:
        assert archetype in COMMISSIONS.get(style_name, {}), (
            f"a provided mesh names {style_name}/{archetype}, which this "
            f"library has no commission for; providing one would be an "
            f"addition rather than a replacement")
    previous = {key: _PROVIDED[key] for key in models if key in _PROVIDED}
    _PROVIDED.update(models)
    try:
        yield
    finally:
        for key in models:
            _PROVIDED.pop(key, None)
        _PROVIDED.update(previous)


def commissioned_styles() -> Tuple[str, ...]:
    """The style names a mesh commission exists for, in the library's order."""
    return tuple(COMMISSIONS)


def has_meshes(style: styles.Style) -> bool:
    """Whether a style's mesh commission has been drawn."""
    return style.name in COMMISSIONS


def commission(style: styles.Style,
               figure: Optional[str] = None) -> Mapping[str, Tuple[Part, ...]]:
    """One style's whole commission, or an empty mapping if it has none.

    ``figure`` selects which body, defaulting to the first — which is the only
    one :data:`COMMISSIONS` holds, so a caller that names none gets the mapping
    this function has always returned. Naming the second figure outside
    :func:`roster` returns an empty mapping rather than the first figure's
    tables: a style has not drawn a second figure as solids unless the roster
    says so, and answering with the wrong body would be a lie the rule checker
    would then certify.
    """
    wanted = _DEFAULT_FIGURE if figure is None else figure
    if _ROSTER:
        held = {archetype: parts
                for (name, archetype, shape), parts in _ROSTER.items()
                if name == style.name and shape == wanted}
        if held:
            # A provided model still wins: it is one author replacing one
            # figure, which is a narrower and more deliberate statement than
            # "this build draws the roster".
            return {archetype: _PROVIDED.get((style.name, archetype), parts)
                    for archetype, parts in held.items()}
    if wanted != _DEFAULT_FIGURE:
        return {}
    drawn = COMMISSIONS.get(style.name, {})
    if not _PROVIDED:
        return drawn
    return {archetype: _PROVIDED.get((style.name, archetype), parts)
            for archetype, parts in drawn.items()}


def parts_for(style: styles.Style, archetype: str,
              figure: Optional[str] = None) -> Optional[Tuple[Part, ...]]:
    """One figure, or ``None`` when this style has not commissioned it.

    The one accessor every consumer should use. It answers for an
    uncommissioned style and for an uncommissioned archetype with the same
    ``None``, because the two are the same fact to everything downstream: the
    header emits a null row, the export writes no file, and the roster page
    says no mesh was commissioned.

    Answers with a **provided** figure wherever one stands in for a
    commissioned one (see :func:`provided`), because every consumer in this
    repository already reaches a mesh through here, and a second accessor for
    "the one this build is actually drawing" would be a second answer to one
    question.

    ``figure`` names the body, and defaults to the first. Three sources answer,
    in this order, and the order is the point:

    1. a **provided** model, one author replacing one figure deliberately;
    2. the **authored roster**, when :func:`roster` is open — the only source
       that holds a second figure;
    3. the **commission**, the figures this package draws.

    Asking for the second figure outside :func:`roster` is ``None``: no style
    has drawn one as solids, and that is the same fact to every consumer as an
    uncommissioned archetype, which is why it is the same answer.
    """
    wanted = _DEFAULT_FIGURE if figure is None else figure
    replacement = _PROVIDED.get((style.name, archetype))
    if replacement is not None:
        return replacement
    written = _ROSTER.get((style.name, archetype, wanted))
    if written is not None:
        return written
    if wanted != _DEFAULT_FIGURE:
        return None
    return COMMISSIONS.get(style.name, {}).get(archetype)


def check(measured_silhouettes: Sequence[Silhouette], style: styles.Style,
          figure: Optional[str] = None) -> List[str]:
    """Every mesh rule, checked over one style. Returns what it accepted.

    ``measured_silhouettes`` is one entry per archetype in
    :data:`..characters.ARCHETYPE_ORDER`, measured on **this style's** sprites
    by :func:`..playstation_header.silhouettes`. Passing another style's is the
    one mistake that would produce a figure the rules accept and a viewer does
    not, so the style and its measurements are taken together here rather than
    resolved separately.

    A style with no commission accepts nothing and raises nothing: it is a
    complete style that has not been drawn as solids.
    """
    return rules.check_commission(commission(style, figure),
                                  measured_silhouettes, style.name)
