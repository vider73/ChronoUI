// =============================================================================
// Dashboard.cpp — a live dashboard in four small widgets.
//
// What this example teaches:
//   * A widget is a class with OnDraw() and, if it moves, OnUpdate(dt).
//     Every panel here (VStatCard, VLineChart, VRing, VBars) is 40-70 lines.
//   * Animation is "remember where you were, ease towards where you are
//     going". Each widget keeps `from`, `to` and a 0..1 clock; OnUpdate
//     advances the clock and returns true while it is still moving, and the
//     host repaints only while some widget says so.
//   * Real data with no dependencies: the two rings read CPU load
//     (GetSystemTimes) and memory load (GlobalMemoryStatusEx) every half
//     second from the window's OnTick hook.
//   * Layout is one function of the client size, called on resize.
//
// Build target: Dashboard. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

// --- palette ---------------------------------------------------------------
static const D2D1_COLOR_F kBg     = vd::Col(0x0B1220);
static const D2D1_COLOR_F kCard   = vd::Col(0x121B2E);
static const D2D1_COLOR_F kBorder = vd::Col(0x1E293B);
static const D2D1_COLOR_F kText   = vd::Col(0xE5E7EB);
static const D2D1_COLOR_F kMuted  = vd::Col(0x8B93A7);
static const D2D1_COLOR_F kAccent = vd::Col(0x60A5FA);
static const D2D1_COLOR_F kGreen  = vd::Col(0x34D399);
static const D2D1_COLOR_F kAmber  = vd::Col(0xFBBF24);
static const D2D1_COLOR_F kRed    = vd::Col(0xF87171);

static float Rand(float lo, float hi) {
	static std::mt19937 g(7);
	return std::uniform_real_distribution<float>(lo, hi)(g);
}

// Card chrome shared by every panel: fill, hairline border, title.
static D2D1_RECT_F CardFrame(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const std::wstring& title) {
	vd::Fill(rt, r, kCard, 12.0f);
	vd::Stroke(rt, r, kBorder, 12.0f);
	D2D1_RECT_F in = vd::Inset(r, 18.0f, 14.0f);
	vd::Text(rt, title, vd::Rect(in.left, in.top, vd::W(in), 20.0f), kMuted, vd::Style().Size(12).Bold().Top());
	return in;
}

// =============================================================================
// VStatCard — a KPI: title, big number that counts up, delta pill, sparkline.
// =============================================================================
class VStatCard : public VirtualWidgetImpl {
	std::wstring m_title, m_prefix, m_suffix;
	int   m_decimals = 0;
	float m_from = 0, m_to = 0, m_shown = 0, m_t = 1.0f, m_delta = 0;
	std::vector<float> m_spark;
public:
	VStatCard(std::wstring title, std::wstring prefix, std::wstring suffix, int decimals)
		: m_title(std::move(title)), m_prefix(std::move(prefix)), m_suffix(std::move(suffix)), m_decimals(decimals) {}
	const char* GetTypeName() const override { return "VStatCard"; }

	void Set(float value, float deltaPct, std::vector<float> spark) {
		m_from = m_shown; m_to = value; m_delta = deltaPct; m_spark = std::move(spark); m_t = 0.0f;
	}
	bool OnUpdate(float dt) override {
		if (m_t >= 1.0f) return false;
		m_t = (std::min)(1.0f, m_t + dt / 0.8f);
		m_shown = vd::Lerp(m_from, m_to, vd::EaseOut(m_t));
		return true;
	}
	void OnDraw(ID2D1RenderTarget* rt) override {
		D2D1_RECT_F in = CardFrame(rt, m_bounds, m_title);
		vd::Text(rt, m_prefix + vd::Num(m_shown, m_decimals) + m_suffix,
			vd::Rect(in.left, in.top + 22.0f, vd::W(in), 40.0f), kText, vd::Style().Size(28).Bold().Top());

		bool up = m_delta >= 0.0f;
		wchar_t buf[32]; swprintf_s(buf, L"%s%.1f%%", up ? L"\x25B2 " : L"\x25BC ", fabsf(m_delta));
		vd::Style pillStyle = vd::Style().Size(11).Bold().Center();
		float pw = vd::TextWidth(buf, pillStyle) + 16.0f;
		D2D1_RECT_F pill = vd::Rect(in.left, in.bottom - 22.0f, pw, 20.0f);
		vd::Fill(rt, pill, vd::Alpha(up ? kGreen : kRed, 0.18f), 10.0f);
		vd::Text(rt, buf, pill, up ? kGreen : kRed, pillStyle);

		// Sparkline, revealed left to right with the same clock as the number.
		if (m_spark.size() > 1) {
			D2D1_RECT_F area = vd::Rect(in.left + pw + 18.0f, in.bottom - 30.0f, vd::W(in) - pw - 18.0f, 28.0f);
			float lo = *std::min_element(m_spark.begin(), m_spark.end());
			float hi = *std::max_element(m_spark.begin(), m_spark.end());
			size_t n = (size_t)(2 + (m_spark.size() - 2) * vd::EaseOut(m_t));
			std::vector<D2D1_POINT_2F> pts;
			for (size_t i = 0; i < n; ++i) {
				float x = area.left + vd::W(area) * (float)i / (float)(m_spark.size() - 1);
				float y = area.bottom - vd::H(area) * (hi > lo ? (m_spark[i] - lo) / (hi - lo) : 0.5f);
				pts.push_back(D2D1::Point2F(x, y));
			}
			vd::Polyline(rt, pts, up ? kGreen : kRed, 2.0f);
		}
	}
};

// =============================================================================
// VLineChart — 24 hourly values, area + line, morphs between data sets.
// =============================================================================
class VLineChart : public VirtualWidgetImpl {
	std::vector<float> m_from, m_to;
	float m_t = 1.0f;
public:
	const char* GetTypeName() const override { return "VLineChart"; }
	void Set(std::vector<float> v) {
		m_from = m_to.empty() ? std::vector<float>(v.size(), 0.0f) : m_to;
		m_to = std::move(v); m_t = 0.0f;
	}
	bool OnUpdate(float dt) override {
		if (m_t >= 1.0f) return false;
		m_t = (std::min)(1.0f, m_t + dt / 0.9f);
		return true;
	}
	void OnDraw(ID2D1RenderTarget* rt) override {
		D2D1_RECT_F in = CardFrame(rt, m_bounds, L"REQUESTS PER HOUR");
		if (m_to.size() < 2) return;
		D2D1_RECT_F plot = D2D1::RectF(in.left + 36.0f, in.top + 32.0f, in.right - 8.0f, in.bottom - 24.0f);
		float e = vd::EaseOut(m_t);
		float hi = 1.0f;
		for (float v : m_to) hi = (std::max)(hi, v);
		hi *= 1.15f;

		// Grid with axis labels.
		for (int g = 0; g <= 4; ++g) {
			float y = plot.bottom - vd::H(plot) * (float)g / 4.0f;
			vd::Line(rt, plot.left, y, plot.right, y, vd::Col(0xFFFFFF, 0.06f));
			vd::Text(rt, vd::Num(hi * (float)g / 4.0f), vd::Rect(in.left, y - 8.0f, 30.0f, 16.0f), kMuted, vd::Style().Size(10).Right());
		}
		std::vector<D2D1_POINT_2F> line, area;
		area.push_back(D2D1::Point2F(plot.left, plot.bottom));
		for (size_t i = 0; i < m_to.size(); ++i) {
			float v = vd::Lerp(m_from[i], m_to[i], e);
			D2D1_POINT_2F p = D2D1::Point2F(plot.left + vd::W(plot) * (float)i / (float)(m_to.size() - 1),
			                                plot.bottom - vd::H(plot) * (v / hi));
			line.push_back(p); area.push_back(p);
			if (i % 4 == 0) {
				wchar_t h[8]; swprintf_s(h, L"%02u:00", (unsigned)i);
				vd::Text(rt, h, vd::Rect(p.x - 20.0f, plot.bottom + 4.0f, 40.0f, 16.0f), kMuted, vd::Style().Size(10).Center());
			}
		}
		area.push_back(D2D1::Point2F(plot.right, plot.bottom));
		vd::Polyline(rt, area, vd::Alpha(kAccent, 0.18f), 0.0f, true);
		vd::Polyline(rt, line, kAccent, 2.5f);
		vd::Circle(rt, line.back().x, line.back().y, 5.0f, kAccent);
		vd::Circle(rt, line.back().x, line.back().y, 2.5f, kBg);
	}
};

// =============================================================================
// VRing — a percentage as an arc; colour follows the value.
// =============================================================================
class VRing : public VirtualWidgetImpl {
	std::wstring m_title, m_sub;
	float m_target = 0, m_shown = 0;
public:
	VRing(std::wstring title, std::wstring sub) : m_title(std::move(title)), m_sub(std::move(sub)) {}
	const char* GetTypeName() const override { return "VRing"; }
	void SetPercent(float p) { m_target = vd::Clamp(p, 0.0f, 100.0f); }
	bool OnUpdate(float dt) override {
		if (fabsf(m_target - m_shown) < 0.05f) return false;
		m_shown = vd::Approach(m_shown, m_target, dt, 6.0f);
		return true;
	}
	void OnDraw(ID2D1RenderTarget* rt) override {
		D2D1_RECT_F in = CardFrame(rt, m_bounds, m_title);
		float cx = in.left + 46.0f, cy = vd::CY(in) + 10.0f, r = 38.0f;
		D2D1_COLOR_F c = m_shown < 60.0f ? kGreen : (m_shown < 85.0f ? kAmber : kRed);
		vd::Ring(rt, cx, cy, r, vd::Col(0xFFFFFF, 0.08f), 8.0f);
		vd::Arc(rt, cx, cy, r, -90.0f, 360.0f * m_shown / 100.0f, c, 8.0f);
		vd::Text(rt, vd::Num(m_shown) + L"%", vd::Rect(cx - r, cy - 12.0f, r * 2.0f, 24.0f), kText, vd::Style().Size(16).Bold().Center());
		vd::Text(rt, m_sub, vd::Rect(cx + r + 18.0f, cy - 20.0f, in.right - cx - r - 18.0f, 40.0f), kMuted, vd::Style().Size(12).Wrap());
	}
};

// =============================================================================
// VBars — seven bars that grow in, with a hover readout.
// =============================================================================
class VBars : public VirtualWidgetImpl {
	std::vector<float> m_vals;
	float m_t = 1.0f;
	int   m_hover = -1;
	static constexpr const wchar_t* kDays[7] = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
	D2D1_RECT_F Plot() const { return D2D1::RectF(m_bounds.left + 18.0f, m_bounds.top + 44.0f, m_bounds.right - 18.0f, m_bounds.bottom - 34.0f); }
	int BarAt(float x) const {
		D2D1_RECT_F p = Plot(); if (x < p.left || x >= p.right) return -1;
		return (int)((x - p.left) / (vd::W(p) / 7.0f));
	}
public:
	const char* GetTypeName() const override { return "VBars"; }
	void Set(std::vector<float> v) { m_vals = std::move(v); m_t = 0.0f; }
	bool OnUpdate(float dt) override {
		if (m_t >= 1.0f) return false;
		m_t = (std::min)(1.0f, m_t + dt / 0.7f);
		return true;
	}
	VInputResult OnMouseMove(float x, float) override {
		int h = BarAt(x); if (h == m_hover) return VInputResult::NotHandled;
		m_hover = h; return VInputResult::Handled;
	}
	VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
	void OnDraw(ID2D1RenderTarget* rt) override {
		CardFrame(rt, m_bounds, L"ORDERS THIS WEEK");
		if (m_vals.size() != 7) return;
		D2D1_RECT_F p = Plot();
		float hi = 1.0f; for (float v : m_vals) hi = (std::max)(hi, v);
		float slot = vd::W(p) / 7.0f;
		for (int i = 0; i < 7; ++i) {
			// Each bar starts a little after the previous one — a stagger.
			float local = vd::EaseOut(vd::Clamp01(m_t * 1.6f - (float)i * 0.08f));
			float h = vd::H(p) * (m_vals[i] / hi) * local;
			D2D1_RECT_F bar = D2D1::RectF(p.left + slot * (float)i + slot * 0.22f, p.bottom - h,
			                              p.left + slot * ((float)i + 1.0f) - slot * 0.22f, p.bottom);
			vd::Fill(rt, bar, i == m_hover ? kAccent : vd::Alpha(kAccent, 0.55f), 6.0f);
			vd::Text(rt, kDays[i], vd::Rect(bar.left - 10.0f, p.bottom + 6.0f, vd::W(bar) + 20.0f, 18.0f), kMuted, vd::Style().Size(11).Center());
			if (i == m_hover)
				vd::Text(rt, vd::Num(m_vals[i]), vd::Rect(bar.left - 20.0f, bar.top - 22.0f, vd::W(bar) + 40.0f, 18.0f), kText, vd::Style().Size(12).Bold().Center());
		}
	}
};

// =============================================================================
// Live system numbers, no dependencies.
// =============================================================================
static float CpuLoadPercent() {
	static ULONGLONG lastIdle = 0, lastTotal = 0;
	FILETIME i, k, u;
	if (!GetSystemTimes(&i, &k, &u)) return 0.0f;
	auto ull = [](FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; };
	ULONGLONG idle = ull(i), total = ull(k) + ull(u);
	float pct = 0.0f;
	if (lastTotal && total > lastTotal) pct = 100.0f * (1.0f - (float)(idle - lastIdle) / (float)(total - lastTotal));
	lastIdle = idle; lastTotal = total;
	return vd::Clamp(pct, 0.0f, 100.0f);
}
static float MemoryLoadPercent() {
	MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
	return GlobalMemoryStatusEx(&ms) ? (float)ms.dwMemoryLoad : 0.0f;
}

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Dashboard", 1180, 780)) return 1;
	win.SetBackground(kBg);

	// Header.
	auto* title = win.Add<VLabel>();    title->Text(L"Operations overview").FontSize(20.0f).Color(kText);
	auto* sub   = win.Add<VLabel>();    sub->Text(L"Synthetic traffic, real CPU and memory. Pick a range or refresh.").FontSize(12.0f).Color(kMuted);
	auto* range = win.Add<VCombo>();
	range->Items({ L"Today", L"Last 7 days", L"Last 30 days" }).Select(0)
	     .Face(kCard).FaceHover(kBorder).Border(kBorder).TextColor(kText);
	auto* refresh = win.Add<VButton>(); refresh->Text(L"Refresh");
	refresh->Face(kAccent).FaceHover(vd::Col(0x3B82F6)).FacePress(vd::Col(0x2563EB));

	// Panels.
	auto* revenue = win.Add<VStatCard>(L"REVENUE", L"\x20AC", L"", 0);
	auto* orders  = win.Add<VStatCard>(L"ORDERS", L"", L"", 0);
	auto* visits  = win.Add<VStatCard>(L"VISITORS", L"", L"", 0);
	auto* conv    = win.Add<VStatCard>(L"CONVERSION", L"", L"%", 2);
	auto* chart   = win.Add<VLineChart>();
	auto* cpu     = win.Add<VRing>(L"CPU", L"This machine, sampled every 500 ms");
	auto* mem     = win.Add<VRing>(L"MEMORY", L"Physical memory in use");
	auto* bars    = win.Add<VBars>();

	// Data: everything except the rings is generated; the range only changes
	// the scale so the morph between data sets is visible.
	auto regenerate = [&](size_t rangeIdx) {
		float scale = rangeIdx == 0 ? 1.0f : (rangeIdx == 1 ? 6.4f : 27.0f);
		auto spark = [&](float base) { std::vector<float> s; for (int i = 0; i < 12; ++i) s.push_back(base * Rand(0.7f, 1.3f)); return s; };
		revenue->Set(Rand(8000.0f, 14000.0f) * scale, Rand(-6.0f, 14.0f), spark(1.0f));
		orders ->Set(Rand(140.0f, 260.0f) * scale, Rand(-4.0f, 10.0f), spark(1.0f));
		visits ->Set(Rand(4000.0f, 9000.0f) * scale, Rand(-8.0f, 18.0f), spark(1.0f));
		conv   ->Set(Rand(2.2f, 4.8f), Rand(-1.0f, 1.5f), spark(1.0f));
		std::vector<float> hours;
		for (int h = 0; h < 24; ++h) {
			float day = 0.35f + 0.65f * sinf(((float)h - 5.0f) / 24.0f * vd::kPi);   // quiet at night
			hours.push_back((std::max)(20.0f, 900.0f * scale * day * Rand(0.8f, 1.2f)));
		}
		chart->Set(hours);
		std::vector<float> week; for (int d = 0; d < 7; ++d) week.push_back(Rand(60.0f, 200.0f) * scale * (d >= 5 ? 0.6f : 1.0f));
		bars->Set(week);
	};
	range->OnChange([&](size_t i) { regenerate(i); });
	refresh->OnClick([&] { regenerate(range->SelectedIndex()); });

	// Layout: one function of the client size.
	auto layout = [&] {
		RECT rc; GetClientRect(win.GetHWND(), &rc);
		float W = (float)rc.right, H = (float)rc.bottom, pad = 20.0f, gap = 16.0f;
		title->SetBounds(vd::Rect(pad, 14.0f, 500.0f, 28.0f));
		sub  ->SetBounds(vd::Rect(pad, 42.0f, 700.0f, 20.0f));
		refresh->SetBounds(vd::Rect(W - pad - 96.0f, 22.0f, 96.0f, 34.0f));
		range  ->SetBounds(vd::Rect(W - pad - 96.0f - 12.0f - 150.0f, 22.0f, 150.0f, 34.0f));

		float top = 76.0f, cardH = 124.0f;
		float cardW = (W - pad * 2.0f - gap * 3.0f) / 4.0f;
		VStatCard* cards[4] = { revenue, orders, visits, conv };
		for (int i = 0; i < 4; ++i) cards[i]->SetBounds(vd::Rect(pad + (cardW + gap) * (float)i, top, cardW, cardH));

		float midTop = top + cardH + gap, midH = (H - midTop - pad - gap) * 0.58f;
		float chartW = (W - pad * 2.0f - gap) * 0.66f;
		chart->SetBounds(vd::Rect(pad, midTop, chartW, midH));
		float ringW = W - pad * 2.0f - gap - chartW, ringH = (midH - gap) * 0.5f;
		cpu->SetBounds(vd::Rect(pad + chartW + gap, midTop, ringW, ringH));
		mem->SetBounds(vd::Rect(pad + chartW + gap, midTop + ringH + gap, ringW, ringH));

		float botTop = midTop + midH + gap;
		bars->SetBounds(vd::Rect(pad, botTop, W - pad * 2.0f, H - botTop - pad));
		InvalidateRect(win.GetHWND(), NULL, FALSE);
	};
	win.OnResize(layout);

	// Sample the machine twice a second from the heartbeat.
	float clock = 0.6f;
	win.OnTick([&](float dt) {
		clock += dt;
		if (clock < 0.5f) return;
		clock = 0.0f;
		cpu->SetPercent(CpuLoadPercent());
		mem->SetPercent(MemoryLoadPercent());
	});

	layout();
	regenerate(0);
	return win.RunMessageLoop();
}
