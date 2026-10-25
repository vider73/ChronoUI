# Fonts (Linux port)

- `ChronoUIIcons.ttf` — the 72 icons ChronoUI's widgets use, at their Segoe MDL2
  Assets code points, cut from Fluent UI System Icons by `tools/make_icon_font.py`.
- `fluent/` — the complete [Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons)
  by Microsoft, Regular and Filled, with their name → code point maps.
  MIT licence: `fluent/LICENSE`.

The Linux build registers every font in this folder for the running app
(fontconfig, process only). Windows uses Segoe MDL2 Assets and ignores them.
See docs/LINUX.md.
