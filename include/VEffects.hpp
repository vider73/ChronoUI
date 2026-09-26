// =============================================================================
// VEffects.hpp — the widgets that are there to be looked at.
//
//   VEyes    two eyes whose pupils follow the mouse anywhere on the screen
//   VSnow    flakes drifting down over its bounds; the mouse passes through
//   VStorm   rain, and every few seconds a bolt and a flash; passes through too
//   VTicker  a line of text scrolling across a dark bar, like a news channel
//   VBusy    a veil with a spinning ring over whatever it covers, while Active
//
// VSnow, VStorm and VBusy are overlays: give them the bounds of what they
// cover and add them AFTER it, so they paint on top. The first two return
// false from HitTest, so clicks reach what is underneath; VBusy takes them
// while it is active, which is the point of a busy veil.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	class VEyes : public VirtualWidgetImpl {
		HWND  m_hwnd = nullptr;                  // to map the cursor into the window
		float m_px = 0.0f, m_py = 0.0f;          // pupil offset, animated
		float m_blink = 0.0f, m_next = 3.0f;     // lid, seconds to the next blink
		D2D1_COLOR_F m_iris = vd::Col(0x2563EB);
		static float Frand() { return (float)rand() / (float)RAND_MAX; }
	public:
		const char* GetTypeName() const override { return "VEyes"; }
		VEyes& Window(HWND h)         { m_hwnd = h; return *this; }     // the VirtualWindow's HWND
		VEyes& Iris(D2D1_COLOR_F c)   { m_iris = c; return *this; }
		bool OnUpdate(float dt) override {
			POINT p; GetCursorPos(&p);
			if (m_hwnd) ScreenToClient(m_hwnd, &p);
			float cx = vd::CX(m_bounds), cy = vd::CY(m_bounds), dx = (float)p.x - cx, dy = (float)p.y - cy;
			float d = sqrtf(dx * dx + dy * dy), reach = (std::min)(1.0f, d / 260.0f);
			float tx = d > 0.5f ? dx / d * reach : 0.0f, ty = d > 0.5f ? dy / d * reach : 0.0f;
			float bx = m_px, by = m_py, bb = m_blink;
			m_px = vd::Approach(m_px, tx, dt, 12.0f); m_py = vd::Approach(m_py, ty, dt, 12.0f);
			m_next -= dt;
			if (m_next <= 0.0f) { m_blink = 1.0f; m_next = 2.5f + Frand() * 4.0f; }
			m_blink = vd::Approach(m_blink, 0.0f, dt, 9.0f); if (m_blink < 0.01f) m_blink = 0.0f;
			return fabsf(bx - m_px) > 0.001f || fabsf(by - m_py) > 0.001f || bb != m_blink;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			float h = vd::H(m_bounds), w = vd::W(m_bounds), ry = h * 0.46f, rx = (std::min)(w * 0.22f, ry * 0.7f), cy = vd::CY(m_bounds);
			for (int k = -1; k <= 1; k += 2) {
				float cx = vd::CX(m_bounds) + (float)k * rx * 1.25f;
				D2D1_ELLIPSE eye = D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry);
				auto white = vd::Brush(rt, vd::Col(0xFFFFFF)), ink = vd::Brush(rt, vctl::Ink());
				if (white) rt->FillEllipse(eye, white.Get());
				if (ink)   rt->DrawEllipse(eye, ink.Get(), 3.0f);
				float px = cx + m_px * rx * 0.45f, py = cy + m_py * ry * 0.45f, pr = rx * 0.42f;
				vd::Circle(rt, px, py, pr, m_iris);
				vd::Circle(rt, px, py, pr * 0.55f, vd::Col(0x111827));
				vd::Circle(rt, px - pr * 0.3f, py - pr * 0.3f, pr * 0.2f, vd::Col(0xFFFFFF, 0.9f));
				if (m_blink > 0.02f) {                                                   // the lid, coming down
					float lid = vd::EaseOut(m_blink) * ry * 2.05f;
					rt->PushAxisAlignedClip(D2D1::RectF(cx - rx - 2.0f, cy - ry - 2.0f, cx + rx + 2.0f, cy - ry + lid), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
					auto skin = vd::Brush(rt, vtheme::IsDark() ? vd::Col(0x3F3F46) : vd::Col(0xFBCFB4));
					if (skin) rt->FillEllipse(eye, skin.Get());
					if (ink)  rt->DrawEllipse(eye, ink.Get(), 3.0f);
					rt->PopAxisAlignedClip();
				}
			}
		}
	};

	// -------------------------------------------------------------------------
	class VSnow : public VirtualWidgetImpl {
		struct Flake { float x, y, r, vy, drift, phase; };
		std::vector<Flake> m_flakes;
		int   m_count = 90;
		float m_wind = 0.0f, m_t = 0.0f;
		static float Frand() { return (float)rand() / (float)RAND_MAX; }
		Flake Spawn(bool anywhere) const {
			Flake f; f.x = m_bounds.left + Frand() * vd::W(m_bounds); f.y = anywhere ? m_bounds.top + Frand() * vd::H(m_bounds) : m_bounds.top - 6.0f;
			f.r = 1.5f + Frand() * 2.5f; f.vy = 18.0f + f.r * 14.0f; f.drift = 8.0f + Frand() * 14.0f; f.phase = Frand() * 6.28f; return f;
		}
	public:
		const char* GetTypeName() const override { return "VSnow"; }
		bool HitTest(float, float) const override { return false; }      // the mouse passes through
		VSnow& Count(int n)     { m_count = n; m_flakes.clear(); return *this; }
		VSnow& Wind(float px)   { m_wind = px; return *this; }           // px per second, sideways
		bool OnUpdate(float dt) override {
			if ((int)m_flakes.size() != m_count) { m_flakes.clear(); for (int i = 0; i < m_count; ++i) m_flakes.push_back(Spawn(true)); }
			m_t += dt;
			for (Flake& f : m_flakes) {
				f.y += f.vy * dt; f.x += (m_wind + sinf(m_t * 1.3f + f.phase) * f.drift) * dt;
				if (f.y > m_bounds.bottom + 6.0f || f.x < m_bounds.left - 10.0f || f.x > m_bounds.right + 10.0f) f = Spawn(false);
			}
			return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (const Flake& f : m_flakes) vd::Circle(rt, f.x, f.y, f.r, vd::Col(0xFFFFFF, vtheme::IsDark() ? 0.85f : 0.95f));
			for (const Flake& f : m_flakes) if (f.r > 3.0f) vd::Ring(rt, f.x, f.y, f.r + 1.0f, vd::Col(0xBFDBFE, 0.5f), 1.0f);
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VStorm : public VirtualWidgetImpl {
		struct Drop { float x, y, len, v; };
		std::vector<Drop> m_rain;
		std::vector<D2D1_POINT_2F> m_bolt;
		float m_flash = 0.0f, m_next = 2.0f, m_boltLife = 0.0f;
		int   m_drops = 140;
		static float Frand() { return (float)rand() / (float)RAND_MAX; }
		Drop Spawn(bool anywhere) const { Drop d; d.x = m_bounds.left + Frand() * (vd::W(m_bounds) + 60.0f) - 30.0f; d.y = anywhere ? m_bounds.top + Frand() * vd::H(m_bounds) : m_bounds.top - 20.0f; d.len = 8.0f + Frand() * 14.0f; d.v = 380.0f + Frand() * 260.0f; return d; }
		void Strike() {
			m_bolt.clear();
			float x = m_bounds.left + vd::W(m_bounds) * (0.2f + 0.6f * Frand()), y = m_bounds.top;
			float H = vd::H(m_bounds);
			while (y < m_bounds.bottom - H * 0.15f) { m_bolt.push_back(D2D1::Point2F(x, y)); y += H * (0.06f + Frand() * 0.08f); x += (Frand() - 0.5f) * 40.0f; }
			m_flash = 1.0f; m_boltLife = 0.35f; m_next = 2.0f + Frand() * 5.0f;
		}
	public:
		const char* GetTypeName() const override { return "VStorm"; }
		bool HitTest(float, float) const override { return false; }
		VStorm& Drops(int n) { m_drops = n; m_rain.clear(); return *this; }
		void StrikeNow()     { Strike(); }
		bool OnUpdate(float dt) override {
			if ((int)m_rain.size() != m_drops) { m_rain.clear(); for (int i = 0; i < m_drops; ++i) m_rain.push_back(Spawn(true)); }
			for (Drop& d : m_rain) { d.y += d.v * dt; d.x += d.v * 0.12f * dt; if (d.y > m_bounds.bottom) d = Spawn(false); }
			m_next -= dt; if (m_next <= 0.0f) Strike();
			m_flash = vd::Approach(m_flash, 0.0f, dt, 7.0f); if (m_flash < 0.01f) m_flash = 0.0f;
			m_boltLife = (std::max)(0.0f, m_boltLife - dt);
			return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			vd::Fill(rt, m_bounds, vd::Col(0x1E293B, 0.35f + 0.1f * m_flash));
			for (const Drop& d : m_rain) vd::Line(rt, d.x, d.y, d.x - d.len * 0.12f, d.y - d.len, vd::Col(0xBFDBFE, 0.45f), 1.2f);
			if (m_flash > 0.0f) vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF, 0.45f * m_flash));
			if (m_boltLife > 0.0f && m_bolt.size() >= 2) {
				vd::Polyline(rt, m_bolt, vd::Col(0xFDE68A, 0.5f), 7.0f);
				vd::Polyline(rt, m_bolt, vd::Col(0xFFFFFF), 2.5f);
			}
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VTicker : public VirtualWidgetImpl {
		std::wstring m_text;
		float m_speed = 60.0f, m_x = 0.0f, m_w = 0.0f;
		D2D1_COLOR_F m_bg = vd::Col(0x111827), m_ink = vd::Col(0xFFFFFF);
	public:
		explicit VTicker(std::wstring text) : m_text(std::move(text)) {}
		const char* GetTypeName() const override { return "VTicker"; }
		VTicker& Text(std::wstring t)          { m_text = std::move(t); m_w = 0.0f; return *this; }
		VTicker& Speed(float pxPerSecond)      { m_speed = pxPerSecond; return *this; }
		VTicker& Colors(D2D1_COLOR_F bg, D2D1_COLOR_F ink) { m_bg = bg; m_ink = ink; return *this; }
		bool OnUpdate(float dt) override {
			if (m_w <= 0.0f) { m_w = vd::TextWidth(m_text, vd::Style().Size(14)) + vd::W(m_bounds) * 0.5f; m_x = 0.0f; }
			m_x -= m_speed * dt; if (m_x <= -m_w) m_x += m_w;
			return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, m_bg, 6.0f);
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (float x = m_bounds.left + m_x; x < m_bounds.right; x += (std::max)(m_w, 1.0f))
				vd::Text(rt, m_text, D2D1::RectF(x, m_bounds.top, x + 1e4f, m_bounds.bottom), m_ink, vd::Style().Size(14));
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VBusy : public VirtualWidgetImpl {
		bool  m_active = false;
		float m_anim = 0.0f, m_t = 0.0f;
		std::wstring m_text;
		D2D1_COLOR_F m_accent = vctl::Blue();
	public:
		const char* GetTypeName() const override { return "VBusy"; }
		VBusy& Active(bool on)          { m_active = on; return *this; }
		VBusy& Text(std::wstring t)     { m_text = std::move(t); return *this; }
		VBusy& Accent(D2D1_COLOR_F c)   { m_accent = c; return *this; }
		bool IsActive() const           { return m_active; }
		bool HitTest(float x, float y) const override { return (m_active || m_anim > 0.05f) && vd::Contains(m_bounds, x, y); }
		VInputResult OnMouseDown(float, float, int) override { return VInputResult::Handled; }   // nothing underneath reacts
		VInputResult OnMouseUp(float, float, int) override   { return VInputResult::Handled; }
		bool OnUpdate(float dt) override {
			float t = m_active ? 1.0f : 0.0f;
			m_t += dt;
			if (fabsf(m_anim - t) < 0.01f) { m_anim = t; return m_active; }
			m_anim = vd::Approach(m_anim, t, dt, 10.0f); return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_anim < 0.02f) return;
			vd::Fill(rt, m_bounds, vd::Alpha(vtheme::Current().window, 0.7f * m_anim), 8.0f);
			float cx = vd::CX(m_bounds), cy = vd::CY(m_bounds) - (m_text.empty() ? 0.0f : 10.0f), r = 18.0f;
			vd::Ring(rt, cx, cy, r, vd::Alpha(m_accent, 0.15f * m_anim), 4.0f);
			vd::Arc(rt, cx, cy, r, fmodf(m_t * 300.0f, 360.0f), 40.0f + 220.0f * (0.5f + 0.5f * sinf(m_t * 2.6f)), vd::Alpha(m_accent, m_anim), 4.0f);
			if (!m_text.empty()) vd::Text(rt, m_text, vd::Rect(m_bounds.left, cy + r + 8.0f, vd::W(m_bounds), 20.0f), vd::Alpha(vctl::Ink(), m_anim), vd::Style().Size(13).Center());
		}
	};

} // namespace ChronoUI
