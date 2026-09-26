"""Generate docs/API.md, the framework's API in one file a model can read.

    python tools/api_md.py            # writes docs/API.md
    python tools/api_md.py --check    # exit 1 when docs/API.md is stale (CI)

The reference part is extracted from the headers in include/: every class,
its base, its nested types, and every public method that is not one of the
framework's own callbacks (OnDraw, OnMouseDown...), plus the free functions
in the helper namespaces (vd::, vctl::, vtheme::, vdate::, vtext::, vmenu::).
The style of the headers is regular enough for a line-based parse: one member
per line, the body after the signature. The preamble at the top is written
by hand, here, so the whole document regenerates from one command.
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
INC = os.path.join(ROOT, "include")
OUT = os.path.join(ROOT, "docs", "API.md")

HEADERS = [
    "ChronoController.hpp", "VirtualWidget.hpp", "VDraw.hpp", "VControls.hpp", "VNavigation.hpp",
    "VCollections.hpp", "VActions.hpp", "VIndicators.hpp", "VText.hpp", "VMedia.hpp", "VLayout.hpp",
    "VInstruments.hpp", "VEffects.hpp", "VirtualChat.hpp", "AppPaths.hpp",
]

# The framework's own callbacks: every widget has them, so they are listed once
# in the contract section and not repeated per class.
CALLBACKS = {
    "GetTypeName", "OnDraw", "OnUpdate", "OnMouseDown", "OnMouseUp", "OnMouseMove", "OnMouseEnter",
    "OnMouseLeave", "OnMouseWheel", "OnKeyDown", "OnChar", "OnFocus", "CanFocus", "HitTest",
    "SetBounds", "GetBounds", "IsVisible", "SetVisible", "SetProp", "GetProp", "OnClick",
    "Width", "Height", "SetTooltip", "GetTooltip",
}

RE_CLASS = re.compile(r"^\t(class|struct)\s+([A-Za-z_]\w*)(?:\s*:\s*public\s+([A-Za-z_]\w*))?\s*\{")
RE_NS = re.compile(r"^(\s*)namespace\s+([A-Za-z]\w*)\s*\{")
RE_ACCESS = re.compile(r"^\t(public|private|protected):")
RE_NESTED = re.compile(r"^\t\t(struct|enum class|enum|using)\s+([A-Za-z_]\w*)(.*)$")
RE_METHOD = re.compile(r"^\t\t(?:explicit\s+|static\s+|virtual\s+|template\s*<[^>]*>\s*)*([A-Za-z_][\w:<>,\s&*]*?)\s+([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(const)?\s*(?:override)?\s*(?:\{|:|$)")
RE_CTOR = re.compile(r"^\t\t(?:explicit\s+)?([A-Z]\w*)\s*\(([^;{}]*)\)\s*(?::|\{|$)")
RE_FREE = re.compile(r"^\s*inline\s+([A-Za-z_][\w:<>,\s&*]*?)\s+([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(?:\{|$)")
RE_BANNER = re.compile(r"^//\s{3}(V\w+|[a-z]\w*::)\s+(.*)$")


def clean(s):
    return re.sub(r"\s+", " ", s).strip()


def parse_header(name):
    """Returns (banner_lines, classes, namespaces).
    classes: list of dict(name, base, kind, nested[], methods[], ctors[])
    namespaces: dict name -> list of "ret name(args)"."""
    path = os.path.join(INC, name)
    lines = open(path, encoding="utf-8").read().split("\n")
    banner = []
    for ln in lines[:40]:
        m = RE_BANNER.match(ln)
        if m: banner.append((m.group(1), clean(m.group(2))))
        if ln.startswith("#pragma once"): break
    classes, namespaces = [], {}
    cur, access, stack = None, "private", []          # stack: (namespace name, its indent)
    i = 0
    while i < len(lines):
        ln = lines[i]
        m = RE_NS.match(ln)
        if m and cur is None:
            stack.append((m.group(2), m.group(1))); namespaces.setdefault(m.group(2), []); i += 1; continue
        if stack and cur is None and ln == stack[-1][1] + "}":
            stack.pop(); i += 1; continue
        m = RE_CLASS.match(ln)
        if m and cur is None:
            if "};" in ln:                               # a one-line struct: record it whole
                body = ln[ln.find("{") + 1: ln.rfind("}")]
                classes.append(dict(name=m.group(2), base=m.group(3) or "", kind=m.group(1), nested=[clean(body)], methods=[], ctors=[]))
                i += 1; continue
            cur = dict(name=m.group(2), base=m.group(3) or "", kind=m.group(1), nested=[], methods=[], ctors=[])
            access = "public" if m.group(1) == "struct" else "private"
            i += 1; continue
        if cur is None and stack:
            m = RE_FREE.match(ln)
            if m: namespaces[stack[-1][0]].append(f"{clean(m.group(1))} {m.group(2)}({clean(m.group(3))})")
            i += 1; continue
        if cur and ln == "\t};":
            if cur["name"] not in ("VirtualWidgetImpl",) or True: classes.append(cur)
            cur = None; i += 1; continue
        if cur:
            m = RE_ACCESS.match(ln)
            if m: access = m.group(1); i += 1; continue
            if access != "public": i += 1; continue
            # a signature may continue on the next line: join until a '{' or ';'
            sig = ln
            while sig.count("(") > sig.count(")") and i + 1 < len(lines):
                i += 1; sig += " " + lines[i].strip()
            m = RE_NESTED.match(sig)
            if m:
                body = m.group(3)
                if m.group(1) == "using": cur["nested"].append(f"using {m.group(2)} {clean(body.split(';')[0])}")
                else:
                    inner = body[body.find("{") + 1: body.rfind("}")] if "{" in body and "}" in body else ""
                    cur["nested"].append(f"{m.group(1)} {m.group(2)} {{ {clean(inner)} }}")
                i += 1; continue
            m = RE_CTOR.match(sig)
            if m and m.group(1) == cur["name"]:
                cur["ctors"].append(f"{cur['name']}({clean(m.group(2))})"); i += 1; continue
            m = RE_METHOD.match(sig)
            if m and m.group(2) not in CALLBACKS and not m.group(2).startswith("operator"):
                ret, nm, args, const = clean(m.group(1)), m.group(2), clean(m.group(3)), m.group(4)
                if ret in ("return", "if", "for", "while", "else", "switch"): i += 1; continue
                cur["methods"].append(f"{ret} {nm}({args}){' const' if const else ''}")
            i += 1; continue
        i += 1
    return banner, classes, namespaces


PREAMBLE = """# ChronoUI — the API, for a model

This file is generated by `tools/api_md.py` from the headers in `include/`;
regenerate it after touching a header. It is the whole surface in one read:
first how an app is put together, then every class and function.

## How an app is put together

```cpp
#include "VirtualWidget.hpp"      // the host, VLabel, VButton, VCombo, the theme
#include "VControls.hpp"          // and whichever families the screen uses
#include "VDraw.hpp"
using namespace ChronoUI;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    ChronoControllerImpl::Instance();                       // the D2D / DWrite / WIC factories, once
    VirtualWindow win;
    if (!win.Create(hInstance, L"Title", 1100, 700)) return 1;
    win.SetBackground(vtheme::Current().window);

    auto* title = win.Add<VLabel>();  title->Text(L"Hello").FontSize(20.0f);
    auto* go    = win.Add<VButton>(); go->Text(L"Go");
    auto* level = win.Add<VSlider>(); level->Set(0.5f);
    level->OnChange([&](float v) { title->Text(L"Level " + vd::Num(v * 100.0) + L"%"); });
    go->OnClick([&] { /* ... */ });

    auto layout = [&] {                                     // every rectangle is decided here
        RECT rc; GetClientRect(win.GetHWND(), &rc);
        float W = (float)rc.right, H = (float)rc.bottom;
        title->SetBounds(vd::Rect(24, 16, W - 48, 32));
        level->SetBounds(vd::Rect(24, 64, W - 48, 28));
        go   ->SetBounds(vd::Rect(W - 144, H - 60, 120, 36));
        InvalidateRect(win.GetHWND(), NULL, FALSE);
    };
    win.OnResize(layout);
    layout();
    return win.RunMessageLoop();
}
```

The rules that make the rest predictable:

- **One `layout()` function** places every widget with `SetBounds`; call it on
  `OnResize` and whenever a container reports through `OnLayout` that it moved
  (`VExpander`, `VInfoBar`, `VNavView`, `VSplitView`, `VTwoPaneView`). There is
  no parent-child tree: containers hand out rects (`ContentRect()`,
  `PaneRect()`, `Pane1Rect()`...) and the app places the children in them.
- **Flyouts are chrome**: anything built on `VFlyout` (`VMenu`, `VDropDown`,
  `VDatePicker`, `VCommandBar`, `VTeachingTip`, `VDialog`...) is added with
  `win.AddChrome<T>()`, after the content, gets `Attach(&win)` once and
  `Cover(W, H)` from `layout()`.
- **Overlays are added after what they cover** (`VSnow`, `VStorm`, `VBusy`,
  the pane widgets of an overlay `VSplitView`), because paint order is
  creation order and hit-testing is the reverse.
- **Repaint** is `InvalidateRect(win.GetHWND(), NULL, FALSE)`. A widget that
  changes something from a callback asks for it; a widget that animates
  returns `true` from `OnUpdate(dt)` and the host repaints on its own.
- **Colours come from the theme**: `vctl::Ink()`, `Muted()`, `Surface()`,
  `Border()`, `Outline()`, `Subtle()`, `Track()`, `Blue()`, `Hover(alpha)`.
  `vtheme::SetDark(true)` plus `win.SetBackground(vtheme::Current().window)`
  and a repaint switch the whole window. A literal grey in a widget is a bug.
- **Everything runs on the UI thread.** A worker posts `WM_APP + n` to
  `win.GetHWND()` and `win.OnMessage(...)` picks it up.
- **Windows.h macros**: write `(std::max)(a, b)`.

## The widget contract

Every widget derives from `VirtualWidgetImpl`, whose members every class below
has and which are therefore not repeated:

```cpp
const char* GetTypeName() const;               // "VSlider"
void        SetBounds(const D2D1_RECT_F&);     D2D1_RECT_F GetBounds() const;
bool        IsVisible() const;                 void SetVisible(bool);
void        SetTooltip(const std::wstring&);   // the host shows it after ~0.55 s
void        OnClick(std::function<void()>);    // buttons, links, icons
void        SetProp(const char*, const char*); const char* GetProp(const char*, const char* def) const;
float       Width() const;                     float Height() const;
```

and the callbacks a new widget overrides (all optional except `OnDraw`):

```cpp
void         OnDraw(ID2D1RenderTarget* rt);              // paint in the host's coordinates, inside GetBounds()
bool         OnUpdate(float dt);                         // true while another frame is wanted
VInputResult OnMouseDown / OnMouseUp(float x, float y, int btn);   // btn 1 = left, 2 = right
VInputResult OnMouseMove(float x, float y);              // return Handled only when something visible changed
VInputResult OnMouseEnter(); OnMouseLeave();
VInputResult OnMouseWheel(float delta, float x, float y);
VInputResult OnKeyDown(UINT vk); OnChar(wchar_t ch);
void         OnFocus(bool gained);  bool CanFocus() const;   // CanFocus true = in the Tab order
bool         HitTest(float x, float y) const;               // default: inside the bounds
```

`VInputResult` is `NotHandled`, `Handled` or `Capture` (from a mouse-down: keep
the drag until release). Protected state a widget can use: `m_bounds`,
`m_hovered`, `m_focused`, `m_pressed`, `m_visible`.

A new widget in full:

```cpp
class VDot : public VirtualWidgetImpl {
    float m_t = 0.0f;
    D2D1_COLOR_F m_accent = vctl::Blue();
    std::function<void()> m_cb;
public:
    const char* GetTypeName() const override { return "VDot"; }
    bool CanFocus() const override { return true; }
    VDot& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }     // setters chain
    void OnChange(std::function<void()> cb) { m_cb = std::move(cb); }
    bool OnUpdate(float dt) override { m_t += dt; return true; }
    VInputResult OnMouseUp(float x, float y, int btn) override {
        if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
        if (m_cb) m_cb(); return VInputResult::Handled;
    }
    void OnDraw(ID2D1RenderTarget* rt) override {
        vd::Fill(rt, m_bounds, vctl::Surface(), 8.0f);
        vd::Stroke(rt, m_bounds, m_focused ? m_accent : vctl::Border(), 8.0f, 1.0f);
        vd::Circle(rt, vd::CX(m_bounds), vd::CY(m_bounds), 8.0f + 2.0f * sinf(m_t * 3.0f), m_accent);
    }
};
```

To add it to the framework: put it in the header of its family (a knob goes
in `VControls.hpp`, a popup on `VFlyout` in `VNavigation.hpp`...), give it a
cell in `src/examples/Catalog.cpp` on the matching page, a row in
`docs/WIDGETS.md`, and regenerate this file. `tools/shoot.ps1 -Exe
build\\Release\\Catalog.exe -ExeArgs "<page>" -Out shot.png` shows what it looks
like; `"<page> dark"` in the dark theme.

## Conventions in the signatures below

- A setter returns `T&` so calls chain: `slider->Set(0.5f).Accent(c);`.
- `OnChange(std::function<void(X)>)` is the one event; a few widgets have more
  (`OnClose`, `OnPick`, `OnLayout`, `OnSubmit`, `OnLink`, `OnResult`).
- `Value()` reads the state; `Set()` writes it without firing the event.
- Flyouts: `Attach(VirtualWindow*)`, `Cover(w, h)`, `Open(anchor)` / `Show(anchor)`, `Close()`, `IsOpen()`.
- Containers: `...Rect()` getters for the app's layout, `OnLayout(std::function<void()>)` while they move.
- Glyphs are Segoe MDL2 Assets code points as wide strings, e.g. `L"\\xE80F"` (home).

---

# Reference

"""


def main():
    out = [PREAMBLE]
    for h in HEADERS:
        banner, classes, namespaces = parse_header(h)
        out.append(f"## `{h}`\n")
        if banner:
            for name, desc in banner: out.append(f"- **{name}** — {desc}")
            out.append("")
        for ns, fns in namespaces.items():
            if not fns: continue
            out.append(f"### namespace `{ns}::`\n")
            out.append("```cpp")
            out.extend(fns)
            out.append("```\n")
        for c in classes:
            if not (c["methods"] or c["ctors"] or c["nested"]): continue
            head = f"### `{c['name']}`" + (f" : {c['base']}" if c["base"] else "")
            out.append(head + "\n")
            out.append("```cpp")
            out.extend(c["nested"])
            out.extend(c["ctors"])
            out.extend(c["methods"])
            out.append("```\n")
    text = "\n".join(out).rstrip() + "\n"
    if "--check" in sys.argv:
        old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if old != text:
            print("docs/API.md is stale: run python tools/api_md.py"); sys.exit(1)
        print("docs/API.md is up to date"); return
    open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    n_classes = sum(1 for h in HEADERS for c in parse_header(h)[1] if c["methods"] or c["ctors"])
    print(f"wrote {OUT}: {len(text.splitlines())} lines, {n_classes} classes")


if __name__ == "__main__":
    main()
