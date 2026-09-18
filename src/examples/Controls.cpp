// =============================================================================
// Controls.cpp — a settings form whose preview updates as you touch it.
//
// What this example teaches:
//   * The everyday controls — toggle, slider, checkbox, segmented picker,
//     progress bar — come from include/VControls.hpp, 30-60 lines each. Read
//     that header next to this file: a widget is OnDraw plus a couple of
//     mouse handlers, and that is the whole trick.
//   * State lives in one plain struct (Settings). Controls write into it
//     through callbacks; the preview reads it every frame. No binding
//     framework, no event bus — a pointer and a repaint.
//   * Small animations make controls feel finished: the toggle knob slides,
//     the checkbox tick pops, the preview eases towards new values.
//   * VChatInput from the framework doubles as a single-line text field, and
//     Tab moves the keyboard focus from control to control.
//
// Build target: Controls. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VirtualChat.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg    = vd::Col(0xF7F8FA);
static const D2D1_COLOR_F kText  = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted = vd::Col(0x6B7280);

static const D2D1_COLOR_F kAccents[4] = { vd::Col(0x3B82F6), vd::Col(0x8B5CF6), vd::Col(0x10B981), vd::Col(0xF43F5E) };
static const wchar_t* kAccentNames[4] = { L"Blue", L"Violet", L"Green", L"Rose" };

struct Settings {
	std::wstring name = L"Ada Lovelace";
	int   accent  = 0;
	float radius  = 0.45f;     // 0..1
	float size    = 0.5f;      // 0..1
	float opacity = 1.0f;      // 0..1
	bool  dark    = false;
	bool  shadow  = true;
	bool  bold    = true;
	bool  notify  = true;
};

// =============================================================================
// VPreview — a profile card that follows the Settings, with eased values.
// =============================================================================
class VPreview : public VirtualWidgetImpl {
	const Settings* m_s;
	float m_radius = 0, m_width = 0, m_opacity = 1.0f, m_dark = 0.0f;
public:
	explicit VPreview(const Settings* s) : m_s(s) {}
	const char* GetTypeName() const override { return "VPreview"; }
	bool OnUpdate(float dt) override {
		float tr = 4.0f + m_s->radius * 28.0f, tw = 220.0f + m_s->size * 180.0f, to = 0.25f + m_s->opacity * 0.75f, td = m_s->dark ? 1.0f : 0.0f;
		bool moving = fabsf(tr - m_radius) > 0.05f || fabsf(tw - m_width) > 0.2f || fabsf(to - m_opacity) > 0.002f || fabsf(td - m_dark) > 0.002f;
		m_radius = vd::Approach(m_radius, tr, dt, 12.0f); m_width = vd::Approach(m_width, tw, dt, 12.0f);
		m_opacity = vd::Approach(m_opacity, to, dt, 12.0f); m_dark = vd::Approach(m_dark, td, dt, 10.0f);
		return moving;
	}
	void OnDraw(ID2D1RenderTarget* rt) override {
		D2D1_COLOR_F bg = vd::Mix(vd::Col(0xEEF1F5), vd::Col(0x0F172A), m_dark);
		D2D1_COLOR_F cardCol = vd::Mix(vd::Col(0xFFFFFF), vd::Col(0x1E293B), m_dark);
		D2D1_COLOR_F ink = vd::Mix(kText, vd::Col(0xF1F5F9), m_dark);
		D2D1_COLOR_F sub = vd::Mix(kMuted, vd::Col(0x94A3B8), m_dark);
		D2D1_COLOR_F accent = kAccents[m_s->accent];
		vd::Fill(rt, m_bounds, bg, 14.0f);
		vd::Text(rt, L"PREVIEW", vd::Rect(m_bounds.left + 18.0f, m_bounds.top + 12.0f, 200.0f, 18.0f), sub, vd::Style().Size(11).Bold().Top());

		float h = 150.0f;
		D2D1_RECT_F card = vd::Rect(vd::CX(m_bounds) - m_width * 0.5f, vd::CY(m_bounds) - h * 0.5f, m_width, h);
		if (m_s->shadow) vd::Shadow(rt, card, m_radius, 0.22f * m_opacity, 5);
		vd::Fill(rt, card, vd::Alpha(cardCol, m_opacity), m_radius);
		vd::Fill(rt, D2D1::RectF(card.left, card.top, card.right, card.top + 6.0f), vd::Alpha(accent, m_opacity), m_radius);
		std::wstring initials;
		for (size_t i = 0; i < m_s->name.size(); ++i) if (i == 0 || m_s->name[i - 1] == L' ') initials += (wchar_t)towupper(m_s->name[i]);
		vd::Circle(rt, card.left + 44.0f, vd::CY(card), 24.0f, vd::Alpha(accent, m_opacity));
		vd::Text(rt, initials.substr(0, 2), vd::Rect(card.left + 20.0f, vd::CY(card) - 12.0f, 48.0f, 24.0f), vd::Alpha(vd::Col(0xFFFFFF), m_opacity), vd::Style().Size(15).Bold().Center());
		vd::Text(rt, m_s->name.empty() ? L"(no name)" : m_s->name, vd::Rect(card.left + 82.0f, vd::CY(card) - 26.0f, vd::W(card) - 98.0f, 26.0f),
			vd::Alpha(ink, m_opacity), m_s->bold ? vd::Style().Size(17).Bold() : vd::Style().Size(17));
		vd::Text(rt, L"Product designer", vd::Rect(card.left + 82.0f, vd::CY(card), vd::W(card) - 98.0f, 20.0f), vd::Alpha(sub, m_opacity), vd::Style().Size(12));
		std::wstring notif = m_s->notify ? L"\x25CF Notifications on" : L"\x25CB Notifications off";
		vd::Text(rt, notif, vd::Rect(card.left + 82.0f, vd::CY(card) + 22.0f, vd::W(card) - 98.0f, 20.0f),
			vd::Alpha(m_s->notify ? vd::Col(0x10B981) : sub, m_opacity), vd::Style().Size(11).Bold());

		wchar_t spec[128];
		swprintf_s(spec, L"radius %.0f px  \x00B7  width %.0f px  \x00B7  opacity %.0f%%  \x00B7  %s",
			m_radius, m_width, m_opacity * 100.0f, kAccentNames[m_s->accent]);
		vd::Text(rt, spec, vd::Rect(m_bounds.left, m_bounds.bottom - 34.0f, vd::W(m_bounds), 20.0f), sub, vd::Style().Size(11).Mono().Center());
	}
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Controls", 1080, 640)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };

	Settings s;

	// Label + control pairs, laid out by the loop in layout() below.
	struct Row { VLabel* label; IVirtualWidget* control; float height; };
	std::vector<Row> rows;
	auto row = [&](const std::wstring& text, IVirtualWidget* control, float height = 36.0f) {
		auto* l = win.Add<VLabel>(); l->Text(text).FontSize(13.0f).Color(kText);
		rows.push_back({ l, control, height });
	};

	auto* heading = win.Add<VLabel>(); heading->Text(L"Appearance").FontSize(20.0f).Color(kText);
	auto* subhead = win.Add<VLabel>(); subhead->Text(L"Every control writes into one struct; the preview reads it each frame. Tab moves between them.").FontSize(12.0f).Color(kMuted);

	auto* name = win.Add<VChatInput>();
	name->SetSingleLine(true).SetPlaceholder(L"Your name").SetText(s.name);
	name->OnTextChanged([&] { s.name = name->GetText(); repaint(); });
	row(L"Name", name);

	auto* accent = win.Add<VSegment>(std::vector<std::wstring>{ L"Blue", L"Violet", L"Green", L"Rose" });
	accent->Set(s.accent); accent->OnChange([&](int i) { s.accent = i; repaint(); });
	row(L"Accent", accent, 32.0f);

	auto* radius = win.Add<VSlider>(); radius->Set(s.radius).Format([](float v) { return vd::Num(4.0 + v * 28.0) + L" px"; });
	radius->OnChange([&](float v) { s.radius = v; repaint(); });
	row(L"Corner radius", radius);

	auto* size = win.Add<VSlider>(); size->Set(s.size).Format([](float v) { return vd::Num(220.0 + v * 180.0) + L" px"; });
	size->OnChange([&](float v) { s.size = v; repaint(); });
	row(L"Card width", size);

	auto* opacity = win.Add<VSlider>(); opacity->Set(s.opacity);
	opacity->OnChange([&](float v) { s.opacity = v; repaint(); });
	row(L"Opacity", opacity);

	auto* dark = win.Add<VToggle>(); dark->Set(s.dark); dark->OnChange([&](bool v) { s.dark = v; repaint(); });
	row(L"Dark preview", dark);

	auto* notify = win.Add<VToggle>(); notify->Set(s.notify); notify->OnChange([&](bool v) { s.notify = v; repaint(); });
	row(L"Notifications", notify);

	auto* shadow = win.Add<VCheck>(L"Drop shadow"); shadow->Set(s.shadow); shadow->OnChange([&](bool v) { s.shadow = v; repaint(); });
	auto* bold   = win.Add<VCheck>(L"Bold name");   bold->Set(s.bold);     bold->OnChange([&](bool v) { s.bold = v; repaint(); });
	row(L"", shadow, 30.0f);
	row(L"", bold, 30.0f);

	auto* progress = win.Add<VProgress>(L"Profile completeness");
	auto* resetBtn = win.Add<VButton>(); resetBtn->Text(L"Reset to defaults");
	resetBtn->Face(vd::Col(0xE5E7EB)).FaceHover(vd::Col(0xD1D5DB)).FacePress(vd::Col(0x9CA3AF)).TextColor(kText);
	resetBtn->OnClick([&] {
		s = Settings();
		name->SetText(s.name); accent->Set(s.accent); radius->Set(s.radius); size->Set(s.size); opacity->Set(s.opacity);
		dark->Set(s.dark); notify->Set(s.notify); shadow->Set(s.shadow); bold->Set(s.bold);
		repaint();
	});
	auto* preview = win.Add<VPreview>(&s);

	auto layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		float formW = 430.0f, x = 28.0f, labelW = 128.0f;
		heading->SetBounds(vd::Rect(x, 18.0f, formW, 28.0f));
		subhead->SetBounds(vd::Rect(x, 46.0f, formW + 260.0f, 20.0f));
		float y = 84.0f;
		for (Row& r : rows) {
			r.label->SetBounds(vd::Rect(x, y, labelW, r.height));
			r.control->SetBounds(vd::Rect(x + labelW, y, formW - labelW, r.height));
			y += r.height + 10.0f;
		}
		progress->SetBounds(vd::Rect(x, y + 6.0f, formW, 30.0f)); y += 50.0f;
		resetBtn->SetBounds(vd::Rect(x, y, 150.0f, 34.0f));
		preview->SetBounds(D2D1::RectF(x + formW + 40.0f, 24.0f, W - 28.0f, H - 24.0f));
		repaint();
	};
	win.OnResize(layout);

	// Completeness: a toy score so the bar has something to react to.
	win.OnTick([&](float) {
		float score = (std::min)(1.0f, (float)s.name.size() / 12.0f) * 0.4f;
		score += s.notify ? 0.2f : 0.0f;
		score += s.shadow ? 0.1f : 0.0f;
		score += s.accent != 0 ? 0.15f : 0.0f;
		score += s.dark ? 0.15f : 0.0f;
		progress->Set(score);
	});

	layout();
	win.SetFocusWidget(name);
	return win.RunMessageLoop();
}
