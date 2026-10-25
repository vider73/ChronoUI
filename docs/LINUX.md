# ChronoUI on Linux

The framework headers are the same on both systems. On Linux the names they
include — `<windows.h>`, `<d2d1.h>`, `<dwrite.h>`, `<wincodec.h>`,
`<wrl/client.h>` — resolve to `include/platform/linux/`, which implements the
slice of Win32, Direct2D, DirectWrite and WIC that ChronoUI uses on free
libraries. `VirtualWindow` and every widget compile unchanged; the WndProc
still receives `WM_PAINT`, `WM_MOUSEMOVE`, `WM_NCHITTEST`...

```bash
sudo apt install cmake ninja-build g++ libsdl2-dev libcairo2-dev libpango1.0-dev libstb-dev fonts-noto-core
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
build-linux/Release/Catalog
```

## What stands in for what

| Windows | Linux | Where |
|---|---|---|
| Win32: windows, message queue, timers, mouse capture, keyboard, clipboard, file drop, `TrackPopupMenu` | **SDL2** (zlib) | `chrono_win32.hpp` |
| Direct2D: brushes, gradients, paths and arcs, clips, layers, bitmaps | **Cairo** (LGPL / MPL) | `chrono_d2d.hpp` |
| DirectWrite: formats, layouts, ranges, metrics, hit-testing | **Pango** + HarfBuzz + fontconfig (LGPL) | `chrono_dwrite.hpp` |
| WIC: decode PNG / JPEG / BMP / GIF | **stb_image** (public domain), compiled in | `chrono_wic.hpp` |
| `ComPtr` | a 60-line refcounting pointer | `wrl/client.h` |
| `wWinMain` | `main()` that builds the command line and calls it | `src/platform/linux_main.cpp` |
| Segoe UI / Consolas | Noto Sans / JetBrains Mono, Noto Sans Mono | `MapFamily()` in `chrono_dwrite.hpp` |
| Segoe MDL2 Assets | **ChronoUI Icons** (below) | `assets/fonts/` |

How the pieces work, in short:

- **A HWND is an SDL window plus a WNDPROC.** `GetMessageW` turns SDL events
  into the WM_* messages Windows would send and orders them the same way:
  posted and input messages first, then `WM_TIMER`, then `WM_PAINT` for
  invalidated windows. `PostMessage` is thread-safe and wakes the loop, so the
  `WM_APP + N` pattern for worker threads works as documented.
- **Custom chrome** runs through SDL's hit-test callback, which asks the
  window's own `WM_NCHITTEST` handler: the title bar drags and the edges resize
  exactly where the Windows build says they do.
- **The render target is a Cairo image surface** (premultiplied BGRA, D2D's
  own layout), copied to the SDL window on `EndDraw`. D2D's world transform is
  kept apart from the clip/layer stack, as D2D defines it, and loaded into Cairo
  before each draw.
- **Text positions are characters of the caller's string.** `wchar_t` is 32
  bits on Linux, so a `std::wstring` holds code points; Pango works in UTF-8
  bytes, and every layout keeps the byte offset of each character so hit-tests
  and ranges convert both ways. DirectWrite's box (alignment inside
  `maxWidth x maxHeight` even without wrapping, paragraph alignment) is
  reproduced by offsetting the Pango layout.
- **MSVC-only spellings** (`swprintf_s`, `localtime_s`, `_wtoi`... and `%s`
  meaning a wide string in a wide format) are provided with MSVC's meaning.

## Icons

The widgets name icons by Segoe MDL2 Assets code point (`L"\xE80F"` is home).
Segoe MDL2 ships with Windows and cannot be redistributed, so the Linux port
uses Microsoft's **Fluent UI System Icons**, which are MIT-licensed and drawn
in the same family style:

- `assets/fonts/ChronoUIIcons.ttf` — 18 KB, the 72 icons ChronoUI uses, each
  placed **at its MDL2 code point**. No code changes and no runtime remapping:
  `Style().Icon()` and `VIcon(L"\xE80F")` just work. Built by
  `tools/make_icon_font.py`, which holds the MDL2 → Fluent table; adding an icon
  is one row there and a rerun.
- `assets/fonts/fluent/` — the complete Fluent UI System Icons, Regular and
  Filled (about 10,000 icons each), with their `name → code point` JSON maps and
  the MIT licence. They are registered at startup like the icon font above
  (family names `FluentSystemIcons-Regular` and `FluentSystemIcons-Filled`), so
  an app can use any of them directly, and they are the source the icon font is
  cut from.

The fonts in `assets/fonts/` are registered with fontconfig for the running
process only (`RegisterAppFonts()`), from `<exe dir>/assets/fonts`, where the
build stages them.

## Screenshots

`CHRONOUI_SHOT=out.png` makes any example write its window to a PNG about a
second after it starts (`CHRONOUI_SHOT_MS` to change the delay) and quit. It is
the Linux counterpart of `tools/shoot.ps1`: it reads the app's own pixels and
never the screen.

```bash
CHRONOUI_SHOT=catalog.png build-linux/Release/Catalog
```

## Known limits

- **96 DPI.** DIPs are pixels; a HiDPI screen gets a small window.
- **Input methods.** Text arrives through SDL's text input; there is no IME
  candidate window positioned at the caret.
- **The popup menu** behind `VCombo` (`TrackPopupMenu`) is drawn by the port in
  a plain light style, not in the app's theme.
- **Rendering differences.** Noto Sans is close to Segoe UI but not the same
  metrics, so a label sized tightly on Windows may wrap or clip slightly
  differently. Gamma 2.2 gradients are interpolated linearly.
