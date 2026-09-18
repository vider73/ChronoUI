// =============================================================================
// Form.cpp — a data-entry form with validation, Tab order and a toast.
//
// What this example teaches:
//   * A real form: text fields, a dropdown, radio buttons, a stepper, a
//     multi-line notes box and a consent checkbox, with labels and inline
//     error messages that appear on Submit and clear as you fix them.
//   * Tab and Shift+Tab move between fields — that is VirtualWindow's own
//     focus cycling; nothing here had to be written for it.
//   * Every field's value is read into one plain struct (Customer) at the
//     moment of Submit; validation is a handful of plain functions.
//   * Feedback that doesn't block: a toast slides in, waits, fades — a chrome
//     widget with a tiny state machine in OnUpdate. Submitted records stack
//     up in a list on the right.
//
// Build target: Form. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VirtualChat.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg     = vd::Col(0xF7F8FA);
static const D2D1_COLOR_F kText   = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted  = vd::Col(0x6B7280);
static const D2D1_COLOR_F kAccent = vd::Col(0x4A90E2);
static const D2D1_COLOR_F kError  = vd::Col(0xDC2626);
static const D2D1_COLOR_F kOk     = vd::Col(0x059669);

struct Customer {
	std::wstring name, email, phone, country, plan, notes;
	int  seats = 1;
	bool terms = false;
};

// --- validation: plain functions over the struct ------------------------------
static bool ValidEmail(const std::wstring& s) {
	size_t at = s.find(L'@');
	return at != std::wstring::npos && at > 0 && s.find(L'.', at) != std::wstring::npos && s.back() != L'.';
}
static bool ValidPhone(const std::wstring& s) {
	int digits = 0;
	for (wchar_t c : s) { if (iswdigit(c)) ++digits; else if (c != L' ' && c != L'+' && c != L'-' && c != L'(' && c != L')') return false; }
	return digits >= 9;
}

// =============================================================================
// VField — the frame under an input: label above, error text below, a red
// ring while invalid. The input itself (VChatInput) is added AFTER it so it
// paints on top; this widget just owns the decoration.
// =============================================================================
class VField : public VirtualWidgetImpl {
	std::wstring m_label, m_error;
	bool m_required;
public:
	VField(std::wstring label, bool required) : m_label(std::move(label)), m_required(required) {}
	const char* GetTypeName() const override { return "VField"; }
	void SetError(const std::wstring& e) { m_error = e; }
	bool HasError() const { return !m_error.empty(); }
	// The rectangle the input should occupy inside this field.
	D2D1_RECT_F InputRect() const { return D2D1::RectF(m_bounds.left, m_bounds.top + 22.0f, m_bounds.right, m_bounds.bottom - 18.0f); }
	void OnDraw(ID2D1RenderTarget* rt) override {
		std::wstring l = m_label + (m_required ? L" *" : L"");
		vd::Text(rt, l, vd::Rect(m_bounds.left, m_bounds.top, vd::W(m_bounds), 20.0f), m_error.empty() ? kMuted : kError, vd::Style().Size(12).Bold().Top());
		if (!m_error.empty()) {
			vd::Stroke(rt, vd::Inset(InputRect(), -2.0f, -2.0f), vd::Alpha(kError, 0.55f), 10.0f, 2.0f);
			vd::Text(rt, m_error, vd::Rect(m_bounds.left, m_bounds.bottom - 17.0f, vd::W(m_bounds), 16.0f), kError, vd::Style().Size(11).Top());
		}
	}
};

// =============================================================================
// VToast — slides up from the bottom, waits, fades. Chrome, so it floats.
// =============================================================================
class VToast : public VirtualWidgetImpl {
	std::wstring m_text;
	float m_t = 99.0f;       // seconds since Show()
	D2D1_COLOR_F m_col = kOk;
public:
	const char* GetTypeName() const override { return "VToast"; }
	void Show(const std::wstring& text, D2D1_COLOR_F col) { m_text = text; m_col = col; m_t = 0.0f; SetVisible(true); }
	bool OnUpdate(float dt) override {
		if (!m_visible) return false;
		m_t += dt;
		if (m_t > 3.2f) { SetVisible(false); return true; }
		return true;
	}
	void OnDraw(ID2D1RenderTarget* rt) override {
		float in = vd::EaseOut(vd::Clamp01(m_t / 0.3f)), out = 1.0f - vd::Clamp01((m_t - 2.6f) / 0.6f);
		float a = in * out, dy = (1.0f - in) * 30.0f;
		D2D1_RECT_F r = D2D1::RectF(m_bounds.left, m_bounds.top + dy, m_bounds.right, m_bounds.bottom + dy);
		vd::Shadow(rt, r, 10.0f, 0.25f * a, 4);
		vd::Fill(rt, r, vd::Alpha(vd::Col(0x111827), 0.95f * a), 10.0f);
		vd::Circle(rt, r.left + 20.0f, vd::CY(r), 6.0f, vd::Alpha(m_col, a));
		vd::Text(rt, m_text, D2D1::RectF(r.left + 36.0f, r.top, r.right - 12.0f, r.bottom), vd::Alpha(vd::Col(0xF9FAFB), a), vd::Style().Size(13).Bold());
	}
};

// =============================================================================
// VRecordList — the submitted customers, newest first.
// =============================================================================
class VRecordList : public VirtualWidgetImpl {
	std::vector<Customer> m_items;
public:
	const char* GetTypeName() const override { return "VRecordList"; }
	void Add(const Customer& c) { m_items.insert(m_items.begin(), c); }
	size_t Count() const { return m_items.size(); }
	void OnDraw(ID2D1RenderTarget* rt) override {
		vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 14.0f);
		vd::Stroke(rt, m_bounds, vd::Col(0xE5E7EB), 14.0f);
		vd::Text(rt, L"SUBMITTED", vd::Rect(m_bounds.left + 18.0f, m_bounds.top + 12.0f, 200.0f, 18.0f), kMuted, vd::Style().Size(11).Bold().Top());
		if (m_items.empty()) {
			vd::Text(rt, L"Nothing yet. Fill in the form and press Save.", vd::Inset(m_bounds, 18.0f, 40.0f), kMuted, vd::Style().Size(13).Center().Wrap());
			return;
		}
		float y = m_bounds.top + 40.0f;
		rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_ALIASED);
		for (const Customer& c : m_items) {
			if (y + 64.0f > m_bounds.bottom) break;
			D2D1_RECT_F r = D2D1::RectF(m_bounds.left + 14.0f, y, m_bounds.right - 14.0f, y + 58.0f);
			vd::Fill(rt, r, kBg, 10.0f);
			std::wstring initials; for (size_t i = 0; i < c.name.size(); ++i) if (i == 0 || c.name[i - 1] == L' ') initials += (wchar_t)towupper(c.name[i]);
			vd::Circle(rt, r.left + 26.0f, vd::CY(r), 17.0f, kAccent);
			vd::Text(rt, initials.substr(0, 2), vd::Rect(r.left + 9.0f, vd::CY(r) - 10.0f, 34.0f, 20.0f), vd::Col(0xFFFFFF), vd::Style().Size(12).Bold().Center());
			vd::Text(rt, c.name, vd::Rect(r.left + 54.0f, r.top + 8.0f, vd::W(r) - 64.0f, 20.0f), kText, vd::Style().Size(13).Bold());
			vd::Text(rt, c.email + L"  \x00B7  " + c.plan + L", " + std::to_wstring(c.seats) + (c.seats == 1 ? L" seat" : L" seats") + L"  \x00B7  " + c.country,
				vd::Rect(r.left + 54.0f, r.top + 30.0f, vd::W(r) - 64.0f, 18.0f), kMuted, vd::Style().Size(11));
			y += 66.0f;
		}
		rt->PopAxisAlignedClip();
	}
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Form", 1120, 720)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };

	auto* heading = win.Add<VLabel>(); heading->Text(L"New customer").FontSize(20.0f).Color(kText);
	auto* subhead = win.Add<VLabel>(); subhead->Text(L"Required fields are marked *. Tab moves between fields; errors show on Save and clear as you type.").FontSize(12.0f).Color(kMuted);

	// Fields: the frame first (paints under), then the input (paints over).
	auto field = [&](const std::wstring& label, bool required, const std::wstring& placeholder, bool multiline = false) {
		VField* f = win.Add<VField>(label, required);
		VChatInput* in = win.Add<VChatInput>();
		in->SetPlaceholder(placeholder).SetSingleLine(!multiline);
		if (multiline) in->SetMaxLines(4);
		in->OnTextChanged([f, &win] { if (f->HasError()) { f->SetError(L""); InvalidateRect(win.GetHWND(), NULL, FALSE); } });
		return std::make_pair(f, in);
	};
	// (pairs unpacked by hand: C++17 lambdas cannot capture structured bindings)
	auto nameP  = field(L"Full name", true, L"e.g. Grace Hopper");   VField* nameF  = nameP.first;  VChatInput* nameIn  = nameP.second;
	auto emailP = field(L"Email", true, L"name@company.com");       VField* emailF = emailP.first; VChatInput* emailIn = emailP.second;
	auto phoneP = field(L"Phone", false, L"+34 600 000 000");        VField* phoneF = phoneP.first; VChatInput* phoneIn = phoneP.second;

	auto* countryL = win.Add<VLabel>(); countryL->Text(L"Country").FontSize(12.0f).Color(kMuted);
	auto* country  = win.Add<VCombo>();
	country->Items({ L"Spain", L"Portugal", L"France", L"Germany", L"Italy", L"United Kingdom", L"United States", L"Other" }).Select(0);

	auto* planL = win.Add<VLabel>(); planL->Text(L"Plan").FontSize(12.0f).Color(kMuted);
	auto* plan  = win.Add<VRadio>(std::vector<std::wstring>{ L"Starter", L"Pro", L"Enterprise" }, true);
	plan->Set(1);

	auto* seatsL = win.Add<VLabel>(); seatsL->Text(L"Seats").FontSize(12.0f).Color(kMuted);
	auto* seats  = win.Add<VStepper>(); seats->Range(1, 500).Set(5);

	auto notesP = field(L"Notes", false, L"Anything the onboarding team should know", true); VField* notesF = notesP.first; VChatInput* notesIn = notesP.second;

	auto* terms = win.Add<VCheck>(L"I accept the terms of service and the privacy policy");
	auto* termsErr = win.Add<VLabel>(); termsErr->FontSize(11.0f).Color(kError);

	auto* save  = win.Add<VButton>(); save->Text(L"Save customer");
	auto* clear = win.Add<VButton>(); clear->Text(L"Clear");
	clear->Face(vd::Col(0xE5E7EB)).FaceHover(vd::Col(0xD1D5DB)).FacePress(vd::Col(0x9CA3AF)).TextColor(kText);

	auto* list  = win.Add<VRecordList>();
	auto* toast = win.AddChrome<VToast>(); toast->SetVisible(false);

	auto clearForm = [&] {
		nameIn->SetText(L""); emailIn->SetText(L""); phoneIn->SetText(L""); notesIn->SetText(L"");
		nameF->SetError(L""); emailF->SetError(L""); phoneF->SetError(L""); termsErr->Text(L"");
		country->Select(0); plan->Set(1); seats->Set(5); terms->Set(false);
		win.SetFocusWidget(nameIn); repaint();
	};
	auto submit = [&] {
		Customer c;
		c.name = nameIn->GetText(); c.email = emailIn->GetText(); c.phone = phoneIn->GetText();
		c.country = country->SelectedText(); c.plan = plan->Text(); c.seats = seats->Value();
		c.notes = notesIn->GetText(); c.terms = terms->Value();

		bool ok = true;
		auto check = [&](VField* f, bool valid, const wchar_t* msg) { f->SetError(valid ? L"" : msg); ok = ok && valid; };
		check(nameF,  c.name.size() >= 2,                    L"Please enter the customer's name.");
		check(emailF, ValidEmail(c.email),                   L"That doesn't look like an email address.");
		check(phoneF, c.phone.empty() || ValidPhone(c.phone), L"Digits only, at least nine of them.");
		termsErr->Text(c.terms ? L"" : L"You need to accept the terms to continue.");
		ok = ok && c.terms;

		if (!ok) { toast->Show(L"Some fields need attention", kError); repaint(); return; }
		list->Add(c);
		toast->Show(L"Saved " + c.name + L" on the " + c.plan + L" plan", kOk);
		clearForm();
	};
	save->OnClick(submit);
	clear->OnClick(clearForm);

	auto layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		float x = 28.0f, formW = 520.0f, y = 84.0f;
		heading->SetBounds(vd::Rect(x, 18.0f, 400.0f, 28.0f));
		subhead->SetBounds(vd::Rect(x, 46.0f, W - 56.0f, 20.0f));

		auto place = [&](VField* f, VChatInput* in, float h) {
			f->SetBounds(vd::Rect(x, y, formW, h));
			D2D1_RECT_F ir = f->InputRect();
			in->SetBounds(ir);
			y += h + 6.0f;
		};
		place(nameF, nameIn, 78.0f);
		place(emailF, emailIn, 78.0f);
		// Phone and country share a row.
		float half = (formW - 16.0f) * 0.5f;
		phoneF->SetBounds(vd::Rect(x, y, half, 78.0f)); phoneIn->SetBounds(phoneF->InputRect());
		countryL->SetBounds(vd::Rect(x + half + 16.0f, y, half, 20.0f));
		country->SetBounds(vd::Rect(x + half + 16.0f, y + 22.0f, half, 38.0f));
		y += 84.0f;
		planL->SetBounds(vd::Rect(x, y, 200.0f, 20.0f));
		plan->SetBounds(vd::Rect(x, y + 22.0f, 320.0f, 30.0f));
		seatsL->SetBounds(vd::Rect(x + 360.0f, y, 100.0f, 20.0f));
		seats->SetBounds(vd::Rect(x + 360.0f, y + 20.0f, 130.0f, 34.0f));
		y += 66.0f;
		place(notesF, notesIn, 118.0f);
		terms->SetBounds(vd::Rect(x, y, formW, 26.0f)); y += 26.0f;
		termsErr->SetBounds(vd::Rect(x + 28.0f, y, formW, 16.0f)); y += 24.0f;
		save ->SetBounds(vd::Rect(x, y, 150.0f, 36.0f));
		clear->SetBounds(vd::Rect(x + 160.0f, y, 90.0f, 36.0f));

		list ->SetBounds(D2D1::RectF(x + formW + 36.0f, 84.0f, W - 28.0f, H - 28.0f));
		toast->SetBounds(vd::Rect(W * 0.5f - 200.0f, H - 68.0f, 400.0f, 44.0f));
		repaint();
	};
	win.OnResize(layout);
	layout();
	win.SetFocusWidget(nameIn);
	return win.RunMessageLoop();
}
