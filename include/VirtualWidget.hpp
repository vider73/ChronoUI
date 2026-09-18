// =============================================================================
// VirtualWidget.hpp — lightweight, HWND-free widgets.
// =============================================================================
//
// MOTIVATION
// ----------
// In the legacy ChronoUI model, every widget owns a Win32 HWND. That gives free
// hit-testing and message routing from the OS, but at the cost of:
//   * One HWND per visible control. A complex screen can easily hit hundreds of
//     child windows, each with its own message loop overhead and its own
//     ID2D1HwndRenderTarget.
//   * No sub-pixel positioning. HWNDs are integer-pixel positioned.
//   * No partial-alpha overlap between siblings. HWNDs can clip each other but
//     don't blend.
//   * Z-order is awkward. Animations that fly across multiple cells are hard.
//
// VirtualWidget moves to a "scene graph" model:
//   * The CONTAINER owns one HWND and one D2D render target.
//   * Widgets are C++ objects with bounds + a paint method. They are drawn
//     directly onto the container's render target, in order.
//   * Hit-testing, hover tracking, focus, and key routing happen in C++ on the
//     container's input messages. Each widget gets OnMouseMove/OnMouseDown/etc.
//
// MIGRATION STRATEGY
// ------------------
// This system is ADDITIVE. Existing IWidget / WidgetImpl widgets and demos
// continue to work unchanged. To opt in, build a VirtualWindow instead of
// an IContainer, then add virtual widgets to it.
//
// We'll port individual legacy widgets to virtual equivalents over time. The
// first few examples live in this header (VLabel, VButton) as proofs of model.
// Real reusable virtual widgets will live in src/widgets/v.*.cpp like the
// existing cw.*.cpp set.
//
// THREADING
// ---------
// Everything runs on the UI thread. No locks needed — keep it that way.
// =============================================================================

#pragma once

#include <windows.h>
#include <shellapi.h>     // DragAcceptFiles / DragQueryFile
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <algorithm>

#pragma comment(lib, "Shell32.lib")

#include "ChronoUI.hpp"        // for D2D types via the wrl ComPtr alias, plus CHRONO_API
#include "WidgetImpl.hpp"      // for ChronoControllerImpl factories (DWrite/D2D)

namespace ChronoUI {

	// -----------------------------------------------------------------------
	// Return value from any virtual-widget input callback.
	//
	// NotHandled  — keep dispatching to other widgets, or fall through to
	//               the OS / parent.
	// Handled     — this widget consumed the event. Stop dispatch.
	// Capture     — this widget consumed the event AND wants future mouse
	//               events until it releases (used by sliders, drag handles).
	// -----------------------------------------------------------------------
	enum class VInputResult { NotHandled, Handled, Capture };

	class IVirtualWidget;
	class VirtualWindow;

	// ========================================================================
	// IVirtualWidget — the public surface every virtual widget exposes.
	// ========================================================================
	class IVirtualWidget {
	public:
		virtual ~IVirtualWidget() = default;

		// Identity (for debug + dispatch). Keep lowercase, ASCII.
		virtual const char* GetTypeName() const = 0;

		// --- Geometry ---
		// Bounds are in the host's local coordinate space (top-left origin).
		virtual void          SetBounds(const D2D1_RECT_F& r) = 0;
		virtual D2D1_RECT_F   GetBounds() const = 0;

		// Default hit-test: point inside bounds. Override for non-rect shapes
		// (e.g. round buttons should reject corner clicks).
		virtual bool HitTest(float x, float y) const {
			D2D1_RECT_F r = GetBounds();
			return (x >= r.left && x < r.right && y >= r.top && y < r.bottom);
		}

		virtual bool IsVisible() const = 0;
		virtual void SetVisible(bool v) = 0;

		// --- Paint ---
		// Called by the host with its render target. The widget paints in the
		// HOST's coordinate system (so it must use its own GetBounds() to know
		// where to draw). The host has already set the appropriate clip rect.
		virtual void OnDraw(ID2D1RenderTarget* pRT) = 0;

		// --- Animation (optional) ---
		// Return true if the widget needs another frame. The host invalidates
		// itself if any virtual widget returns true.
		virtual bool OnUpdate(float deltaTime) { (void)deltaTime; return false; }

		// --- Input ---
		// Coordinates are in the HOST's coordinate system (same as bounds).
		// All methods are optional; default = NotHandled.
		virtual VInputResult OnMouseDown (float x, float y, int /*btn*/) { (void)x; (void)y; return VInputResult::NotHandled; }
		virtual VInputResult OnMouseUp   (float x, float y, int /*btn*/) { (void)x; (void)y; return VInputResult::NotHandled; }
		virtual VInputResult OnMouseMove (float x, float y)              { (void)x; (void)y; return VInputResult::NotHandled; }
		virtual VInputResult OnMouseEnter() { return VInputResult::NotHandled; }
		virtual VInputResult OnMouseLeave() { return VInputResult::NotHandled; }
		virtual VInputResult OnMouseWheel(float /*delta*/, float /*x*/, float /*y*/) { return VInputResult::NotHandled; }
		virtual VInputResult OnKeyDown   (UINT /*vk*/) { return VInputResult::NotHandled; }
		virtual VInputResult OnChar      (wchar_t /*ch*/) { return VInputResult::NotHandled; }

		// --- Focus ---
		virtual void OnFocus(bool /*gained*/) {}
		virtual bool CanFocus() const { return false; }

		// --- Properties / events (lightweight, opt-in) ---
		// Same string-keyed property store as IWidget, but inline here so the
		// virtual base doesn't depend on the heavyweight WidgetImpl tree.
		virtual void                SetProp (const char* key, const char* value) = 0;
		virtual const char*         GetProp (const char* key, const char* def = "") const = 0;
		virtual void                OnClick (std::function<void()> cb) = 0;
	};


	// ========================================================================
	// VirtualWidgetImpl — common implementation. Most widgets derive from this.
	// ========================================================================
	class VirtualWidgetImpl : public IVirtualWidget {
	protected:
		D2D1_RECT_F m_bounds  = D2D1::RectF(0, 0, 0, 0);
		bool        m_visible = true;
		bool        m_hovered = false;
		bool        m_focused = false;
		bool        m_pressed = false;
		std::unordered_map<std::string, std::string> m_props;
		std::function<void()> m_onClick;

	public:
		void          SetBounds(const D2D1_RECT_F& r) override { m_bounds = r; }
		D2D1_RECT_F   GetBounds() const override { return m_bounds; }
		bool          IsVisible() const override { return m_visible; }
		void          SetVisible(bool v) override { m_visible = v; }

		void          SetProp(const char* key, const char* value) override {
			if (!key) return;
			m_props[key] = value ? value : "";
		}
		const char*   GetProp(const char* key, const char* def = "") const override {
			if (!key) return def;
			auto it = m_props.find(key);
			return (it != m_props.end()) ? it->second.c_str() : def;
		}
		void          OnClick(std::function<void()> cb) override { m_onClick = std::move(cb); }

		// Tooltip text. Empty = no tooltip. The host (VirtualWindow) tracks
		// dwell time over the hovered widget and renders this text as an
		// overlay after ~700ms.
		void                SetTooltip(const std::wstring& s) { m_tooltip = s; }
		const std::wstring& GetTooltip() const                 { return m_tooltip; }

	protected:
		std::wstring m_tooltip;
	public:

		// Convenience accessors widget code can use during paint.
		float Width()  const { return m_bounds.right  - m_bounds.left; }
		float Height() const { return m_bounds.bottom - m_bounds.top;  }
	};


	// ========================================================================
	// VirtualWindow — owns a single HWND + D2D render target, hosts a flat
	// list of virtual widgets, and routes paint + input to them.
	//
	// This is a self-contained mini-framework. It does NOT integrate into the
	// existing IContainer / ILayout stack — that's a deliberate choice so we
	// can iterate on the virtual model in isolation. Once the model is solid,
	// we'll bridge it back into ContainerImpl.
	// ========================================================================
	class VirtualWindow {
	private:
		HWND   m_hwnd = nullptr;
		ComPtr<ID2D1HwndRenderTarget> m_pRT;

		// Scrollable content. Painted with the scroll transform; clipped to the
		// "safe area" (the rect inside the chrome insets). These widgets live in
		// CONTENT space — their bounds can exceed the viewport, and the user
		// scrolls through them.
		std::vector<std::unique_ptr<IVirtualWidget>> m_widgets;

		// Pinned chrome. Painted last, on top, in VIEWPORT space — no scroll
		// transform applied. Hit-tested first. Use for things like a title bar
		// or a bottom input strip that must always be visible.
		std::vector<std::unique_ptr<IVirtualWidget>> m_chrome;

		IVirtualWidget* m_capture = nullptr;  // widget with mouse capture
		IVirtualWidget* m_hovered = nullptr;  // last widget under the mouse
		IVirtualWidget* m_focused = nullptr;  // keyboard focus target
		bool            m_hoveredIsChrome = false;

		// Tooltip overlay state. m_hoverDwellS counts seconds since the mouse
		// entered m_hovered; once it crosses the threshold, the tooltip is
		// shown until the user moves to another widget or leaves the window.
		float        m_hoverDwellS    = 0.0f;
		bool         m_tooltipVisible = false;
		std::wstring m_tooltipText;
		float        m_lastMouseX     = 0.0f;
		float        m_lastMouseY     = 0.0f;
		static constexpr float kTooltipDelayS = 0.55f;

		// Safe-area insets — how many pixels of the viewport are occupied by
		// chrome (top header + bottom input strip + left/right sidebars).
		// The host clips scrollable rendering to the rectangle inside the insets,
		// so content can't visually bleed under chrome.
		float m_insetTop = 0.0f, m_insetBottom = 0.0f;
		float m_insetLeft = 0.0f, m_insetRight = 0.0f;

		// Custom-chrome state. When true the OS does NOT draw a title bar; the
		// host paints its own and routes drag/resize via WM_NCHITTEST.
		bool  m_customChrome = false;
		float m_titleBarH    = 36.0f;     // height of the synthesized title bar

		// Window background, painted before any widget. White by default so
		// every existing app keeps its look; dark-themed apps call
		// SetBackground() once instead of adding a full-window widget.
		D2D1_COLOR_F m_bg = D2D1::ColorF(0xFFFFFF, 1.0f);

		// Cuando true, WM_DESTROY NO llama PostQuitMessage. Útil para
		// ventanas modales que se destruyen sin que la app principal deba
		// cerrarse en consecuencia.
		bool  m_suppressQuit = false;

		// Scroll state. The window paints all widgets translated by (-scrollX, -scrollY)
		// so a vertical wheel naturally scrolls content. Bounds are still expressed in
		// content-space; the host does the translation at draw and input time.
		float m_scrollY = 0.0f;
		float m_contentHeight = 0.0f;  // optional; set via SetContentHeight() to clamp scroll

		// Heartbeat (16ms timer) so virtual widgets can animate. Each tick computes a
		// real deltaTime from QueryPerformanceCounter, calls OnUpdate on every widget,
		// and invalidates the window if any widget reported "I want to redraw."
		static const UINT_PTR kAnimTimerId = 2001;
		LARGE_INTEGER m_qpcFreq{};
		LARGE_INTEGER m_qpcLast{};
		bool m_animRunning = false;

	public:
		// Take ownership of a SCROLLABLE virtual widget. Lives in content space:
		// its bounds can exceed the viewport, and the user scrolls through it.
		template <typename T, typename... Args>
		T* Add(Args&&... args) {
			auto p = std::make_unique<T>(std::forward<Args>(args)...);
			T* raw = p.get();
			m_widgets.push_back(std::move(p));
			return raw;
		}

		// Take ownership of a CHROME widget. Stays anchored in viewport space —
		// scroll doesn't affect its position. Always painted on top of the
		// scrollable layer. Hit-tested first.
		template <typename T, typename... Args>
		T* AddChrome(Args&&... args) {
			auto p = std::make_unique<T>(std::forward<Args>(args)...);
			T* raw = p.get();
			m_chrome.push_back(std::move(p));
			return raw;
		}

		// Remove a scrollable widget by pointer. Frees the unique_ptr, so any
		// stored copies of the pointer become dangling — caller is responsible
		// for clearing them (e.g. a vector of message pointers should be wiped
		// in the same operation). Also clears capture / hover / focus if those
		// referenced the removed widget, so input stays consistent.
		// Programmatic keyboard focus. Mouse-down on a CanFocus widget
		// already sets focus inline; this overload lets the host hand
		// focus to a specific widget without faking a click — used
		// after selecting a conversation so the user can type a reply
		// without first clicking the input field.
		//
		// Calls OnFocus(false) on the previous widget and OnFocus(true)
		// on the new one. Passing nullptr just clears focus. The
		// invalidate is so any focus-style affordance (caret blink,
		// border colour) repaints immediately.
		void SetFocusWidget(IVirtualWidget* w) {
			if (m_focused == w) return;
			if (w && !w->CanFocus()) return;
			if (m_focused) m_focused->OnFocus(false);
			m_focused = w;
			if (m_focused) m_focused->OnFocus(true);
			if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
		}

		// Move keyboard focus to the next (+1) or previous (-1) visible widget
		// that CanFocus(), wrapping around. Content widgets come first in
		// creation order, then chrome. Used by Tab; apps may call it too.
		void CycleFocus(int dir) {
			std::vector<IVirtualWidget*> order;
			for (auto& w : m_widgets) if (w->IsVisible() && w->CanFocus()) order.push_back(w.get());
			for (auto& w : m_chrome)  if (w->IsVisible() && w->CanFocus()) order.push_back(w.get());
			if (order.empty()) return;
			int n = (int)order.size(), at = -1;
			for (int i = 0; i < n; ++i) if (order[(size_t)i] == m_focused) { at = i; break; }
			int next = (at < 0) ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
			SetFocusWidget(order[(size_t)next]);
		}

		// Returns true if a widget was removed.
		bool RemoveWidget(IVirtualWidget* w) {
			if (!w) return false;
			auto it = std::find_if(m_widgets.begin(), m_widgets.end(),
				[w](const std::unique_ptr<IVirtualWidget>& p) { return p.get() == w; });
			if (it == m_widgets.end()) return false;
			if (m_capture == w) { m_capture = nullptr; ReleaseCapture(); }
			if (m_hovered == w) m_hovered = nullptr;
			if (m_focused == w) m_focused = nullptr;
			m_widgets.erase(it);
			return true;
		}

		// Same as RemoveWidget but for chrome widgets (those added via
		// AddChrome, which live in m_chrome, NOT m_widgets). RemoveWidget
		// silently no-ops on chrome -- callers that AddChrome must remove via
		// THIS. (The ask_user balloon hit exactly that: its option buttons are
		// chrome, so RemoveWidget left them painted after the answer.)
		bool RemoveChrome(IVirtualWidget* w) {
			if (!w) return false;
			auto it = std::find_if(m_chrome.begin(), m_chrome.end(),
				[w](const std::unique_ptr<IVirtualWidget>& p) { return p.get() == w; });
			if (it == m_chrome.end()) return false;
			if (m_capture == w) { m_capture = nullptr; ReleaseCapture(); }
			if (m_hovered == w) m_hovered = nullptr;
			if (m_focused == w) m_focused = nullptr;
			m_chrome.erase(it);
			return true;
		}

		// Raise a chrome widget to the TOP of the chrome stack. Chrome paints in
		// insertion order (last = on top) and hit-tests in reverse, so a panel
		// created early (e.g. a modal review overlay) can end up painted UNDER
		// chrome added later. Call this on each widget of an overlay when it
		// opens — in back-to-front paint order (scrim first, buttons last) — so
		// it sits above everything regardless of creation order. No-op (false)
		// if the pointer isn't a chrome widget. Keeps capture/hover/focus intact.
		bool BringChromeToFront(IVirtualWidget* w) {
			if (!w) return false;
			auto it = std::find_if(m_chrome.begin(), m_chrome.end(),
				[w](const std::unique_ptr<IVirtualWidget>& p) { return p.get() == w; });
			if (it == m_chrome.end() || it + 1 == m_chrome.end()) return it != m_chrome.end();
			auto p = std::move(*it);
			m_chrome.erase(it);
			m_chrome.push_back(std::move(p));
			return true;
		}

		// Tell the host how much of the viewport is occupied by chrome. The
		// scrollable layer is clipped to the rectangle inside these insets, so
		// content can never bleed visually under header/footer/sidebars.
		void SetSafeAreaInsets(float top, float right, float bottom, float left) {
			m_insetTop = top; m_insetRight = right; m_insetBottom = bottom; m_insetLeft = left;
		}

		// Colour the window is cleared to before painting. Safe to call at any
		// time; repaints if the window already exists.
		void SetBackground(D2D1_COLOR_F c) {
			m_bg = c;
			if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
		}

		HWND GetHWND() const { return m_hwnd; }

		// Create + show the window. Title is UTF-16. Returns false on failure.
		// If customChrome=true, the OS title bar is suppressed and the host
		// paints its own (with widgets); drag and resize are handled internally
		// via WM_NCHITTEST. titleBarHeight only matters when customChrome=true.
		// `deferShow` keeps the window HIDDEN after CreateWindowExW so
		// the caller can wire up widgets / load state without the
		// user seeing a small white flash at the default position.
		// Pair with `ShowMaximized()` (or any other ShowWindow call)
		// once initialisation is complete. Default false preserves
		// the historical behaviour for every existing caller.
		bool Create(HINSTANCE hInstance, const wchar_t* title, int w, int h,
			bool customChrome = false, float titleBarHeight = 36.0f,
			bool deferShow = false)
		{
			static const wchar_t* kClass = L"ChronoUI.VirtualWindow";
			m_customChrome = customChrome;
			m_titleBarH    = titleBarHeight;

			WNDCLASSW wc = {};
			if (!GetClassInfoW(hInstance, kClass, &wc)) {
				wc = {};
				wc.lpfnWndProc = StaticWndProc;
				wc.hInstance   = hInstance;
				wc.hCursor     = LoadCursor(NULL, IDC_ARROW);
				wc.hbrBackground = NULL;       // we paint everything
				wc.lpszClassName = kClass;
				wc.style       = CS_HREDRAW | CS_VREDRAW;
				RegisterClassW(&wc);
			}

			// Style selection: classic with OS title bar, or borderless with
			// resize border (WS_THICKFRAME) when we're drawing chrome ourselves.
			DWORD style = customChrome
				? (WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN)
				: WS_OVERLAPPEDWINDOW;

			m_hwnd = CreateWindowExW(0, kClass, title,
				style,
				CW_USEDEFAULT, CW_USEDEFAULT, w, h,
				NULL, NULL, hInstance, this);
			if (!m_hwnd) return false;

			if (customChrome) {
				// Force the OS to recompute the non-client size now that our
				// WM_NCCALCSIZE handler is hooked up; this removes the title bar.
				SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
					SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
			}

			if (!deferShow) {
				ShowWindow(m_hwnd, SW_SHOW);
				UpdateWindow(m_hwnd);
			}

			// Start animation heartbeat. ~60 Hz; cheap if no widget wants frames.
			QueryPerformanceFrequency(&m_qpcFreq);
			QueryPerformanceCounter(&m_qpcLast);
			// 15, not 16: USER timers round the period up to the system tick
			// (15.6 ms), and 16 lands on the NEXT one - measured 25 ms per tick
			// (~40 fps) on Windows 11, while 15 fires every 16.3 ms (~60 fps).
			// timeBeginPeriod(1) does not change this; a period <= 15 does.
			SetTimer(m_hwnd, kAnimTimerId, 15, NULL);
			m_animRunning = true;
			return true;
		}

		// Total scrollable content height in pixels. The host clamps m_scrollY so
		// the user can never scroll past the end. Demos should call this after
		// laying out their widget list (or after adding new widgets).
		void SetContentHeight(float h) { m_contentHeight = h; ClampScroll(); }
		float GetContentHeight() const { return m_contentHeight; }
		float GetScrollY() const { return m_scrollY; }
		void  SetScrollY(float y) { m_scrollY = y; ClampScroll(); InvalidateRect(m_hwnd, NULL, FALSE); }
		void  ScrollToBottom() { SetScrollY(1e9f); }   // ClampScroll clamps to max
		void  ScrollToTop()    { SetScrollY(0.0f); }

		// Edge predicates for chrome-side UI (e.g. "scroll to bottom" FAB that
		// hides when already at the bottom). Tolerant half-pixel comparison so
		// floating-point round-trips through Clamp don't make these flicker.
		bool IsAtTop() const { return m_scrollY <= 0.5f; }
		bool IsAtBottom() const {
			if (!m_hwnd) return true;
			RECT rc; GetClientRect(m_hwnd, &rc);
			float viewH = (float)(rc.bottom - rc.top);
			float visibleBottom = viewH - m_insetBottom;
			float maxScroll = (m_contentHeight > visibleBottom)
				? (m_contentHeight - visibleBottom) : 0.0f;
			return m_scrollY >= maxScroll - 0.5f;
		}

		// Demo hooks. All optional.
		//   OnResize — called on WM_SIZE, before the next paint.
		//   OnScroll — called every time the scroll Y changes (wheel, ScrollToBottom).
		//   OnTick   — called once per heartbeat tick (~16ms) AFTER widget
		//              OnUpdates run. Use to drive app-level state that depends on
		//              widget state (e.g. "any bubble still streaming?" → toggle
		//              the typing indicator).
		// Use OnScroll to re-pin viewport-anchored chrome (header / input strip)
		// in content-space so it looks fixed as the body scrolls under it.
		void OnResize(std::function<void()> cb) { m_resizeCb = std::move(cb); }
		void OnScroll(std::function<void()> cb) { m_scrollCb = std::move(cb); }
		void OnTick  (std::function<void(float)> cb) { m_tickCb   = std::move(cb); }

		// Hook genérico para mensajes Win32 no manejados por el host (típicamente
		// los WM_APP+N que se postean entre threads). Si la callback devuelve
		// true, el resultado se considera consumido y se devuelve 0 al sistema;
		// si devuelve false, cae a DefWindowProc.
		void OnMessage(std::function<bool(UINT, WPARAM, LPARAM)> cb) { m_msgCb = std::move(cb); }
		// First crack at the mouse wheel (client coords). Return true to consume it
		// (e.g. an overlay that scrolls its own content regardless of which inner
		// widget is under the cursor). Bypasses the per-widget wheel dispatch.
		void OnWheelHook(std::function<bool(float, float, float)> cb) { m_wheelHook = std::move(cb); }
		// Window-level key pre-handler: sees every WM_KEYDOWN before the focused
		// widget; return true to swallow it (a suggestion list steering the arrows
		// while a text box keeps the caret).
		void OnKeyHook(std::function<bool(UINT)> cb) { m_keyHook = std::move(cb); }

		// Para ventanas modales: cuando se destruyen NO deben matar la app.
		// Llamar antes de Create() — luego es no-op (el flag se consulta en
		// WM_DESTROY).
		void SetSuppressQuitOnClose(bool v) { m_suppressQuit = v; }

		// Activa drag&drop de archivos desde Explorer. Tras llamar, el host
		// recibe WM_DROPFILES y dispara OnFilesDropped con la lista de rutas.
		// Idempotente (llamar dos veces no rompe nada).
		void EnableFileDrop() {
			if (m_hwnd) DragAcceptFiles(m_hwnd, TRUE);
			m_fileDropEnabled = true;
		}
		void OnFilesDropped(std::function<void(const std::vector<std::wstring>&)> cb) {
			m_filesDroppedCb = std::move(cb);
		}

	private:
		std::function<void()>      m_resizeCb;
		std::function<void()>      m_scrollCb;
		std::function<void(float)> m_tickCb;
		std::function<bool(UINT, WPARAM, LPARAM)> m_msgCb;
		std::function<bool(float, float, float)>  m_wheelHook;   // window-level wheel pre-handler (client coords)
		std::function<bool(UINT)>                 m_keyHook;     // window-level key pre-handler
		std::function<void(const std::vector<std::wstring>&)> m_filesDroppedCb;
		bool m_fileDropEnabled = false;
	public:

		// Standard message loop. Returns wParam from WM_QUIT.
		int RunMessageLoop() {
			MSG msg;
			while (GetMessageW(&msg, NULL, 0, 0) > 0) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
			return (int)msg.wParam;
		}

	private:
		// --- D2D resource lifecycle ---
		HRESULT EnsureRT() {
			if (m_pRT) return S_OK;
			RECT rc; GetClientRect(m_hwnd, &rc);
			D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
			HRESULT hr = ChronoControllerImpl::Instance().m_pD2DFactory->CreateHwndRenderTarget(
				D2D1::RenderTargetProperties(),
				D2D1::HwndRenderTargetProperties(m_hwnd, size),
				&m_pRT);
			if (SUCCEEDED(hr)) {
				float dpi = (float)GetDpiForWindow(m_hwnd);
				m_pRT->SetDpi(dpi, dpi);
			}
			return hr;
		}

		void DiscardRT() { m_pRT.Reset(); }

		// --- Scroll helpers ---
		// Max scroll = contentHeight - (visible bottom edge in content space).
		// The visible bottom edge is at viewportBottom - bottomInset (top of the
		// bottom chrome strip). Without subtracting bottomInset the user can
		// only scroll until contentHeight aligns with viewportBottom — which
		// means the last bubble ends up BEHIND the input strip. Adding the
		// inset lets that final bubble fully clear the chrome.
		void ClampScroll() {
			float before = m_scrollY;
			if (m_scrollY < 0.0f) m_scrollY = 0.0f;
			RECT rc; GetClientRect(m_hwnd, &rc);
			float viewH = (float)(rc.bottom - rc.top);
			float visibleBottom = viewH - m_insetBottom;
			float maxScroll = (m_contentHeight > visibleBottom)
				? (m_contentHeight - visibleBottom)
				: 0.0f;
			if (m_scrollY > maxScroll) m_scrollY = maxScroll;
			if (m_scrollCb && before != m_scrollY) m_scrollCb();
		}

		// --- Hit-test helper ---
		// Hit-tests both layers. Chrome widgets always win — they're "above"
		// the scrollable content. Returns (widget, isChrome). Returns
		// (nullptr, false) if nothing was hit.
		// IMPORTANT: chromeX/chromeY are viewport coords; contentX/contentY are
		// content coords (viewport + scrollY).
		struct HitResult { IVirtualWidget* w; bool isChrome; };
		HitResult HitTopmostBothLayers(float cx, float cy) {
			// Chrome first (viewport coords).
			for (auto it = m_chrome.rbegin(); it != m_chrome.rend(); ++it) {
				if ((*it)->IsVisible() && (*it)->HitTest(cx, cy)) return { it->get(), true };
			}
			// Then scrollable content (content coords).
			float wx = cx, wy = cy + m_scrollY;
			for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
				if ((*it)->IsVisible() && (*it)->HitTest(wx, wy)) return { it->get(), false };
			}
			return { nullptr, false };
		}

		// Legacy entry: content-only hit-test, retained for places that already
		// pre-translated to content space (the WM_NCHITTEST handler uses this).
		IVirtualWidget* HitTopmost(float x, float y) {
			for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
				if ((*it)->IsVisible() && (*it)->HitTest(x, y)) return it->get();
			}
			return nullptr;
		}

		// --- Paint ---
		// Two phases:
		//   1. Scrollable layer — clipped to the safe area (the rect inside the
		//      chrome insets), translated by -scrollY. Bubbles, lists, anything
		//      that should scroll. Bleeding outside the safe rect is clipped
		//      away, so content visually stops at the chrome boundary.
		//   2. Chrome layer — identity transform, no clip. Drawn on top so the
		//      header / input strip / sidebars sit visually above scrolled content.
		void DoPaint() {
			PAINTSTRUCT ps;
			BeginPaint(m_hwnd, &ps);
			HRESULT hr = EnsureRT();
			if (SUCCEEDED(hr) && m_pRT) {
				m_pRT->BeginDraw();

				// Window background.
				m_pRT->SetTransform(D2D1::Matrix3x2F::Identity());
				m_pRT->Clear(m_bg);

				// Phase 1: scrollable layer.
				RECT rc; GetClientRect(m_hwnd, &rc);
				D2D1_RECT_F safe = D2D1::RectF(
					(float)rc.left   + m_insetLeft,
					(float)rc.top    + m_insetTop,
					(float)rc.right  - m_insetRight,
					(float)rc.bottom - m_insetBottom);
				if (safe.right > safe.left && safe.bottom > safe.top) {
					m_pRT->PushAxisAlignedClip(safe, D2D1_ANTIALIAS_MODE_ALIASED);
					m_pRT->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -m_scrollY));
					for (auto& w : m_widgets) if (w->IsVisible()) w->OnDraw(m_pRT.Get());
					m_pRT->SetTransform(D2D1::Matrix3x2F::Identity());
					m_pRT->PopAxisAlignedClip();
				}

				// Phase 2: chrome layer (on top, viewport-fixed).
				for (auto& w : m_chrome) if (w->IsVisible()) w->OnDraw(m_pRT.Get());

				// Phase 2.5: borde sutil de 1px alrededor del client rect.
				// Solo en custom-chrome — con el title bar nativo, el sistema
				// ya dibuja un borde y duplicarlo se vería raro. Pintado tras
				// el chrome para que pase por encima de cualquier widget que
				// llegue hasta el borde (p.ej. el input strip).
				if (m_customChrome) {
					RECT edgeRc; GetClientRect(m_hwnd, &edgeRc);
					D2D1_RECT_F edge = D2D1::RectF(
						0.5f, 0.5f,
						(float)(edgeRc.right - edgeRc.left) - 0.5f,
						(float)(edgeRc.bottom - edgeRc.top) - 0.5f);
					ComPtr<ID2D1SolidColorBrush> pEdge;
					m_pRT->CreateSolidColorBrush(D2D1::ColorF(0x111827, 0.22f), &pEdge);
					if (pEdge) m_pRT->DrawRectangle(edge, pEdge.Get(), 1.0f);
				}

				// Phase 3: tooltip overlay. Drawn last so it floats above
				// everything (including the title bar / FABs). Anchored just
				// below the hovered widget's bottom edge, clipped horizontally
				// to the client rect so it never overflows off-screen.
				if (m_tooltipVisible && m_hovered && !m_tooltipText.empty()) {
					DrawTooltip(m_pRT.Get());
				}

				hr = m_pRT->EndDraw();
				if (hr == D2DERR_RECREATE_TARGET) DiscardRT();
			}
			EndPaint(m_hwnd, &ps);
		}

		// Dark rounded pill with light text, like Office / VS Code tooltips.
		// Placed below the hovered widget. If that would clip the client rect
		// bottom, flips above. Width auto-fits the text up to a soft cap.
		void DrawTooltip(ID2D1RenderTarget* pRT) {
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				12.0f, L"en-us", &fmt);
			if (!fmt) return;
			// LEADING + NEAR alignments: el texto se sitúa en (0, 0) dentro
			// del layout, así dibujarlo en (rectLeft+pad, rectTop+pad) lo
			// pone justo dentro del pill. Antes usábamos CENTER+CENTER y
			// DWrite empujaba el texto al medio de un layout de 64px de
			// alto — fuera del rect del tooltip, dejando sólo un cuadrado
			// "negro" visible.
			fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

			// Measure the text to size the pill.
			const float maxW = 320.0f;
			ComPtr<IDWriteTextLayout> layout;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				m_tooltipText.c_str(), (UINT32)m_tooltipText.size(),
				fmt.Get(), maxW, 4096.0f, &layout);
			if (!layout) return;
			DWRITE_TEXT_METRICS tm{};
			layout->GetMetrics(&tm);
			const float padX = 10.0f, padY = 6.0f;
			float w = tm.width + padX * 2.0f;
			float h = tm.height + padY * 2.0f;

			// Anchor: below the hovered widget, horizontally centered on the
			// widget. Translate content-space bounds into viewport space when
			// the widget isn't chrome.
			D2D1_RECT_F b = m_hovered->GetBounds();
			float yOff = m_hoveredIsChrome ? 0.0f : -m_scrollY;
			float anchorX = (b.left + b.right) * 0.5f;
			float anchorTop    = b.top    + yOff;
			float anchorBottom = b.bottom + yOff;

			RECT rc; GetClientRect(m_hwnd, &rc);
			float clientW = (float)(rc.right  - rc.left);
			float clientH = (float)(rc.bottom - rc.top);

			float x = anchorX - w * 0.5f;
			float y = anchorBottom + 6.0f;
			if (y + h > clientH) y = anchorTop - h - 6.0f;     // flip above
			if (x < 4.0f) x = 4.0f;
			if (x + w > clientW - 4.0f) x = clientW - 4.0f - w;

			D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 5.0f, 5.0f);

			ComPtr<ID2D1SolidColorBrush> pBg, pBorder, pText;
			pRT->CreateSolidColorBrush(D2D1::ColorF(0x1F2937, 0.96f), &pBg);
			pRT->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.20f), &pBorder);
			pRT->CreateSolidColorBrush(D2D1::ColorF(0xF5F7FA),         &pText);
			if (pBg)     pRT->FillRoundedRectangle(rr, pBg.Get());
			if (pBorder) pRT->DrawRoundedRectangle(rr, pBorder.Get(), 1.0f);
			if (pText)   pRT->DrawTextLayout(D2D1::Point2F(x + padX, y + padY),
			                                 layout.Get(), pText.Get());
		}

		// --- Heartbeat tick ---
		void TickAnimation() {
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			float dt = 0.016f;
			if (m_qpcFreq.QuadPart > 0) {
				double seconds = (double)(now.QuadPart - m_qpcLast.QuadPart) / (double)m_qpcFreq.QuadPart;
				if (seconds > 0.0 && seconds < 0.25) dt = (float)seconds;
			}
			m_qpcLast = now;
			bool needsRedraw = false;
			for (auto& w : m_widgets) if (w->IsVisible() && w->OnUpdate(dt)) needsRedraw = true;
			for (auto& w : m_chrome)  if (w->IsVisible() && w->OnUpdate(dt)) needsRedraw = true;
			// App-level tick AFTER widget updates so the host sees the latest
			// per-widget state when it decides to toggle visibility / layout.
			// The callback can call InvalidateRect itself; we also redraw if any
			// widget asked for it via OnUpdate.
			if (m_tickCb) m_tickCb(dt);

			// Tooltip dwell: count seconds the mouse has been resting on a
			// widget with a non-empty tooltip; when threshold hits, flip to
			// visible and invalidate. The state is reset by DispatchMouseMove
			// every time the hovered widget changes.
			if (m_hovered) {
				if (auto* impl = dynamic_cast<VirtualWidgetImpl*>(m_hovered)) {
					if (!impl->GetTooltip().empty()) {
						m_hoverDwellS += dt;
						if (!m_tooltipVisible && m_hoverDwellS >= kTooltipDelayS) {
							m_tooltipVisible = true;
							m_tooltipText    = impl->GetTooltip();
							needsRedraw = true;
						}
					}
				}
			}

			if (needsRedraw) InvalidateRect(m_hwnd, NULL, FALSE);
		}

		// --- Input dispatch ---
		// Mouse coordinates arrive in CLIENT-space (top-left origin of the HWND
		// client area). HitTopmostBothLayers handles the two-layer transform —
		// it tries chrome (in client space) first, then content (client +
		// scrollY). The widget always gets coords in ITS OWN space.
		void DispatchMouseMove(float cx, float cy) {
			IVirtualWidget* target;
			bool targetIsChrome = false;
			float lx, ly;
			if (m_capture) {
				target = m_capture;
				targetIsChrome = m_hoveredIsChrome;
				// Approximate: we don't track whether capture was chrome or
				// content. Use whichever the widget's bounds are positioned in
				// by checking if its bounds overlap viewport coords directly.
				// In practice this is good enough since drag-captures don't
				// cross the boundary.
				lx = cx; ly = cy;
				D2D1_RECT_F b = m_capture->GetBounds();
				if (cy < b.top || cy > b.bottom) ly = cy + m_scrollY;
			} else {
				HitResult hr = HitTopmostBothLayers(cx, cy);
				target = hr.w;
				targetIsChrome = hr.isChrome;
				lx = cx; ly = hr.isChrome ? cy : (cy + m_scrollY);
			}
			// Cache cursor position for tooltip placement.
			m_lastMouseX = cx;
			m_lastMouseY = cy;
			if (target != m_hovered) {
				if (m_hovered) m_hovered->OnMouseLeave();
				if (target)    target->OnMouseEnter();
				m_hovered = target;
				m_hoveredIsChrome = targetIsChrome;
				// Hover changed → start the tooltip dwell timer over.
				m_hoverDwellS    = 0.0f;
				m_tooltipVisible = false;
				m_tooltipText.clear();
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			if (target) {
				// A widget that returns Handled from OnMouseMove is signalling
				// "I changed my visual state on this move" — invalidate so the
				// host repaints. NotHandled means "nothing visual changed",
				// which lets cheap mouse movement skip the repaint.
				VInputResult r = target->OnMouseMove(lx, ly);
				if (r == VInputResult::Handled) {
					InvalidateRect(m_hwnd, NULL, FALSE);
				}
			}
		}

		void DispatchMouseDown(float cx, float cy, int btn) {
			HitResult hr = HitTopmostBothLayers(cx, cy);
			if (!hr.w) return;
			float lx = cx, ly = hr.isChrome ? cy : (cy + m_scrollY);
			VInputResult r = hr.w->OnMouseDown(lx, ly, btn);
			if (r == VInputResult::Capture) {
				m_capture = hr.w;
				SetCapture(m_hwnd);
			}
			if (hr.w->CanFocus() && m_focused != hr.w) {
				if (m_focused) m_focused->OnFocus(false);
				m_focused = hr.w;
				hr.w->OnFocus(true);
			}
			InvalidateRect(m_hwnd, NULL, FALSE);
		}

		void DispatchMouseUp(float cx, float cy, int btn) {
			IVirtualWidget* target = nullptr;
			float lx = cx, ly = cy;
			if (m_capture) {
				target = m_capture;
				D2D1_RECT_F b = m_capture->GetBounds();
				if (cy < b.top || cy > b.bottom) ly = cy + m_scrollY;
			} else {
				HitResult hr = HitTopmostBothLayers(cx, cy);
				target = hr.w;
				ly = hr.isChrome ? cy : (cy + m_scrollY);
			}
			if (target) target->OnMouseUp(lx, ly, btn);
			if (m_capture) {
				m_capture = nullptr;
				ReleaseCapture();
			}
			InvalidateRect(m_hwnd, NULL, FALSE);
		}

		void DispatchMouseWheel(short delta, float cx, float cy) {
			// Window-level pre-handler gets first crack (e.g. an open overlay that
			// scrolls its own content no matter which inner widget is hovered).
			if (m_wheelHook && m_wheelHook((float)delta, cx, cy)) { InvalidateRect(m_hwnd, NULL, FALSE); return; }
			// Chrome widget under the cursor consumes wheel events first — this
			// is important so wheeling over the input strip doesn't scroll the
			// conversation behind it.
			HitResult hr = HitTopmostBothLayers(cx, cy);
			if (hr.w) {
				float lx = cx, ly = hr.isChrome ? cy : (cy + m_scrollY);
				VInputResult r = hr.w->OnMouseWheel((float)delta, lx, ly);
				if (r != VInputResult::NotHandled) {
					InvalidateRect(m_hwnd, NULL, FALSE);
					return;
				}
				// If it's chrome and declined, swallow the wheel — don't scroll
				// content from over chrome.
				if (hr.isChrome) return;
			}
			// 120 = one notch on a typical wheel. Scroll three lines per notch.
			float step = -((float)delta / 120.0f) * 48.0f;
			m_scrollY += step;
			ClampScroll();
			InvalidateRect(m_hwnd, NULL, FALSE);
		}

		// --- WndProc plumbing ---
		static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
			VirtualWindow* self = nullptr;
			if (msg == WM_NCCREATE) {
				LPCREATESTRUCT cs = (LPCREATESTRUCT)lp;
				self = (VirtualWindow*)cs->lpCreateParams;
				SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
				self->m_hwnd = hwnd;
			} else {
				self = (VirtualWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			}
			if (self) return self->HandleMessage(msg, wp, lp);
			return DefWindowProc(hwnd, msg, wp, lp);
		}

		LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
			switch (msg) {
			// --- Custom chrome plumbing ---
			// WM_NCCALCSIZE with wp=TRUE asks "what is the client rect?". Returning 0
			// with the rect unchanged makes client == window, hiding the OS title bar.
			case WM_NCCALCSIZE:
				if (m_customChrome && wp == TRUE) return 0;
				break;

			// Custom hit-test: title row drags, edges resize, anything else
			// (including widget areas) is HTCLIENT so virtual-widget dispatch runs.
			case WM_NCHITTEST:
				if (m_customChrome) {
					POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
					RECT rcW; GetWindowRect(m_hwnd, &rcW);
					int edge = 6; // resize border thickness
					bool onL = pt.x < rcW.left + edge;
					bool onR = pt.x >= rcW.right - edge;
					bool onT = pt.y < rcW.top + edge;
					bool onB = pt.y >= rcW.bottom - edge;
					if (onT && onL) return HTTOPLEFT;
					if (onT && onR) return HTTOPRIGHT;
					if (onB && onL) return HTBOTTOMLEFT;
					if (onB && onR) return HTBOTTOMRIGHT;
					if (onL) return HTLEFT;
					if (onR) return HTRIGHT;
					if (onT) return HTTOP;
					if (onB) return HTBOTTOM;
					// Inside the client area: title row drags, everything else is
					// HTCLIENT so the widget-routing layer runs.
					POINT cp = pt;
					ScreenToClient(m_hwnd, &cp);
					if ((float)cp.y < m_titleBarH) {
						// Chrome widgets (combo, settings, close…) viven en
						// viewport coords y son hit-tested PRIMERO, igual que
						// con clicks normales. Si alguno está debajo del cursor,
						// devolvemos HTCLIENT para que el dispatch de eventos
						// del widget reciba el WM_LBUTTONDOWN.
						float cx = (float)cp.x;
						float cy = (float)cp.y;
						// Only INTERACTIVE chrome (buttons, combos — CanFocus) grabs
						// the title-bar hit. A decorative title VLabel used to span
						// the whole bar and swallow every drag, so the window could
						// not be moved. Non-focusable chrome falls through to
						// HTCAPTION and the title bar drags as expected.
						for (auto it = m_chrome.rbegin(); it != m_chrome.rend(); ++it) {
							if ((*it)->IsVisible() && (*it)->CanFocus()
								&& (*it)->HitTest(cx, cy)) {
								return HTCLIENT;
							}
						}
						// Si no había chrome, miramos contenido scrollable.
						float wx = cx;
						float wy = cy + m_scrollY;
						IVirtualWidget* hit = HitTopmost(wx, wy);
						if (hit && hit->CanFocus()) return HTCLIENT;
						return HTCAPTION;
					}
					return HTCLIENT;
				}
				break;

			case WM_PAINT:
				DoPaint();
				return 0;

			case WM_ERASEBKGND:
				return 1; // We paint everything

			case WM_SIZE:
				if (m_pRT) m_pRT->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
				if (m_resizeCb) m_resizeCb();
				ClampScroll();
				InvalidateRect(m_hwnd, NULL, FALSE);
				return 0;

			case WM_TIMER:
				if (wp == kAnimTimerId) { TickAnimation(); return 0; }
				break;

			case WM_MOUSEWHEEL: {
				// WM_MOUSEWHEEL position is in screen coords, unlike most mouse msgs.
				POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
				ScreenToClient(m_hwnd, &pt);
				DispatchMouseWheel(GET_WHEEL_DELTA_WPARAM(wp), (float)pt.x, (float)pt.y);
				return 0;
			}

			case WM_MOUSEMOVE: {
				TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
				TrackMouseEvent(&tme);
				DispatchMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
				return 0;
			}
			case WM_MOUSELEAVE:
				if (m_hovered) { m_hovered->OnMouseLeave(); m_hovered = nullptr; }
				m_hoverDwellS    = 0.0f;
				m_tooltipVisible = false;
				m_tooltipText.clear();
				InvalidateRect(m_hwnd, NULL, FALSE);
				return 0;

			case WM_LBUTTONDOWN:
				DispatchMouseDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 1);
				return 0;
			case WM_LBUTTONUP:
				DispatchMouseUp((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 1);
				return 0;
			// Right button is routed identically to left, but with btn=2. Widgets
			// that want a context menu override OnMouseUp(btn=2). Most fire on UP
			// so a right-drag doesn't immediately pop the menu before the user
			// has released — matches Explorer / browsers.
			case WM_RBUTTONDOWN:
				DispatchMouseDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 2);
				return 0;
			case WM_RBUTTONUP:
				DispatchMouseUp((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 2);
				return 0;

			case WM_KEYDOWN: {
				// Repaint right away when the widget consumed the key: waiting
				// for the next heartbeat (~16 ms) shows as lag while typing fast.
				if (m_keyHook && m_keyHook((UINT)wp)) { InvalidateRect(m_hwnd, NULL, FALSE); return 0; }
				VInputResult r = VInputResult::NotHandled;
				if (m_focused) r = m_focused->OnKeyDown((UINT)wp);
				if (r == VInputResult::Handled) InvalidateRect(m_hwnd, NULL, FALSE);
				// Tab / Shift+Tab move focus through the focusable widgets in
				// creation order (content first, then chrome) unless the
				// focused widget claimed the key itself.
				if (r == VInputResult::NotHandled && wp == VK_TAB)
					CycleFocus((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : +1);
				return 0;
			}
			case WM_CHAR:
				if ((wchar_t)wp == L'\t') return 0;   // Tab is navigation, never text
				if (m_focused) {
					VInputResult r = m_focused->OnChar((wchar_t)wp);
					if (r == VInputResult::Handled) InvalidateRect(m_hwnd, NULL, FALSE);
				}
				return 0;

			case WM_DESTROY:
				if (m_animRunning) { KillTimer(m_hwnd, kAnimTimerId); m_animRunning = false; }
				DiscardRT();
				if (!m_suppressQuit) PostQuitMessage(0);
				return 0;

			case WM_DROPFILES: {
				// El usuario soltó uno o más ficheros desde Explorer encima
				// de nuestra ventana. Extraemos las rutas y disparamos la
				// callback registrada vía OnFilesDropped().
				HDROP hDrop = (HDROP)wp;
				if (hDrop && m_filesDroppedCb) {
					UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
					std::vector<std::wstring> paths;
					paths.reserve(n);
					for (UINT i = 0; i < n; ++i) {
						UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
						if (len == 0) continue;
						std::wstring p((size_t)len, L'\0');
						DragQueryFileW(hDrop, i, p.data(), len + 1);
						paths.push_back(std::move(p));
					}
					DragFinish(hDrop);
					m_filesDroppedCb(paths);
				} else if (hDrop) {
					DragFinish(hDrop);
				}
				return 0;
			}
			}
			// Mensajes no-conocidos: si la app ha registrado OnMessage, dale
			// primero la oportunidad de manejarlos (ej. WM_APP+N posteados
			// desde threads worker).
			if (m_msgCb && m_msgCb(msg, wp, lp)) return 0;
			return DefWindowProc(m_hwnd, msg, wp, lp);
		}
	};


	// ========================================================================
	// VLabel — simplest virtual widget. Draws a single line of text. No input.
	// ========================================================================
	class VLabel : public VirtualWidgetImpl {
		std::wstring m_text;
		float m_fontSize = 14.0f;
		D2D1_COLOR_F m_color = D2D1::ColorF(0x222222);
	public:
		const char* GetTypeName() const override { return "VLabel"; }

		VLabel& Text(const std::wstring& t) { m_text = t; return *this; }
		VLabel& FontSize(float px)          { m_fontSize = px; return *this; }
		VLabel& Color(D2D1_COLOR_F c)       { m_color = c; return *this; }

		void OnDraw(ID2D1RenderTarget* pRT) override {
			if (m_text.empty()) return;
			ComPtr<IDWriteTextFormat> pTextFormat;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &pTextFormat);
			if (!pTextFormat) return;
			pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

			ComPtr<ID2D1SolidColorBrush> pBrush;
			pRT->CreateSolidColorBrush(m_color, &pBrush);
			if (pBrush) {
				pRT->DrawText(m_text.c_str(), (UINT32)m_text.size(),
					pTextFormat.Get(), m_bounds, pBrush.Get());
			}
		}
	};


	// ========================================================================
	// VButton — clickable rectangle with hover/press state, fires OnClick().
	//
	// Disabled state (Enabled(false)) paints with a muted face, suppresses
	// hover/press transitions, and swallows clicks. Useful for the "while
	// streaming, don't let the user fire another Send" pattern.
	// ========================================================================
	class VButton : public VirtualWidgetImpl {
		std::wstring m_text;
		D2D1_COLOR_F m_face       = D2D1::ColorF(0x4A90E2);
		D2D1_COLOR_F m_faceHover  = D2D1::ColorF(0x3B7AC8);
		D2D1_COLOR_F m_facePress  = D2D1::ColorF(0x2E63A6);
		D2D1_COLOR_F m_text_color = D2D1::ColorF(0xFFFFFF);
		float        m_radius     = 6.0f;
		bool         m_enabled    = true;
	public:
		const char* GetTypeName() const override { return "VButton"; }
		bool CanFocus() const override { return m_enabled; }

		VButton& Text(const std::wstring& t) { m_text = t; return *this; }
		VButton& Face(D2D1_COLOR_F c)        { m_face = c; return *this; }
		VButton& FaceHover(D2D1_COLOR_F c)   { m_faceHover = c; return *this; }
		VButton& FacePress(D2D1_COLOR_F c)   { m_facePress = c; return *this; }
		VButton& TextColor(D2D1_COLOR_F c)   { m_text_color = c; return *this; }
		VButton& CornerRadius(float r)       { m_radius = r; return *this; }
		VButton& Enabled(bool e) {
			m_enabled = e;
			// Drop transient interaction state so a button that gets disabled
			// mid-hover doesn't keep its hover/press visual until the next
			// mouse move.
			if (!e) { m_hovered = false; m_pressed = false; }
			return *this;
		}
		bool IsEnabled() const { return m_enabled; }

		void OnDraw(ID2D1RenderTarget* pRT) override {
			// Disabled: 35% blend between the face color and a neutral gray.
			// Pre-baked here rather than via opacity so the button still looks
			// solid on whatever background the host paints.
			auto blend = [](D2D1_COLOR_F a, D2D1_COLOR_F b, float t) -> D2D1_COLOR_F {
				return D2D1::ColorF(
					a.r + (b.r - a.r) * t,
					a.g + (b.g - a.g) * t,
					a.b + (b.b - a.b) * t,
					a.a + (b.a - a.a) * t);
			};

			D2D1_COLOR_F face = m_face;
			if (!m_enabled) {
				face = blend(m_face, D2D1::ColorF(0xC8CCD2), 0.55f);
			} else if (m_pressed) {
				face = m_facePress;
			} else if (m_hovered) {
				face = m_faceHover;
			}

			ComPtr<ID2D1SolidColorBrush> pFill;
			pRT->CreateSolidColorBrush(face, &pFill);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_bounds, m_radius, m_radius);
			if (pFill) pRT->FillRoundedRectangle(rr, pFill.Get());

			if (!m_text.empty()) {
				ComPtr<IDWriteTextFormat> pTextFormat;
				ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
					L"Segoe UI", NULL,
					DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					13.0f, L"en-us", &pTextFormat);
				if (pTextFormat) {
					pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
					D2D1_COLOR_F textCol = m_enabled
						? m_text_color
						: blend(m_text_color, D2D1::ColorF(0xC8CCD2), 0.45f);
					ComPtr<ID2D1SolidColorBrush> pTextBrush;
					pRT->CreateSolidColorBrush(textCol, &pTextBrush);
					if (pTextBrush) {
						pRT->DrawText(m_text.c_str(), (UINT32)m_text.size(),
							pTextFormat.Get(), m_bounds, pTextBrush.Get());
					}
				}
			}
		}

		VInputResult OnMouseEnter() override {
			if (!m_enabled) return VInputResult::NotHandled;
			m_hovered = true;
			return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hovered = false; m_pressed = false; return VInputResult::Handled; }
		VInputResult OnMouseDown(float, float, int btn) override {
			if (!m_enabled || btn != 1) return VInputResult::NotHandled;
			m_pressed = true;
			return VInputResult::Capture;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (!m_enabled || btn != 1) return VInputResult::NotHandled;
			bool wasPressedInside = m_pressed && HitTest(x, y);
			m_pressed = false;
			if (wasPressedInside && m_onClick) m_onClick();
			return VInputResult::Handled;
		}
	};


	// ========================================================================
	// VCombo — flat dropdown selector. Shows the current item + a chevron;
	// clicking opens a native popup menu with the list and updates selection.
	//
	// Designed to fit in chrome strips (header bars, toolbars). For very long
	// lists, TrackPopupMenu does the scrolling automatically — nothing to do
	// on our side.
	// ========================================================================
	class VCombo : public VirtualWidgetImpl {
		std::vector<std::wstring> m_items;
		size_t                    m_selected = 0;
		float                     m_fontSize = 13.0f;
		float                     m_radius   = 6.0f;
		std::function<void(size_t)> m_onChange;

		D2D1_COLOR_F m_face       = D2D1::ColorF(0xF6F7F9);
		D2D1_COLOR_F m_faceHover  = D2D1::ColorF(0xEAECEF);
		D2D1_COLOR_F m_border     = D2D1::ColorF(0xD8DBDF);
		D2D1_COLOR_F m_textColor  = D2D1::ColorF(0x111111);
		D2D1_COLOR_F m_chevColor  = D2D1::ColorF(0x6B7280);

	public:
		const char* GetTypeName() const override { return "VCombo"; }
		bool CanFocus() const override { return true; }

		VCombo& Items(std::vector<std::wstring> v) { m_items = std::move(v); if (m_selected >= m_items.size()) m_selected = 0; return *this; }
		VCombo& Select(size_t i)                  { if (i < m_items.size()) m_selected = i; return *this; }
		VCombo& Face(D2D1_COLOR_F c)              { m_face = c; return *this; }
		VCombo& FaceHover(D2D1_COLOR_F c)         { m_faceHover = c; return *this; }
		VCombo& Border(D2D1_COLOR_F c)            { m_border = c; return *this; }
		VCombo& TextColor(D2D1_COLOR_F c)         { m_textColor = c; return *this; }
		VCombo& FontSize(float px)                { m_fontSize = px; return *this; }
		VCombo& CornerRadius(float r)             { m_radius = r; return *this; }

		size_t SelectedIndex() const { return m_selected; }
		const std::wstring& SelectedText() const {
			static const std::wstring empty;
			return (m_selected < m_items.size()) ? m_items[m_selected] : empty;
		}
		void OnChange(std::function<void(size_t)> cb) { m_onChange = std::move(cb); }

		void OnDraw(ID2D1RenderTarget* pRT) override {
			D2D1_COLOR_F face = m_hovered ? m_faceHover : m_face;
			ComPtr<ID2D1SolidColorBrush> pFill, pBorder, pText, pChev;
			pRT->CreateSolidColorBrush(face,        &pFill);
			pRT->CreateSolidColorBrush(m_border,    &pBorder);
			pRT->CreateSolidColorBrush(m_textColor, &pText);
			pRT->CreateSolidColorBrush(m_chevColor, &pChev);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_bounds, m_radius, m_radius);
			if (pFill)   pRT->FillRoundedRectangle(rr, pFill.Get());
			if (pBorder) pRT->DrawRoundedRectangle(rr, pBorder.Get(), 1.0f);

			// Reserve a 22px gutter on the right for the chevron triangle.
			const float padX = 10.0f;
			const float gutter = 22.0f;
			D2D1_RECT_F inner = D2D1::RectF(
				m_bounds.left  + padX,
				m_bounds.top,
				m_bounds.right - gutter,
				m_bounds.bottom);

			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (fmt && pText && m_selected < m_items.size()) {
				fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
				fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				const std::wstring& sel = m_items[m_selected];
				pRT->DrawText(sel.c_str(), (UINT32)sel.size(),
					fmt.Get(), inner, pText.Get(),
					D2D1_DRAW_TEXT_OPTIONS_CLIP);
			}

			// Down-pointing chevron triangle in the gutter — drawn as a
			// 3-line polyline so its weight matches typical UI iconography.
			if (pChev) {
				float cx = m_bounds.right - gutter * 0.5f;
				float cy = (m_bounds.top + m_bounds.bottom) * 0.5f;
				float w = 4.5f, h = 3.5f;
				ID2D1Factory* factory = nullptr;
				pRT->GetFactory(&factory);
				ComPtr<ID2D1PathGeometry> path;
				if (factory && SUCCEEDED(factory->CreatePathGeometry(&path))) {
					ComPtr<ID2D1GeometrySink> sink;
					if (SUCCEEDED(path->Open(&sink))) {
						sink->BeginFigure(D2D1::Point2F(cx - w, cy - h * 0.5f), D2D1_FIGURE_BEGIN_HOLLOW);
						sink->AddLine(D2D1::Point2F(cx,      cy + h * 0.5f + 1.0f));
						sink->AddLine(D2D1::Point2F(cx + w,  cy - h * 0.5f));
						sink->EndFigure(D2D1_FIGURE_END_OPEN);
						sink->Close();
					}
					pRT->DrawGeometry(path.Get(), pChev.Get(), 1.6f);
				}
				if (factory) factory->Release();
			}
		}

		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }

		VInputResult OnMouseDown(float, float, int btn) override {
			if (btn != 1 || m_items.empty()) return VInputResult::NotHandled;
			// Anchor the popup at the combo's bottom-left edge so the menu
			// looks attached to the combo, like a typical dropdown.
			POINT anchor = { (LONG)m_bounds.left, (LONG)m_bounds.bottom };
			ClientToScreen(GetActiveWindow(), &anchor);
			HMENU menu = CreatePopupMenu();
			if (!menu) return VInputResult::Handled;
			for (size_t i = 0; i < m_items.size(); ++i) {
				UINT flags = MF_STRING | (i == m_selected ? MF_CHECKED : 0);
				AppendMenuW(menu, flags, (UINT)(i + 1), m_items[i].c_str());
			}
			int cmd = TrackPopupMenu(menu,
				TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
				anchor.x, anchor.y, 0, GetActiveWindow(), NULL);
			DestroyMenu(menu);
			if (cmd > 0) {
				size_t idx = (size_t)(cmd - 1);
				if (idx < m_items.size() && idx != m_selected) {
					m_selected = idx;
					if (m_onChange) m_onChange(m_selected);
				}
			}
			return VInputResult::Handled;
		}
	};

} // namespace ChronoUI
