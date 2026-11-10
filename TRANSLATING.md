# Submit a translation

For now, please focus translation work mainly on the editor. Its wording is
still evolving, so some strings will change. Use the demo campaign mainly to
test translations, text layout and accents.

You can translate Grandleon's menus with a small pull request:

1. Copy `platform/godot/project/localization/en.json` to a file named with your
   language code, such as `fr.json`.
2. Translate the text on the right of each `:`. Keep the message IDs on the
   left and placeholders such as `{name}` unchanged.
3. Add the language to
   `platform/godot/project/localization/languages.json`.
4. Open a pull request titled `Add <language> translation`.

If you can run the project tests, use:

```sh
platform/godot/scripts/smoke-localization.sh build-godot
```

If you cannot, submit the pull request anyway. The automated checks will point
out missing messages or placeholders, and maintainers can help finish it.

To translate a game made with Grandleon instead, open that game's
**Translations** screen. You can work there directly or use **Export .PO** and
**Import .PO** with any gettext-compatible translation tool.
