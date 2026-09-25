// =============================================================================
// Booking.cpp — a hotel stay, on the widgets that came with v3.
//
// A split view holds the filters (a calendar, a number box, repeat buttons, a
// toggle, a slider); the content is a two-pane view: a picture gallery (a
// selector bar over a flip view with a pips pager) beside the details (a
// pivot over a rich-text overview, a scrolling list of reviews and a zoomable
// map in a scroll viewer). A toggle split button, a command bar flyout, an
// animated heart, icons for the amenities, a shape for the price tag, an info
// bar that appears when the stay goes over budget, and a dialog to book.
//
// Build target: Booking. Headers only.
// =============================================================================

#include <windows.h>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VNavigation.hpp"
#include "VCollections.hpp"
#include "VActions.hpp"
#include "VIndicators.hpp"
#include "VText.hpp"
#include "VMedia.hpp"
#include "VLayout.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg    = vd::Col(0xF3F3F3);
static const D2D1_COLOR_F kText  = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted = vd::Col(0x6B7280);

class VCard : public VirtualWidgetImpl {
public:
	const char* GetTypeName() const override { return "VCard"; }
	void OnDraw(ID2D1RenderTarget* rt) override { vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 10.0f); vd::Stroke(rt, m_bounds, vd::Col(0xE5E7EB), 10.0f, 1.0f); }
};

struct Room { const wchar_t* name; double rate; };
static const Room kRooms[3] = { { L"Standard", 96.0 }, { L"Deluxe", 148.0 }, { L"Suite", 260.0 } };

// Four painted pictures per room: the view, the bed, the pool, the terrace.
static void PaintPicture(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int room, int page) {
	float hue = 200.0f + (float)room * 25.0f;
	switch (page) {
		case 0: {   // the sea at sunset
			vd::Gradient(rt, r, vd::FromHSV(hue + 20.0f, 0.55f, 0.95f), vd::FromHSV(30.0f, 0.7f, 0.95f));
			vd::Circle(rt, r.right - vd::W(r) * 0.3f, r.top + vd::H(r) * 0.42f, vd::H(r) * 0.11f, vd::Col(0xFDE68A));
			vd::Fill(rt, D2D1::RectF(r.left, r.top + vd::H(r) * 0.55f, r.right, r.bottom), vd::FromHSV(hue, 0.6f, 0.55f));
			for (int k = 0; k < 14; ++k) vd::Line(rt, r.left + 20.0f + (float)(k * 37 % 90), r.top + vd::H(r) * 0.6f + (float)k * 12.0f, r.left + 90.0f + (float)(k * 53 % 160), r.top + vd::H(r) * 0.6f + (float)k * 12.0f, vd::Col(0xFFFFFF, 0.35f), 1.5f);
			break;
		}
		case 1: {   // the bed
			vd::Fill(rt, r, vd::Col(0xF5EFE6));
			vd::Fill(rt, D2D1::RectF(r.left, r.top + vd::H(r) * 0.62f, r.right, r.bottom), vd::Col(0xD6CCC2));
			D2D1_RECT_F bed = vd::Rect(r.left + vd::W(r) * 0.2f, r.top + vd::H(r) * 0.42f, vd::W(r) * 0.6f, vd::H(r) * 0.35f);
			vd::Fill(rt, bed, vd::Col(0xFFFFFF), 6.0f);
			vd::Fill(rt, vd::Rect(bed.left, bed.top - 18.0f, vd::W(bed), 22.0f), vd::FromHSV(hue, 0.35f, 0.6f), 4.0f);
			vd::Fill(rt, vd::Rect(bed.left + 12.0f, bed.top + 8.0f, vd::W(bed) * 0.4f, 18.0f), vd::Col(0xF3F4F6), 4.0f);
			vd::Fill(rt, vd::Rect(bed.right - 12.0f - vd::W(bed) * 0.4f, bed.top + 8.0f, vd::W(bed) * 0.4f, 18.0f), vd::Col(0xF3F4F6), 4.0f);
			break;
		}
		case 2: {   // the pool
			vd::Fill(rt, r, vd::Col(0xE7E5E4));
			D2D1_RECT_F pool = vd::Inset(r, vd::W(r) * 0.15f, vd::H(r) * 0.2f);
			vd::Fill(rt, pool, vd::Col(0x38BDF8), 24.0f);
			for (int k = 0; k < 6; ++k) vd::Ring(rt, vd::CX(pool) + (float)(k * 61 % 140) - 70.0f, vd::CY(pool) + (float)(k * 37 % 60) - 30.0f, 12.0f + (float)k * 5.0f, vd::Col(0xFFFFFF, 0.25f), 1.5f);
			break;
		}
		default: {  // the terrace
			vd::Gradient(rt, r, vd::Col(0xBAE6FD), vd::Col(0xF0F9FF));
			vd::Fill(rt, D2D1::RectF(r.left, r.top + vd::H(r) * 0.66f, r.right, r.bottom), vd::Col(0xA8A29E));
			for (int k = 0; k < 3; ++k) {
				float x = r.left + vd::W(r) * (0.22f + 0.28f * (float)k), y = r.top + vd::H(r) * 0.66f;
				vd::Fill(rt, vd::Rect(x - 22.0f, y - 20.0f, 44.0f, 6.0f), vd::Col(0x78350F), 2.0f);
				vd::Fill(rt, vd::Rect(x - 3.0f, y - 20.0f, 6.0f, 22.0f), vd::Col(0x78350F));
				vd::Circle(rt, x, y - 44.0f, 26.0f, vd::FromHSV(hue - 40.0f, 0.7f, 0.9f));
			}
			break;
		}
	}
	vd::Text(rt, std::wstring(kRooms[room].name) + (page == 0 ? L" \x2014 sea view" : page == 1 ? L" \x2014 bedroom" : page == 2 ? L" \x2014 pool" : L" \x2014 terrace"),
	         vd::Rect(r.left + 16.0f, r.bottom - 36.0f, vd::W(r) - 32.0f, 24.0f), vd::Col(0xFFFFFF), vd::Style().Size(14).Bold());
}

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Booking", 1220, 780)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };
	auto label = [&](const std::wstring& text, float size = 13.0f, D2D1_COLOR_F col = kText) {
		VLabel* l = win.Add<VLabel>(); l->Text(text).FontSize(size).Color(col); return l;
	};
	std::vector<VFlyout*> flyouts;

	// --- state ------------------------------------------------------------------
	int room = 1, nights = 3;
	VDate checkIn = vdate::AddDays(vdate::Today(), 14);
	double total = 0.0;

	// --- header --------------------------------------------------------------------
	auto* title = label(L"Sea View Hotel", 22.0f);
	auto* sub   = label(L"Cala Blanca \x00B7 4 stars \x00B7 240 m from the beach", 12.0f, kMuted);
	auto* heart = win.Add<VAnimatedIcon>(L"\xEB52"); heart->Size(20.0f).Accent(vd::Col(0xDC2626));
	auto* status = label(L"", 12.0f, kMuted);
	bool favourite = false;
	heart->OnClick([&] { favourite = !favourite; heart->Color(favourite ? vd::Col(0xDC2626) : kText); status->Text(favourite ? L"Saved to favourites." : L"Removed from favourites."); repaint(); });
	auto* notify = win.AddChrome<VToggleSplitButton>(L"Notify me"); notify->Attach(&win); flyouts.push_back(notify);
	notify->Add(L"By e-mail", L"\xE715").Add(L"By SMS", L"\xE8BD").Add(L"Push", L"\xEA8F");
	notify->OnChange([&](bool on) { status->Text(on ? L"You will hear about price drops." : L"Notifications off."); repaint(); });
	notify->OnPick([&](int i) { notify->Set(true); status->Text(L"Notifications: " + notify->At(i).label); repaint(); });
	auto* book = win.Add<VButton>(); book->Text(L"Book now");

	// --- the filters pane ---------------------------------------------------------
	auto* splitv = win.Add<VSplitView>(); splitv->SetMode(VSplitView::Mode::CompactInline).PaneWidth(276.0f);
	auto* burger = win.Add<VButton>(); burger->Text(L"\x2630").Face(vd::Col(0x000000, 0.0f)).FaceHover(vd::Col(0x000000, 0.06f)).FacePress(vd::Col(0x000000, 0.1f)).TextColor(kText);
	burger->OnClick([&] { splitv->Toggle(); });
	auto* fGuests = label(L"GUESTS", 11.0f, kMuted);
	auto* guests = win.Add<VNumberBox>(); guests->Range(1.0, 6.0).Set(2.0);
	auto* fIn = label(L"CHECK-IN", 11.0f, kMuted);
	auto* cal = win.Add<VCalendarView>(); cal->Set(checkIn);
	auto* fNights = label(L"NIGHTS", 11.0f, kMuted);
	auto* minus = win.Add<VRepeatButton>(); minus->Text(L"\x2212").Face(vd::Col(0xE9E9E9)).FaceHover(vd::Col(0xDCDCDC)).FacePress(vd::Col(0xCFCFCF)).TextColor(kText);
	auto* plus  = win.Add<VRepeatButton>(); plus->Text(L"+").Face(vd::Col(0xE9E9E9)).FaceHover(vd::Col(0xDCDCDC)).FacePress(vd::Col(0xCFCFCF)).TextColor(kText);
	auto* nightsLbl = label(L"3", 16.0f);
	auto* fBreakfast = label(L"BREAKFAST", 11.0f, kMuted);
	auto* breakfast = win.Add<VToggle>(); breakfast->Set(true);
	auto* fBudget = label(L"BUDGET", 11.0f, kMuted);
	auto* budget = win.Add<VSlider>(); budget->Set(0.5f).Format([](float v) { return L"\x20AC " + vd::Num(200.0 + 1800.0 * v); });
	// the compact strip: one icon per filter
	std::vector<VIcon*> compactIcons;
	for (const wchar_t* g : { L"\xE716", L"\xE787", L"\xE708", L"\xEC32", L"\xE825" }) { auto* ic = win.Add<VIcon>(g); ic->Size(16.0f).Color(kMuted); compactIcons.push_back(ic); }

	// --- gallery (pane 1) -----------------------------------------------------------
	auto* two = win.Add<VTwoPaneView>(); two->Threshold(900.0f).Split(0.5f);
	auto* galleryCard = win.Add<VCard>();
	auto* rooms = win.Add<VSelectorBar>(std::vector<VSelectorBar::Item>{ { L"\xE7C3", L"Standard" }, { L"\xE734", L"Deluxe" }, { L"\xE8D6", L"Suite" } }); rooms->Select(room);
	auto* flip = win.Add<VFlipView>(); flip->Count(4);
	flip->Paint([&](ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int i) { PaintPicture(rt, r, room, i); });
	auto* pips = win.Add<VPipsPager>(); pips->Count(4).Arrows(true);
	flip->OnChange([&](int i) { pips->Select(i); });
	pips->OnChange([&](int i) { flip->Select(i); });
	auto* share = win.Add<VAnimatedIcon>(L"\xE72D"); share->Size(16.0f);
	auto* shareFly = win.AddChrome<VCommandBarFlyout>(); shareFly->Attach(&win); flyouts.push_back(shareFly);
	shareFly->Add(L"\xE8C8", L"Copy link").Add(L"\xE725", L"Share").Add(L"\xE74E", L"Save picture").AddSecondary(L"Report a problem", L"\xE7BA").AddSecondary(L"Open in browser", L"\xE774");
	share->OnClick([&] { shareFly->Show(share->GetBounds()); });
	shareFly->OnPick([&](int i) { status->Text(L"Gallery: " + shareFly->At(i).label); repaint(); });
	auto* priceTag = win.Add<VShape>(VShape::Kind::Polygon);
	priceTag->Points({ { 0.0f, 0.5f }, { 0.22f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.22f, 1.0f } }).Fill(vd::Col(0x111827, 0.85f));
	auto* priceLbl = label(L"", 13.0f, vd::Col(0xFFFFFF));

	// --- details (pane 2) -----------------------------------------------------------
	auto* detailsCard = win.Add<VCard>();
	auto* pivot = win.Add<VPivot>(std::vector<std::wstring>{ L"Overview", L"Reviews", L"Map" });
	std::vector<VIcon*> amenities;
	for (const wchar_t* g : { L"\xE701", L"\xEB6C", L"\xE7EC", L"\xECAD", L"\xE8FD" }) { auto* ic = win.Add<VIcon>(g); ic->Size(16.0f).Color(vd::Col(0x2563EB)); amenities.push_back(ic); }
	auto* amenLbl = label(L"Wi-Fi \x00B7 parking \x00B7 pool \x00B7 spa \x00B7 restaurant", 12.0f, kMuted);
	auto* rich = win.Add<VRichText>();
	rich->Set(L"A quiet **family-run hotel** on the cliff above Cala Blanca, twelve rooms and a terrace that looks straight at the sea. "
	          L"Breakfast is served until 11, the pool is heated from *April to October*, and the kitchen closes at `23:00`. "
	          L"The beach is a five-minute walk down the steps; the [village and its market](https://example.com/cala-blanca) are ten more. "
	          L"Free cancellation until 48 hours before check-in.");
	rich->OnLink([&](const std::wstring& u) { status->Text(L"Link: " + u); repaint(); });
	auto* scroll = win.Add<VScrollViewer>(); scroll->Border(false);
	struct Review { const wchar_t* name; int stars; const wchar_t* text; };
	static const Review kReviews[] = {
		{ L"Grace H.",    5, L"The terrace at sunset alone is worth the trip. Spotless rooms." },
		{ L"Alan T.",     4, L"Quiet, well run, breakfast excellent. Wi-Fi drops on the terrace." },
		{ L"Ada L.",      5, L"We came for two nights and stayed five. The pool is heated as promised." },
		{ L"Margaret H.", 4, L"Steep steps down to the beach, so mind your knees. Otherwise perfect." },
		{ L"Barbara L.",  5, L"Best coffee on the island. Ask for a room on the top floor." },
		{ L"Dennis R.",   3, L"Good hotel, but the suite is small for the price." },
		{ L"Linus T.",    5, L"Kids loved the pool, we loved the silence after nine." },
		{ L"Ken T.",      4, L"Parking is tight in August. Book it with the room." },
	};
	auto paintReviews = [](ID2D1RenderTarget* rt, const D2D1_RECT_F& r) {
		float y = 0.0f;
		for (const Review& rv : kReviews) {
			D2D1_RECT_F card = vd::Rect(0.0f, y, vd::W(r), 78.0f);
			vd::Fill(rt, vd::Inset(card, 0.0f, 4.0f), vd::Col(0xF9FAFB), 8.0f);
			vd::Avatar(rt, 28.0f, y + 39.0f, 16.0f, rv.name);
			vd::Text(rt, rv.name, vd::Rect(56.0f, y + 12.0f, 200.0f, 20.0f), kText, vd::Style().Size(13).Bold());
			vd::Text(rt, std::wstring((size_t)rv.stars, L'\x2605') + std::wstring((size_t)(5 - rv.stars), L'\x2606'), vd::Rect(vd::W(r) - 90.0f, y + 12.0f, 80.0f, 20.0f), vd::Col(0xF59E0B), vd::Style().Size(13).Right());
			vd::Text(rt, rv.text, vd::Rect(56.0f, y + 34.0f, vd::W(r) - 70.0f, 36.0f), kMuted, vd::Style().Size(12).Wrap().Top());
			y += 84.0f;
		}
	};
	auto paintMap = [](ID2D1RenderTarget* rt, const D2D1_RECT_F& r) {
		vd::Fill(rt, r, vd::Col(0xEFF6FF));
		vd::Fill(rt, D2D1::RectF(r.left, r.bottom - 320.0f, r.right, r.bottom), vd::Col(0x93C5FD));           // the sea
		for (float x = 60.0f; x < vd::W(r); x += 140.0f) vd::Fill(rt, vd::Rect(x, 0.0f, 18.0f, vd::H(r) - 320.0f), vd::Col(0xFFFFFF));
		for (float y = 80.0f; y < vd::H(r) - 320.0f; y += 120.0f) vd::Fill(rt, vd::Rect(0.0f, y, vd::W(r), 14.0f), vd::Col(0xFFFFFF));
		for (int k = 0; k < 30; ++k) vd::Fill(rt, vd::Rect(90.0f + (float)(k * 197 % 900), 110.0f + (float)(k * 131 % 380), 40.0f + (float)(k % 4) * 12.0f, 30.0f + (float)(k % 3) * 10.0f), vd::Col(0xD1D5DB), 3.0f);
		vd::Text(rt, L"Cala Blanca", vd::Rect(vd::W(r) * 0.3f, vd::H(r) - 200.0f, 300.0f, 40.0f), vd::Col(0x1E3A8A, 0.6f), vd::Style().Size(30).Light());
		float px = 640.0f, py = vd::H(r) - 360.0f;                                                                  // the pin
		vd::Polyline(rt, { D2D1::Point2F(px, py), D2D1::Point2F(px - 16.0f, py - 30.0f), D2D1::Point2F(px + 16.0f, py - 30.0f) }, vd::Col(0xDC2626), 1.0f, true);
		vd::Circle(rt, px, py - 32.0f, 18.0f, vd::Col(0xDC2626));
		vd::Circle(rt, px, py - 32.0f, 7.0f, vd::Col(0xFFFFFF));
		vd::Text(rt, L"Sea View Hotel", vd::Rect(px + 26.0f, py - 46.0f, 200.0f, 28.0f), kText, vd::Style().Size(14).Bold());
	};
	auto showTab = [&](int i) {
		rich->SetVisible(i == 0); scroll->SetVisible(i != 0);
		if (i == 1) { scroll->Paint(paintReviews).Content(0.0f, 84.0f * (float)(sizeof(kReviews) / sizeof(kReviews[0]))).Zoomable(false); }
		if (i == 2) { scroll->Paint(paintMap).Content(1200.0f, 900.0f).Zoomable(true).ScrollTo(300.0f, 240.0f); }
		scroll->Zoom(1.0f); scroll->ScrollTo(i == 2 ? 300.0f : 0.0f, i == 2 ? 240.0f : 0.0f);
		repaint();
	};
	pivot->OnChange(showTab);

	// --- the bill ------------------------------------------------------------------
	auto* summary = label(L"", 13.0f);
	auto* overBudget = win.Add<VInfoBar>(); overBudget->Set(VSeverity::Warning, L"Over budget.", L"Fewer nights, a smaller room, or raise the budget.").Open(false);
	auto* dialog = win.AddChrome<VDialog>(); dialog->Attach(&win);
	dialog->Title(L"Book this stay?").Primary(L"Book").Secondary(L"Not yet");
	std::function<void()> layout;
	auto recompute = [&] {
		int g = (int)guests->Value();
		total = kRooms[room].rate * (double)nights + (breakfast->Value() ? 18.0 * (double)g * (double)nights : 0.0);
		double cap = 200.0 + 1800.0 * (double)budget->Value();
		wchar_t b[200];
		swprintf_s(b, L"%s \x00B7 %d night%s from %s \x00B7 %d guest%s%s \x00B7 total \x20AC %s",
			kRooms[room].name, nights, nights == 1 ? L"" : L"s", vdate::Format(checkIn).c_str(), g, g == 1 ? L"" : L"s",
			breakfast->Value() ? L" \x00B7 breakfast" : L"", vd::Num(total).c_str());
		summary->Text(b);
		priceLbl->Text(L"\x20AC " + vd::Num(kRooms[room].rate) + L" / night");
		nightsLbl->Text(std::to_wstring(nights));
		bool over = total > cap;
		if (over != overBudget->IsOpen()) { overBudget->Open(over); }
		repaint();
	};
	rooms->OnChange([&](int i) { room = i; recompute(); });
	guests->OnChange([&](double) { recompute(); });
	cal->OnChange([&](VDate d) { checkIn = d; recompute(); });
	minus->OnClick([&] { if (nights > 1) { --nights; recompute(); } });
	plus->OnClick([&] { if (nights < 30) { ++nights; recompute(); } });
	breakfast->OnChange([&](bool) { recompute(); });
	budget->OnChange([&](float) { recompute(); });
	book->OnClick([&] { dialog->Body(std::wstring(L"You are booking: ") + std::wstring(kRooms[room].name) + L", " + std::to_wstring(nights) + L" nights from " + vdate::Format(checkIn) + L", \x20AC " + vd::Num(total) + L" in total. Free cancellation until 48 hours before."); dialog->Open(); });
	dialog->OnResult([&](int r) { status->Text(r == 0 ? L"Booked. See you in Cala Blanca." : L"Not booked."); repaint(); });

	// --- layout -----------------------------------------------------------------------
	layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		for (VFlyout* f : flyouts) f->Cover(W, H);
		dialog->Cover(W, H);
		title ->SetBounds(vd::Rect(24.0f, 14.0f, 400.0f, 30.0f));
		sub   ->SetBounds(vd::Rect(24.0f, 42.0f, 500.0f, 18.0f));
		heart ->SetBounds(vd::Rect(W - 330.0f, 20.0f, 36.0f, 36.0f));
		notify->SetBounds(vd::Rect(W - 284.0f, 22.0f, 140.0f, 32.0f));
		book  ->SetBounds(vd::Rect(W - 134.0f, 20.0f, 110.0f, 36.0f));
		status->SetBounds(vd::Rect(24.0f, H - 28.0f, W - 48.0f, 20.0f));

		splitv->SetBounds(vd::Rect(0.0f, 70.0f, W, H - 70.0f - 36.0f));
		D2D1_RECT_F pane = splitv->PaneRect(), content = splitv->ContentRect();
		bool open = splitv->PaneOpen();
		burger->SetBounds(vd::Rect(pane.left + 6.0f, pane.top + 6.0f, 36.0f, 36.0f));
		float py = pane.top + 56.0f, px = pane.left + 16.0f, pw = 276.0f - 32.0f;
		fGuests->SetBounds(vd::Rect(px, py, pw, 16.0f));            guests->SetBounds(vd::Rect(px, py + 20.0f, 120.0f, 32.0f));
		fIn->SetBounds(vd::Rect(px, py + 64.0f, pw, 16.0f));        cal->SetBounds(vd::Rect(px, py + 84.0f, pw, 268.0f));
		fNights->SetBounds(vd::Rect(px, py + 364.0f, pw, 16.0f));   minus->SetBounds(vd::Rect(px, py + 384.0f, 36.0f, 32.0f)); nightsLbl->SetBounds(vd::Rect(px + 44.0f, py + 384.0f, 40.0f, 32.0f)); plus->SetBounds(vd::Rect(px + 84.0f, py + 384.0f, 36.0f, 32.0f));
		fBreakfast->SetBounds(vd::Rect(px + 140.0f, py + 364.0f, 100.0f, 16.0f)); breakfast->SetBounds(vd::Rect(px + 140.0f, py + 388.0f, 60.0f, 24.0f));
		fBudget->SetBounds(vd::Rect(px, py + 430.0f, pw, 16.0f));   budget->SetBounds(vd::Rect(px, py + 450.0f, pw, 28.0f));
		for (auto* w : std::vector<IVirtualWidget*>{ fGuests, guests, fIn, cal, fNights, minus, nightsLbl, plus, fBreakfast, breakfast, fBudget, budget }) w->SetVisible(open);
		for (size_t k = 0; k < compactIcons.size(); ++k) { compactIcons[k]->SetBounds(vd::Rect(pane.left + 6.0f, pane.top + 56.0f + 40.0f * (float)k, 36.0f, 36.0f)); compactIcons[k]->SetVisible(!open); }

		D2D1_RECT_F body = vd::Inset(content, 24.0f, 0.0f); body.bottom -= 8.0f;
		two->SetBounds(body);
		D2D1_RECT_F g = two->Pane1Rect(), d = two->Pane2Rect();
		galleryCard->SetBounds(g);
		rooms->SetBounds(vd::Rect(g.left + 12.0f, g.top + 10.0f, vd::W(g) - 60.0f, 40.0f));
		share->SetBounds(vd::Rect(g.right - 46.0f, g.top + 12.0f, 36.0f, 36.0f));
		flip->SetBounds(vd::Rect(g.left + 12.0f, g.top + 58.0f, vd::W(g) - 24.0f, vd::H(g) - 58.0f - 40.0f));
		pips->SetBounds(vd::Rect(g.left + 12.0f, g.bottom - 34.0f, vd::W(g) - 24.0f, 24.0f));
		priceTag->SetBounds(vd::Rect(g.right - 152.0f, g.top + 72.0f, 130.0f, 30.0f));
		priceLbl->SetBounds(vd::Rect(g.right - 122.0f, g.top + 72.0f, 100.0f, 30.0f));

		detailsCard->SetBounds(d);
		pivot->SetBounds(vd::Rect(d.left + 12.0f, d.top + 8.0f, vd::W(d) - 24.0f, 44.0f));
		for (size_t k = 0; k < amenities.size(); ++k) amenities[k]->SetBounds(vd::Rect(d.left + 16.0f + 26.0f * (float)k, d.top + 60.0f, 24.0f, 24.0f));
		amenLbl->SetBounds(vd::Rect(d.left + 16.0f + 26.0f * (float)amenities.size() + 6.0f, d.top + 60.0f, vd::W(d) - 40.0f, 24.0f));
		float obH = overBudget->ShownHeight();
		overBudget->SetBounds(vd::Rect(d.left + 12.0f, d.bottom - 12.0f - 30.0f - obH, vd::W(d) - 24.0f, obH));
		summary->SetBounds(vd::Rect(d.left + 16.0f, d.bottom - 36.0f, vd::W(d) - 32.0f, 24.0f));
		D2D1_RECT_F inner = D2D1::RectF(d.left + 16.0f, d.top + 96.0f, d.right - 16.0f, overBudget->GetBounds().top - 8.0f);
		rich->SetBounds(inner);
		scroll->SetBounds(inner);
		repaint();
	};
	splitv->OnLayout(layout);
	two->OnLayout(layout);
	overBudget->OnLayout(layout);
	win.OnResize(layout);
	showTab(0);
	recompute();
	layout();
	win.SetFocusWidget(cal);
	return win.RunMessageLoop();
}
