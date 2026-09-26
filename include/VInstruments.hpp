// =============================================================================
// VInstruments.hpp — dials and meters, the fun end of the dashboard.
//
//   VGauge         a 270° arc with a needle, coloured zones, ticks and a readout;
//                  a speedometer, an engine temperature, a battery, one class
//   VAnalogClock   hours, minutes and a sweeping second hand, live
//   VVitals        a CRT-style trace with a sweep bar: an ECG, or any signal fed in
//   VEqualizer     LED bars with peak hold, green / amber / red
//   VPlot          a scrolling line plot: Push(value) and it moves
//
// Values are animated with vd::Approach so a Set() glides; the theme's ink
// and surface colours apply, the accents are each instrument's own. Same
// conventions as VControls.hpp.
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
	class VGauge : public VirtualWidgetImpl {
	public:
		struct Zone { float from, to; D2D1_COLOR_F color; };
	private:
		float m_min = 0.0f, m_max = 100.0f, m_v = 0.0f, m_shown = 0.0f;
		float m_start = 135.0f, m_sweep = 270.0f;
		int   m_ticks = 10;
		bool  m_fill = true, m_needle = true;
		std::wstring m_unit, m_label;
		std::vector<Zone> m_zones;
		D2D1_COLOR_F m_accent = vctl::Blue();
		float Angle(float v) const { return m_start + m_sweep * vd::Clamp01((v - m_min) / (m_max - m_min)); }
	public:
		const char* GetTypeName() const override { return "VGauge"; }
		VGauge& Range(float lo, float hi)     { m_min = lo; m_max = hi; return *this; }
		VGauge& Set(float v)                  { m_v = vd::Clamp(v, m_min, m_max); return *this; }
		VGauge& Snap(float v)                 { Set(v); m_shown = m_v; return *this; }
		VGauge& Unit(std::wstring u)          { m_unit = std::move(u); return *this; }
		VGauge& Label(std::wstring l)         { m_label = std::move(l); return *this; }
		VGauge& Ticks(int n)                  { m_ticks = n; return *this; }
		VGauge& Zones(std::vector<Zone> z)    { m_zones = std::move(z); return *this; }
		VGauge& Fill(bool on)                 { m_fill = on; return *this; }      // the arc up to the value in the accent
		VGauge& Needle(bool on)               { m_needle = on; return *this; }
		VGauge& Accent(D2D1_COLOR_F c)        { m_accent = c; return *this; }
		float Value() const                   { return m_v; }
		bool OnUpdate(float dt) override {
			if (fabsf(m_shown - m_v) < 0.02f * (m_max - m_min) * 0.01f) { m_shown = m_v; return false; }
			m_shown = vd::Approach(m_shown, m_v, dt, 6.0f);
			return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			float cx = vd::CX(m_bounds), cy = vd::CY(m_bounds) + vd::H(m_bounds) * 0.06f;
			float r = (std::min)(vd::W(m_bounds), vd::H(m_bounds)) * 0.42f, w = r * 0.16f;
			vd::Arc(rt, cx, cy, r, m_start, m_sweep, vctl::Track(), w);
			for (const Zone& z : m_zones) vd::Arc(rt, cx, cy, r, Angle(z.from), Angle(z.to) - Angle(z.from), z.color, w);
			if (m_fill) vd::Arc(rt, cx, cy, r, m_start, Angle(m_shown) - m_start, m_accent, w);
			for (int i = 0; i <= m_ticks; ++i) {
				float a = (m_start + m_sweep * (float)i / (float)m_ticks) * vd::kPi / 180.0f;
				float r0 = r - w * 0.5f - 4.0f, r1 = r0 - (i % 2 == 0 ? 9.0f : 5.0f);
				vd::Line(rt, cx + r0 * cosf(a), cy + r0 * sinf(a), cx + r1 * cosf(a), cy + r1 * sinf(a), vctl::Muted(), i % 2 == 0 ? 2.0f : 1.0f);
			}
			if (m_needle) {
				float a = Angle(m_shown) * vd::kPi / 180.0f, len = r - w * 0.5f - 8.0f;
				vd::Line(rt, cx - 10.0f * cosf(a), cy - 10.0f * sinf(a), cx + len * cosf(a), cy + len * sinf(a), vd::Col(0xEF4444), 3.0f);
				vd::Circle(rt, cx, cy, 6.0f, vctl::Ink());
				vd::Circle(rt, cx, cy, 2.5f, vd::Col(0xEF4444));
			}
			std::wstring v = vd::Num(m_shown, (m_max - m_min) <= 20.0f ? 1 : 0) + (m_unit.empty() ? L"" : L" " + m_unit);
			vd::Text(rt, v, vd::Rect(cx - r, cy + r * 0.35f, r * 2.0f, r * 0.5f), vctl::Ink(), vd::Style().Size(r * 0.3f).Heavy().Center());
			if (!m_label.empty()) vd::Text(rt, m_label, vd::Rect(cx - r, cy + r * 0.8f, r * 2.0f, 20.0f), vctl::Muted(), vd::Style().Size(11).Bold().Center());
		}
	};

	// -------------------------------------------------------------------------
	class VAnalogClock : public VirtualWidgetImpl {
		bool  m_seconds = true;
		float m_t = 0.0f;
		D2D1_COLOR_F m_accent = vd::Col(0xEF4444);
	public:
		const char* GetTypeName() const override { return "VAnalogClock"; }
		VAnalogClock& Seconds(bool on)        { m_seconds = on; return *this; }
		VAnalogClock& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		bool OnUpdate(float dt) override { m_t += dt; return m_seconds || m_t > 1.0f; }
		void OnDraw(ID2D1RenderTarget* rt) override {
			m_t = 0.0f;
			SYSTEMTIME st; GetLocalTime(&st);
			float cx = vd::CX(m_bounds), cy = vd::CY(m_bounds), r = (std::min)(vd::W(m_bounds), vd::H(m_bounds)) * 0.46f;
			vd::Circle(rt, cx, cy, r, vctl::Surface());
			vd::Ring(rt, cx, cy, r, vctl::Ink(), r * 0.06f);
			for (int i = 0; i < 12; ++i) {
				float a = (float)i * vd::kPi / 6.0f, r0 = r * 0.86f, r1 = r * (i % 3 == 0 ? 0.72f : 0.78f);
				vd::Line(rt, cx + r0 * sinf(a), cy - r0 * cosf(a), cx + r1 * sinf(a), cy - r1 * cosf(a), vctl::Ink(), i % 3 == 0 ? r * 0.05f : r * 0.03f);
			}
			float sec = (float)st.wSecond + (float)st.wMilliseconds / 1000.0f;
			float mn  = (float)st.wMinute + sec / 60.0f, hr = (float)(st.wHour % 12) + mn / 60.0f;
			auto hand = [&](float turns, float len, float width, D2D1_COLOR_F c, float tail) {
				float a = turns * 2.0f * vd::kPi;
				vd::Line(rt, cx - tail * sinf(a), cy + tail * cosf(a), cx + len * sinf(a), cy - len * cosf(a), c, width);
			};
			hand(hr / 12.0f, r * 0.5f, r * 0.07f, vctl::Ink(), r * 0.1f);
			hand(mn / 60.0f, r * 0.72f, r * 0.05f, vctl::Ink(), r * 0.1f);
			if (m_seconds) hand(sec / 60.0f, r * 0.8f, r * 0.02f, m_accent, r * 0.18f);
			vd::Circle(rt, cx, cy, r * 0.05f, m_accent);
		}
	};

	// -------------------------------------------------------------------------
	// VVitals — feed it samples (0..1) with Push(), or let Simulate(true) make
	// a heartbeat; the trace scrolls behind a sweep bar the way a monitor does.
	// -------------------------------------------------------------------------
	class VVitals : public VirtualWidgetImpl {
		std::vector<float> m_buf;
		int   m_head = 0;
		bool  m_sim = false;
		float m_acc = 0.0f, m_phase = 0.0f, m_rate = 72.0f, m_blink = 0.0f;
		std::wstring m_label = L"ECG";
		D2D1_COLOR_F m_trace = vd::Col(0x22C55E);
		static constexpr float kSps = 180.0f;                 // samples per second drawn
		float Beat(float p) const {                           // one ECG period, p in 0..1
			auto bump = [](float x, float c, float w, float h) { float d = (x - c) / w; return h * expf(-d * d); };
			return 0.5f + bump(p, 0.18f, 0.03f, 0.06f) - bump(p, 0.27f, 0.008f, 0.08f) + bump(p, 0.30f, 0.012f, 0.42f) - bump(p, 0.335f, 0.01f, 0.14f) + bump(p, 0.52f, 0.04f, 0.1f);
		}
		void Fit() { int n = (std::max)(2, (int)vd::W(m_bounds)); if ((int)m_buf.size() != n) { m_buf.assign((size_t)n, 0.5f); m_head = 0; } }
	public:
		const char* GetTypeName() const override { return "VVitals"; }
		VVitals& Simulate(bool on)         { m_sim = on; return *this; }
		VVitals& Rate(float bpm)           { m_rate = bpm; return *this; }
		VVitals& Label(std::wstring l)     { m_label = std::move(l); return *this; }
		VVitals& Trace(D2D1_COLOR_F c)     { m_trace = c; return *this; }
		void Push(float v)                 { Fit(); m_buf[(size_t)m_head] = vd::Clamp01(v); m_head = (m_head + 1) % (int)m_buf.size(); }
		bool OnUpdate(float dt) override {
			m_blink += dt;
			if (!m_sim) return false;
			m_acc += dt * kSps;
			while (m_acc >= 1.0f) {
				m_acc -= 1.0f;
				m_phase += m_rate / 60.0f / kSps; if (m_phase >= 1.0f) m_phase -= 1.0f;
				Push(Beat(m_phase) + ((float)(rand() % 100) - 50.0f) * 0.0004f);
			}
			return true;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			Fit();
			vd::Fill(rt, m_bounds, vd::Col(0x05140A), 8.0f);
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (float x = m_bounds.left; x < m_bounds.right; x += 20.0f) vd::Line(rt, x, m_bounds.top, x, m_bounds.bottom, vd::Col(0x22C55E, 0.12f), 1.0f);
			for (float y = m_bounds.top; y < m_bounds.bottom; y += 20.0f) vd::Line(rt, m_bounds.left, y, m_bounds.right, y, vd::Col(0x22C55E, 0.12f), 1.0f);
			int n = (int)m_buf.size();
			std::vector<D2D1_POINT_2F> pts; pts.reserve((size_t)n);
			float H = vd::H(m_bounds) - 24.0f;
			for (int i = 0; i < n; ++i) {
				if (i > m_head && i < m_head + 24) { if (pts.size() >= 2) vd::Polyline(rt, pts, m_trace, 2.0f); pts.clear(); continue; }   // the gap behind the sweep
				pts.push_back(D2D1::Point2F(m_bounds.left + (float)i, m_bounds.bottom - 12.0f - H * m_buf[(size_t)i]));
			}
			if (pts.size() >= 2) vd::Polyline(rt, pts, m_trace, 2.0f);
			vd::Line(rt, m_bounds.left + (float)m_head, m_bounds.top, m_bounds.left + (float)m_head, m_bounds.bottom, vd::Col(0xFFFFFF, 0.35f), 2.0f);
			vd::Text(rt, m_label, vd::Rect(m_bounds.left + 12.0f, m_bounds.top + 8.0f, 200.0f, 20.0f), m_trace, vd::Style().Size(12).Bold());
			if (m_sim) {
				std::wstring bpm = vd::Num(m_rate) + L" bpm";
				vd::Text(rt, bpm, vd::Rect(m_bounds.right - 112.0f, m_bounds.top + 8.0f, 80.0f, 20.0f), m_trace, vd::Style().Size(12).Bold().Right());
				if (fmodf(m_blink, 60.0f / m_rate) < 0.15f) vd::Circle(rt, m_bounds.right - 20.0f, m_bounds.top + 18.0f, 5.0f, vd::Col(0xEF4444));
			}
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VEqualizer : public VirtualWidgetImpl {
		std::vector<float> m_target, m_level, m_peak;
		bool  m_sim = false;
		float m_t = 0.0f;
		int   m_segments = 16;
		void Fit(size_t n) { if (m_level.size() != n) { m_target.assign(n, 0.0f); m_level.assign(n, 0.0f); m_peak.assign(n, 0.0f); } }
	public:
		explicit VEqualizer(int bars = 24) { Fit((size_t)bars); }
		const char* GetTypeName() const override { return "VEqualizer"; }
		VEqualizer& Simulate(bool on)              { m_sim = on; return *this; }
		VEqualizer& Segments(int n)                { m_segments = n; return *this; }
		void Set(const std::vector<float>& levels) { Fit(levels.size()); for (size_t i = 0; i < levels.size(); ++i) m_target[i] = vd::Clamp01(levels[i]); }
		bool OnUpdate(float dt) override {
			m_t += dt;
			size_t n = m_level.size();
			if (m_sim) for (size_t i = 0; i < n; ++i) {
				float f = (float)i / (float)n;
				m_target[i] = vd::Clamp01(0.35f + 0.3f * sinf(m_t * (2.0f + f * 3.0f) + f * 6.0f) + 0.25f * sinf(m_t * 7.0f + (float)i) * (1.0f - f) + ((float)(rand() % 100) - 50.0f) * 0.002f);
			}
			bool busy = false;
			for (size_t i = 0; i < n; ++i) {
				float before = m_level[i];
				m_level[i] = m_level[i] < m_target[i] ? vd::Approach(m_level[i], m_target[i], dt, 30.0f) : vd::Approach(m_level[i], m_target[i], dt, 6.0f);
				if (m_level[i] > m_peak[i]) m_peak[i] = m_level[i]; else m_peak[i] = (std::max)(m_level[i], m_peak[i] - dt * 0.35f);
				if (fabsf(before - m_level[i]) > 0.002f) busy = true;
			}
			return busy || m_sim;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0x0B0F19), 8.0f);
			size_t n = m_level.size(); if (!n) return;
			float pad = 10.0f, gap = 3.0f, bw = (vd::W(m_bounds) - 2.0f * pad - gap * (float)(n - 1)) / (float)n;
			float H = vd::H(m_bounds) - 2.0f * pad, sh = (H - 2.0f * (float)(m_segments - 1)) / (float)m_segments;
			for (size_t i = 0; i < n; ++i) {
				float x = m_bounds.left + pad + (bw + gap) * (float)i;
				int lit = (int)(m_level[i] * (float)m_segments + 0.5f), pk = (int)(m_peak[i] * (float)m_segments + 0.5f);
				for (int s = 0; s < m_segments; ++s) {
					float f = (float)s / (float)m_segments;
					D2D1_COLOR_F c = f < 0.6f ? vd::Col(0x22C55E) : (f < 0.85f ? vd::Col(0xF59E0B) : vd::Col(0xEF4444));
					bool on = s < lit || s == pk - 1;
					vd::Fill(rt, vd::Rect(x, m_bounds.bottom - pad - (sh + 2.0f) * (float)s - sh, bw, sh), on ? c : vd::Alpha(c, 0.14f), 1.5f);
				}
			}
		}
	};

	// -------------------------------------------------------------------------
	class VPlot : public VirtualWidgetImpl {
		std::vector<float> m_v;
		size_t m_cap = 120;
		float  m_min = 0.0f, m_max = 100.0f;
		std::wstring m_label, m_unit;
		D2D1_COLOR_F m_line = vd::Col(0x22C55E);
	public:
		const char* GetTypeName() const override { return "VPlot"; }
		VPlot& Range(float lo, float hi)      { m_min = lo; m_max = hi; return *this; }
		VPlot& Capacity(size_t n)             { m_cap = (std::max)((size_t)2, n); while (m_v.size() > m_cap) m_v.erase(m_v.begin()); return *this; }
		VPlot& Label(std::wstring l)          { m_label = std::move(l); return *this; }
		VPlot& Unit(std::wstring u)           { m_unit = std::move(u); return *this; }
		VPlot& Line(D2D1_COLOR_F c)           { m_line = c; return *this; }
		void Push(float v)                    { m_v.push_back(v); if (m_v.size() > m_cap) m_v.erase(m_v.begin()); }
		float Last() const                    { return m_v.empty() ? 0.0f : m_v.back(); }
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0x05140A), 8.0f);
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			D2D1_RECT_F p = vd::Inset(m_bounds, 12.0f, 28.0f);
			for (int k = 0; k <= 4; ++k) {
				float y = p.top + vd::H(p) * (float)k / 4.0f;
				vd::Line(rt, p.left, y, p.right, y, vd::Alpha(m_line, 0.15f), 1.0f);
			}
			for (int k = 0; k <= 8; ++k) { float x = p.left + vd::W(p) * (float)k / 8.0f; vd::Line(rt, x, p.top, x, p.bottom, vd::Alpha(m_line, 0.15f), 1.0f); }
			if (m_v.size() >= 2) {
				std::vector<D2D1_POINT_2F> pts; pts.reserve(m_v.size() + 2);
				float step = vd::W(p) / (float)(m_cap - 1), x0 = p.right - step * (float)(m_v.size() - 1);
				for (size_t i = 0; i < m_v.size(); ++i)
					pts.push_back(D2D1::Point2F(x0 + step * (float)i, p.bottom - vd::H(p) * vd::Clamp01((m_v[i] - m_min) / (m_max - m_min))));
				std::vector<D2D1_POINT_2F> area = pts;
				area.push_back(D2D1::Point2F(pts.back().x, p.bottom)); area.push_back(D2D1::Point2F(pts.front().x, p.bottom));
				vd::Polyline(rt, area, vd::Alpha(m_line, 0.18f), 1.0f, true);
				vd::Polyline(rt, pts, m_line, 2.0f);
				vd::Circle(rt, pts.back().x, pts.back().y, 3.5f, m_line);
			}
			vd::Text(rt, m_label, vd::Rect(m_bounds.left + 12.0f, m_bounds.top + 6.0f, 200.0f, 20.0f), m_line, vd::Style().Size(12).Bold());
			vd::Text(rt, vd::Num(Last(), 1) + m_unit, vd::Rect(m_bounds.right - 112.0f, m_bounds.top + 6.0f, 100.0f, 20.0f), m_line, vd::Style().Size(12).Bold().Right());
			vd::Text(rt, vd::Num(m_max), vd::Rect(m_bounds.left + 12.0f, p.top - 2.0f, 60.0f, 14.0f), vd::Alpha(m_line, 0.6f), vd::Style().Size(9));
			vd::Text(rt, vd::Num(m_min), vd::Rect(m_bounds.left + 12.0f, p.bottom - 12.0f, 60.0f, 14.0f), vd::Alpha(m_line, 0.6f), vd::Style().Size(9));
			rt->PopAxisAlignedClip();
		}
	};

} // namespace ChronoUI
