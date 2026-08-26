# art/

Art this repository did not draw. Everything Grandleon ships is generated,
deterministically, by `tools/placeholder_art/generate.py`; this directory is
where a drawing that stands in for part of that set lives.

- **`provided/`**: the tree the generator reads before it writes, keyed by the
  path of the asset each file stands in for. Absent here, because this
  repository provides nothing.
- **`examples/`**: one worked replacement, a character sheet, used by
  `tools/placeholder_art/check_provided.py` as the submissions that must be
  accepted. Deliberately *not* read by the generator: an example that took
  effect would be a replacement, and the shipped default set has to stay the
  shipped default set.
