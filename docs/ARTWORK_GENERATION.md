# Tweaking character artwork

This is the practical record for changing one of Grandleon's curated Godot
characters. The committed PNG is the authoritative source: keep it as an image
reference and make a targeted edit whenever possible. Generating the whole
character again from text is much more likely to change the face, equipment,
camera, or visual language.

## What generated the current roster

The project owner confirmed on 2026-11-05 that all 112 current character sheets
were created by **Codex (GPT-5)** using its built-in image-generation mode. The
sheets were accepted into the repository between 2026-11-02 and 2026-11-04.

That records the authoring model, but not a separate image-renderer version. The
built-in tool did not expose its internal renderer name or dated snapshot,
quality setting, seed, or revised prompt. Those values cannot be recovered from
the PNG metadata or repository history, so this project must not retroactively
label the artwork as having come from a particular `gpt-image-*` API model.

The original text prompts are preserved for the eight female sci-fi characters:

- [knight](screenshots/art-roster/scifi-knight-female.prompt.txt),
  [archer](screenshots/art-roster/scifi-archer-female.prompt.txt), and
  [mage](screenshots/art-roster/scifi-mage-female.prompt.txt);
- [rogue](screenshots/art-roster/scifi-rogue-female.prompt.txt) and
  [beast](screenshots/art-roster/scifi-beast-female.prompt.txt); and
- [stormcaller, healer, and commander](screenshots/art-roster/scifi-female-prompts.md).

The exact prompts for the other 104 sheets were not saved. Their final PNGs,
their paired body designs, and the style and role guides below are the available
provenance. They are enough for reference-led edits, but not for byte-for-byte
regeneration.

## The sheet contract

Every source is a transparent 1772×886 RGBA image: four columns by two rows of
443×443 cells. The cells are always:

| Row | Cell 1 | Cell 2 | Cell 3 | Cell 4 |
|---|---|---|---|---|
| top | idle A | idle B | walk A | walk B |
| bottom | attack wind-up | attack follow-through | ability or item | hurt or defeat |

Keep one identity, outfit, equipment hand, three-quarter camera, scale, and
baseline through all eight cells. Figures and effects must stay inside their
cells with a transparent gutter. At board scale, readable silhouettes and
action poses matter more than small surface detail.

Male and female characters are complementary designs, not one costume pasted
onto two bodies. They may have different faces, hair, stature, clothing, and
equipment construction. They must still share the same proportions, camera,
contours, material treatment, role, palette logic, and rendering density.

Leave a broad, clean source-colour region on cloth or armour for the team mask.
Do not generate six painted colour copies. The mask changes that region to blue,
red, green, violet, amber, or bone while leaving skin, hair, equipment, natural
features, and effects alone.

## Style and role guides

Combine one row from each table to describe an individual character. Always use
the current target sheet, its male or female counterpart, and two other sheets
from the same style as image references.

| Style | Shared visual language |
|---|---|
| Sci-fi | compact white and navy ceramic armour, practical tech, clean team-colour cloth or plates, restrained cyan effects |
| Mythical | compact illustrated fantasy, readable armour and cloth, strong heroic silhouettes, contained magical effects |
| Medieval | grounded steel, leather, cloth, wood, and heraldry |
| Nature | animal-folk silhouettes, bark, leaf, leather, warm metal, and woodland construction |
| Sengoku | lacquered lamellar armour, indigo cloth, period-inspired weapons and ritual tools, and subtle woodblock influence |
| Undead | corroded dark iron, old ivory bone, worn leather, tattered heraldry, and pale spectral effects |
| Pirates | weathered leather and linen, tarnished brass, dark wood, naval cloth, and sea-green or warm-gold effects |

| Archetype | Details that must remain readable |
|---|---|
| Knight | durable front-line silhouette, close weapon, and a clear guarding or defensive action |
| Archer | ranged weapon, consistent draw or firing hand, and a visible projectile action |
| Mage | magical focus or casting equipment and a deliberate spell action |
| Stormcaller | weather, wind, lightning, or equivalent elemental equipment and effect language |
| Healer | supportive silhouette and a clearly restorative or protective ability |
| Commander | leadership tool such as a baton, banner, standard, or signal device and a rallying ability |
| Rogue | light, agile silhouette, short weapons or stealth tools, and a quick displacement or concealment ability |
| Beast | unmistakably creature-like silhouette, stable limb count, and a lunge, claw, bite, or equivalent natural attack |

Most source filenames follow
`<style>_<archetype>_<male|female>_hd.png`. Sci-fi's original male sheets use
the `relay_*`, `morrow_*`, `rook_*`, and `kestrel_*` names. Mythical's male
knight is `mythical_knight_hd.png`, and its female archer is
`mythical_archer_hd.png`. The runtime's exact source bindings live in
[`graphics_profiles.json`](../platform/godot/project/content/graphics_profiles.json).

## Safe tweak workflow

1. Start from the current PNG in
   [`platform/godot/project/art/characters`](../platform/godot/project/art/characters).
   Keep the target, its counterpart, and same-style references in the edit
   request.
2. For a local problem, request a precise edit and name the cells and objects
   that may change. Explicitly preserve every other cell. Regenerate the full
   sheet only for a deliberate redesign.
3. Request genuine transparency, the exact action order, fixed equipment sides,
   generous gutters, and no text, scenery, floor, shadow, border, or watermark.
4. If normalization is needed, resize the complete output once to 1772×886 with
   high-quality filtering. Apply any safety inset uniformly to every cell; do
   not independently stretch poses.
5. Save the accepted source over its existing filename. Regenerate its team
   mask instead of painting faction colours into the source.
6. Add a prompt note beside the review material in
   `docs/screenshots/art-roster/`. Record the fields below, including any failed
   or corrective pass that materially influenced the final result.
7. Rebuild the previews and colour gallery, then inspect the character both at
   portrait size and at the real board size before accepting it.

For every future generation or edit, record:

```text
Asset:
Date:
Authoring model and version:
Image renderer and dated snapshot, if exposed:
Mode: generate or edit
Quality and requested size:
Reference files and their Git revision:
Prompt and revised prompt, if exposed:
Normalization or manual edits:
Accepted output Git revision:
```

A newer image model is a reason to run a small comparison, not to replace the
roster automatically. Compare the target and one difficult action at board
scale. Prefer the candidate that preserves identity, references, alpha, cell
boundaries, equipment sides, and action continuity; extra detail by itself is
not an improvement if it disappears in play or makes the roster less coherent.

## Rebuild and check a change

Replace `<style>` with `scifi`, `mythical`, `medieval`, `nature`, `sengoku`,
`undead`, or `pirates`:

```sh
tools/placeholder_art/.venv/bin/python \
  platform/godot/scripts/generate-mythical-team-masks.py --style <style>
platform/godot/scripts/generate-art-roster.sh
platform/godot/scripts/generate-team-colour-showcase.sh --style <style>
platform/godot/scripts/smoke-art-library.sh build-godot
platform/godot/scripts/smoke-animated-characters.sh build-godot
```

The mask and preview steps are deterministic. Image generation is not: keep the
accepted source under version control, and compare new candidates rather than
assuming that the same prompt will recreate it.
