// =============================================================================
// VDraw.hpp — small drawing helpers for virtual widgets.
//
// Everything a widget's OnDraw() reaches for ten times per frame, in one
// place: colours, easing, rounded rectangles, circles, lines, arcs, gradients,
// polylines and text. All functions are stateless and take the render target
// the host handed to OnDraw(). Brushes and text formats are created on the fly:
// Direct2D makes that cheap enough for UI-scale drawing, and it keeps widget
// code free of cached resources that would otherwise have to survive a
// D2DERR_RECREATE_TARGET.
//
//     using namespace ChronoUI;
//     vd::Fill(rt, m_bounds, vd::Col(0x1F2937), 8.0f);
//     vd::Text(rt, L"Revenue", vd::Inset(m_bounds, 14, 10), vd::Col(0x9CA3AF),
//              vd::Style().Size(12).Bold());
//
// The examples under src/examples/ are the reference for how these compose.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

namespace ChronoUI {
namespace vd {

	// --- colours ----------------------------------------------------------
	inline D2D1_COLOR_F Col(uint32_t rgb, float alpha = 1.0f) { return D2D1::ColorF(rgb, alpha); }
	inline D2D1_COLOR_F Alpha(D2D1_COLOR_F c, float alpha) { c.a = alpha; return c; }
	inline D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
		return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
		                    a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
	}

	// --- numbers ----------------------------------------------------------
	constexpr float kPi = 3.14159265358979f;
	inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
	inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	inline float EaseOut(float t)   { t = Clamp01(t); float u = 1.0f - t; return 1.0f - u * u * u; }
	inline float EaseInOut(float t) { t = Clamp01(t); return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f; }
	// Critically damped approach — the "snap to target" used by animated values.
	inline float Approach(float current, float target, float dt, float speed = 10.0f) {
		return Lerp(current, target, 1.0f - expf(-speed * dt));
	}

	// --- rectangles -------------------------------------------------------
	inline D2D1_RECT_F Rect(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
	inline D2D1_RECT_F Inset(const D2D1_RECT_F& r, float dx, float dy) { return D2D1::RectF(r.left + dx, r.top + dy, r.right - dx, r.bottom - dy); }
	inline D2D1_RECT_F LerpRect(const D2D1_RECT_F& a, const D2D1_RECT_F& b, float t) {
		return D2D1::RectF(Lerp(a.left, b.left, t), Lerp(a.top, b.top, t), Lerp(a.right, b.right, t), Lerp(a.bottom, b.bottom, t));
	}
	inline float W(const D2D1_RECT_F& r)  { return r.right - r.left; }
	inline float H(const D2D1_RECT_F& r)  { return r.bottom - r.top; }
	inline float CX(const D2D1_RECT_F& r) { return (r.left + r.right) * 0.5f; }
	inline float CY(const D2D1_RECT_F& r) { return (r.top + r.bottom) * 0.5f; }
	inline bool  Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

	// --- brushes ----------------------------------------------------------
	inline ComPtr<ID2D1SolidColorBrush> Brush(ID2D1RenderTarget* rt, D2D1_COLOR_F c) {
		ComPtr<ID2D1SolidColorBrush> b;
		rt->CreateSolidColorBrush(c, &b);
		return b;
	}
	inline ComPtr<ID2D1LinearGradientBrush> GradientBrush(ID2D1RenderTarget* rt,
		D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F c0, D2D1_COLOR_F c1)
	{
		D2D1_GRADIENT_STOP stops[2] = { { 0.0f, c0 }, { 1.0f, c1 } };
		ComPtr<ID2D1GradientStopCollection> coll;
		rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll);
		ComPtr<ID2D1LinearGradientBrush> b;
		if (coll) rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), coll.Get(), &b);
		return b;
	}
	inline ComPtr<ID2D1StrokeStyle> RoundCaps(ID2D1RenderTarget* rt) {
		ComPtr<ID2D1Factory> f; rt->GetFactory(&f);
		ComPtr<ID2D1StrokeStyle> s;
		if (f) f->CreateStrokeStyle(D2D1::StrokeStyleProperties(
			D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
			D2D1_LINE_JOIN_ROUND), nullptr, 0, &s);
		return s;
	}

	inline ComPtr<ID2D1LinearGradientBrush> GradientBrushN(ID2D1RenderTarget* rt,
		D2D1_POINT_2F from, D2D1_POINT_2F to, const std::vector<D2D1_GRADIENT_STOP>& stops)
	{
		ComPtr<ID2D1GradientStopCollection> coll;
		rt->CreateGradientStopCollection(stops.data(), (UINT32)stops.size(), D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll);
		ComPtr<ID2D1LinearGradientBrush> b;
		if (coll) rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), coll.Get(), &b);
		return b;
	}

	// --- colour models ----------------------------------------------------
	inline D2D1_COLOR_F FromHSV(float h, float s, float v, float a = 1.0f) {
		h = fmodf(h, 360.0f); if (h < 0.0f) h += 360.0f;
		float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c, r, g, b;
		switch ((int)(h / 60.0f)) {
			case 0:  r = c; g = x; b = 0; break;  case 1:  r = x; g = c; b = 0; break;
			case 2:  r = 0; g = c; b = x; break;  case 3:  r = 0; g = x; b = c; break;
			case 4:  r = x; g = 0; b = c; break;  default: r = c; g = 0; b = x; break;
		}
		return D2D1::ColorF(r + m, g + m, b + m, a);
	}
	inline void ToHSV(D2D1_COLOR_F c, float& h, float& s, float& v) {
		float mx = (std::max)(c.r, (std::max)(c.g, c.b)), mn = (std::min)(c.r, (std::min)(c.g, c.b)), d = mx - mn;
		v = mx; s = mx > 0.0f ? d / mx : 0.0f;
		if (d <= 1e-6f) { h = 0.0f; return; }
		if (mx == c.r)      h = 60.0f * fmodf((c.g - c.b) / d, 6.0f);
		else if (mx == c.g) h = 60.0f * ((c.b - c.r) / d + 2.0f);
		else                h = 60.0f * ((c.r - c.g) / d + 4.0f);
		if (h < 0.0f) h += 360.0f;
	}
	inline std::wstring Hex(D2D1_COLOR_F c) {
		wchar_t b[16]; swprintf_s(b, L"#%02X%02X%02X", (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f), (int)(c.b * 255.0f + 0.5f));
		return b;
	}

	// --- shapes -----------------------------------------------------------
	inline void Fill(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F c, float radius = 0.0f) {
		auto b = Brush(rt, c); if (!b) return;
		if (radius > 0.0f) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b.Get());
		else               rt->FillRectangle(r, b.Get());
	}
	inline void Stroke(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F c, float radius = 0.0f, float width = 1.0f) {
		auto b = Brush(rt, c); if (!b) return;
		if (radius > 0.0f) rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b.Get(), width);
		else               rt->DrawRectangle(r, b.Get(), width);
	}
	inline void Gradient(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F top, D2D1_COLOR_F bottom,
	                     float radius = 0.0f, bool horizontal = false)
	{
		auto b = horizontal
			? GradientBrush(rt, D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.top), top, bottom)
			: GradientBrush(rt, D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.left, r.bottom), top, bottom);
		if (!b) return;
		if (radius > 0.0f) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b.Get());
		else               rt->FillRectangle(r, b.Get());
	}
	inline void Circle(ID2D1RenderTarget* rt, float cx, float cy, float radius, D2D1_COLOR_F c) {
		auto b = Brush(rt, c); if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), b.Get());
	}
	inline void Ring(ID2D1RenderTarget* rt, float cx, float cy, float radius, D2D1_COLOR_F c, float width = 1.0f) {
		auto b = Brush(rt, c); if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), b.Get(), width);
	}
	inline void Line(ID2D1RenderTarget* rt, float x0, float y0, float x1, float y1, D2D1_COLOR_F c, float width = 1.0f) {
		auto b = Brush(rt, c); if (b) rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), b.Get(), width);
	}
	// Soft drop shadow: a few translucent rounded rects widening downwards.
	inline void Shadow(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, float strength = 0.10f, int layers = 4) {
		for (int i = layers; i >= 1; --i) {
			float g = (float)i * 1.5f;
			D2D1_RECT_F s = D2D1::RectF(r.left - g * 0.5f, r.top + g * 0.6f, r.right + g * 0.5f, r.bottom + g * 1.4f);
			Fill(rt, s, Col(0x000000, strength / (float)layers), radius + g * 0.5f);
		}
	}

	// --- paths ------------------------------------------------------------
	inline ComPtr<ID2D1PathGeometry> Path(ID2D1RenderTarget* rt) {
		ComPtr<ID2D1Factory> f; rt->GetFactory(&f);
		ComPtr<ID2D1PathGeometry> p;
		if (f) f->CreatePathGeometry(&p);
		return p;
	}
	// Open polyline, or a closed filled polygon when `fill` is set.
	inline void Polyline(ID2D1RenderTarget* rt, const std::vector<D2D1_POINT_2F>& pts, D2D1_COLOR_F c,
	                     float width = 2.0f, bool fill = false)
	{
		if (pts.size() < 2) return;
		auto path = Path(rt); if (!path) return;
		ComPtr<ID2D1GeometrySink> s;
		if (FAILED(path->Open(&s))) return;
		s->BeginFigure(pts[0], fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
		s->AddLines(pts.data() + 1, (UINT32)pts.size() - 1);
		s->EndFigure(fill ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
		s->Close();
		auto b = Brush(rt, c); if (!b) return;
		if (fill) rt->FillGeometry(path.Get(), b.Get());
		else      rt->DrawGeometry(path.Get(), b.Get(), width, RoundCaps(rt).Get());
	}
	// Arc of a circle, angles in degrees, 0 = 3 o'clock, clockwise.
	inline void Arc(ID2D1RenderTarget* rt, float cx, float cy, float radius, float startDeg, float sweepDeg,
	                D2D1_COLOR_F c, float width = 6.0f)
	{
		if (sweepDeg <= 0.0f) return;
		if (sweepDeg >= 359.9f) { Ring(rt, cx, cy, radius, c, width); return; }
		float a0 = startDeg * kPi / 180.0f, a1 = (startDeg + sweepDeg) * kPi / 180.0f;
		auto path = Path(rt); if (!path) return;
		ComPtr<ID2D1GeometrySink> s;
		if (FAILED(path->Open(&s))) return;
		s->BeginFigure(D2D1::Point2F(cx + radius * cosf(a0), cy + radius * sinf(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
		s->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + radius * cosf(a1), cy + radius * sinf(a1)),
			D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
			sweepDeg > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
		s->EndFigure(D2D1_FIGURE_END_OPEN);
		s->Close();
		auto b = Brush(rt, c); if (b) rt->DrawGeometry(path.Get(), b.Get(), width, RoundCaps(rt).Get());
	}

	// --- text -------------------------------------------------------------
	struct Style {
		float                       size   = 14.0f;
		DWRITE_FONT_WEIGHT          weight = DWRITE_FONT_WEIGHT_NORMAL;
		DWRITE_TEXT_ALIGNMENT       align  = DWRITE_TEXT_ALIGNMENT_LEADING;
		DWRITE_PARAGRAPH_ALIGNMENT  valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
		const wchar_t*              family = L"Segoe UI";
		bool                        wrap   = false;

		Style& Size(float px)   { size = px; return *this; }
		Style& Bold()           { weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; return *this; }
		Style& Heavy()          { weight = DWRITE_FONT_WEIGHT_BOLD; return *this; }
		Style& Light()          { weight = DWRITE_FONT_WEIGHT_LIGHT; return *this; }
		Style& Center()         { align = DWRITE_TEXT_ALIGNMENT_CENTER; return *this; }
		Style& Right()          { align = DWRITE_TEXT_ALIGNMENT_TRAILING; return *this; }
		Style& Top()            { valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR; return *this; }
		Style& Bottom()         { valign = DWRITE_PARAGRAPH_ALIGNMENT_FAR; return *this; }
		Style& Mono()           { family = L"Consolas"; return *this; }
		Style& Icon()           { family = L"Segoe MDL2 Assets"; return *this; }   // glyphs: L"\xE80F" = home, ...
		Style& Wrap()           { wrap = true; return *this; }
	};

	// Small chevron: a "V" pointing down at 0°, rotated clockwise by angleDeg
	// (-90 = pointing right). The weight matches VCombo's.
	inline void Chevron(ID2D1RenderTarget* rt, float cx, float cy, float angleDeg, D2D1_COLOR_F c,
	                    float size = 4.5f, float width = 1.6f)
	{
		float a = angleDeg * kPi / 180.0f, ca = cosf(a), sa = sinf(a);
		auto P = [&](float x, float y) { return D2D1::Point2F(cx + x * ca - y * sa, cy + x * sa + y * ca); };
		float h = size * 0.6f;
		Polyline(rt, { P(-size, -h * 0.5f), P(0.0f, h * 0.5f + 1.0f), P(size, -h * 0.5f) }, c, width);
	}

	inline ComPtr<IDWriteTextFormat> Format(const Style& st) {
		ComPtr<IDWriteTextFormat> f;
		ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
			st.family, NULL, st.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			st.size, L"en-us", &f);
		if (f) {
			f->SetTextAlignment(st.align);
			f->SetParagraphAlignment(st.valign);
			f->SetWordWrapping(st.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
		}
		return f;
	}
	inline void Text(ID2D1RenderTarget* rt, const std::wstring& s, const D2D1_RECT_F& r, D2D1_COLOR_F c, const Style& st = Style()) {
		if (s.empty()) return;
		auto f = Format(st); auto b = Brush(rt, c);
		if (f && b) rt->DrawText(s.c_str(), (UINT32)s.size(), f.Get(), r, b.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
	inline float TextWidth(const std::wstring& s, const Style& st = Style()) {
		auto f = Format(st); if (!f || s.empty()) return 0.0f;
		ComPtr<IDWriteTextLayout> layout;
		ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
			s.c_str(), (UINT32)s.size(), f.Get(), 1e4f, 1e4f, &layout);
		if (!layout) return 0.0f;
		DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m);
		return m.widthIncludingTrailingWhitespace;
	}

	// Height of `s` wrapped to `width` in the given style (dialogs, bodies).
	inline float TextHeight(const std::wstring& s, float width, const Style& st = Style()) {
		auto f = Format(st); if (!f || s.empty()) return 0.0f;
		ComPtr<IDWriteTextLayout> layout;
		ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
			s.c_str(), (UINT32)s.size(), f.Get(), width, 1e4f, &layout);
		if (!layout) return 0.0f;
		DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m);
		return m.height;
	}

	// --- people -----------------------------------------------------------
	// "Grace Hopper" -> "GH", "Ada" -> "AD": what a person picture shows.
	inline std::wstring Initials(const std::wstring& name) {
		std::wstring out; bool start = true;
		for (wchar_t ch : name) {
			if (ch == L' ' || ch == L'.' || ch == L'-') { start = true; continue; }
			if (start && out.size() < 2) out += (wchar_t)towupper(ch);
			start = false;
		}
		if (out.size() == 1 && name.size() >= 2) out += (wchar_t)towupper(name[1]);
		return out;
	}
	// A stable colour for a name, from a palette that reads well under white text.
	inline D2D1_COLOR_F HashColor(const std::wstring& s) {
		static const uint32_t palette[8] = { 0x2563EB, 0x7C3AED, 0xDB2777, 0xEA580C, 0xCA8A04, 0x16A34A, 0x0891B2, 0x4F46E5 };
		uint32_t h = 2166136261u;
		for (wchar_t c : s) { h ^= (uint32_t)c; h *= 16777619u; }
		return Col(palette[h % 8]);
	}
	inline void Avatar(ID2D1RenderTarget* rt, float cx, float cy, float radius, const std::wstring& name) {
		Circle(rt, cx, cy, radius, HashColor(name));
		Text(rt, Initials(name), Rect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f), Col(0xFFFFFF), Style().Size(radius * 0.85f).Bold().Center());
	}

	// "12,345" / "1,234.5" — thousands separators, fixed decimals.
	inline std::wstring Num(double v, int decimals = 0) {
		wchar_t buf[64]; swprintf_s(buf, L"%.*f", decimals, v);
		std::wstring s(buf);
		size_t dot = s.find(L'.'); if (dot == std::wstring::npos) dot = s.size();
		for (size_t i = dot; i > 3 + (s[0] == L'-' ? 1u : 0u); ) { i -= 3; s.insert(i, L","); }
		return s;
	}

} // namespace vd
} // namespace ChronoUI
