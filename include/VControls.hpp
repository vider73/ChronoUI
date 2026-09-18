// =============================================================================
// VControls.hpp — the everyday form controls, as virtual widgets.
//
//   VToggle    on/off switch, knob slides
//   VCheck     checkbox with a label, tick pops in
//   VRadio     one-of-many, vertical or horizontal
//   VSegment   segmented picker, selected pill slides
//   VSlider    0..1 with a draggable knob, wheel nudges
//   VStepper   integer with − / + buttons
//   VProgress  labelled bar that fills smoothly
//   VColorPicker  saturation/value square + hue bar, hex readout
//
// Each is 30-60 lines and follows the same shape: setters return *this so
// they chain, OnChange(callback) reports the user's edits, and the little
// animations run off OnUpdate(dt) with vd::Approach. Colours default to a
// light theme with a blue accent; Accent() changes the one colour that
// matters. See src/examples/Controls.cpp and Form.cpp for them in use.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VDraw.hpp"
#include <functional>
#include <string>
#include <vector>

namespace ChronoUI {

	namespace vctl {
		inline D2D1_COLOR_F Ink()   { return vd::Col(0x111827); }
		inline D2D1_COLOR_F Muted() { return vd::Col(0x6B7280); }
		inline D2D1_COLOR_F Track() { return vd::Col(0xE5E7EB); }
		inline D2D1_COLOR_F Blue()  { return vd::Col(0x4A90E2); }
	}

	// -------------------------------------------------------------------------
	class VToggle : public VirtualWidgetImpl {
		bool  m_on = false;
		float m_anim = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(bool)> m_cb;
	public:
		const char* GetTypeName() const override { return "VToggle"; }
		bool CanFocus() const override { return true; }
		VToggle& Set(bool on)              { m_on = on; m_anim = on ? 1.0f : 0.0f; return *this; }
		VToggle& Accent(D2D1_COLOR_F c)    { m_accent = c; return *this; }
		bool     Value() const             { return m_on; }
		void OnChange(std::function<void(bool)> cb) { m_cb = std::move(cb); }
		bool OnUpdate(float dt) override {
			float target = m_on ? 1.0f : 0.0f;
			if (fabsf(m_anim - target) < 0.005f) { m_anim = target; return false; }
			m_anim = vd::Approach(m_anim, target, dt, 18.0f);
			return true;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
			m_on = !m_on; if (m_cb) m_cb(m_on);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk != VK_SPACE && vk != VK_RETURN) return VInputResult::NotHandled;
			m_on = !m_on; if (m_cb) m_cb(m_on);
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			const float w = 44.0f;
			D2D1_RECT_F track = vd::Rect(m_bounds.left, vd::CY(m_bounds) - 12.0f, w, 24.0f);
			vd::Fill(rt, track, vd::Mix(vd::Col(0xD1D5DB), m_accent, m_anim), 12.0f);
			if (m_focused) vd::Stroke(rt, vd::Inset(track, -2.0f, -2.0f), vd::Alpha(m_accent, 0.5f), 14.0f, 1.5f);
			float kx = track.left + 12.0f + (w - 24.0f) * m_anim;
			vd::Shadow(rt, vd::Rect(kx - 9.0f, vd::CY(track) - 9.0f, 18.0f, 18.0f), 9.0f, 0.12f, 2);
			vd::Circle(rt, kx, vd::CY(track), 9.0f, vd::Col(0xFFFFFF));
		}
	};

	// -------------------------------------------------------------------------
	class VCheck : public VirtualWidgetImpl {
		std::wstring m_label;
		bool  m_on = false;
		float m_anim = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(bool)> m_cb;
	public:
		explicit VCheck(std::wstring label) : m_label(std::move(label)) {}
		const char* GetTypeName() const override { return "VCheck"; }
		bool CanFocus() const override { return true; }
		VCheck& Set(bool on)              { m_on = on; m_anim = on ? 1.0f : 0.0f; return *this; }
		VCheck& Accent(D2D1_COLOR_F c)    { m_accent = c; return *this; }
		bool    Value() const             { return m_on; }
		void OnChange(std::function<void(bool)> cb) { m_cb = std::move(cb); }
		bool OnUpdate(float dt) override {
			float target = m_on ? 1.0f : 0.0f;
			if (fabsf(m_anim - target) < 0.005f) { m_anim = target; return false; }
			m_anim = vd::Approach(m_anim, target, dt, 22.0f);
			return true;
		}
		VInputResult OnMouseEnter() override { m_hovered = true; return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
			m_on = !m_on; if (m_cb) m_cb(m_on);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk != VK_SPACE) return VInputResult::NotHandled;
			m_on = !m_on; if (m_cb) m_cb(m_on);
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F box = vd::Rect(m_bounds.left, vd::CY(m_bounds) - 9.0f, 18.0f, 18.0f);
			vd::Fill(rt, box, vd::Mix(vd::Col(0xFFFFFF), m_accent, m_anim), 5.0f);
			vd::Stroke(rt, box, (m_anim > 0.5f || m_hovered || m_focused) ? m_accent : vd::Col(0xD1D5DB), 5.0f, 1.5f);
			if (m_anim > 0.05f) {
				float s = vd::EaseOut(m_anim), cx = vd::CX(box), cy = vd::CY(box);
				vd::Polyline(rt, { D2D1::Point2F(cx - 5.0f * s, cy + 0.5f * s), D2D1::Point2F(cx - 1.5f * s, cy + 4.0f * s),
				                   D2D1::Point2F(cx + 5.5f * s, cy - 4.0f * s) }, vd::Col(0xFFFFFF), 2.2f);
			}
			vd::Text(rt, m_label, D2D1::RectF(box.right + 10.0f, m_bounds.top, m_bounds.right, m_bounds.bottom), vctl::Ink(), vd::Style().Size(13));
		}
	};

	// -------------------------------------------------------------------------
	class VRadio : public VirtualWidgetImpl {
		std::vector<std::wstring> m_items;
		int  m_sel = 0, m_hover = -1;
		bool m_horizontal = false;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;
		D2D1_RECT_F ItemRect(int i) const {
			int n = (int)m_items.size();
			if (m_horizontal) { float w = vd::W(m_bounds) / (float)n; return vd::Rect(m_bounds.left + w * (float)i, m_bounds.top, w, vd::H(m_bounds)); }
			float h = vd::H(m_bounds) / (float)n;
			return vd::Rect(m_bounds.left, m_bounds.top + h * (float)i, vd::W(m_bounds), h);
		}
		int ItemAt(float x, float y) const {
			for (int i = 0; i < (int)m_items.size(); ++i) if (vd::Contains(ItemRect(i), x, y)) return i;
			return -1;
		}
	public:
		explicit VRadio(std::vector<std::wstring> items, bool horizontal = false) : m_items(std::move(items)), m_horizontal(horizontal) {}
		const char* GetTypeName() const override { return "VRadio"; }
		bool CanFocus() const override { return true; }
		VRadio& Set(int i)              { m_sel = i; return *this; }
		VRadio& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		int     Value() const           { return m_sel; }
		const std::wstring& Text() const { return m_items[(size_t)m_sel]; }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseMove(float x, float y) override {
			int h = ItemAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = ItemAt(x, y);
			if (btn != 1 || i < 0) return VInputResult::NotHandled;
			if (i != m_sel) { m_sel = i; if (m_cb) m_cb(i); }
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			int d = (vk == VK_DOWN || vk == VK_RIGHT) ? 1 : ((vk == VK_UP || vk == VK_LEFT) ? -1 : 0);
			if (!d) return VInputResult::NotHandled;
			m_sel = (m_sel + d + (int)m_items.size()) % (int)m_items.size(); if (m_cb) m_cb(m_sel);
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			for (int i = 0; i < (int)m_items.size(); ++i) {
				D2D1_RECT_F r = ItemRect(i);
				float cx = r.left + 9.0f, cy = vd::CY(r);
				bool on = i == m_sel;
				vd::Circle(rt, cx, cy, 9.0f, vd::Col(0xFFFFFF));
				vd::Ring(rt, cx, cy, 8.5f, (on || i == m_hover) ? m_accent : vd::Col(0xD1D5DB), on ? 2.0f : 1.5f);
				if (on) vd::Circle(rt, cx, cy, 4.5f, m_accent);
				vd::Text(rt, m_items[(size_t)i], D2D1::RectF(r.left + 26.0f, r.top, r.right, r.bottom), vctl::Ink(), vd::Style().Size(13));
			}
			if (m_focused) vd::Stroke(rt, vd::Inset(ItemRect(m_sel), 1.0f, 1.0f), vd::Alpha(m_accent, 0.35f), 6.0f, 1.0f);
		}
	};

	// -------------------------------------------------------------------------
	class VSegment : public VirtualWidgetImpl {
		std::vector<std::wstring> m_items;
		int   m_sel = 0, m_hover = -1;
		float m_pos = 0.0f;
		std::function<void(int)> m_cb;
		int IndexAt(float x) const {
			float w = vd::W(m_bounds) / (float)m_items.size();
			int i = (int)((x - m_bounds.left) / w);
			return (i < 0 || i >= (int)m_items.size()) ? -1 : i;
		}
	public:
		explicit VSegment(std::vector<std::wstring> items) : m_items(std::move(items)) {}
		const char* GetTypeName() const override { return "VSegment"; }
		bool CanFocus() const override { return true; }
		VSegment& Set(int i) { m_sel = i; m_pos = (float)i; return *this; }
		int       Value() const { return m_sel; }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
		bool OnUpdate(float dt) override {
			if (fabsf(m_pos - (float)m_sel) < 0.002f) { m_pos = (float)m_sel; return false; }
			m_pos = vd::Approach(m_pos, (float)m_sel, dt, 16.0f);
			return true;
		}
		VInputResult OnMouseMove(float x, float) override {
			int h = IndexAt(x); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = IndexAt(x);
			if (btn != 1 || i < 0 || !HitTest(x, y)) return VInputResult::NotHandled;
			if (i != m_sel) { m_sel = i; if (m_cb) m_cb(i); }
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			int d = vk == VK_RIGHT ? 1 : (vk == VK_LEFT ? -1 : 0);
			if (!d) return VInputResult::NotHandled;
			int n = (int)m_items.size(); m_sel = (m_sel + d + n) % n; if (m_cb) m_cb(m_sel);
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vctl::Track(), 9.0f);
			float w = vd::W(m_bounds) / (float)m_items.size();
			D2D1_RECT_F pill = vd::Inset(vd::Rect(m_bounds.left + w * m_pos, m_bounds.top, w, vd::H(m_bounds)), 3.0f, 3.0f);
			vd::Shadow(rt, pill, 7.0f, 0.10f, 2);
			vd::Fill(rt, pill, vd::Col(0xFFFFFF), 7.0f);
			if (m_focused) vd::Stroke(rt, pill, vd::Alpha(vctl::Blue(), 0.6f), 7.0f, 1.5f);
			for (size_t i = 0; i < m_items.size(); ++i) {
				D2D1_RECT_F r = vd::Rect(m_bounds.left + w * (float)i, m_bounds.top, w, vd::H(m_bounds));
				bool lit = (int)i == m_sel || (int)i == m_hover;
				vd::Text(rt, m_items[i], r, lit ? vctl::Ink() : vctl::Muted(), vd::Style().Size(12).Bold().Center());
			}
		}
	};

	// -------------------------------------------------------------------------
	class VSlider : public VirtualWidgetImpl {
		float m_v = 0.5f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(float)> m_cb;
		std::function<std::wstring(float)> m_fmt;
		D2D1_RECT_F Track() const { return vd::Rect(m_bounds.left + 9.0f, vd::CY(m_bounds) - 3.0f, vd::W(m_bounds) - 18.0f - 56.0f, 6.0f); }
		void FromX(float x) {
			D2D1_RECT_F t = Track();
			float v = vd::Clamp01((x - t.left) / vd::W(t));
			if (v != m_v) { m_v = v; if (m_cb) m_cb(m_v); }
		}
	public:
		const char* GetTypeName() const override { return "VSlider"; }
		bool CanFocus() const override { return true; }
		VSlider& Set(float v)             { m_v = vd::Clamp01(v); return *this; }
		VSlider& Accent(D2D1_COLOR_F c)   { m_accent = c; return *this; }
		VSlider& Format(std::function<std::wstring(float)> f) { m_fmt = std::move(f); return *this; }
		float    Value() const            { return m_v; }
		void OnChange(std::function<void(float)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseDown(float x, float, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			m_pressed = true; FromX(x); return VInputResult::Capture;
		}
		VInputResult OnMouseMove(float x, float) override {
			if (!m_pressed) return VInputResult::NotHandled;
			FromX(x); return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float, float, int) override { m_pressed = false; return VInputResult::Handled; }
		VInputResult OnMouseWheel(float delta, float, float) override {
			m_v = vd::Clamp01(m_v + (delta > 0 ? 0.05f : -0.05f)); if (m_cb) m_cb(m_v);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			float d = vk == VK_RIGHT ? 0.05f : (vk == VK_LEFT ? -0.05f : 0.0f);
			if (d == 0.0f) return VInputResult::NotHandled;
			m_v = vd::Clamp01(m_v + d); if (m_cb) m_cb(m_v);
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F t = Track();
			float kx = t.left + vd::W(t) * m_v, kr = m_pressed ? 10.0f : 9.0f;
			vd::Fill(rt, t, vctl::Track(), 3.0f);
			vd::Fill(rt, D2D1::RectF(t.left, t.top, kx, t.bottom), m_accent, 3.0f);
			vd::Shadow(rt, vd::Rect(kx - 9.0f, vd::CY(t) - 9.0f, 18.0f, 18.0f), 9.0f, 0.14f, 2);
			vd::Circle(rt, kx, vd::CY(t), kr, vd::Col(0xFFFFFF));
			vd::Ring(rt, kx, vd::CY(t), kr, m_accent, m_focused ? 3.0f : 2.0f);
			std::wstring label = m_fmt ? m_fmt(m_v) : vd::Num(m_v * 100.0) + L"%";
			vd::Text(rt, label, D2D1::RectF(t.right + 12.0f, m_bounds.top, m_bounds.right, m_bounds.bottom), vctl::Muted(), vd::Style().Size(12).Right());
		}
	};

	// -------------------------------------------------------------------------
	class VStepper : public VirtualWidgetImpl {
		int m_v = 1, m_min = 0, m_max = 99;
		int m_hover = 0;                    // -1 minus, +1 plus, 0 none
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;
		D2D1_RECT_F Minus() const { return vd::Rect(m_bounds.left, m_bounds.top, 34.0f, vd::H(m_bounds)); }
		D2D1_RECT_F Plus()  const { return vd::Rect(m_bounds.right - 34.0f, m_bounds.top, 34.0f, vd::H(m_bounds)); }
		void Nudge(int d) {
			int v = (std::max)(m_min, (std::min)(m_max, m_v + d));
			if (v != m_v) { m_v = v; if (m_cb) m_cb(m_v); }
		}
	public:
		const char* GetTypeName() const override { return "VStepper"; }
		bool CanFocus() const override { return true; }
		VStepper& Range(int lo, int hi)   { m_min = lo; m_max = hi; return *this; }
		VStepper& Set(int v)              { m_v = v; return *this; }
		VStepper& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		int       Value() const           { return m_v; }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseMove(float x, float y) override {
			int h = vd::Contains(Minus(), x, y) ? -1 : (vd::Contains(Plus(), x, y) ? 1 : 0);
			if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = 0; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (vd::Contains(Minus(), x, y)) Nudge(-1); else if (vd::Contains(Plus(), x, y)) Nudge(+1);
			return VInputResult::Handled;
		}
		VInputResult OnMouseWheel(float delta, float, float) override { Nudge(delta > 0 ? 1 : -1); return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			if (vk == VK_UP || vk == VK_RIGHT) Nudge(+1); else if (vk == VK_DOWN || vk == VK_LEFT) Nudge(-1); else return VInputResult::NotHandled;
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f);
			vd::Stroke(rt, m_bounds, m_focused ? m_accent : vd::Col(0xD1D5DB), 8.0f, 1.5f);
			D2D1_RECT_F mi = Minus(), pl = Plus();
			if (m_hover == -1) vd::Fill(rt, vd::Inset(mi, 3.0f, 3.0f), vctl::Track(), 6.0f);
			if (m_hover == +1) vd::Fill(rt, vd::Inset(pl, 3.0f, 3.0f), vctl::Track(), 6.0f);
			vd::Text(rt, L"\x2212", mi, m_v > m_min ? vctl::Ink() : vd::Col(0xD1D5DB), vd::Style().Size(16).Center());
			vd::Text(rt, L"+", pl, m_v < m_max ? vctl::Ink() : vd::Col(0xD1D5DB), vd::Style().Size(16).Center());
			vd::Text(rt, std::to_wstring(m_v), D2D1::RectF(mi.right, m_bounds.top, pl.left, m_bounds.bottom), vctl::Ink(), vd::Style().Size(14).Bold().Center());
		}
	};

	// -------------------------------------------------------------------------
	class VProgress : public VirtualWidgetImpl {
		std::wstring m_label;
		float m_target = 0.0f, m_shown = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
	public:
		explicit VProgress(std::wstring label) : m_label(std::move(label)) {}
		const char* GetTypeName() const override { return "VProgress"; }
		VProgress& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }
		void Set(float v) { m_target = vd::Clamp01(v); }
		bool OnUpdate(float dt) override {
			if (fabsf(m_shown - m_target) < 0.002f) { m_shown = m_target; return false; }
			m_shown = vd::Approach(m_shown, m_target, dt, 8.0f);
			return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Text(rt, m_label, vd::Rect(m_bounds.left, m_bounds.top, vd::W(m_bounds) - 50.0f, 18.0f), vctl::Muted(), vd::Style().Size(12).Top());
			vd::Text(rt, vd::Num(m_shown * 100.0) + L"%", vd::Rect(m_bounds.right - 50.0f, m_bounds.top, 50.0f, 18.0f), vctl::Ink(), vd::Style().Size(12).Bold().Right().Top());
			D2D1_RECT_F bar = vd::Rect(m_bounds.left, m_bounds.bottom - 8.0f, vd::W(m_bounds), 8.0f);
			vd::Fill(rt, bar, vctl::Track(), 4.0f);
			vd::Fill(rt, D2D1::RectF(bar.left, bar.top, bar.left + vd::W(bar) * m_shown, bar.bottom), m_accent, 4.0f);
		}
	};

	// -------------------------------------------------------------------------
	class VColorPicker : public VirtualWidgetImpl {
		float m_h = 210.0f, m_s = 0.7f, m_v = 0.9f;
		int   m_drag = 0;                          // 1 = the square, 2 = the hue bar
		std::function<void(D2D1_COLOR_F)> m_cb;
		D2D1_RECT_F Square() const { return vd::Rect(m_bounds.left, m_bounds.top, vd::W(m_bounds), vd::H(m_bounds) - 64.0f); }
		D2D1_RECT_F HueBar() const { return vd::Rect(m_bounds.left, m_bounds.bottom - 50.0f, vd::W(m_bounds), 14.0f); }
		void Fire() { if (m_cb) m_cb(Value()); }
		void FromSquare(float x, float y) {
			D2D1_RECT_F q = Square();
			m_s = vd::Clamp01((x - q.left) / vd::W(q)); m_v = 1.0f - vd::Clamp01((y - q.top) / vd::H(q)); Fire();
		}
		void FromHue(float x) { D2D1_RECT_F b = HueBar(); m_h = vd::Clamp01((x - b.left) / vd::W(b)) * 359.9f; Fire(); }
	public:
		const char* GetTypeName() const override { return "VColorPicker"; }
		bool CanFocus() const override { return true; }
		VColorPicker& Set(D2D1_COLOR_F c) { vd::ToHSV(c, m_h, m_s, m_v); return *this; }
		D2D1_COLOR_F Value() const        { return vd::FromHSV(m_h, m_s, m_v); }
		void OnChange(std::function<void(D2D1_COLOR_F)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (vd::Contains(Square(), x, y))      { m_drag = 1; FromSquare(x, y); return VInputResult::Capture; }
			if (vd::Contains(vd::Inset(HueBar(), 0.0f, -6.0f), x, y)) { m_drag = 2; FromHue(x); return VInputResult::Capture; }
			return VInputResult::NotHandled;
		}
		VInputResult OnMouseMove(float x, float y) override {
			if (m_drag == 1) FromSquare(x, y); else if (m_drag == 2) FromHue(x); else return VInputResult::NotHandled;
			return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float, float, int) override { m_drag = 0; return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			switch (vk) {
				case VK_LEFT:  m_h = fmodf(m_h + 355.0f, 360.0f); break;
				case VK_RIGHT: m_h = fmodf(m_h + 5.0f, 360.0f); break;
				case VK_UP:    m_v = vd::Clamp01(m_v + 0.05f); break;
				case VK_DOWN:  m_v = vd::Clamp01(m_v - 0.05f); break;
				default: return VInputResult::NotHandled;
			}
			Fire(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F q = Square(), b = HueBar();
			D2D1_ROUNDED_RECT rq = D2D1::RoundedRect(q, 6.0f, 6.0f);
			if (auto br = vd::GradientBrush(rt, D2D1::Point2F(q.left, q.top), D2D1::Point2F(q.right, q.top), vd::Col(0xFFFFFF), vd::FromHSV(m_h, 1.0f, 1.0f)))
				rt->FillRoundedRectangle(rq, br.Get());
			if (auto br = vd::GradientBrush(rt, D2D1::Point2F(q.left, q.top), D2D1::Point2F(q.left, q.bottom), vd::Col(0x000000, 0.0f), vd::Col(0x000000, 1.0f)))
				rt->FillRoundedRectangle(rq, br.Get());
			vd::Stroke(rt, q, vd::Col(0x000000, 0.12f), 6.0f, 1.0f);
			float kx = q.left + m_s * vd::W(q), ky = q.top + (1.0f - m_v) * vd::H(q);
			vd::Circle(rt, kx, ky, 8.0f, Value());
			vd::Ring(rt, kx, ky, 8.0f, vd::Col(0xFFFFFF), 2.0f);
			vd::Ring(rt, kx, ky, 9.5f, vd::Col(0x000000, 0.25f), 1.0f);

			std::vector<D2D1_GRADIENT_STOP> stops;
			for (int k = 0; k <= 6; ++k) stops.push_back({ (float)k / 6.0f, vd::FromHSV((float)k * 60.0f, 1.0f, 1.0f) });
			if (auto br = vd::GradientBrushN(rt, D2D1::Point2F(b.left, b.top), D2D1::Point2F(b.right, b.top), stops))
				rt->FillRoundedRectangle(D2D1::RoundedRect(b, 7.0f, 7.0f), br.Get());
			float hx = b.left + (m_h / 360.0f) * vd::W(b);
			vd::Circle(rt, hx, vd::CY(b), 9.0f, vd::FromHSV(m_h, 1.0f, 1.0f));
			vd::Ring(rt, hx, vd::CY(b), 9.0f, vd::Col(0xFFFFFF), 2.0f);
			vd::Ring(rt, hx, vd::CY(b), 10.5f, vd::Col(0x000000, 0.25f), 1.0f);

			D2D1_RECT_F sw = vd::Rect(m_bounds.left, m_bounds.bottom - 24.0f, 24.0f, 24.0f);
			vd::Fill(rt, sw, Value(), 5.0f);
			vd::Stroke(rt, sw, vd::Col(0x000000, 0.15f), 5.0f, 1.0f);
			vd::Text(rt, vd::Hex(Value()), D2D1::RectF(sw.right + 10.0f, sw.top, m_bounds.right, sw.bottom), vctl::Ink(), vd::Style().Size(13).Mono());
			if (m_focused) vd::Stroke(rt, vd::Inset(m_bounds, -4.0f, -4.0f), vd::Alpha(vctl::Blue(), 0.5f), 8.0f, 1.0f);
		}
	};

} // namespace ChronoUI
