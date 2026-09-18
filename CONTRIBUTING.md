# Contributing to ChronoUI

Short version: open a pull request. Models are welcome co-authors — most of this
codebase was written that way. What follows is what will save you a round trip.

## Getting it to build

Visual Studio 2022 with the Desktop C++ workload, and CMake ≥ 3.21. Nothing
else: the framework, the widgets and the examples have no external
dependencies. One target wants nlohmann/json (`WidgetTesterDemo`); without it that one
is skipped and the rest still builds.

Close any running example before rebuilding — a running exe holds its own
lock and the link step fails with `LNK1104`.

## Good first contributions

- **A new `cw.*` widget.** One self-contained `.cpp` in `src/widgets/`, plus its
  path in `WIDGET_SOURCES` in the root `CMakeLists.txt`. Nothing else to touch.
  Copy the closest existing widget as a template.
- **Tests for the parsers** — `TableSpec`, base64, the small JSON walkers. They
  are textual and easy to fuzz, and they are where malformed model output bites.
- **A GitHub Actions workflow** that builds and attaches a zip on tag.
- **A dark palette** for the virtual widgets.

## Conventions

- **C++17, MSVC.** No exceptions-as-flow, no RTTI-heavy designs. The framework
  core avoids third-party dependencies; apps built on it may use what they need.
- **English** for code, comments and UI strings. Some Spanish survives in older
  files; replacing it as you pass through is welcome.
- **Everything runs on the UI thread.** Worker threads post `WM_APP+N` and the
  window's `OnMessage` hook picks it up. Do not touch widgets from a worker.
- **`windows.h` defines `max`/`min` macros.** Brace-init calls need parentheses:
  `(std::max)({a, b, c})`.
- **A widget paints in its host's coordinate space** using its own `GetBounds()`,
  and returns `Handled` from `OnMouseMove` only when something visual actually
  changed — that return value is what triggers a repaint.
- **New DLL widgets must export the JSON manifest** (properties, types, defaults,
  events). It is the contract the tooling and the assistants read.

`CLAUDE.md` at the repo root is the long-form version: the design vocabulary,
the conventions, and the traps that cost someone a day. Worth a skim before a
larger change, whether you are a human or not.

## Pull requests

- One topic per PR. A new widget and a framework fix are two PRs.
- Say how you tested it. "Built Release and ran `VirtualShowcase`" is a fine
  answer; a screenshot is better, and `tools/shoot.ps1` will take it for you.
- Note if a model wrote most of it. Nobody minds, and it helps reviewing.

## Reporting bugs

Include the Windows build, whether you are on a scaled or multi-monitor setup
(a good share of the historical bugs were DPI or monitor-boundary related), and
the smallest snippet that reproduces it. If a window paints wrong, a screenshot
says more than a paragraph.
