# Portable art replacements

This directory holds the input contract and worked examples for replacing
portable generated assets. It is not the project's complete artwork library.
The current Godot character and terrain art lives under
`platform/godot/project/art/` and is described in
[Artwork](../docs/ARTWORK.md).

- **`provided/`** is the optional replacement tree read by
  `tools/placeholder_art/generate.py`. A file's path identifies the generated
  asset it replaces.
- **`examples/`** contains a worked character-sheet replacement used by
  `tools/placeholder_art/check_provided.py`. Examples are deliberately not
  loaded as shipped replacements.

The current release does not provide an in-app external-art importer. This
filesystem contract remains useful to developers and gives future editor work
a tested foundation if that feature is added later.
