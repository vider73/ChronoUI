# ChronoUI — Widget Catalog

ChronoUI ships **two widget models** that coexist in the same process and
share one Direct2D/DirectWrite controller (`ChronoControllerImpl`):

| Model | Header | One HWND per… | Styling | Best for |
|---|---|---|---|---|
| **DLL widgets** (`cw.*.dll`) | `ChronoUI.hpp`, `WidgetImpl.hpp` | widget | CSS classes + properties, JSON manifest | dashboards, forms, plug-in controls an LLM can generate in one file |
| **Virtual widgets** (`V*`) | `VirtualWidget.hpp`, `VirtualChat.hpp` | window | C++ fluent setters | dense, scrolling, animated screens: chats, lists, mind maps, panels |

Both are hardware accelerated, both run on the UI thread, and a single app can
mix them (JLToys and ClaudeMM are `VirtualWindow` apps; the dashboard demo is
all DLL widgets).

---

## 1. DLL widgets (`src/widgets/cw.*.cpp`)

Each widget is a self-contained `.cpp` compiled to its own DLL and loaded by
`WidgetFactory::Create("cw.Name.dll")`. Every DLL exports a **JSON manifest**
(name, description, properties with types and defaults, events) — that manifest
is what makes the model *LLM-native*: an assistant can read one file, learn the
whole API, and write a new widget in a single pass
(see the README's *Add a widget with a prompt*).

Common surface (all widgets): `SetProperty(key, value)`, CSS classes via
`AddClass("btn btn-primary")`, `:hover` / `:disabled` states,
`addEventHandler("onClick", lambda)`, `AddTimer(name, ms)`, and reactive
`Bind("prop", ChronoObservable<T>)`.

| Widget | What it is |
|---|---|
| `cw.Button` | A stable, layout-consistent button. Icons (file or base64), pill shape, badge, tooltip, chevron. |
| `cw.EditBox` | Text input with CSS styling, validation, masking and icon support. |
| `cw.StaticText` | Label with CSS state support (`:hover`, `:disabled`). |
| `cw.SwitchButton` | Animated toggle switch. |
| `cw.SliderControl` | Hardware-accelerated range slider. |
| `cw.TextSlider` | Smooth scrolling text ticker. |
| `cw.Progress` | Modern progress bar. |
| `cw.AnimatedParticlesProgress` | Animated progress control with smart title alignment and overflow protection. |
| `cw.ListCards` | Fixed-layout card list. |
| `cw.TitleDescCard` | Card with a pill-styled icon, title and description. |
| `cw.ImageViewerWidget` | Image viewer with zoom, pan and magnifier. |
| `cw.DataPlotControl` | Real-time scrolling data plot. |
| `cw.VitalsMonitor` | Retro CRT monitor for ECG or real-time signal plotting. |
| `cw.EqualizerBar` | Segmented LED audio visualizer with smooth transitions. |
| `cw.GaugeSpeedOmeter` | Animated speedometer gauge. |
| `cw.GaugeEngineTemperatureControl` | Analog engine-temperature gauge with warning zones. |
| `cw.GaugeBatteryLevelControl` | Battery voltage gauge with animated needle and status zones. |
| `cw.AnalogClock` | Analog clock (configurable ring, dial and hand colours, optional second hand). |
| `cw.ViewDateTimeWidget` | System clock with date. |
| `cw.EyesControl` | Animated eyes that follow the mouse cursor. |
| `cw.SnowingOverlay` | Snowing particle overlay; mouse passes through. |
| `cw.LightingStormOverlay` | Storm overlay with glow effects. |
| `cw.WaitingOverlay` | Dims content and shows a spinner. |

The layout engine behind them (`CreateRootLayout(rows, cols)`, `WidgetSize::Fixed / Percent / Fill`,
`StackMode::CommandBar`, `justify-content` / `align-items`) is in `src/core/ChronoUI.cpp`;
CSS parsing (`bootstrap_lite.css`, `rgb()`, named colours, numeric `font-weight`,
margin shorthand, runtime theme swap with `StyleManager::LoadCSSReplace`) is in
`src/core/ChronoStyles.cpp`. `src/examples/Welcome.cpp` is the short reference
app for this model; `src/examples/main.cpp` (ChronoUIDemo) exercises every widget above.

---

## 2. Virtual widgets (`include/VirtualWidget.hpp`)

A `VirtualWindow` owns **one HWND and one render target** and hosts a scene
graph of C++ objects. Hit-testing, hover, capture, focus, keyboard routing,
tooltips, scrolling and a 60 Hz animation heartbeat are all done in C++ on the
container. This is the model every app built on ChronoUI uses today.

### The host: `VirtualWindow`

- `Add<T>()` — scrollable content (content space, can exceed the viewport).
- `AddChrome<T>()` — pinned chrome (header bars, input strips, side panels);
  painted on top, hit-tested first, never scrolls. `BringChromeToFront` for overlays.
- `SetSafeAreaInsets(top,right,bottom,left)` — content is clipped inside the chrome.
- Scrolling: `SetContentHeight`, `ScrollToBottom`, `IsAtBottom`, wheel handling
  with a window-level `OnWheelHook`.
- `Create(hInst, title, w, h, customChrome, titleBarH, deferShow)` — borderless
  window with a painted title bar; drag/resize handled through `WM_NCHITTEST`
  (decorative chrome falls through to the caption, interactive chrome does not).
- Hooks: `OnResize`, `OnScroll`, `OnTick(dt)`, `OnMessage(msg,wp,lp)` for
  `WM_APP+N` from worker threads, `EnableFileDrop` + `OnFilesDropped`.
- Hooks: `OnKeyHook` sees every key before the focused widget (a suggestion list steering the arrows), `OnWheelHook` the same for the wheel.
- Focus: `SetFocusWidget`, `CycleFocus(±1)` (what Tab / Shift+Tab call: every visible
  widget whose `CanFocus()` is true, content first then chrome), and `RemoveWidget` /
  `RemoveChrome` keep capture/hover/focus consistent.
- Tooltips: any `VirtualWidgetImpl::SetTooltip(text)`; the host shows a dark pill after ~0.55 s.
- Modal use: `SetSuppressQuitOnClose(true)` for dialogs that must not end the app.

### The contract: `IVirtualWidget` / `VirtualWidgetImpl`

`OnDraw(pRT)`, `OnUpdate(dt) -> bool` (ask for another frame), `OnMouseDown/Up/Move/Enter/Leave/Wheel`,
`OnKeyDown`, `OnChar`, `OnFocus`, `CanFocus`, `HitTest`, plus the same string
property store and `OnClick` as the DLL model. Return `VInputResult::Capture`
from a mouse-down to receive the drag until release.

`src/examples/VirtualShowcase.cpp` exercises all of the above in one window
(custom chrome, two layers, chat widgets, tooltips, animation, file drop,
scroll-aware chrome) and is the file to copy when starting a virtual-widget app.
The other examples in [EXAMPLES.md](EXAMPLES.md) each isolate one idea:
animated panels (Dashboard), capture and drag (Kanban), form controls
(Controls), drawing and a lightbox (Gallery), simulation (Bounce).

### Drawing: `VDraw.hpp`

`namespace vd` holds what an `OnDraw` reaches for: `Col`/`Mix`/`Alpha` for
colours, `Lerp`/`EaseOut`/`EaseInOut`/`Approach` for motion, `Rect`/`Inset`/
`LerpRect`, `Fill`/`Stroke`/`Gradient`/`Circle`/`Ring`/`Line`/`Shadow`,
`Polyline` (open or filled) and `Arc` on path geometry, and `Text`/`TextWidth`
with a fluent `vd::Style()` (`.Size().Bold().Center().Wrap().Mono()`).
Brushes and formats are created per call — cheap in Direct2D, and it keeps
widgets free of cached resources.

### Widgets in the framework

| Widget | Header | What it is |
|---|---|---|
| `VLabel` | VirtualWidget.hpp | Single-line text: `Text`, `FontSize`, `Color`. |
| `VButton` | VirtualWidget.hpp | Rounded button with hover/press/disabled states, `OnClick`. |
| `VCombo` | VirtualWidget.hpp | Flat dropdown (native popup menu), `Items`, `Select`, `OnChange`. |
| `VChatBubble` | VirtualChat.hpp | User/assistant message bubble: word-wrap, markdown-ish rendering, inline images, streaming text, self-measuring height. |
| `VCodeBlock` | VirtualChat.hpp | Monospace dark code block with language label and copy affordance. |
| `VTypingIndicator` | VirtualChat.hpp | Three animated dots driven by the heartbeat. |
| `VChatInput` | VirtualChat.hpp | Multi-line editor with caret, selection, clipboard, attachments and Send/Stop. |
| `VStackItem` / chat layout | VirtualChat.hpp | Stacks widgets vertically, measures total height for scrolling. |
| `VToggle`, `VCheck`, `VRadio`, `VSegment`, `VSlider`, `VStepper`, `VProgress` | VControls.hpp | The everyday form controls, 30–60 lines each, animated with `vd::Approach`; all keyboard-operable and part of the Tab order. |
| `VNavView` | VNavigation.hpp | Side navigation: glyph + label items, a footer, compact mode behind the menu button; `OnLayout` reports the animated width. |
| `VTabView` | VNavigation.hpp | A strip of closable tabs with an add button; `Add` / `Remove`, `OnChange` / `OnClose` / `OnAdd`. |
| `VExpander` | VNavigation.hpp | A card with a header that opens a content area; `ShownHeight` and `ContentRect` for the children, `OnLayout` while it animates. |
| `VInfoBar` | VNavigation.hpp | Inline message with a severity, an optional action and a close button; collapses to nothing when closed. |
| `VFlyout` | VNavigation.hpp | The light-dismiss popup base: covers the window while open, closes on outside click / Esc / focus loss, derived classes draw the panel. |
| `VMenu` | VNavigation.hpp | A menu flyout: glyph, label, shortcut, separators, check items; keyboard and mouse. |
| `VDatePicker` | VNavigation.hpp | A date button that opens a calendar flyout; arrows move the day, PgUp/PgDn the month. `vdate::` has the date arithmetic. |
| `VListView` | VCollections.hpp | Glyph / title / subtitle rows with single or multi selection (Ctrl, Shift, Ctrl+A), own scrolling, `Erase`. |
| `VTreeView` | VCollections.hpp | Nested nodes with chevrons; open state lives in the node, arrows walk and fold, `OnSelect`. |
| `VFlyoutButton` | VNavigation.hpp | A button that opens its own flyout beneath it; `DrawButton` + `DrawPanel`. `VDatePicker`, `VDropDown` and `VCommandBar` are built on it. |
| `VDropDown` | VActions.hpp | A drawn combo box: `Items`, `Select`, `Prefix`, `OnChange`; the list is a flyout. |
| `VCommandBar` | VActions.hpp | Icon + label buttons in a row; what does not fit goes behind "..." into an overflow flyout. `Add`, `Separator`, `Enable`, `OnPick`. |
| `VBreadcrumb` | VActions.hpp | A › B › C with the parents clickable; collapses the middle to "…" when narrow. `OnPick(parent index)`. |
| `VDialog` | VActions.hpp | Modal content dialog: scrim, title, wrapped body, Primary / Secondary / Close, Enter and Esc; `OnResult(0/1/2)`. |
| `VLink` | VActions.hpp | A hyperlink: underline on hover or focus, `OnClick`, Enter activates. |
| `VProgressRing` | VIndicators.hpp | Spinning arc while `Active`, or a determinate ring with `Set(0..1)`. |
| `VRating` | VIndicators.hpp | Stars: hover previews, click sets (again to clear), arrows nudge; `Max`, `OnChange`. |
| `VBadge` | VIndicators.hpp | A count pill (99+) or a dot, in a colour. `VNavView::Badge` draws the same thing on a navigation row. |
| `VPersonPicture` | VIndicators.hpp | Initials on a colour hashed from the name (`vd::Avatar`), or a glyph. |
| `VColorPicker` | VControls.hpp | Saturation/value square, hue bar, swatch and hex; drag or arrows. `OnChange(D2D1_COLOR_F)`. |
| `VTimePicker` | VNavigation.hpp | A time button opening an hour / minute grid; arrows nudge. `VTime {h, m}`. |
| `VTeachingTip` | VNavigation.hpp | A callout with a beak towards its anchor: title, wrapped body, close, an action. `Show(anchor)`. |
| `VMenuBar` | VActions.hpp | File / Edit / View menus in a bar; the open menu follows the mouse across titles; `OnPick(menu, item)`. |
| `VSplitButton` | VActions.hpp | Main part fires `OnClick`, the chevron opens a menu, `OnPick`. |
| `VToggleButton` | VActions.hpp | Glyph (+ label) button that stays pressed; `Set`, `Value`, `OnChange`. |
| `VSuggestions` | VActions.hpp | The list under a search box: shown with `Show(anchor, items)`, steered with `HandleKey` from `OnKeyHook`, never takes focus. |
| `VGridView` | VCollections.hpp | Tiles painted by a callback with captions; columns from the width, own scrolling, list-style selection. |

### Widgets in the apps built on it

Not part of the framework and not in this repository (the apps have their own),
but listed as proof of what the contract above carries: each is one header.

| Widget | App | Purpose |
|---|---|---|
| `VMindMap` | ClaudeMM | Two-sided tidy tree with pan/zoom, folding, drag & drop, flashing, live pulses. |
| `VSessionListPanel`, `VInspectorPanel`, `VPickerButton` + `VPickerPopup` | ClaudeMM | Searchable list with inline tag editor, right inspector with multi-line note editor, project picker popup. |
| `VWorldClock` | JLToys | Rack of city clocks with day/night tint, scrolling, time shifting. |
| `VNoteList` | JLToys | Sticky-note list with alarms and archiving. |

---

## 3. Writing a new virtual widget

```cpp
class VDot : public ChronoUI::VirtualWidgetImpl {
    float m_t = 0.f;
public:
    const char* GetTypeName() const override { return "VDot"; }
    bool OnUpdate(float dt) override { m_t += dt; return true; }     // animate every frame
    void OnDraw(ID2D1RenderTarget* rt) override {
        ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x4A90E2, 0.6f + 0.4f * sinf(m_t * 3)), &b);
        float cx = (m_bounds.left + m_bounds.right) * 0.5f, cy = (m_bounds.top + m_bounds.bottom) * 0.5f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8, 8), b.Get());
    }
};

// in wWinMain:
ChronoControllerImpl::Instance();
VirtualWindow win;
win.Create(hInst, L"Dot", 320, 200);
auto* dot = win.Add<VDot>();
dot->SetBounds(D2D1::RectF(140, 80, 180, 120));
return win.RunMessageLoop();
```

Rules that keep it fast: paint in the host's coordinate system using your own
`GetBounds()`, return `Handled` from `OnMouseMove` **only** when something visual
changed (that is what triggers a repaint), and never block the UI thread — post
`WM_APP+N` from workers and handle it in `OnMessage`.

---

## 4. Where the screenshots come from

`tools/shoot.ps1` launches an exe, waits for its window, optionally drives it
(typing, keys, clicks) and saves the window as PNG. The gallery in
`docs/screenshots/` is regenerated with it; see the README for the exact calls.
