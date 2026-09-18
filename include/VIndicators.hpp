// =============================================================================
// VIndicators.hpp — the small things that show state, WinUI 3 vocabulary.
//
//   VProgressRing   a spinning arc while something works; or a determinate ring
//   VRating         stars, hover previews, click sets, arrows nudge
//   VBadge          a count pill or a dot, in a colour that means something
//   VPersonPicture  initials on a colour hashed from the name, or a glyph
//
// Ten to forty lines each; same conventions as VControls.hpp.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <cmath>
#include <functional>
#include <string>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	class VProgressRing : public VirtualWidgetImpl {
		bool  m_active = true;
		float m_t = 0.0f, m_value = -1.0f;          // value >= 0: determinate
		float m_thick = 3.5f;
		D2D1_COLOR_F m_accent = vctl::Blue();
	public:
		const char* GetTypeName() const override { return "VProgressRing"; }
		VProgressRing& Active(bool on)        { m_active = on; return *this; }
		VProgressRing& Set(float v)           { m_value = vd::Clamp01(v); return *this; }   // switch to a determinate ring
		VProgressRing& Indeterminate()        { m_value = -1.0f; return *this; }
		VProgressRing& Thickness(float px)    { m_thick = px; return *this; }
		VProgressRing& Accent(D2D1_COLOR_F c) { m_accent = c; return *this; }
		bool IsActive() const { return m_active; }
		bool OnUpdate(float dt) override {
			if (!m_active || m_value >= 0.0f) return false;
			m_t += dt; return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (!m_active) return;
			float r = (std::min)(vd::W(m_bounds), vd::H(m_bounds)) * 0.5f - m_thick, cx = vd::CX(m_bounds), cy = vd::CY(m_bounds);
			vd::Ring(rt, cx, cy, r, vd::Alpha(m_accent, 0.15f), m_thick);
			if (m_value >= 0.0f) { vd::Arc(rt, cx, cy, r, -90.0f, 360.0f * m_value, m_accent, m_thick); return; }
			float head  = fmodf(m_t * 300.0f, 360.0f);
			float sweep = 40.0f + 220.0f * (0.5f + 0.5f * sinf(m_t * 2.6f));
			vd::Arc(rt, cx, cy, r, head, sweep, m_accent, m_thick);
		}
	};

	// -------------------------------------------------------------------------
	class VRating : public VirtualWidgetImpl {
		int m_max = 5, m_v = 0, m_hover = 0;         // hover = stars under the mouse (1-based), 0 = none
		D2D1_COLOR_F m_lit = vd::Col(0xF59E0B), m_dim = vd::Col(0xD1D5DB);
		std::function<void(int)> m_cb;
		float StarW() const { return vd::H(m_bounds); }
		int StarAt(float x) const { int i = (int)((x - m_bounds.left) / (StarW() + 4.0f)) + 1; return (i >= 1 && i <= m_max) ? i : 0; }
		void Star(ID2D1RenderTarget* rt, float cx, float cy, float r, D2D1_COLOR_F c) {
			std::vector<D2D1_POINT_2F> pts;
			for (int k = 0; k < 10; ++k) {
				float a = -vd::kPi * 0.5f + (float)k * vd::kPi / 5.0f, rr = (k % 2) ? r * 0.45f : r;
				pts.push_back(D2D1::Point2F(cx + rr * cosf(a), cy + rr * sinf(a)));
			}
			vd::Polyline(rt, pts, c, 1.0f, true);
		}
	public:
		const char* GetTypeName() const override { return "VRating"; }
		bool CanFocus() const override { return true; }
		VRating& Set(int v)            { m_v = (std::max)(0, (std::min)(m_max, v)); return *this; }
		VRating& Max(int n)            { m_max = n; return *this; }
		VRating& Lit(D2D1_COLOR_F c)   { m_lit = c; return *this; }
		int Value() const { return m_v; }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseMove(float x, float) override {
			int h = StarAt(x); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = 0; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
			int s = StarAt(x); if (s) { m_v = (s == m_v) ? 0 : s; if (m_cb) m_cb(m_v); }   // click the lit star again to clear
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			int d = vk == VK_RIGHT ? 1 : (vk == VK_LEFT ? -1 : 0);
			if (!d) return VInputResult::NotHandled;
			Set(m_v + d); if (m_cb) m_cb(m_v); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			float w = StarW(), r = w * 0.5f - 1.0f, cy = vd::CY(m_bounds);
			int shown = m_hover ? m_hover : m_v;
			for (int i = 1; i <= m_max; ++i) {
				float cx = m_bounds.left + (w + 4.0f) * (float)(i - 1) + w * 0.5f;
				Star(rt, cx, cy, r, i <= shown ? m_lit : m_dim);
			}
			if (m_focused) vd::Stroke(rt, vd::Inset(m_bounds, -3.0f, -3.0f), vd::Alpha(vctl::Blue(), 0.5f), 6.0f, 1.0f);
		}
	};

	// -------------------------------------------------------------------------
	class VBadge : public VirtualWidgetImpl {
		int  m_count = 0;
		bool m_dot = false;
		D2D1_COLOR_F m_col = vd::Col(0xDC2626);
	public:
		const char* GetTypeName() const override { return "VBadge"; }
		VBadge& Count(int n)          { m_count = n; return *this; }
		VBadge& Dot(bool on)          { m_dot = on; return *this; }
		VBadge& Color(D2D1_COLOR_F c) { m_col = c; return *this; }
		void OnDraw(ID2D1RenderTarget* rt) override {
			float h = vd::H(m_bounds), cy = vd::CY(m_bounds);
			if (m_dot) { vd::Circle(rt, m_bounds.left + h * 0.5f, cy, h * 0.3f, m_col); return; }
			if (m_count <= 0) return;
			std::wstring t = m_count > 99 ? L"99+" : std::to_wstring(m_count);
			float w = (std::max)(h, vd::TextWidth(t, vd::Style().Size(h * 0.55f).Bold()) + h * 0.6f);
			D2D1_RECT_F r = vd::Rect(m_bounds.left, m_bounds.top, w, h);
			vd::Fill(rt, r, m_col, h * 0.5f);
			vd::Text(rt, t, r, vd::Col(0xFFFFFF), vd::Style().Size(h * 0.55f).Bold().Center());
		}
	};

	// -------------------------------------------------------------------------
	class VPersonPicture : public VirtualWidgetImpl {
		std::wstring m_name, m_glyph;
	public:
		const char* GetTypeName() const override { return "VPersonPicture"; }
		VPersonPicture& Name(std::wstring n)  { m_name = std::move(n); return *this; }
		VPersonPicture& Glyph(std::wstring g) { m_glyph = std::move(g); return *this; }
		void OnDraw(ID2D1RenderTarget* rt) override {
			float r = (std::min)(vd::W(m_bounds), vd::H(m_bounds)) * 0.5f, cx = vd::CX(m_bounds), cy = vd::CY(m_bounds);
			if (!m_glyph.empty() || m_name.empty()) {
				vd::Circle(rt, cx, cy, r, vctl::Track());
				vd::Text(rt, m_glyph.empty() ? std::wstring(L"\xE77B") : m_glyph, vd::Rect(cx - r, cy - r, 2.0f * r, 2.0f * r), vctl::Muted(), vd::Style().Icon().Size(r).Center());
				return;
			}
			vd::Avatar(rt, cx, cy, r, m_name);
		}
	};

} // namespace ChronoUI
