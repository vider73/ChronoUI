// =============================================================================
// VNavigation.hpp — the app-shell widgets, in the WinUI 3 vocabulary.
//
//   VNavView     side navigation: glyph + label items, compact mode, a footer
//   VTabView     a strip of closable tabs with an add button
//   VExpander    a card whose content area opens and closes
//   VInfoBar     an inline message: info / success / warning / error
//   VFlyout      the light-dismiss popup the next ones build on
//   VFlyoutButton a button that opens its own flyout (VDatePicker, VDropDown)
//   VMenu        a menu flyout: glyph, label, shortcut, check items
//   VDatePicker  a date button that opens a calendar flyout
//   VTimePicker  a time button that opens an hour / minute grid
//   VTeachingTip a callout with a beak, a title, a body and an action
//
// Same shape as VControls.hpp: chainable setters, OnChange-style callbacks,
// animation off OnUpdate(dt) with vd::Approach. Widgets that change their own
// height (VExpander, VInfoBar) or width (VNavView) report it through
// OnLayout so the app can place everything again. Glyphs are Segoe MDL2
// Assets code points, e.g. L"\xE80F" (home); pass L"" for none.
//
// A flyout covers the whole window while open (Cover(w, h) tells it the size)
// so the click that lands outside its panel closes it. Add flyouts with
// AddChrome, last, so they paint on top of everything else.
// See src/examples/Settings.cpp for all of them in one window.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	class VNavView : public VirtualWidgetImpl {
	public:
		struct Item { std::wstring glyph, label; };
		static constexpr float kOpen = 240.0f, kCompact = 48.0f;
	private:
		std::vector<Item> m_items, m_footer;
		std::vector<int>  m_badges;                    // per row, 0 = none
		int   m_sel = 0, m_hover = -1;                 // -2 = the menu button
		bool  m_compact = false;
		float m_w = kOpen, m_indY = -1.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)>   m_cb;
		std::function<void(float)> m_layout;
		static constexpr float kRowH = 40.0f, kGap = 4.0f;

		int Count() const { return (int)(m_items.size() + m_footer.size()); }
		const Item& At(int i) const {
			return i < (int)m_items.size() ? m_items[(size_t)i] : m_footer[(size_t)(i - (int)m_items.size())];
		}
		D2D1_RECT_F Burger() const { return vd::Rect(m_bounds.left + 4.0f, m_bounds.top + 8.0f, 40.0f, 36.0f); }
		D2D1_RECT_F RowRect(int i) const {
			float w = m_w - 8.0f;
			if (i < (int)m_items.size())
				return vd::Rect(m_bounds.left + 4.0f, m_bounds.top + 56.0f + (kRowH + kGap) * (float)i, w, kRowH);
			int fromBottom = (int)m_footer.size() - 1 - (i - (int)m_items.size());
			return vd::Rect(m_bounds.left + 4.0f, m_bounds.bottom - 8.0f - kRowH - (kRowH + kGap) * (float)fromBottom, w, kRowH);
		}
		int RowAt(float x, float y) const {
			if (vd::Contains(Burger(), x, y)) return -2;
			for (int i = 0; i < Count(); ++i) if (vd::Contains(RowRect(i), x, y)) return i;
			return -1;
		}
		void Pick(int i) { if (i == m_sel) return; m_sel = i; if (m_cb) m_cb(i); }
	public:
		VNavView(std::vector<Item> items, std::vector<Item> footer = {})
			: m_items(std::move(items)), m_footer(std::move(footer)) {}
		const char* GetTypeName() const override { return "VNavView"; }
		bool CanFocus() const override { return true; }
		VNavView& Select(int i)          { m_sel = i; return *this; }
		VNavView& Compact(bool c)        { m_compact = c; m_w = c ? kCompact : kOpen; return *this; }
		VNavView& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }
		VNavView& Badge(int i, int n) {                // a count pill on row i (a dot when compact)
			if ((int)m_badges.size() < Count()) m_badges.resize((size_t)Count(), 0);
			if (i >= 0 && i < Count()) m_badges[(size_t)i] = n;
			return *this;
		}
		int   Value() const        { return m_sel; }
		bool  IsCompact() const    { return m_compact; }
		float CurrentWidth() const { return m_w; }
		void OnChange(std::function<void(int)> cb)   { m_cb = std::move(cb); }
		void OnLayout(std::function<void(float)> cb) { m_layout = std::move(cb); }

		bool OnUpdate(float dt) override {
			bool busy = false;
			float tw = m_compact ? kCompact : kOpen;
			if (fabsf(m_w - tw) > 0.3f)  { m_w = vd::Approach(m_w, tw, dt, 16.0f); busy = true; if (m_layout) m_layout(m_w); }
			else if (m_w != tw)          { m_w = tw; busy = true; if (m_layout) m_layout(m_w); }
			float ty = RowRect(m_sel).top;
			if (m_indY < 0.0f) m_indY = ty;
			if (fabsf(m_indY - ty) > 0.3f) { m_indY = vd::Approach(m_indY, ty, dt, 18.0f); busy = true; }
			else m_indY = ty;
			return busy;
		}
		VInputResult OnMouseMove(float x, float y) override {
			int h = RowAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			int i = RowAt(x, y);
			if (i == -2) m_compact = !m_compact;
			else if (i >= 0) Pick(i);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			int d = vk == VK_DOWN ? 1 : (vk == VK_UP ? -1 : 0);
			if (!d) return VInputResult::NotHandled;
			Pick((m_sel + d + Count()) % Count());
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F b = Burger();
			if (m_hover == -2) vd::Fill(rt, b, vd::Col(0x000000, 0.06f), 6.0f);
			for (int k = -1; k <= 1; ++k)
				vd::Line(rt, vd::CX(b) - 8.0f, vd::CY(b) + 5.0f * (float)k, vd::CX(b) + 8.0f, vd::CY(b) + 5.0f * (float)k, vctl::Ink(), 1.5f);
			float labelA = vd::Clamp01((m_w - kCompact - 40.0f) / (kOpen - kCompact - 40.0f));
			for (int i = 0; i < Count(); ++i) {
				D2D1_RECT_F r = RowRect(i);
				bool on = i == m_sel;
				if (on || i == m_hover) vd::Fill(rt, r, vd::Col(0x000000, on ? 0.07f : 0.045f), 6.0f);
				if (on && m_focused) vd::Stroke(rt, r, vd::Alpha(m_accent, 0.5f), 6.0f, 1.0f);
				const Item& it = At(i);
				vd::Text(rt, it.glyph, vd::Rect(r.left, r.top, 40.0f, kRowH), vctl::Ink(), vd::Style().Icon().Size(16).Center());
				if (labelA > 0.01f)
					vd::Text(rt, it.label, D2D1::RectF(r.left + 44.0f, r.top, r.right - 8.0f, r.bottom), vd::Alpha(vctl::Ink(), labelA), vd::Style().Size(13));
				int badge = i < (int)m_badges.size() ? m_badges[(size_t)i] : 0;
				if (badge > 0) {
					if (labelA > 0.5f) {
						std::wstring t = std::to_wstring(badge);
						float bw = (std::max)(20.0f, vd::TextWidth(t, vd::Style().Size(11).Bold()) + 12.0f);
						D2D1_RECT_F pill = vd::Rect(r.right - 10.0f - bw, vd::CY(r) - 10.0f, bw, 20.0f);
						vd::Fill(rt, pill, m_accent, 10.0f);
						vd::Text(rt, t, pill, vd::Col(0xFFFFFF), vd::Style().Size(11).Bold().Center());
					} else {
						vd::Circle(rt, r.left + 30.0f, r.top + 11.0f, 4.0f, m_accent);
					}
				}
			}
			vd::Fill(rt, vd::Rect(m_bounds.left + 4.0f, m_indY + 12.0f, 3.0f, kRowH - 24.0f), m_accent, 1.5f);
		}
	};

	// -------------------------------------------------------------------------
	class VTabView : public VirtualWidgetImpl {
		std::vector<std::wstring> m_tabs;
		int   m_sel = 0, m_hover = -1;
		bool  m_hoverClose = false, m_hoverAdd = false, m_closable = true, m_addable = true;
		float m_tabW = 170.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb, m_close;
		std::function<void()>    m_add;

		D2D1_RECT_F TabRect(int i) const   { return vd::Rect(m_bounds.left + m_tabW * (float)i, m_bounds.top, m_tabW, vd::H(m_bounds)); }
		D2D1_RECT_F CloseRect(int i) const { D2D1_RECT_F t = TabRect(i); return vd::Rect(t.right - 30.0f, vd::CY(t) - 10.0f, 20.0f, 20.0f); }
		D2D1_RECT_F AddRect() const        { return vd::Rect(m_bounds.left + m_tabW * (float)m_tabs.size() + 6.0f, vd::CY(m_bounds) - 14.0f, 28.0f, 28.0f); }
		int TabAt(float x, float y) const {
			for (int i = 0; i < (int)m_tabs.size(); ++i) if (vd::Contains(TabRect(i), x, y)) return i;
			return -1;
		}
		void Pick(int i) { if (i == m_sel) return; m_sel = i; if (m_cb) m_cb(i); }
		void CloseTab(int i) { if (m_close) m_close(i); else Remove(i); }
	public:
		explicit VTabView(std::vector<std::wstring> tabs) : m_tabs(std::move(tabs)) {}
		const char* GetTypeName() const override { return "VTabView"; }
		bool CanFocus() const override { return true; }
		VTabView& Select(int i)          { m_sel = i; return *this; }
		VTabView& TabWidth(float w)      { m_tabW = w; return *this; }
		VTabView& Closable(bool c)       { m_closable = c; return *this; }
		VTabView& Addable(bool a)        { m_addable = a; return *this; }
		VTabView& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }
		int  Value() const { return m_sel; }
		int  Count() const { return (int)m_tabs.size(); }
		const std::wstring& Title(int i) const { return m_tabs[(size_t)i]; }
		// Add appends and selects; Remove keeps the selection on a neighbour.
		// Neither fires OnChange: the caller knows what it just did.
		int  Add(const std::wstring& title) { m_tabs.push_back(title); m_sel = Count() - 1; return m_sel; }
		void Remove(int i) {
			if (i < 0 || i >= Count() || Count() <= 1) return;
			m_tabs.erase(m_tabs.begin() + i);
			if (m_sel >= Count()) m_sel = Count() - 1; else if (m_sel > i) --m_sel;
			m_hover = -1;
		}
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
		void OnClose (std::function<void(int)> cb) { m_close = std::move(cb); }   // when set, the app removes the tab
		void OnAdd   (std::function<void()> cb)    { m_add = std::move(cb); }

		VInputResult OnMouseMove(float x, float y) override {
			int h = TabAt(x, y);
			bool hc = h >= 0 && m_closable && vd::Contains(CloseRect(h), x, y);
			bool ha = m_addable && vd::Contains(AddRect(), x, y);
			if (h == m_hover && hc == m_hoverClose && ha == m_hoverAdd) return VInputResult::NotHandled;
			m_hover = h; m_hoverClose = hc; m_hoverAdd = ha;
			return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; m_hoverClose = m_hoverAdd = false; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (m_addable && vd::Contains(AddRect(), x, y)) { if (m_add) m_add(); return VInputResult::Handled; }
			int i = TabAt(x, y);
			if (i < 0) return VInputResult::NotHandled;
			if (m_closable && vd::Contains(CloseRect(i), x, y)) CloseTab(i); else Pick(i);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk == VK_DELETE && m_closable) { CloseTab(m_sel); return VInputResult::Handled; }
			int d = vk == VK_RIGHT ? 1 : (vk == VK_LEFT ? -1 : 0);
			if (!d) return VInputResult::NotHandled;
			Pick((m_sel + d + Count()) % Count());
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F selR = TabRect(m_sel);
			float yb = m_bounds.bottom - 0.5f;
			vd::Line(rt, m_bounds.left, yb, selR.left, yb, vd::Col(0xE5E7EB), 1.0f);
			vd::Line(rt, selR.right, yb, m_bounds.right, yb, vd::Col(0xE5E7EB), 1.0f);
			for (int i = 0; i < Count(); ++i) {
				D2D1_RECT_F r = TabRect(i);
				bool on = i == m_sel;
				if (on) {
					// Rounded top corners; the bottom edge runs 10 px below the
					// strip, where the content card painted after us covers it.
					D2D1_RECT_F card = D2D1::RectF(r.left, r.top + 4.0f, r.right, r.bottom + 10.0f);
					vd::Fill(rt, card, vd::Col(0xFFFFFF), 8.0f);
					vd::Stroke(rt, card, vd::Col(0xE5E7EB), 8.0f, 1.0f);
					if (m_focused) vd::Stroke(rt, vd::Inset(card, 3.0f, 3.0f), vd::Alpha(m_accent, 0.5f), 6.0f, 1.0f);
				} else if (i == m_hover) {
					vd::Fill(rt, vd::Inset(r, 3.0f, 6.0f), vd::Col(0x000000, 0.04f), 6.0f);
				}
				float textR = m_closable ? r.right - 34.0f : r.right - 12.0f;
				vd::Text(rt, m_tabs[(size_t)i], D2D1::RectF(r.left + 14.0f, r.top + 2.0f, textR, r.bottom),
					on ? vctl::Ink() : vctl::Muted(), on ? vd::Style().Size(13).Bold() : vd::Style().Size(13));
				if (m_closable) {
					D2D1_RECT_F c = CloseRect(i);
					if (i == m_hover && m_hoverClose) vd::Fill(rt, c, vd::Col(0x000000, 0.08f), 4.0f);
					vd::Text(rt, L"\xE711", c, on || i == m_hover ? vctl::Ink() : vctl::Muted(), vd::Style().Icon().Size(9).Center());
				}
			}
			if (m_addable) {
				D2D1_RECT_F a = AddRect();
				if (m_hoverAdd) vd::Fill(rt, a, vd::Col(0x000000, 0.06f), 6.0f);
				vd::Text(rt, L"\xE710", a, vctl::Ink(), vd::Style().Icon().Size(11).Center());
			}
		}
	};

	// -------------------------------------------------------------------------
	class VExpander : public VirtualWidgetImpl {
		std::wstring m_glyph, m_title, m_sub;
		bool  m_open = false;
		float m_anim = 0.0f, m_content = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(bool)> m_cb;
		std::function<void()>     m_layout;
		void Toggle() { m_open = !m_open; if (m_cb) m_cb(m_open); if (m_layout) m_layout(); }
	public:
		static constexpr float kHeader = 64.0f;
		VExpander(std::wstring glyph, std::wstring title, std::wstring subtitle = L"")
			: m_glyph(std::move(glyph)), m_title(std::move(title)), m_sub(std::move(subtitle)) {}
		const char* GetTypeName() const override { return "VExpander"; }
		bool CanFocus() const override { return true; }
		VExpander& Content(float h)       { m_content = h; return *this; }
		VExpander& Open(bool o)           { m_open = o; m_anim = o ? 1.0f : 0.0f; return *this; }
		VExpander& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }
		bool  IsOpen() const         { return m_open; }
		float ShownHeight() const    { return kHeader + m_content * m_anim; }
		bool  ContentVisible() const { return m_open && m_anim > 0.85f; }   // show children once the card is nearly open
		D2D1_RECT_F ContentRect() const { return vd::Rect(m_bounds.left + 16.0f, m_bounds.top + kHeader, vd::W(m_bounds) - 32.0f, m_content); }
		void OnChange(std::function<void(bool)> cb) { m_cb = std::move(cb); }
		void OnLayout(std::function<void()> cb)     { m_layout = std::move(cb); }   // height is animating

		bool OnUpdate(float dt) override {
			float t = m_open ? 1.0f : 0.0f;
			if (fabsf(m_anim - t) < 0.004f) {
				if (m_anim == t) return false;
				m_anim = t; if (m_layout) m_layout(); return true;
			}
			m_anim = vd::Approach(m_anim, t, dt, 18.0f);
			if (m_layout) m_layout();
			return true;
		}
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y) || y >= m_bounds.top + kHeader) return VInputResult::NotHandled;
			Toggle(); return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk != VK_SPACE && vk != VK_RETURN) return VInputResult::NotHandled;
			Toggle(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f);
			vd::Stroke(rt, m_bounds, m_focused ? vd::Alpha(m_accent, 0.6f) : vd::Col(0xE5E7EB), 8.0f, 1.0f);
			D2D1_RECT_F head = vd::Rect(m_bounds.left, m_bounds.top, vd::W(m_bounds), kHeader);
			if (m_hovered) vd::Fill(rt, vd::Inset(head, 1.0f, 1.0f), vd::Col(0x000000, 0.025f), 8.0f);
			float gx = m_bounds.left + 36.0f, gy = m_bounds.top + kHeader * 0.5f;
			vd::Circle(rt, gx, gy, 18.0f, vd::Alpha(m_accent, 0.12f));
			vd::Text(rt, m_glyph, vd::Rect(gx - 18.0f, gy - 18.0f, 36.0f, 36.0f), m_accent, vd::Style().Icon().Size(16).Center());
			float textR = m_bounds.right - 56.0f;
			if (m_sub.empty()) {
				vd::Text(rt, m_title, D2D1::RectF(m_bounds.left + 66.0f, head.top, textR, head.bottom), vctl::Ink(), vd::Style().Size(14).Bold());
			} else {
				vd::Text(rt, m_title, D2D1::RectF(m_bounds.left + 66.0f, head.top + 13.0f, textR, head.top + 35.0f), vctl::Ink(), vd::Style().Size(14).Bold());
				vd::Text(rt, m_sub,   D2D1::RectF(m_bounds.left + 66.0f, head.top + 34.0f, textR, head.top + 52.0f), vctl::Muted(), vd::Style().Size(12));
			}
			vd::Chevron(rt, m_bounds.right - 28.0f, gy, 180.0f * m_anim, vctl::Muted());
			if (m_anim > 0.02f)
				vd::Line(rt, m_bounds.left + 16.0f, head.bottom - 0.5f, m_bounds.right - 16.0f, head.bottom - 0.5f, vd::Col(0xE5E7EB, m_anim), 1.0f);
		}
	};

	// -------------------------------------------------------------------------
	enum class VSeverity { Info, Success, Warning, Error };

	class VInfoBar : public VirtualWidgetImpl {
		VSeverity    m_sev = VSeverity::Info;
		std::wstring m_title, m_msg, m_action;
		bool  m_open = true;
		float m_anim = 1.0f;
		int   m_hover = 0;                              // 1 = close, 2 = action
		std::function<void()> m_close, m_act, m_layout;
		static constexpr float kH = 56.0f;

		D2D1_COLOR_F Tint() const {
			switch (m_sev) { case VSeverity::Success: return vd::Col(0xECFDF3); case VSeverity::Warning: return vd::Col(0xFFF7E6);
			                 case VSeverity::Error:   return vd::Col(0xFEF2F2); default:                 return vd::Col(0xEFF6FF); }
		}
		D2D1_COLOR_F Ink() const {
			switch (m_sev) { case VSeverity::Success: return vd::Col(0x16A34A); case VSeverity::Warning: return vd::Col(0xD97706);
			                 case VSeverity::Error:   return vd::Col(0xDC2626); default:                 return vd::Col(0x2563EB); }
		}
		const wchar_t* Glyph() const {
			switch (m_sev) { case VSeverity::Success: return L"\xE73E"; case VSeverity::Warning: return L"\xE7BA";
			                 case VSeverity::Error:   return L"\xEA39"; default:                 return L"\xE946"; }
		}
		D2D1_RECT_F CloseRect() const  { return vd::Rect(m_bounds.right - 40.0f, m_bounds.top + kH * 0.5f - 12.0f, 24.0f, 24.0f); }
		D2D1_RECT_F ActionRect() const {
			float w = vd::TextWidth(m_action, vd::Style().Size(13).Bold()) + 24.0f;
			return vd::Rect(m_bounds.right - 48.0f - w, m_bounds.top + kH * 0.5f - 15.0f, w, 30.0f);
		}
	public:
		const char* GetTypeName() const override { return "VInfoBar"; }
		VInfoBar& Set(VSeverity s, std::wstring title, std::wstring message) {
			m_sev = s; m_title = std::move(title); m_msg = std::move(message); return *this;
		}
		VInfoBar& Action(std::wstring text) { m_action = std::move(text); return *this; }
		void Open(bool o) { if (m_open == o) return; m_open = o; if (m_layout) m_layout(); }
		bool  IsOpen() const      { return m_open; }
		float ShownHeight() const { return kH * m_anim; }
		void OnClose (std::function<void()> cb) { m_close = std::move(cb); }
		void OnAction(std::function<void()> cb) { m_act = std::move(cb); }
		void OnLayout(std::function<void()> cb) { m_layout = std::move(cb); }

		bool OnUpdate(float dt) override {
			float t = m_open ? 1.0f : 0.0f;
			if (fabsf(m_anim - t) < 0.004f) {
				if (m_anim == t) return false;
				m_anim = t; if (m_layout) m_layout(); return true;
			}
			m_anim = vd::Approach(m_anim, t, dt, 16.0f);
			if (m_layout) m_layout();
			return true;
		}
		VInputResult OnMouseMove(float x, float y) override {
			int h = vd::Contains(CloseRect(), x, y) ? 1 : (!m_action.empty() && vd::Contains(ActionRect(), x, y) ? 2 : 0);
			if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = 0; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !m_open) return VInputResult::NotHandled;
			if (vd::Contains(CloseRect(), x, y)) { Open(false); if (m_close) m_close(); return VInputResult::Handled; }
			if (!m_action.empty() && vd::Contains(ActionRect(), x, y)) { if (m_act) m_act(); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_anim < 0.02f) return;
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			D2D1_RECT_F full = vd::Rect(m_bounds.left, m_bounds.top, vd::W(m_bounds), kH);
			vd::Fill(rt, full, Tint(), 8.0f);
			vd::Stroke(rt, full, vd::Alpha(Ink(), 0.25f), 8.0f, 1.0f);
			float cy = m_bounds.top + kH * 0.5f;
			vd::Circle(rt, m_bounds.left + 28.0f, cy, 11.0f, Ink());
			vd::Text(rt, Glyph(), vd::Rect(m_bounds.left + 17.0f, cy - 11.0f, 22.0f, 22.0f), vd::Col(0xFFFFFF), vd::Style().Icon().Size(11).Center());
			float tw = vd::TextWidth(m_title, vd::Style().Size(13).Bold());
			float textR = m_action.empty() ? m_bounds.right - 48.0f : ActionRect().left - 12.0f;
			vd::Text(rt, m_title, D2D1::RectF(m_bounds.left + 50.0f, full.top, m_bounds.left + 50.0f + tw + 4.0f, full.bottom), vctl::Ink(), vd::Style().Size(13).Bold());
			vd::Text(rt, m_msg, D2D1::RectF(m_bounds.left + 58.0f + tw, full.top, textR, full.bottom), vctl::Muted(), vd::Style().Size(13));
			if (!m_action.empty()) {
				D2D1_RECT_F a = ActionRect();
				if (m_hover == 2) vd::Fill(rt, a, vd::Alpha(Ink(), 0.10f), 6.0f);
				vd::Text(rt, m_action, a, Ink(), vd::Style().Size(13).Bold().Center());
			}
			D2D1_RECT_F c = CloseRect();
			if (m_hover == 1) vd::Fill(rt, c, vd::Col(0x000000, 0.07f), 4.0f);
			vd::Text(rt, L"\xE711", c, vctl::Muted(), vd::Style().Icon().Size(10).Center());
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	// Menu rows, shared by VMenu, VMenuBar, VSplitButton and the overflow of
	// VCommandBar: one item type, one way to measure, hit and draw them.
	// -------------------------------------------------------------------------
	struct VMenuItem { std::wstring label, glyph, shortcut; bool separator = false, checkable = false, checked = false, enabled = true; };

	namespace vmenu {
		constexpr float kRow = 34.0f, kSep = 9.0f, kPad = 6.0f;
		inline float RowH(const VMenuItem& it)    { return it.separator ? kSep : kRow; }
		inline bool  Pickable(const VMenuItem& it) { return !it.separator && it.enabled; }
		// Rows are items[first..], stacked from the panel top.
		inline D2D1_RECT_F RowRect(const D2D1_RECT_F& p, const std::vector<VMenuItem>& items, int first, int i) {
			float y = p.top + kPad;
			for (int k = first; k < i; ++k) y += RowH(items[(size_t)k]);
			return vd::Rect(p.left + kPad, y, vd::W(p) - 2.0f * kPad, RowH(items[(size_t)i]));
		}
		inline int RowAt(const D2D1_RECT_F& p, const std::vector<VMenuItem>& items, int first, float x, float y) {
			for (int i = first; i < (int)items.size(); ++i)
				if (Pickable(items[(size_t)i]) && vd::Contains(RowRect(p, items, first, i), x, y)) return i;
			return -1;
		}
		inline void Measure(const std::vector<VMenuItem>& items, int first, float& w, float& h) {
			w = 200.0f; h = 2.0f * kPad;
			for (int i = first; i < (int)items.size(); ++i) {
				const VMenuItem& it = items[(size_t)i];
				h += RowH(it);
				if (!it.separator)
					w = (std::max)(w, 70.0f + vd::TextWidth(it.label, vd::Style().Size(13)) + (it.shortcut.empty() ? 0.0f : vd::TextWidth(it.shortcut, vd::Style().Size(12)) + 28.0f));
			}
		}
		// The next pickable row from `from` in direction d (+1 / -1), wrapping.
		inline int Step(const std::vector<VMenuItem>& items, int first, int from, int d) {
			int n = (int)items.size(); if (n <= first) return -1;
			int j = from;
			for (int k = 0; k < n - first; ++k) {
				if (j < first) j = d > 0 ? first : n - 1;
				else { j += d; if (j < first) j = n - 1; if (j >= n) j = first; }
				if (Pickable(items[(size_t)j])) return j;
			}
			return -1;
		}
		inline void Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& p, const std::vector<VMenuItem>& items, int first, int hover) {
			for (int i = first; i < (int)items.size(); ++i) {
				const VMenuItem& it = items[(size_t)i];
				D2D1_RECT_F r = RowRect(p, items, first, i);
				if (it.separator) { vd::Line(rt, r.left + 8.0f, vd::CY(r), r.right - 8.0f, vd::CY(r), vd::Col(0xE5E7EB), 1.0f); continue; }
				if (i == hover) vd::Fill(rt, r, vd::Col(0x000000, 0.05f), 6.0f);
				D2D1_COLOR_F ink = it.enabled ? vctl::Ink() : vd::Col(0x9CA3AF);
				const std::wstring g = it.checkable ? (it.checked ? std::wstring(L"\xE73E") : std::wstring()) : it.glyph;
				vd::Text(rt, g, vd::Rect(r.left, r.top, 36.0f, kRow), ink, vd::Style().Icon().Size(13).Center());
				vd::Text(rt, it.label, D2D1::RectF(r.left + 38.0f, r.top, r.right - 12.0f, r.bottom), ink, vd::Style().Size(13));
				if (!it.shortcut.empty())
					vd::Text(rt, it.shortcut, D2D1::RectF(r.left, r.top, r.right - 12.0f, r.bottom), vctl::Muted(), vd::Style().Size(12).Right());
			}
		}
	}

	// -------------------------------------------------------------------------
	// VFlyout — a panel that opens next to an anchor and closes on a click
	// outside it, Esc, or losing focus. Derive and draw the panel; the base
	// owns placement, the scrim, the fade and the dismiss rules.
	// -------------------------------------------------------------------------
	class VFlyout : public VirtualWidgetImpl {
	protected:
		VirtualWindow* m_win = nullptr;
		D2D1_RECT_F m_cover = {}, m_anchor = {}, m_panel = {};
		float m_pw = 220.0f, m_ph = 120.0f, m_anim = 0.0f;
		bool  m_open = false;

		virtual void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& panel) = 0;
		virtual VInputResult PanelDown(float, float, int) { return VInputResult::Handled; }
		virtual VInputResult PanelUp  (float, float, int) { return VInputResult::Handled; }
		virtual VInputResult PanelMove(float, float)      { return VInputResult::NotHandled; }
		virtual VInputResult PanelKey (UINT)              { return VInputResult::NotHandled; }
		virtual void OnOpened() {}                        // set m_pw / m_ph, then Place()

		void Place() {
			float x = m_anchor.left, y = m_anchor.bottom + 4.0f;
			if (y + m_ph > m_cover.bottom && m_anchor.top - 4.0f - m_ph >= m_cover.top) y = m_anchor.top - 4.0f - m_ph;
			x = (std::max)(m_cover.left + 8.0f, (std::min)(x, m_cover.right - m_pw - 8.0f));
			y = (std::max)(m_cover.top + 8.0f,  (std::min)(y, m_cover.bottom - m_ph - 8.0f));
			m_panel = vd::Rect(x, y, m_pw, m_ph);
		}
	public:
		void Attach(VirtualWindow* w)  { m_win = w; }
		void Cover(float w, float h)   { m_cover = vd::Rect(0.0f, 0.0f, w, h); if (m_open) Place(); }
		void Open(const D2D1_RECT_F& anchor) {
			m_anchor = anchor; Place(); m_open = true; OnOpened();
			if (m_win) m_win->SetFocusWidget(this);
		}
		void Close()         { m_open = false; }
		bool IsOpen() const  { return m_open; }
		const D2D1_RECT_F& Panel() const { return m_panel; }

		bool CanFocus() const override { return m_open; }
		bool HitTest(float x, float y) const override { return m_open && vd::Contains(m_cover, x, y); }
		void OnFocus(bool gained) override { if (!gained) m_open = false; }
		bool OnUpdate(float dt) override {
			float t = m_open ? 1.0f : 0.0f;
			if (fabsf(m_anim - t) < 0.01f) { if (m_anim == t) return false; m_anim = t; return true; }
			m_anim = vd::Approach(m_anim, t, dt, 22.0f);
			return true;
		}
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (!m_open) return VInputResult::NotHandled;
			if (!vd::Contains(m_panel, x, y)) { Close(); return VInputResult::Handled; }
			return PanelDown(x, y, btn);
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (!m_open) return VInputResult::NotHandled;
			return vd::Contains(m_panel, x, y) ? PanelUp(x, y, btn) : VInputResult::Handled;
		}
		VInputResult OnMouseMove(float x, float y) override { return m_open ? PanelMove(x, y) : VInputResult::NotHandled; }
		VInputResult OnKeyDown(UINT vk) override {
			if (!m_open) return VInputResult::NotHandled;
			if (vk == VK_ESCAPE) { Close(); return VInputResult::Handled; }
			return PanelKey(vk);
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_anim < 0.02f) return;
			D2D1_RECT_F p = m_panel;
			float dy = (1.0f - m_anim) * -6.0f; p.top += dy; p.bottom += dy;
			ComPtr<ID2D1Layer> layer; rt->CreateLayer(nullptr, &layer);
			if (layer) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
				D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), m_anim), layer.Get());
			vd::Shadow(rt, p, 8.0f, 0.18f, 5);
			vd::Fill(rt, p, vd::Col(0xFFFFFF), 8.0f);
			vd::Stroke(rt, p, vd::Col(0xE5E7EB), 8.0f, 1.0f);
			DrawPanel(rt, p);
			if (layer) rt->PopLayer();
		}
	};

	// -------------------------------------------------------------------------
	// VFlyoutButton — a button (this widget's bounds) that opens its own flyout
	// beneath it. Derived classes draw the button in DrawButton and the panel
	// in DrawPanel; the base wires hover, the opening click / key and the
	// light dismiss. OpenerRect narrows the part that opens (a command bar's
	// "..." only); ButtonUp receives clicks on the rest.
	// -------------------------------------------------------------------------
	class VFlyoutButton : public VFlyout {
	protected:
		virtual void DrawButton(ID2D1RenderTarget* rt) = 0;
		virtual D2D1_RECT_F  OpenerRect() const { return m_bounds; }
		virtual VInputResult ButtonUp(float, float, int) { return VInputResult::NotHandled; }
	public:
		bool CanFocus() const override { return true; }
		bool HitTest(float x, float y) const override { return vd::Contains(m_bounds, x, y) || VFlyout::HitTest(x, y); }
		VInputResult OnMouseMove(float x, float y) override {
			if (m_open) return VFlyout::OnMouseMove(x, y);
			bool h = vd::Contains(m_bounds, x, y);
			if (h == m_hovered) return VInputResult::NotHandled;
			m_hovered = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (m_open) return VFlyout::OnMouseDown(x, y, btn);
			if (btn == 1 && vd::Contains(OpenerRect(), x, y)) { Open(OpenerRect()); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (m_open) return VFlyout::OnMouseUp(x, y, btn);
			return ButtonUp(x, y, btn);
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (m_open) return VFlyout::OnKeyDown(vk);
			if (vk == VK_SPACE || vk == VK_RETURN) { Open(OpenerRect()); return VInputResult::Handled; }
			return VInputResult::NotHandled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override { DrawButton(rt); VFlyout::OnDraw(rt); }
	};

	// -------------------------------------------------------------------------
	class VMenu : public VFlyout {
	public:
		using Item = VMenuItem;
	private:
		std::vector<VMenuItem> m_items;
		int m_hover = -1;
		std::function<void(int)> m_cb;
		void Pick(int i) {
			VMenuItem& it = m_items[(size_t)i];
			if (it.checkable) it.checked = !it.checked;
			Close();
			if (m_cb) m_cb(i);
		}
	protected:
		void OnOpened() override { m_hover = -1; vmenu::Measure(m_items, 0, m_pw, m_ph); Place(); }
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
	public:
		const char* GetTypeName() const override { return "VMenu"; }
		VMenu& Add(std::wstring label, std::wstring glyph = L"", std::wstring shortcut = L"") {
			VMenuItem it; it.label = std::move(label); it.glyph = std::move(glyph); it.shortcut = std::move(shortcut);
			m_items.push_back(std::move(it)); return *this;
		}
		VMenu& Check(std::wstring label, bool on) { VMenuItem it; it.label = std::move(label); it.checkable = true; it.checked = on; m_items.push_back(std::move(it)); return *this; }
		VMenu& Separator()                        { VMenuItem it; it.separator = true; m_items.push_back(std::move(it)); return *this; }
		VMenuItem& At(int i) { return m_items[(size_t)i]; }
		int   Count() const  { return (int)m_items.size(); }
		void OnPick(std::function<void(int)> cb) { m_cb = std::move(cb); }
	};

	// -------------------------------------------------------------------------
	struct VDate { int y = 2000, m = 1, d = 1; };

	namespace vdate {
		inline bool Leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
		inline int  DaysIn(int y, int m) { static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 }; return (m == 2 && Leap(y)) ? 29 : d[m - 1]; }
		inline int  Weekday(int y, int m, int d) {                 // 0 = Monday .. 6 = Sunday
			static const int t[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
			if (m < 3) --y;
			int sun0 = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
			return (sun0 + 6) % 7;
		}
		inline bool  Same(VDate a, VDate b) { return a.y == b.y && a.m == b.m && a.d == b.d; }
		inline VDate Today() { SYSTEMTIME st; GetLocalTime(&st); return { st.wYear, st.wMonth, st.wDay }; }
		inline VDate AddDays(VDate v, int n) {
			v.d += n;
			while (v.d > DaysIn(v.y, v.m)) { v.d -= DaysIn(v.y, v.m); if (++v.m > 12) { v.m = 1; ++v.y; } }
			while (v.d < 1)                { if (--v.m < 1) { v.m = 12; --v.y; } v.d += DaysIn(v.y, v.m); }
			return v;
		}
		inline const wchar_t* MonthName(int m) {
			static const wchar_t* n[12] = { L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December" };
			return n[m - 1];
		}
		inline const wchar_t* DayName(int wd) {
			static const wchar_t* n[7] = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
			return n[wd];
		}
		inline std::wstring Format(VDate v) {                     // "Wed, 18 Sep 2026"
			wchar_t b[48]; swprintf_s(b, L"%s, %d %.3s %d", DayName(Weekday(v.y, v.m, v.d)), v.d, MonthName(v.m), v.y);
			return b;
		}
	}

	class VDatePicker : public VFlyoutButton {
		VDate m_v, m_view;
		int   m_hoverCell = -1, m_hoverNav = 0;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(VDate)> m_cb;
		static constexpr float kCell = 34.0f, kHead = 44.0f, kDays = 24.0f, kPadX = 12.0f;

		D2D1_RECT_F CellRect(const D2D1_RECT_F& p, int i) const {
			return vd::Rect(p.left + kPadX + kCell * (float)(i % 7), p.top + kHead + kDays + kCell * (float)(i / 7), kCell, kCell);
		}
		D2D1_RECT_F NavRect(const D2D1_RECT_F& p, int dir) const {
			return vd::Rect(dir < 0 ? p.right - 78.0f : p.right - 42.0f, p.top + 7.0f, 30.0f, 30.0f);
		}
		VDate CellDate(int i) const {
			int wd = vdate::Weekday(m_view.y, m_view.m, 1);
			return vdate::AddDays(VDate{ m_view.y, m_view.m, 1 }, i - wd);
		}
		int CellAt(float x, float y) const {
			for (int i = 0; i < 42; ++i) if (vd::Contains(CellRect(m_panel, i), x, y)) return i;
			return -1;
		}
		void ShiftMonth(int d) {
			m_view.m += d;
			if (m_view.m > 12) { m_view.m = 1; ++m_view.y; }
			if (m_view.m < 1)  { m_view.m = 12; --m_view.y; }
		}
		void Set(VDate v, bool fire) { m_v = v; m_view = { v.y, v.m, 1 }; if (fire && m_cb) m_cb(v); }
	protected:
		void OnOpened() override {
			m_view = { m_v.y, m_v.m, 1 }; m_hoverCell = -1; m_hoverNav = 0;
			m_pw = kPadX * 2.0f + kCell * 7.0f; m_ph = kHead + kDays + kCell * 6.0f + 10.0f;
			Place();
		}
		VInputResult PanelMove(float x, float y) override {
			int c = CellAt(x, y);
			int nav = vd::Contains(NavRect(m_panel, -1), x, y) ? -1 : (vd::Contains(NavRect(m_panel, 1), x, y) ? 1 : 0);
			if (c == m_hoverCell && nav == m_hoverNav) return VInputResult::NotHandled;
			m_hoverCell = c; m_hoverNav = nav; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override {
			if (vd::Contains(NavRect(m_panel, -1), x, y)) ShiftMonth(-1);
			else if (vd::Contains(NavRect(m_panel, 1), x, y)) ShiftMonth(+1);
			else { int c = CellAt(x, y); if (c >= 0) { Set(CellDate(c), true); Close(); } }
			return VInputResult::Handled;
		}
		VInputResult PanelKey(UINT vk) override {
			switch (vk) {
				case VK_LEFT:  Set(vdate::AddDays(m_v, -1), true); break;
				case VK_RIGHT: Set(vdate::AddDays(m_v, +1), true); break;
				case VK_UP:    Set(vdate::AddDays(m_v, -7), true); break;
				case VK_DOWN:  Set(vdate::AddDays(m_v, +7), true); break;
				case VK_PRIOR: ShiftMonth(-1); break;
				case VK_NEXT:  ShiftMonth(+1); break;
				case VK_RETURN: case VK_SPACE: Close(); break;
				default: return VInputResult::NotHandled;
			}
			return VInputResult::Handled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override {
			std::wstring head = std::wstring(vdate::MonthName(m_view.m)) + L" " + std::to_wstring(m_view.y);
			vd::Text(rt, head, D2D1::RectF(p.left + 16.0f, p.top + 6.0f, p.right - 80.0f, p.top + kHead), vctl::Ink(), vd::Style().Size(14).Bold());
			for (int dir = -1; dir <= 1; dir += 2) {
				D2D1_RECT_F n = NavRect(p, dir);
				if (m_hoverNav == dir) vd::Fill(rt, n, vd::Col(0x000000, 0.06f), 6.0f);
				vd::Chevron(rt, vd::CX(n), vd::CY(n), dir < 0 ? 90.0f : -90.0f, vctl::Ink());
			}
			for (int k = 0; k < 7; ++k)
				vd::Text(rt, std::wstring(1, vdate::DayName(k)[0]), vd::Rect(p.left + kPadX + kCell * (float)k, p.top + kHead, kCell, kDays), vctl::Muted(), vd::Style().Size(11).Center());
			VDate today = vdate::Today();
			for (int i = 0; i < 42; ++i) {
				VDate d = CellDate(i);
				D2D1_RECT_F r = CellRect(p, i);
				bool sel = vdate::Same(d, m_v), inMonth = d.m == m_view.m;
				if (sel)                    vd::Circle(rt, vd::CX(r), vd::CY(r), 15.0f, m_accent);
				else if (i == m_hoverCell)  vd::Circle(rt, vd::CX(r), vd::CY(r), 15.0f, vd::Col(0x000000, 0.06f));
				if (!sel && vdate::Same(d, today)) vd::Ring(rt, vd::CX(r), vd::CY(r), 15.0f, m_accent, 1.5f);
				vd::Text(rt, std::to_wstring(d.d), r, sel ? vd::Col(0xFFFFFF) : (inMonth ? vctl::Ink() : vd::Col(0xB0B7C3)), vd::Style().Size(12).Center());
			}
		}
	public:
		VDatePicker() { m_v = vdate::Today(); m_view = { m_v.y, m_v.m, 1 }; }
		const char* GetTypeName() const override { return "VDatePicker"; }
		VDatePicker& Set(VDate v)             { Set(v, false); return *this; }
		VDatePicker& Accent(D2D1_COLOR_F c)   { m_accent = c; return *this; }
		VDate Value() const                   { return m_v; }
		void OnChange(std::function<void(VDate)> cb) { m_cb = std::move(cb); }
	protected:
		void DrawButton(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, m_hovered && !m_open ? vd::Col(0xF9FAFB) : vd::Col(0xFFFFFF), 6.0f);
			vd::Stroke(rt, m_bounds, (m_focused || m_open) ? m_accent : vd::Col(0xD1D5DB), 6.0f, 1.5f);
			vd::Text(rt, vdate::Format(m_v), D2D1::RectF(m_bounds.left + 12.0f, m_bounds.top, m_bounds.right - 36.0f, m_bounds.bottom), vctl::Ink(), vd::Style().Size(13));
			vd::Text(rt, L"\xE787", vd::Rect(m_bounds.right - 34.0f, m_bounds.top, 30.0f, vd::H(m_bounds)), vctl::Muted(), vd::Style().Icon().Size(14).Center());
		}
	};

	// -------------------------------------------------------------------------
	struct VTime { int h = 9, m = 0; };

	class VTimePicker : public VFlyoutButton {
		VTime m_v;
		int   m_hoverH = -1, m_hoverM = -1;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(VTime)> m_cb;
		static constexpr float kCell = 34.0f, kPad = 12.0f, kLabel = 22.0f;

		D2D1_RECT_F HourCell(const D2D1_RECT_F& p, int i) const { return vd::Rect(p.left + kPad + kCell * (float)(i % 6), p.top + kPad + kLabel + kCell * (float)(i / 6), kCell, kCell); }
		D2D1_RECT_F MinCell(const D2D1_RECT_F& p, int k) const  { return vd::Rect(p.left + kPad + kCell + kCell * (float)(k % 4), p.top + kPad + kLabel + kCell * 4.0f + 12.0f + kLabel + kCell * (float)(k / 4), kCell, kCell); }
		int HourAt(float x, float y) const { for (int i = 0; i < 24; ++i) if (vd::Contains(HourCell(m_panel, i), x, y)) return i; return -1; }
		int MinAt(float x, float y) const  { for (int k = 0; k < 12; ++k) if (vd::Contains(MinCell(m_panel, k), x, y)) return k; return -1; }
		void Fire() { if (m_cb) m_cb(m_v); }
	protected:
		void OnOpened() override {
			m_hoverH = m_hoverM = -1;
			m_pw = kPad * 2.0f + kCell * 6.0f; m_ph = kPad + kLabel + kCell * 4.0f + 12.0f + kLabel + kCell * 3.0f + kPad; Place();
		}
		VInputResult PanelMove(float x, float y) override {
			int h = HourAt(x, y), m = MinAt(x, y);
			if (h == m_hoverH && m == m_hoverM) return VInputResult::NotHandled;
			m_hoverH = h; m_hoverM = m; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override {
			int h = HourAt(x, y), m = MinAt(x, y);
			if (h >= 0) { m_v.h = h; Fire(); }
			else if (m >= 0) { m_v.m = m * 5; Fire(); Close(); }
			return VInputResult::Handled;
		}
		VInputResult PanelKey(UINT vk) override {
			switch (vk) {
				case VK_LEFT:  m_v.h = (m_v.h + 23) % 24; break;
				case VK_RIGHT: m_v.h = (m_v.h + 1) % 24; break;
				case VK_UP:    m_v.m = (m_v.m + 5) % 60; break;
				case VK_DOWN:  m_v.m = (m_v.m + 55) % 60; break;
				case VK_RETURN: case VK_SPACE: Close(); return VInputResult::Handled;
				default: return VInputResult::NotHandled;
			}
			Fire(); return VInputResult::Handled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override {
			vd::Text(rt, L"Hour", vd::Rect(p.left + kPad, p.top + kPad, 100.0f, kLabel), vctl::Muted(), vd::Style().Size(11).Bold());
			for (int i = 0; i < 24; ++i) {
				D2D1_RECT_F r = HourCell(p, i); bool sel = i == m_v.h;
				if (sel) vd::Circle(rt, vd::CX(r), vd::CY(r), 15.0f, m_accent); else if (i == m_hoverH) vd::Circle(rt, vd::CX(r), vd::CY(r), 15.0f, vd::Col(0x000000, 0.06f));
				vd::Text(rt, std::to_wstring(i), r, sel ? vd::Col(0xFFFFFF) : vctl::Ink(), vd::Style().Size(12).Center());
			}
			float my = p.top + kPad + kLabel + kCell * 4.0f + 12.0f;
			vd::Text(rt, L"Minute", vd::Rect(p.left + kPad, my, 100.0f, kLabel), vctl::Muted(), vd::Style().Size(11).Bold());
			for (int k = 0; k < 12; ++k) {
				D2D1_RECT_F r = MinCell(p, k); bool sel = k * 5 == m_v.m;
				if (sel) vd::Circle(rt, vd::CX(r), vd::CY(r), 15.0f, m_accent); else if (k == m_hoverM) vd::Circle(rt, vd::CX(r), vd::CY(r), 15.0f, vd::Col(0x000000, 0.06f));
				wchar_t t[8]; swprintf_s(t, L"%02d", k * 5);
				vd::Text(rt, t, r, sel ? vd::Col(0xFFFFFF) : vctl::Ink(), vd::Style().Size(12).Center());
			}
		}
		void DrawButton(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, m_hovered && !m_open ? vd::Col(0xF9FAFB) : vd::Col(0xFFFFFF), 6.0f);
			vd::Stroke(rt, m_bounds, (m_focused || m_open) ? m_accent : vd::Col(0xD1D5DB), 6.0f, 1.5f);
			vd::Text(rt, Format(), D2D1::RectF(m_bounds.left + 12.0f, m_bounds.top, m_bounds.right - 36.0f, m_bounds.bottom), vctl::Ink(), vd::Style().Size(13));
			vd::Text(rt, L"\xE823", vd::Rect(m_bounds.right - 34.0f, m_bounds.top, 30.0f, vd::H(m_bounds)), vctl::Muted(), vd::Style().Icon().Size(14).Center());
		}
	public:
		const char* GetTypeName() const override { return "VTimePicker"; }
		VTimePicker& Set(VTime v)            { m_v = v; return *this; }
		VTimePicker& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		VTime Value() const                  { return m_v; }
		std::wstring Format() const          { wchar_t b[8]; swprintf_s(b, L"%02d:%02d", m_v.h, m_v.m); return b; }
		void OnChange(std::function<void(VTime)> cb) { m_cb = std::move(cb); }
	};

	// -------------------------------------------------------------------------
	class VTeachingTip : public VFlyout {
		std::wstring m_title, m_body, m_action;
		int m_hover = 0;                           // 1 = close, 2 = action
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void()> m_close, m_act;
		float BodyH() const { return vd::TextHeight(m_body, m_pw - 32.0f, vd::Style().Size(13).Wrap()); }
		D2D1_RECT_F CloseRect(const D2D1_RECT_F& p) const  { return vd::Rect(p.right - 36.0f, p.top + 12.0f, 24.0f, 24.0f); }
		D2D1_RECT_F ActionRect(const D2D1_RECT_F& p) const { return vd::Rect(p.left + 16.0f, p.bottom - 16.0f - 32.0f, vd::TextWidth(m_action, vd::Style().Size(13).Bold()) + 28.0f, 32.0f); }
	protected:
		void OnOpened() override {
			m_hover = 0; m_pw = 320.0f;
			m_ph = 16.0f + 24.0f + 8.0f + BodyH() + (m_action.empty() ? 0.0f : 16.0f + 32.0f) + 16.0f;
			Place();
		}
		VInputResult PanelMove(float x, float y) override {
			int h = vd::Contains(CloseRect(m_panel), x, y) ? 1 : (!m_action.empty() && vd::Contains(ActionRect(m_panel), x, y) ? 2 : 0);
			if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult PanelUp(float x, float y, int) override {
			if (vd::Contains(CloseRect(m_panel), x, y)) { Close(); if (m_close) m_close(); }
			else if (!m_action.empty() && vd::Contains(ActionRect(m_panel), x, y)) { Close(); if (m_act) m_act(); }
			return VInputResult::Handled;
		}
		void DrawPanel(ID2D1RenderTarget* rt, const D2D1_RECT_F& p) override {
			// the beak: on the top edge when the tip sits below its anchor, else on the bottom edge
			float ax = vd::Clamp(vd::CX(m_anchor), p.left + 20.0f, p.right - 20.0f);
			bool below = p.top >= m_anchor.bottom - 1.0f;
			float ey = below ? p.top : p.bottom, dir = below ? -1.0f : 1.0f;
			vd::Polyline(rt, { D2D1::Point2F(ax - 9.0f, ey), D2D1::Point2F(ax, ey + 9.0f * dir), D2D1::Point2F(ax + 9.0f, ey) }, vd::Col(0xFFFFFF), 1.0f, true);
			vd::Polyline(rt, { D2D1::Point2F(ax - 9.0f, ey), D2D1::Point2F(ax, ey + 9.0f * dir), D2D1::Point2F(ax + 9.0f, ey) }, vd::Col(0xE5E7EB), 1.0f);
			vd::Text(rt, m_title, vd::Rect(p.left + 16.0f, p.top + 16.0f, m_pw - 64.0f, 24.0f), vctl::Ink(), vd::Style().Size(14).Bold());
			vd::Text(rt, m_body, vd::Rect(p.left + 16.0f, p.top + 48.0f, m_pw - 32.0f, BodyH() + 4.0f), vctl::Ink(), vd::Style().Size(13).Wrap().Top());
			D2D1_RECT_F c = CloseRect(p);
			if (m_hover == 1) vd::Fill(rt, c, vd::Col(0x000000, 0.07f), 4.0f);
			vd::Text(rt, L"\xE711", c, vctl::Muted(), vd::Style().Icon().Size(10).Center());
			if (!m_action.empty()) {
				D2D1_RECT_F a = ActionRect(p);
				vd::Fill(rt, a, m_hover == 2 ? vd::Col(0x3B7AC8) : m_accent, 6.0f);
				vd::Text(rt, m_action, a, vd::Col(0xFFFFFF), vd::Style().Size(13).Bold().Center());
			}
		}
	public:
		const char* GetTypeName() const override { return "VTeachingTip"; }
		VTeachingTip& Title(std::wstring t)   { m_title = std::move(t); return *this; }
		VTeachingTip& Body(std::wstring t)    { m_body = std::move(t); return *this; }
		VTeachingTip& Action(std::wstring t)  { m_action = std::move(t); return *this; }
		VTeachingTip& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		void Show(const D2D1_RECT_F& anchor)  { Open(anchor); }
		void OnClose(std::function<void()> cb)  { m_close = std::move(cb); }
		void OnAction(std::function<void()> cb) { m_act = std::move(cb); }
	};

} // namespace ChronoUI
