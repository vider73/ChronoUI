// =============================================================================
// VActions.hpp — the widgets that make things happen, WinUI 3 vocabulary.
//
//   VDropDown    a drawn combo box: a button that opens a list flyout
//   VCommandBar  a row of icon buttons; what does not fit goes behind "..."
//   VBreadcrumb  Mail › Inbox › Subject, the parents clickable
//   VDialog      a modal content dialog: scrim, card, Primary / Secondary / Close
//   VLink        a hyperlink
//   VMenuBar     File / Edit / View menus in a bar
//   VSplitButton a button with a menu behind its chevron
//   VToggleButton an on/off button for toolbars
//   VSuggestions the list under a search box: arrows and Enter pick, the box keeps the caret
//
// VDropDown and VCommandBar are VFlyoutButton s (see VNavigation.hpp): add
// them with AddChrome, last, and give them the window size with Cover(w, h).
// VDialog is chrome too. See src/examples/Mail.cpp for all of them in use.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VNavigation.hpp"
#include "VDraw.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	class VDropDown : public VFlyoutButton {
		std::vector<std::wstring> m_items;
		std::wstring m_prefix;                       // "Sort: " before the selected item
		int m_sel = 0, m_hover = -1;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;
		static constexpr float kRow = 34.0f, kPad = 6.0f;

		D2D1_RECT_F RowRect(const D2D1_RECT_F& p, int i) const { return vd::Rect(p.left + kPad, p.top + kPad + kRow * (float)i, vd::W(p) - 2.0f * kPad, kRow); }
		int RowAt(float x, float y) const {
			for (int i = 0; i < (int)m_items.size(); ++i) if (vd::Contains(RowRect(m_panel, i), x, y)) return i;
			return -1;
		}
		void Pick(int i) { Close(); if (i != m_sel) { m_sel = i; if (m_cb) m_cb(i); } }
	protected:
		void OnOpened() override {
			m_hover = m_sel;
			float w = vd::W(m_bounds);
			for (const auto& it : m_items) w = (std::max)(w, vd::TextWidth(it, vd::Style().Size(13)) + 64.0f);
			m_pw = w; m_ph = 2.0f * kPad + kRow * (float)m_items.size(); Place();
		}
		VInputResult PanelMove(float x, float y) override {
			int h = RowAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override { int i = RowAt(x, y); if (i >= 0) Pick(i); return VInputResult::Handled; }
		VInputResult PanelKey(UINT vk) override {
			int n = (int)m_items.size();
			if (vk == VK_DOWN || vk == VK_UP) { m_hover = (m_hover + (vk == VK_DOWN ? 1 : -1) + n) % n; return VInputResult::Handled; }
			if ((vk == VK_RETURN || vk == VK_SPACE) && m_hover >= 0) { Pick(m_hover); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override {
			for (int i = 0; i < (int)m_items.size(); ++i) {
				D2D1_RECT_F r = RowRect(p, i);
				if (i == m_hover) vd::Fill(rt, r, vd::Col(0x000000, 0.05f), 6.0f);
				if (i == m_sel)   vd::Text(rt, L"\xE73E", vd::Rect(r.left, r.top, 36.0f, kRow), m_accent, vd::Style().Icon().Size(12).Center());
				vd::Text(rt, m_items[(size_t)i], D2D1::RectF(r.left + 38.0f, r.top, r.right - 12.0f, r.bottom), vctl::Ink(), vd::Style().Size(13));
			}
		}
		void DrawButton(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, m_hovered && !m_open ? vd::Col(0xF9FAFB) : vd::Col(0xFFFFFF), 6.0f);
			vd::Stroke(rt, m_bounds, (m_focused || m_open) ? m_accent : vd::Col(0xD1D5DB), 6.0f, 1.5f);
			std::wstring t = m_prefix + SelectedText();
			vd::Text(rt, t, D2D1::RectF(m_bounds.left + 12.0f, m_bounds.top, m_bounds.right - 30.0f, m_bounds.bottom), vctl::Ink(), vd::Style().Size(13));
			vd::Chevron(rt, m_bounds.right - 16.0f, vd::CY(m_bounds), m_open ? 180.0f : 0.0f, vctl::Muted());
		}
	public:
		const char* GetTypeName() const override { return "VDropDown"; }
		VDropDown& Items(std::vector<std::wstring> v) { m_items = std::move(v); if (m_sel >= (int)m_items.size()) m_sel = 0; return *this; }
		VDropDown& Select(int i)                     { if (i >= 0 && i < (int)m_items.size()) m_sel = i; return *this; }
		VDropDown& Prefix(std::wstring p)            { m_prefix = std::move(p); return *this; }
		VDropDown& Accent(D2D1_COLOR_F c)            { m_accent = c; return *this; }
		int SelectedIndex() const { return m_sel; }
		const std::wstring& SelectedText() const { static const std::wstring none; return m_sel < (int)m_items.size() ? m_items[(size_t)m_sel] : none; }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
	};

	// -------------------------------------------------------------------------
	class VCommandBar : public VFlyoutButton {
	public:
		using Item = VMenuItem;
	private:
		std::vector<VMenuItem> m_items;
		int   m_visible = 0;                // how many items fit in the bar; the rest overflow
		int   m_hover = -1, m_hoverOver = -1, m_focusIdx = 0;
		bool  m_labels = true, m_hoverMore = false;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;
		static constexpr float kMore = 40.0f;

		float ItemW(const Item& it) const {
			if (it.separator) return 13.0f;
			return (m_labels && !it.label.empty()) ? 40.0f + vd::TextWidth(it.label, vd::Style().Size(13)) + 12.0f : 40.0f;
		}
		void Measure() {
			float avail = vd::W(m_bounds) - kMore - 8.0f, x = 0.0f;
			m_visible = 0;
			for (int i = 0; i < (int)m_items.size(); ++i) { x += ItemW(m_items[(size_t)i]); if (x > avail) break; m_visible = i + 1; }
			if (m_visible == (int)m_items.size()) return;                                // everything fits, no "..."
			while (m_visible > 0 && m_items[(size_t)m_visible - 1].separator) --m_visible;   // no trailing rule
		}
		D2D1_RECT_F ItemRect(int i) const {
			float x = m_bounds.left + 4.0f;
			for (int k = 0; k < i; ++k) x += ItemW(m_items[(size_t)k]);
			return vd::Rect(x, vd::CY(m_bounds) - 18.0f, ItemW(m_items[(size_t)i]), 36.0f);
		}
		D2D1_RECT_F MoreRect() const { return vd::Rect(m_bounds.right - kMore - 4.0f, vd::CY(m_bounds) - 18.0f, kMore, 36.0f); }
		bool HasMore() const { return m_visible < (int)m_items.size(); }
		bool Pickable(int i) const { return vmenu::Pickable(m_items[(size_t)i]); }
		int BarAt(float x, float y) const {
			for (int i = 0; i < m_visible; ++i) if (Pickable(i) && vd::Contains(ItemRect(i), x, y)) return i;
			return -1;
		}
		void Pick(int i) { if (i < 0 || !Pickable(i)) return; Close(); if (m_cb) m_cb(i); }
	protected:
		D2D1_RECT_F OpenerRect() const override { return HasMore() ? MoreRect() : D2D1::RectF(0, 0, 0, 0); }
		void OnOpened() override { m_hoverOver = -1; vmenu::Measure(m_items, m_visible, m_pw, m_ph); Place(); }
		VInputResult ButtonUp(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			int i = BarAt(x, y); if (i >= 0) { m_focusIdx = i; Pick(i); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		VInputResult PanelMove(float x, float y) override {
			int h = vmenu::RowAt(m_panel, m_items, m_visible, x, y); if (h == m_hoverOver) return VInputResult::NotHandled;
			m_hoverOver = h; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override { Pick(vmenu::RowAt(m_panel, m_items, m_visible, x, y)); return VInputResult::Handled; }
		VInputResult PanelKey(UINT vk) override {
			if (vk == VK_DOWN || vk == VK_UP) { m_hoverOver = vmenu::Step(m_items, m_visible, m_hoverOver, vk == VK_DOWN ? 1 : -1); return VInputResult::Handled; }
			if ((vk == VK_RETURN || vk == VK_SPACE) && m_hoverOver >= 0) { Pick(m_hoverOver); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override { vmenu::Draw(rt, p, m_items, m_visible, m_hoverOver); }
		void DrawButton(ID2D1RenderTarget* rt) override {
			Measure();
			for (int i = 0; i < m_visible; ++i) {
				const Item& it = m_items[(size_t)i];
				D2D1_RECT_F r = ItemRect(i);
				if (it.separator) { vd::Line(rt, vd::CX(r), r.top + 8.0f, vd::CX(r), r.bottom - 8.0f, vd::Col(0xE5E7EB), 1.0f); continue; }
				if (i == m_hover && it.enabled) vd::Fill(rt, r, vd::Col(0x000000, 0.05f), 6.0f);
				if (m_focused && !m_open && i == m_focusIdx) vd::Stroke(rt, r, vd::Alpha(m_accent, 0.5f), 6.0f, 1.0f);
				D2D1_COLOR_F ink = it.enabled ? vctl::Ink() : vd::Col(0x9CA3AF);
				vd::Text(rt, it.glyph, vd::Rect(r.left, r.top, 40.0f, 36.0f), ink, vd::Style().Icon().Size(16).Center());
				if (m_labels && !it.label.empty())
					vd::Text(rt, it.label, D2D1::RectF(r.left + 38.0f, r.top, r.right - 6.0f, r.bottom), ink, vd::Style().Size(13));
			}
			if (HasMore()) {
				D2D1_RECT_F m = MoreRect();
				if (m_hoverMore || m_open) vd::Fill(rt, m, vd::Col(0x000000, 0.05f), 6.0f);
				vd::Text(rt, L"\xE712", m, vctl::Ink(), vd::Style().Icon().Size(16).Center());
			}
		}
	public:
		const char* GetTypeName() const override { return "VCommandBar"; }
		VCommandBar& Add(std::wstring glyph, std::wstring label, std::wstring shortcut = L"") {
			VMenuItem it; it.glyph = std::move(glyph); it.label = std::move(label); it.shortcut = std::move(shortcut);
			m_items.push_back(std::move(it)); return *this;
		}
		VCommandBar& Separator()          { VMenuItem it; it.separator = true; m_items.push_back(std::move(it)); return *this; }
		VCommandBar& Labels(bool on)      { m_labels = on; return *this; }
		VCommandBar& Enable(int i, bool e){ if (i >= 0 && i < (int)m_items.size()) m_items[(size_t)i].enabled = e; return *this; }
		VCommandBar& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }
		int Count() const { return (int)m_items.size(); }
		const VMenuItem& At(int i) const { return m_items[(size_t)i]; }
		void OnPick(std::function<void(int)> cb) { m_cb = std::move(cb); }

		void SetBounds(const D2D1_RECT_F& r) override { VirtualWidgetImpl::SetBounds(r); Measure(); }
		VInputResult OnMouseMove(float x, float y) override {
			if (m_open) return VFlyout::OnMouseMove(x, y);
			int h = BarAt(x, y); bool hm = HasMore() && vd::Contains(MoreRect(), x, y);
			if (h == m_hover && hm == m_hoverMore) return VInputResult::NotHandled;
			m_hover = h; m_hoverMore = hm; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; m_hoverMore = false; return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			if (m_open) return VFlyout::OnKeyDown(vk);
			if (vk == VK_LEFT || vk == VK_RIGHT) {
				int d = vk == VK_RIGHT ? 1 : -1, j = m_focusIdx;
				for (int k = 0; k < m_visible; ++k) { j = (j + d + m_visible) % (std::max)(1, m_visible); if (Pickable(j)) break; }
				m_focusIdx = j; return VInputResult::Handled;
			}
			if (vk == VK_RETURN || vk == VK_SPACE) { Pick(m_focusIdx); return VInputResult::Handled; }
			if (vk == VK_DOWN && HasMore()) { Open(MoreRect()); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
	};

	// -------------------------------------------------------------------------
	class VBreadcrumb : public VirtualWidgetImpl {
		std::vector<std::wstring> m_items;
		int m_hover = -1, m_focus = -1;
		float m_size = 14.0f;
		std::function<void(int)> m_cb;
		struct Seg { int idx; D2D1_RECT_F r; };     // idx -1 = the "..." for what did not fit
		mutable std::vector<Seg> m_segs;
		static constexpr float kSep = 24.0f;

		vd::Style StyleOf(int i) const { return i == (int)m_items.size() - 1 ? vd::Style().Size(m_size).Bold() : vd::Style().Size(m_size); }
		void Layout() const {
			m_segs.clear();
			int n = (int)m_items.size(); if (!n) return;
			std::vector<float> w((size_t)n);
			for (int i = 0; i < n; ++i) w[(size_t)i] = vd::TextWidth(m_items[(size_t)i], StyleOf(i)) + 8.0f;
			float avail = vd::W(m_bounds);
			// Keep the first item and as many trailing ones as fit; "..." stands for the rest.
			int first = 1;                                       // first index shown after the head
			float total = w[0]; for (int i = 1; i < n; ++i) total += kSep + w[(size_t)i];
			bool ell = false;
			while (total > avail && first < n - 1) { total -= kSep + w[(size_t)first]; if (!ell) { ell = true; total += kSep + 20.0f; } ++first; }
			float x = m_bounds.left;
			auto put = [&](int idx, float ww) { m_segs.push_back({ idx, vd::Rect(x, m_bounds.top, ww, vd::H(m_bounds)) }); x += ww + kSep; };
			if (n == 1) { put(0, w[0]); return; }
			put(0, w[0]);
			if (ell) put(-1, 20.0f);
			for (int i = first; i < n; ++i) put(i, w[(size_t)i]);
		}
		bool Clickable(int i) const { return i >= 0 && i < (int)m_items.size() - 1; }
		int SegAt(float x, float y) const {
			Layout();
			for (const Seg& s : m_segs) if (Clickable(s.idx) && vd::Contains(s.r, x, y)) return s.idx;
			return -1;
		}
	public:
		const char* GetTypeName() const override { return "VBreadcrumb"; }
		bool CanFocus() const override { return m_items.size() > 1; }
		VBreadcrumb& Items(std::vector<std::wstring> v) { m_items = std::move(v); m_hover = -1; m_focus = -1; return *this; }
		VBreadcrumb& Push(std::wstring s)               { m_items.push_back(std::move(s)); return *this; }
		VBreadcrumb& Truncate(int keep)                 { if (keep >= 0 && keep < (int)m_items.size()) m_items.resize((size_t)keep + 1); return *this; }
		VBreadcrumb& FontSize(float px)                 { m_size = px; return *this; }
		int Count() const { return (int)m_items.size(); }
		const std::wstring& At(int i) const { return m_items[(size_t)i]; }
		void OnPick(std::function<void(int)> cb) { m_cb = std::move(cb); }   // index of the parent clicked

		VInputResult OnMouseMove(float x, float y) override {
			int h = SegAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = SegAt(x, y);
			if (btn != 1 || i < 0) return VInputResult::NotHandled;
			if (m_cb) m_cb(i);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			int n = (int)m_items.size() - 1; if (n <= 0) return VInputResult::NotHandled;
			if (vk == VK_LEFT || vk == VK_RIGHT) { m_focus = (m_focus + (vk == VK_RIGHT ? 1 : -1) + n) % n; return VInputResult::Handled; }
			if ((vk == VK_RETURN || vk == VK_SPACE) && m_focus >= 0) { if (m_cb) m_cb(m_focus); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			Layout();
			for (size_t k = 0; k < m_segs.size(); ++k) {
				const Seg& s = m_segs[k];
				if (k) vd::Chevron(rt, s.r.left - kSep * 0.5f, vd::CY(s.r), -90.0f, vd::Col(0x9CA3AF), 4.0f);
				if (s.idx < 0) { vd::Text(rt, L"\x2026", s.r, vctl::Muted(), vd::Style().Size(m_size).Center()); continue; }
				bool last = s.idx == (int)m_items.size() - 1, lit = s.idx == m_hover || (m_focused && s.idx == m_focus);
				D2D1_COLOR_F c = last ? vctl::Ink() : (lit ? vctl::Ink() : vctl::Muted());
				vd::Text(rt, m_items[(size_t)s.idx], s.r, c, StyleOf(s.idx));
				if (lit && !last) vd::Line(rt, s.r.left + 2.0f, vd::CY(s.r) + m_size * 0.7f, s.r.right - 6.0f, vd::CY(s.r) + m_size * 0.7f, c, 1.0f);
			}
		}
	};

	// -------------------------------------------------------------------------
	// VDialog — modal. Open() shows it over a scrim; nothing outside reacts
	// until one of the buttons (or Enter / Esc) answers through OnResult:
	// 0 = primary, 1 = secondary, 2 = close.
	// -------------------------------------------------------------------------
	class VDialog : public VirtualWidgetImpl {
		std::wstring m_title, m_body, m_btn[3];
		VirtualWindow* m_win = nullptr;
		D2D1_RECT_F m_cover = {};
		bool  m_open = false;
		float m_anim = 0.0f, m_width = 440.0f;
		int   m_hover = -1, m_focus = 0;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;

		std::vector<int> Buttons() const { std::vector<int> v; for (int k = 0; k < 3; ++k) if (!m_btn[k].empty()) v.push_back(k); return v; }
		float BodyH() const { return vd::TextHeight(m_body, m_width - 48.0f, vd::Style().Size(13).Wrap()); }
		D2D1_RECT_F Panel() const {
			float h = 24.0f + 30.0f + 10.0f + BodyH() + 28.0f + 36.0f + 24.0f;
			float x = vd::CX(m_cover) - m_width * 0.5f, y = vd::CY(m_cover) - h * 0.5f;
			return vd::Rect(x, y, m_width, h);
		}
		D2D1_RECT_F ButtonRect(int slot, int n, const D2D1_RECT_F& p) const {
			float gap = 8.0f, w = (m_width - 48.0f - gap * (float)(n - 1)) / (float)n;
			return vd::Rect(p.left + 24.0f + (w + gap) * (float)slot, p.bottom - 24.0f - 36.0f, w, 36.0f);
		}
		int ButtonAt(float x, float y) const {
			std::vector<int> b = Buttons(); D2D1_RECT_F p = Panel();
			for (int s = 0; s < (int)b.size(); ++s) if (vd::Contains(ButtonRect(s, (int)b.size(), p), x, y)) return s;
			return -1;
		}
		void Answer(int slot) {
			std::vector<int> b = Buttons(); if (b.empty()) { m_open = false; return; }
			int result = b[(size_t)(std::max)(0, (std::min)(slot, (int)b.size() - 1))];
			m_open = false;
			if (m_cb) m_cb(result);
		}
	public:
		const char* GetTypeName() const override { return "VDialog"; }
		VDialog& Title(std::wstring t)     { m_title = std::move(t); return *this; }
		VDialog& Body(std::wstring t)      { m_body = std::move(t); return *this; }
		VDialog& Primary(std::wstring t)   { m_btn[0] = std::move(t); return *this; }
		VDialog& Secondary(std::wstring t) { m_btn[1] = std::move(t); return *this; }
		VDialog& CloseText(std::wstring t) { m_btn[2] = std::move(t); return *this; }
		VDialog& Width(float w)            { m_width = w; return *this; }
		VDialog& Accent(D2D1_COLOR_F c)    { m_accent = c; return *this; }
		void Attach(VirtualWindow* w)      { m_win = w; }
		void Cover(float w, float h)       { m_cover = vd::Rect(0.0f, 0.0f, w, h); }
		void Open() { m_open = true; m_hover = -1; m_focus = 0; if (m_win) m_win->SetFocusWidget(this); }
		bool IsOpen() const { return m_open; }
		void OnResult(std::function<void(int)> cb) { m_cb = std::move(cb); }

		bool CanFocus() const override { return m_open; }
		bool HitTest(float x, float y) const override { return m_open && vd::Contains(m_cover, x, y); }
		bool OnUpdate(float dt) override {
			float t = m_open ? 1.0f : 0.0f;
			if (fabsf(m_anim - t) < 0.01f) { if (m_anim == t) return false; m_anim = t; return true; }
			m_anim = vd::Approach(m_anim, t, dt, 20.0f);
			return true;
		}
		VInputResult OnMouseMove(float x, float y) override {
			if (!m_open) return VInputResult::NotHandled;
			int h = ButtonAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseDown(float, float, int) override { return m_open ? VInputResult::Handled : VInputResult::NotHandled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (!m_open) return VInputResult::NotHandled;
			int b = ButtonAt(x, y);
			if (btn == 1 && b >= 0) Answer(b);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (!m_open) return VInputResult::NotHandled;
			int n = (int)Buttons().size();
			if (vk == VK_ESCAPE) { std::vector<int> b = Buttons(); int slot = (int)b.size() - 1; Answer(slot); return VInputResult::Handled; }
			if (vk == VK_RETURN || vk == VK_SPACE) { Answer(m_focus); return VInputResult::Handled; }
			if ((vk == VK_LEFT || vk == VK_RIGHT || vk == VK_TAB) && n > 0) {
				int d = (vk == VK_LEFT || (vk == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000))) ? -1 : 1;
				m_focus = (m_focus + d + n) % n; return VInputResult::Handled;
			}
			return VInputResult::Handled;                    // modal: swallow the rest
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_anim < 0.02f) return;
			vd::Fill(rt, m_cover, vd::Col(0x000000, 0.35f * m_anim));
			D2D1_RECT_F p = Panel();
			float sc = 0.94f + 0.06f * m_anim;
			D2D1_MATRIX_3X2_F old; rt->GetTransform(&old);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(vd::CX(p), vd::CY(p))) * old);
			ComPtr<ID2D1Layer> layer; rt->CreateLayer(nullptr, &layer);
			if (layer) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), m_anim), layer.Get());
			vd::Shadow(rt, p, 10.0f, 0.25f, 6);
			vd::Fill(rt, p, vd::Col(0xFFFFFF), 10.0f);
			vd::Stroke(rt, p, vd::Col(0xE5E7EB), 10.0f, 1.0f);
			vd::Text(rt, m_title, vd::Rect(p.left + 24.0f, p.top + 24.0f, m_width - 48.0f, 30.0f), vctl::Ink(), vd::Style().Size(20).Bold());
			vd::Text(rt, m_body, vd::Rect(p.left + 24.0f, p.top + 64.0f, m_width - 48.0f, BodyH() + 4.0f), vctl::Ink(), vd::Style().Size(13).Wrap().Top());
			std::vector<int> b = Buttons();
			for (int s = 0; s < (int)b.size(); ++s) {
				D2D1_RECT_F r = ButtonRect(s, (int)b.size(), p);
				bool primary = b[(size_t)s] == 0, lit = s == m_hover;
				D2D1_COLOR_F face = primary ? (lit ? vd::Col(0x3B7AC8) : m_accent) : (lit ? vd::Col(0xE5E7EB) : vd::Col(0xF3F4F6));
				vd::Fill(rt, r, face, 6.0f);
				if (!primary) vd::Stroke(rt, r, vd::Col(0xD1D5DB), 6.0f, 1.0f);
				if (s == m_focus) vd::Stroke(rt, vd::Inset(r, -2.0f, -2.0f), vd::Alpha(m_accent, 0.6f), 8.0f, 1.5f);
				vd::Text(rt, m_btn[(size_t)b[(size_t)s]], r, primary ? vd::Col(0xFFFFFF) : vctl::Ink(), vd::Style().Size(13).Bold().Center());
			}
			if (layer) rt->PopLayer();
			rt->SetTransform(old);
		}
	};

	// -------------------------------------------------------------------------
	class VLink : public VirtualWidgetImpl {
		std::wstring m_text;
		D2D1_COLOR_F m_col = vctl::Blue();
		float m_size = 13.0f;
	public:
		explicit VLink(std::wstring text) : m_text(std::move(text)) {}
		const char* GetTypeName() const override { return "VLink"; }
		bool CanFocus() const override { return true; }
		VLink& Text(std::wstring t)     { m_text = std::move(t); return *this; }
		VLink& Color(D2D1_COLOR_F c)    { m_col = c; return *this; }
		VLink& FontSize(float px)       { m_size = px; return *this; }
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
			if (m_onClick) m_onClick(); return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk != VK_RETURN && vk != VK_SPACE) return VInputResult::NotHandled;
			if (m_onClick) m_onClick(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Text(rt, m_text, m_bounds, m_col, vd::Style().Size(m_size));
			if (m_hovered || m_focused) {
				float w = vd::TextWidth(m_text, vd::Style().Size(m_size)), y = vd::CY(m_bounds) + m_size * 0.7f;
				vd::Line(rt, m_bounds.left, y, m_bounds.left + w, y, m_col, 1.0f);
			}
		}
	};

	// -------------------------------------------------------------------------
	class VMenuBar : public VFlyout {
		struct Menu { std::wstring title; std::vector<VMenuItem> items; };
		std::vector<Menu> m_menus;
		int m_cur = -1, m_hoverTitle = -1, m_focusTitle = 0, m_hoverRow = -1;
		std::function<void(int, int)> m_cb;

		float TitleW(int i) const { return vd::TextWidth(m_menus[(size_t)i].title, vd::Style().Size(13)) + 24.0f; }
		D2D1_RECT_F TitleRect(int i) const {
			float x = m_bounds.left + 4.0f;
			for (int k = 0; k < i; ++k) x += TitleW(k);
			return vd::Rect(x, m_bounds.top + 2.0f, TitleW(i), vd::H(m_bounds) - 4.0f);
		}
		int TitleAt(float x, float y) const {
			for (int i = 0; i < (int)m_menus.size(); ++i) if (vd::Contains(TitleRect(i), x, y)) return i;
			return -1;
		}
		std::vector<VMenuItem>& Items() { return m_menus[(size_t)m_cur].items; }
		void OpenMenu(int i) { m_cur = i; m_focusTitle = i; m_hoverRow = -1; Open(TitleRect(i)); }
		void Pick(int i) {
			VMenuItem& it = Items()[(size_t)i];
			if (it.checkable) it.checked = !it.checked;
			int m = m_cur; Close();
			if (m_cb) m_cb(m, i);
		}
	protected:
		void OnOpened() override { vmenu::Measure(Items(), 0, m_pw, m_ph); Place(); }
		VInputResult PanelMove(float x, float y) override {
			int h = vmenu::RowAt(m_panel, Items(), 0, x, y); if (h == m_hoverRow) return VInputResult::NotHandled;
			m_hoverRow = h; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override { int i = vmenu::RowAt(m_panel, Items(), 0, x, y); if (i >= 0) Pick(i); return VInputResult::Handled; }
		VInputResult PanelKey(UINT vk) override {
			int n = (int)m_menus.size();
			if (vk == VK_LEFT || vk == VK_RIGHT) { OpenMenu((m_cur + (vk == VK_RIGHT ? 1 : -1) + n) % n); return VInputResult::Handled; }
			if (vk == VK_DOWN || vk == VK_UP) { m_hoverRow = vmenu::Step(Items(), 0, m_hoverRow, vk == VK_DOWN ? 1 : -1); return VInputResult::Handled; }
			if ((vk == VK_RETURN || vk == VK_SPACE) && m_hoverRow >= 0) { Pick(m_hoverRow); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override { vmenu::Draw(rt, p, Items(), 0, m_hoverRow); }
	public:
		const char* GetTypeName() const override { return "VMenuBar"; }
		VMenuBar& Menu(std::wstring title) { m_menus.push_back({ std::move(title), {} }); return *this; }
		VMenuBar& Add(std::wstring label, std::wstring glyph = L"", std::wstring shortcut = L"") {
			VMenuItem it; it.label = std::move(label); it.glyph = std::move(glyph); it.shortcut = std::move(shortcut);
			m_menus.back().items.push_back(std::move(it)); return *this;
		}
		VMenuBar& Check(std::wstring label, bool on) { VMenuItem it; it.label = std::move(label); it.checkable = true; it.checked = on; m_menus.back().items.push_back(std::move(it)); return *this; }
		VMenuBar& Separator()                        { VMenuItem it; it.separator = true; m_menus.back().items.push_back(std::move(it)); return *this; }
		VMenuItem& At(int menu, int item) { return m_menus[(size_t)menu].items[(size_t)item]; }
		void OnPick(std::function<void(int, int)> cb) { m_cb = std::move(cb); }   // (menu, item)

		bool CanFocus() const override { return true; }
		bool HitTest(float x, float y) const override { return vd::Contains(m_bounds, x, y) || VFlyout::HitTest(x, y); }
		VInputResult OnMouseMove(float x, float y) override {
			if (m_open) {
				int t = TitleAt(x, y);
				if (t >= 0 && t != m_cur) { OpenMenu(t); return VInputResult::Handled; }   // slide across the bar
				return VFlyout::OnMouseMove(x, y);
			}
			int t = TitleAt(x, y); if (t == m_hoverTitle) return VInputResult::NotHandled;
			m_hoverTitle = t; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hoverTitle = -1; return VInputResult::Handled; }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			int t = TitleAt(x, y);
			if (m_open) {
				if (t == m_cur) { Close(); return VInputResult::Handled; }
				if (t >= 0)     { OpenMenu(t); return VInputResult::Handled; }
				return VFlyout::OnMouseDown(x, y, btn);
			}
			if (btn == 1 && t >= 0) { OpenMenu(t); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override { return m_open ? VFlyout::OnMouseUp(x, y, btn) : VInputResult::NotHandled; }
		VInputResult OnKeyDown(UINT vk) override {
			if (m_open) return VFlyout::OnKeyDown(vk);
			int n = (int)m_menus.size(); if (!n) return VInputResult::NotHandled;
			if (vk == VK_LEFT || vk == VK_RIGHT) { m_focusTitle = (m_focusTitle + (vk == VK_RIGHT ? 1 : -1) + n) % n; return VInputResult::Handled; }
			if (vk == VK_RETURN || vk == VK_SPACE || vk == VK_DOWN) { OpenMenu(m_focusTitle); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			for (int i = 0; i < (int)m_menus.size(); ++i) {
				D2D1_RECT_F r = TitleRect(i);
				bool lit = (m_open && i == m_cur) || (!m_open && i == m_hoverTitle);
				if (lit) vd::Fill(rt, r, vd::Col(0x000000, 0.06f), 6.0f);
				if (m_focused && !m_open && i == m_focusTitle) vd::Stroke(rt, r, vd::Alpha(vctl::Blue(), 0.5f), 6.0f, 1.0f);
				vd::Text(rt, m_menus[(size_t)i].title, r, vctl::Ink(), vd::Style().Size(13).Center());
			}
			VFlyout::OnDraw(rt);
		}
	};

	// -------------------------------------------------------------------------
	class VSplitButton : public VFlyoutButton {
		std::wstring m_text;
		std::vector<VMenuItem> m_items;
		int  m_hover = -1;
		bool m_primary = true;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;
		D2D1_RECT_F MainRect() const { return D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right - 32.0f, m_bounds.bottom); }
		D2D1_RECT_F ChevRect() const { return D2D1::RectF(m_bounds.right - 32.0f, m_bounds.top, m_bounds.right, m_bounds.bottom); }
		void Pick(int i) { Close(); if (m_cb) m_cb(i); }
	protected:
		D2D1_RECT_F OpenerRect() const override { return ChevRect(); }
		VInputResult ButtonUp(float x, float y, int btn) override {
			if (btn != 1 || !vd::Contains(MainRect(), x, y)) return VInputResult::NotHandled;
			if (m_onClick) m_onClick(); return VInputResult::Handled;
		}
		void OnOpened() override { m_hover = -1; vmenu::Measure(m_items, 0, m_pw, m_ph); m_pw = (std::max)(m_pw, vd::W(m_bounds)); m_anchor = m_bounds; Place(); }
		VInputResult PanelMove(float x, float y) override {
			int h = vmenu::RowAt(m_panel, m_items, 0, x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override { int i = vmenu::RowAt(m_panel, m_items, 0, x, y); if (i >= 0) Pick(i); return VInputResult::Handled; }
		VInputResult PanelKey(UINT vk) override {
			if (vk == VK_DOWN || vk == VK_UP) { m_hover = vmenu::Step(m_items, 0, m_hover, vk == VK_DOWN ? 1 : -1); return VInputResult::Handled; }
			if ((vk == VK_RETURN || vk == VK_SPACE) && m_hover >= 0) { Pick(m_hover); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override { vmenu::Draw(rt, p, m_items, 0, m_hover); }
		void DrawButton(ID2D1RenderTarget* rt) override {
			D2D1_COLOR_F face = m_primary ? (m_hovered ? vd::Col(0x3B7AC8) : m_accent) : (m_hovered ? vd::Col(0xE5E7EB) : vd::Col(0xF3F4F6));
			D2D1_COLOR_F ink  = m_primary ? vd::Col(0xFFFFFF) : vctl::Ink();
			vd::Fill(rt, m_bounds, face, 6.0f);
			if (!m_primary) vd::Stroke(rt, m_bounds, vd::Col(0xD1D5DB), 6.0f, 1.0f);
			if (m_focused && !m_open) vd::Stroke(rt, vd::Inset(m_bounds, -2.0f, -2.0f), vd::Alpha(m_accent, 0.6f), 8.0f, 1.5f);
			vd::Text(rt, m_text, MainRect(), ink, vd::Style().Size(13).Bold().Center());
			D2D1_RECT_F c = ChevRect();
			vd::Line(rt, c.left, c.top + 8.0f, c.left, c.bottom - 8.0f, vd::Alpha(ink, 0.35f), 1.0f);
			vd::Chevron(rt, vd::CX(c), vd::CY(c), m_open ? 180.0f : 0.0f, ink);
		}
	public:
		explicit VSplitButton(std::wstring text) : m_text(std::move(text)) {}
		const char* GetTypeName() const override { return "VSplitButton"; }
		VSplitButton& Add(std::wstring label, std::wstring glyph = L"") { VMenuItem it; it.label = std::move(label); it.glyph = std::move(glyph); m_items.push_back(std::move(it)); return *this; }
		VSplitButton& Primary(bool p)          { m_primary = p; return *this; }
		VSplitButton& Accent(D2D1_COLOR_F c)   { m_accent = c; return *this; }
		const VMenuItem& At(int i) const       { return m_items[(size_t)i]; }
		void OnPick(std::function<void(int)> cb) { m_cb = std::move(cb); }   // a menu item; the main part fires OnClick
		VInputResult OnKeyDown(UINT vk) override {
			if (m_open) return VFlyout::OnKeyDown(vk);
			if (vk == VK_RETURN || vk == VK_SPACE) { if (m_onClick) m_onClick(); return VInputResult::Handled; }
			if (vk == VK_DOWN) { Open(ChevRect()); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
	};

	// -------------------------------------------------------------------------
	class VToggleButton : public VirtualWidgetImpl {
		std::wstring m_glyph, m_label;
		bool m_on = false;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(bool)> m_cb;
		void Flip() { m_on = !m_on; if (m_cb) m_cb(m_on); }
	public:
		VToggleButton(std::wstring glyph, std::wstring label = L"") : m_glyph(std::move(glyph)), m_label(std::move(label)) {}
		const char* GetTypeName() const override { return "VToggleButton"; }
		bool CanFocus() const override { return true; }
		VToggleButton& Set(bool on)            { m_on = on; return *this; }
		VToggleButton& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		bool Value() const { return m_on; }
		void OnChange(std::function<void(bool)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
			Flip(); return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk != VK_SPACE && vk != VK_RETURN) return VInputResult::NotHandled;
			Flip(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_on)            vd::Fill(rt, m_bounds, vd::Alpha(m_accent, 0.14f), 6.0f);
			else if (m_hovered)  vd::Fill(rt, m_bounds, vd::Col(0x000000, 0.05f), 6.0f);
			if (m_on)            vd::Stroke(rt, m_bounds, vd::Alpha(m_accent, 0.45f), 6.0f, 1.0f);
			if (m_focused)       vd::Stroke(rt, vd::Inset(m_bounds, -2.0f, -2.0f), vd::Alpha(m_accent, 0.5f), 8.0f, 1.0f);
			D2D1_COLOR_F ink = m_on ? m_accent : vctl::Ink();
			if (m_label.empty()) { vd::Text(rt, m_glyph, m_bounds, ink, vd::Style().Icon().Size(16).Center()); return; }
			vd::Text(rt, m_glyph, vd::Rect(m_bounds.left, m_bounds.top, 36.0f, vd::H(m_bounds)), ink, vd::Style().Icon().Size(15).Center());
			vd::Text(rt, m_label, D2D1::RectF(m_bounds.left + 34.0f, m_bounds.top, m_bounds.right - 10.0f, m_bounds.bottom), ink, vd::Style().Size(13));
		}
	};

	// -------------------------------------------------------------------------
	// VSuggestions — the list under a search box. It never takes the keyboard:
	// the text box keeps its caret and the app routes Up / Down / Enter / Esc
	// through HandleKey (from VirtualWindow::OnKeyHook). There is no scrim
	// either: clicks elsewhere reach their widgets; the app closes the list
	// when the text changes to nothing or the box loses focus.
	// -------------------------------------------------------------------------
	class VSuggestions : public VFlyout {
		std::vector<std::wstring> m_items;
		int m_hover = -1;
		std::function<void(int)> m_cb;
		static constexpr float kRow = 32.0f, kPad = 6.0f;
		D2D1_RECT_F RowRect(const D2D1_RECT_F& p, int i) const { return vd::Rect(p.left + kPad, p.top + kPad + kRow * (float)i, vd::W(p) - 2.0f * kPad, kRow); }
		int RowAt(float x, float y) const { for (int i = 0; i < (int)m_items.size(); ++i) if (vd::Contains(RowRect(m_panel, i), x, y)) return i; return -1; }
		void Pick(int i) { Close(); if (m_cb) m_cb(i); }
	protected:
		void OnOpened() override { m_hover = -1; m_pw = (std::max)(vd::W(m_anchor), 200.0f); m_ph = 2.0f * kPad + kRow * (float)m_items.size(); Place(); }
		VInputResult PanelMove(float x, float y) override {
			int h = RowAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override { int i = RowAt(x, y); if (i >= 0) Pick(i); return VInputResult::Handled; }
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override {
			for (int i = 0; i < (int)m_items.size(); ++i) {
				D2D1_RECT_F r = RowRect(p, i);
				if (i == m_hover) vd::Fill(rt, r, vd::Col(0x000000, 0.05f), 6.0f);
				vd::Text(rt, L"\xE721", vd::Rect(r.left, r.top, 32.0f, kRow), vctl::Muted(), vd::Style().Icon().Size(12).Center());
				vd::Text(rt, m_items[(size_t)i], D2D1::RectF(r.left + 34.0f, r.top, r.right - 12.0f, r.bottom), vctl::Ink(), vd::Style().Size(13));
			}
		}
	public:
		const char* GetTypeName() const override { return "VSuggestions"; }
		bool CanFocus() const override { return false; }
		bool HitTest(float x, float y) const override { return m_open && vd::Contains(m_panel, x, y); }
		void OnFocus(bool) override {}
		void Show(const D2D1_RECT_F& anchor, std::vector<std::wstring> items) {
			m_items = std::move(items);
			if (m_items.empty()) { Close(); return; }
			m_anchor = anchor; m_open = true; OnOpened();
		}
		bool HandleKey(UINT vk) {
			if (!m_open) return false;
			int n = (int)m_items.size();
			switch (vk) {
				case VK_DOWN:   m_hover = (m_hover + 1) % n; return true;
				case VK_UP:     m_hover = (m_hover - 1 + n) % n; return true;
				case VK_RETURN: if (m_hover >= 0) Pick(m_hover); else Close(); return true;
				case VK_ESCAPE: Close(); return true;
				default: return false;
			}
		}
		const std::wstring& At(int i) const { return m_items[(size_t)i]; }
		void OnPick(std::function<void(int)> cb) { m_cb = std::move(cb); }
	};

} // namespace ChronoUI
