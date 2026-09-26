# Working notes for Claude (and any other model reading this repository)

This file is loaded as context when a model opens the project. It is the
short version of what a session needs to be productive in its first turn.

## What this is

**ChronoUI is a Win32 / Direct2D widget framework built to be driven by a
model like you.** An app is one `.cpp` that includes headers; every widget has
the same shape (chainable setters, `OnChange`, `SetBounds`); a screen is one
`layout()` function of rectangles; a widget is `OnDraw` and maybe
`OnUpdate(dt)`. When asked for a new app, start from the closest example in
`src/examples/` and keep its structure. When asked for a new widget, copy the
closest class in its family's header and keep its shape.

The framework is **headers only**: `include/V*.hpp` on top of
`ChronoController.hpp` (the Direct2D, DirectWrite and WIC factories). There is
no DLL, no library to build, and **no external dependency**: Visual Studio
2022 and the Windows SDK build everything. Keep it that way; a
`find_package(... REQUIRED)` in `CMakeLists.txt` is a regression. Fourteen
examples in `src/examples/`, one file each.

Human-facing docs: `README.md` (front page), `docs/EXAMPLES.md` (every example
and how it works), `docs/WIDGETS.md` (every widget), `CONTRIBUTING.md`. Keep
them in sync when you add a widget or an example.

## How it works

One HWND and one render target per window; widgets are C++ objects with
`OnDraw` and an optional `OnUpdate(dt)`. Hit-testing, hover, capture, focus,
Tab order, tooltips, scrolling, custom chrome and the 60 Hz heartbeat live in
`VirtualWindow` (`include/VirtualWidget.hpp`). `src/examples/VirtualShowcase.cpp`
is the guided tour; `src/examples/Catalog.cpp` shows every widget on nine
pages (a page number on its command line opens that page, `dark` after it
starts dark); `Booking.cpp` puts the layout, calendar, flip view, pivot and
scroll viewer widgets in one screen.

## Where the widgets are

- `VirtualWidget.hpp` — the host, the theme (`VTheme`, `vtheme::`), `VLabel`,
  `VButton`, `VCombo`.
- `VDraw.hpp` — the `vd::` drawing helpers every `OnDraw` uses: fills, text,
  arcs, gradients, polylines, easing, icons (`Style().Icon()` = Segoe MDL2
  Assets glyphs).
- `VControls.hpp` — toggle, check, radio, segment, slider, stepper, progress,
  colour picker; and the `vctl::` colour shorthands.
- `VNavigation.hpp` — navigation view, tab view, expander, info bar, the
  light-dismiss `VFlyout` and `VFlyoutButton`, menu, date picker, time picker,
  teaching tip, selector bar, pivot, calendar view. Menu rows are shared
  through `VMenuItem` + `vmenu::`.
- `VCollections.hpp` — list view, tree view, grid view, flip view, pips pager,
  annotated scrollbar; `VSelection` holds the click / Ctrl / Shift / Ctrl+A
  gestures.
- `VActions.hpp` — dropdown, command bar, menu bar, split button, toggle
  split button, toggle button, repeat button, breadcrumb, suggestions, the
  modal dialog, link, command bar flyout.
- `VIndicators.hpp` — progress ring, rating, badge, person picture.
- `VText.hpp` — number box (it evaluates expressions), password box, rich text.
- `VMedia.hpp` — icon, animated icon, image (WIC), shapes.
- `VLayout.hpp` — scroll viewer, split view, two-pane view. A container owns
  its geometry and motion and exposes rects (`PaneRect`, `ContentRect`,
  `Pane1Rect`...); the app places the children from them and the container
  calls `OnLayout` while it animates. There is no parent-child tree.
- `VInstruments.hpp` — gauge, analog clock, vitals trace, equalizer, plot.
- `VEffects.hpp` — eyes, snow, storm, ticker, busy veil (overlays: give them
  the bounds of what they cover and add them after it).
- `VirtualChat.hpp` — bubbles, code blocks and the text editor `VChatInput`.

## Theme

`VTheme` (VirtualWidget.hpp) is the palette; widgets read it at draw time via
`vtheme::Current()` and the `vctl::` shorthands in VControls.hpp. Never write
a literal grey or white in a widget: `vctl::Surface()`, `Border()`,
`Outline()`, `Subtle()`, `Dim()`, `Hover(alpha)` are what a card, a hairline,
a field edge, a hover wash are called, and they flip with `vtheme::SetDark`.
White text on the accent, scrims and shadows stay literal on purpose.

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
- **Widgets that change their own size** (`VExpander`, `VInfoBar`, `VNavView`,
  `VSplitView`) report it through `OnLayout`; the app's single `layout()`
  places everything again.
- **Assets resolve next to the executable** via `AppPaths.hpp`.
- Adding an example = one `.cpp` in `src/examples/` + its name in
  `CHRONOUI_VIRTUAL_EXAMPLES` + a section in `docs/EXAMPLES.md`.
- Adding a widget = one class in the header of its family + a cell in the
  Catalog + a row in `docs/WIDGETS.md`.

## Surprises worth knowing

- `windows.h` `max` / `min` macros bite `std::max({a, b, c})`; write
  `(std::max)(...)`. NOMINMAX is not defined project-wide.
- The heartbeat is `SetTimer(15)`, not 16: USER timers round up to the system
  tick and 16 ms measured 25 ms per tick (~40 fps) on Windows 11; 15 gives
  ~60 fps. `timeBeginPeriod` changes nothing there.
- A virtual-widget app never initialises COM, so the controller's WIC factory
  is null there; `VImage` makes its own after `CoInitializeEx`. Anything else
  that decodes images needs the same.
- A running example holds its own `.exe` lock; the link fails with `LNK1104`
  until it is closed.
- Screenshots and GIFs come from `tools/shoot.ps1` (+ `tools/gif.py`): it
  launches an exe, drives it with clicks, drags and keys, and captures the
  window with PrintWindow, never the screen. It refuses synthetic input when
  another window is in front of the one it launched; that refusal is the
  script working, not a bug. Its click coordinates are physical pixels.

## Build

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output lands in `build/Release/`. A project that uses ChronoUI needs no build
step at all: `add_subdirectory` it and link `ChronoUI::Virtual`, which is the
include path and nothing more. `.github/workflows/build.yml` builds the tree
on every push with warnings as errors.
