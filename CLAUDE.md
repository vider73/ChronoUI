# Working notes for Claude (and any other model reading this repository)

This file is loaded as context when a model opens the project. It is the
short version of what a session needs to be productive in its first turn.

## What this is

**ChronoUI is a Win32 / Direct2D widget framework.** Two models: the virtual
widgets, which are headers only (`include/V*.hpp`, no DLL to ship), and the
older DLL model, `ChronoUI.dll` (a layout engine and a CSS parser in
`src/core/`) plus 23 hot-pluggable widget DLLs in `src/widgets/`. Thirteen
examples in `src/examples/`, all on the virtual model. It has **no external
dependencies**: Visual Studio 2022 and the Windows SDK build everything. Keep
it that way; a `find_package(... REQUIRED)` in the root `CMakeLists.txt` is a
regression.

Human-facing docs: `README.md` (front page), `docs/EXAMPLES.md` (every example
and how it works), `docs/WIDGETS.md` (every widget in both models),
`CONTRIBUTING.md`. Keep them in sync when you add a widget or an example.

## The two widget models

- **Virtual widgets** — one HWND and one render target per window; widgets are
  C++ objects with `OnDraw` and an optional `OnUpdate(dt)`. Hit-testing, hover,
  capture, focus, Tab order, tooltips, scrolling, custom chrome and the 60 Hz
  heartbeat live in `VirtualWindow` (`include/VirtualWidget.hpp`). **This is the
  model to use.** `src/examples/VirtualShowcase.cpp` is the guided tour;
  `src/examples/Catalog.cpp` shows every widget on five pages.
- **DLL widgets** (`ChronoUI.hpp`, `WidgetImpl.hpp`, `src/widgets/cw.*.cpp`) —
  one HWND per widget, CSS classes, reactive `Bind`, a JSON manifest per widget.
  Only the older demos use it. It stays because the manifest + CSS story is what
  makes the project extensible by prompt.

## Where the virtual widgets are

- `VirtualWidget.hpp` — the host, `VLabel`, `VButton`, `VCombo`.
- `VControls.hpp` — toggle, check, radio, segment, slider, stepper, progress,
  colour picker.
- `VNavigation.hpp` — navigation view, tab view, expander, info bar, the
  light-dismiss `VFlyout` and `VFlyoutButton`, menu, date picker, time picker,
  teaching tip. Menu rows are shared through `VMenuItem` + `vmenu::`.
- `VCollections.hpp` — list view, tree view, grid view; `VSelection` holds the
  click / Ctrl / Shift / Ctrl+A gestures.
- `VActions.hpp` — dropdown, command bar, menu bar, split button, toggle button,
  breadcrumb, suggestions, the modal dialog, link.
- `VIndicators.hpp` — progress ring, rating, badge, person picture.
- `VDraw.hpp` — the `vd::` drawing helpers every `OnDraw` uses: fills, text,
  arcs, gradients, polylines, easing, icons (`Style().Icon()` = Segoe MDL2
  Assets glyphs).
- `VirtualChat.hpp` — bubbles, code blocks and the text editor `VChatInput`.

## Conventions

- **Simplicity and cleanliness.** If something is not used, it goes. If a block
  is copy-pasted, it becomes a loop or a helper.
- **English** for code, comments and UI strings.
- **Everything runs on the UI thread.** Worker threads post `WM_APP+N`; the
  window's `OnMessage` hook picks it up. Never touch a widget from a worker.
- **A widget paints in its host's coordinate space** using its own
  `GetBounds()`, and returns `Handled` from `OnMouseMove` only when something
  visual changed: that return value is what triggers the repaint.
- **Flyouts are chrome, added last** (`AddChrome`), so they paint on top and
  get the click; give them the window size with `Cover(w, h)` from the app's
  layout function.
- **Widgets that change their own size** (`VExpander`, `VInfoBar`, `VNavView`)
  report it through `OnLayout`; the app's single `layout()` places everything
  again.
- **Assets resolve next to the executable** via `AppPaths.hpp`.
- **New DLL widgets export the JSON manifest** and go in `WIDGET_SOURCES`.
- Adding an example = one `.cpp` in `src/examples/` + its name in
  `CHRONOUI_VIRTUAL_EXAMPLES` + a section in `docs/EXAMPLES.md`.

## Surprises worth knowing

- `windows.h` `max` / `min` macros bite `std::max({a, b, c})`; write
  `(std::max)(...)`. NOMINMAX is not defined project-wide.
- The heartbeat is `SetTimer(15)`, not 16: USER timers round up to the system
  tick and 16 ms measured 25 ms per tick (~40 fps) on Windows 11; 15 gives
  ~60 fps. `timeBeginPeriod` changes nothing there.
- A running example holds its own `.exe` lock; the link fails with `LNK1104`
  until it is closed.
- Screenshots and GIFs come from `tools/shoot.ps1` (+ `tools/gif.py`): it
  launches an exe, drives it with clicks, drags and keys, and captures the
  window with PrintWindow, never the screen. It refuses synthetic input when
  another window is in front of the one it launched; that refusal is the
  script working, not a bug.

## Build

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output lands in `build/Release/`. `CHRONOUI_BUILD_EXAMPLES=OFF` builds only the
legacy `ChronoUI.dll` for a parent project that pulls this in with
`add_subdirectory`; a project that uses the virtual widgets needs no build step
at all, just the include path.
