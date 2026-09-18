// =============================================================================
// Photos.cpp — a photo library: a menu bar, a toolbar, a grid of pictures and
// a detail pane with a colour picker. Every picture is painted, not loaded.
//
// What this example teaches:
//   * VMenuBar: File / Edit / View / Help, keyboard and mouse, check items.
//     One flyout, one callback with (menu, item).
//   * VGridView paints its tiles through a callback: the app decides what a
//     tile looks like, the grid handles layout, scrolling and selection.
//   * VSuggestions under a search box: the box keeps the caret while the list
//     takes Up / Down / Enter through VirtualWindow::OnKeyHook.
//   * VColorPicker, VSplitButton, VToggleButton, VTimePicker, VTeachingTip:
//     small controls that finish a desktop app.
//
// Build target: Photos. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VirtualChat.hpp"
#include "VControls.hpp"
#include "VNavigation.hpp"
#include "VCollections.hpp"
#include "VActions.hpp"
#include "VIndicators.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg     = vd::Col(0xF3F3F3);
static const D2D1_COLOR_F kText   = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted  = vd::Col(0x6B7280);
static const D2D1_COLOR_F kBorder = vd::Col(0xE5E7EB);

struct Photo {
	std::wstring name, date, tag;
	float hue;              // the sky
	int   seed;             // the hills
	int   rating = 0;
	bool  favourite = false;
	D2D1_COLOR_F tint = D2D1::ColorF(0, 0, 0, 0);   // alpha 0 = none
};

static const wchar_t* kTags[] = { L"sea", L"hills", L"sunset", L"night", L"forest", L"desert" };

static std::vector<Photo> Seed() {
	std::vector<Photo> v;
	for (int i = 0; i < 24; ++i) {
		Photo p;
		wchar_t n[16]; swprintf_s(n, L"IMG_%04d", 1001 + i * 7);
		p.name = n;
		wchar_t d[24]; swprintf_s(d, L"%d Sep 2026 \x00B7 %d.%d MB", 1 + (i * 5) % 28, 2 + i % 4, (i * 3) % 10);
		p.date = d;
		p.tag  = kTags[i % 6];
		p.hue  = (float)((i * 47) % 360);
		p.seed = i * 131 + 7;
		p.favourite = i % 5 == 0;
		p.rating = i % 5 == 0 ? 5 : (i % 3 == 0 ? 3 : 0);
		v.push_back(p);
	}
	return v;
}

// A landscape from three numbers. The same function paints a 180 px tile and
// the 480 px preview; Direct2D does not care about the size.
static void PaintPhoto(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const Photo& p) {
	float W = vd::W(r), H = vd::H(r);
	bool night = p.tag == L"night";
	vd::Gradient(rt, r, vd::FromHSV(p.hue, night ? 0.6f : 0.5f, night ? 0.25f : 0.95f), vd::FromHSV(p.hue + 35.0f, 0.3f, night ? 0.15f : 0.8f));
	vd::Circle(rt, r.left + W * 0.72f, r.top + H * 0.3f, (std::min)(W, H) * 0.09f, night ? vd::Col(0xF1F5F9, 0.95f) : vd::Col(0xFFF3C4, 0.95f));
	if (night) for (int k = 0; k < 40; ++k) {
		int s = p.seed * (k + 3);
		vd::Circle(rt, r.left + (float)(s % 997) / 997.0f * W, r.top + (float)((s / 7) % 611) / 611.0f * H * 0.55f, 0.8f, vd::Col(0xFFFFFF, 0.8f));
	}
	for (int layer = 0; layer < 3; ++layer) {
		std::vector<D2D1_POINT_2F> pts;
		float base = H * (0.55f + 0.14f * (float)layer);
		pts.push_back(D2D1::Point2F(r.left, r.bottom));
		for (int k = 0; k <= 12; ++k) {
			float x = r.left + W * (float)k / 12.0f;
			float f = (float)p.seed * 0.01f + (float)layer * 1.7f;
			float y = r.top + base - H * (0.10f * sinf((float)k * 0.9f + f) + 0.06f * sinf((float)k * 2.3f + f * 2.0f)) - (float)layer * H * 0.02f;
			pts.push_back(D2D1::Point2F(x, y));
		}
		pts.push_back(D2D1::Point2F(r.right, r.bottom));
		vd::Polyline(rt, pts, vd::FromHSV(p.hue + 110.0f, 0.45f, (night ? 0.12f : 0.32f) + 0.12f * (float)layer), 1.0f, true);
	}
	if (p.tint.a > 0.0f) vd::Fill(rt, r, p.tint);
}

// The big picture on the right: the same painter, one photo.
class VPreview : public VirtualWidgetImpl {
	const Photo* m_p = nullptr;
public:
	const char* GetTypeName() const override { return "VPreview"; }
	void Show(const Photo* p) { m_p = p; }
	void OnDraw(ID2D1RenderTarget* rt) override {
		if (!m_p) { vd::Fill(rt, m_bounds, vd::Col(0xE5E7EB), 8.0f); return; }
		rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		PaintPhoto(rt, m_bounds, *m_p);
		rt->PopAxisAlignedClip();
		vd::Stroke(rt, m_bounds, vd::Col(0x000000, 0.12f), 8.0f, 1.0f);
	}
};
class VCard : public VirtualWidgetImpl {
public:
	const char* GetTypeName() const override { return "VCard"; }
	void OnDraw(ID2D1RenderTarget* rt) override { vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f); vd::Stroke(rt, m_bounds, kBorder, 8.0f, 1.0f); }
};

static std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Photos", 1280, 840)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };
	auto label = [&](const std::wstring& text, float size = 13.0f, D2D1_COLOR_F col = kText) {
		VLabel* l = win.Add<VLabel>(); l->Text(text).FontSize(size).Color(col); return l;
	};

	std::vector<Photo> photos = Seed();
	std::vector<int> view;                    // indices into photos, in grid order
	bool favOnly = false, sortByDate = false;
	float tipIn = 0.9f;                       // the teaching tip shows a moment after start

	// --- toolbar --------------------------------------------------------------
	auto* search   = win.Add<VChatInput>(); search->SetPlaceholder(L"Filter by tag \x2014 try \"sun\"").SetSingleLine(true);
	auto* favBtn   = win.Add<VToggleButton>(L"\xE734", L"Favourites");
	auto* capBtn   = win.Add<VToggleButton>(L"\xE7C3", L"Captions"); capBtn->Set(true);
	auto* countL   = label(L"", 12.0f, kMuted);

	// --- the grid -------------------------------------------------------------
	auto* grid = win.Add<VGridView>();
	grid->TileSize(180.0f, 120.0f).Gap(14.0f);
	grid->Paint([&](ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int i) { PaintPhoto(rt, r, photos[(size_t)view[(size_t)i]]); });

	// --- the detail pane ----------------------------------------------------
	auto* card     = win.Add<VCard>();
	auto* preview  = win.Add<VPreview>();
	auto* nameL    = label(L"", 18.0f);
	auto* dateL    = label(L"", 12.0f, kMuted);
	auto* ratingL  = label(L"Rating", 12.0f, kMuted);
	auto* rating   = win.Add<VRating>();
	auto* tintL    = label(L"Tint", 12.0f, kMuted);
	auto* tint     = win.Add<VColorPicker>();
	auto* clearTint= win.Add<VLink>(L"Clear tint");
	auto* slideL   = label(L"Slideshow starts at", 12.0f, kMuted);
	auto* empty    = label(L"Click a photo to see it here.", 13.0f, kMuted);
	auto* status   = label(L"Ready.", 12.0f, kMuted);
	std::vector<IVirtualWidget*> detail = { preview, nameL, dateL, ratingL, rating, tintL, tint, clearTint, slideL };

	// --- chrome: menus, popups, the tip (painted on top, added last) ----------
	auto* menu = win.AddChrome<VMenuBar>(); menu->Attach(&win);
	menu->Menu(L"File").Add(L"New album", L"\xE8F4").Add(L"Import photos\x2026", L"\xE8B5", L"Ctrl+I").Separator().Add(L"Exit", L"", L"Alt+F4")
	    .Menu(L"Edit").Add(L"Select all", L"", L"Ctrl+A").Add(L"Deselect", L"", L"Esc").Separator().Add(L"Rotate left", L"\xE7A7").Add(L"Rotate right", L"\xE7A6")
	    .Menu(L"View").Check(L"Captions", true).Check(L"Favourites only", false).Separator().Check(L"Sort by date", false)
	    .Menu(L"Help").Add(L"Show tip", L"\xE946").Add(L"About Photos");
	auto* suggest = win.AddChrome<VSuggestions>(); suggest->Attach(&win);
	auto* exportBtn = win.AddChrome<VSplitButton>(L"Export"); exportBtn->Attach(&win);
	exportBtn->Add(L"PNG", L"\xEB9F").Add(L"JPEG", L"\xEB9F").Add(L"PDF contact sheet", L"\xE8A5");
	auto* slideAt = win.AddChrome<VTimePicker>(); slideAt->Attach(&win); slideAt->Set({ 20, 30 });
	auto* tip = win.AddChrome<VTeachingTip>(); tip->Attach(&win);
	tip->Title(L"Pick more than one").Body(L"Click a photo to open it on the right. Ctrl+click adds another, Shift+click takes a range, and Ctrl+A takes them all.").Action(L"Got it");

	// --- state ----------------------------------------------------------------
	std::function<void()> layout;
	int current = -1;                          // index into photos of the open picture
	auto showDetail = [&] {
		std::vector<int> sel = grid->Selected();
		current = sel.size() == 1 ? view[(size_t)sel[0]] : -1;
		bool one = current >= 0;
		for (auto* w : detail) w->SetVisible(one);
		empty->SetVisible(!one);
		if (sel.size() > 1) empty->Text(std::to_wstring(sel.size()) + L" photos selected.");
		else if (sel.empty()) empty->Text(L"Click a photo to see it here.");
		if (one) {
			const Photo& p = photos[(size_t)current];
			preview->Show(&p); nameL->Text(p.name); dateL->Text(p.date + L" \x00B7 " + p.tag);
			rating->Set(p.rating);
			if (p.tint.a > 0.0f) tint->Set(p.tint);
		}
		repaint();
	};
	auto rebuild = [&] {
		std::wstring q = Lower(search->GetText());
		view.clear();
		for (size_t i = 0; i < photos.size(); ++i) {
			const Photo& p = photos[i];
			if (favOnly && !p.favourite) continue;
			if (!q.empty() && Lower(p.tag + L" " + p.name).find(q) == std::wstring::npos) continue;
			view.push_back((int)i);
		}
		if (sortByDate) std::stable_sort(view.begin(), view.end(), [&](int a, int b) { return photos[(size_t)a].date < photos[(size_t)b].date; });
		std::vector<VGridView::Tile> tiles;
		for (int i : view) tiles.push_back({ photos[(size_t)i].name, photos[(size_t)i].date });
		grid->Tiles(std::move(tiles));
		countL->Text(std::to_wstring(view.size()) + L" of " + std::to_wstring(photos.size()) + L" photos");
		showDetail();
	};

	// --- layout ---------------------------------------------------------------
	layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		menu   ->SetBounds(vd::Rect(0.0f, 0.0f, W, 34.0f));
		float y = 46.0f, x = 16.0f;
		search ->SetBounds(vd::Rect(x, y, 300.0f, 34.0f));
		favBtn ->SetBounds(vd::Rect(x + 312.0f, y, 130.0f, 34.0f));
		capBtn ->SetBounds(vd::Rect(x + 450.0f, y, 120.0f, 34.0f));
		exportBtn->SetBounds(vd::Rect(x + 590.0f, y, 132.0f, 34.0f));
		countL ->SetBounds(vd::Rect(x + 740.0f, y, 200.0f, 34.0f));
		float paneW = 340.0f, gx = 16.0f, gy = 94.0f;
		grid   ->SetBounds(D2D1::RectF(gx, gy, W - paneW - 32.0f, H - 40.0f));
		float px = W - paneW - 8.0f;
		card   ->SetBounds(D2D1::RectF(px, gy, W - 16.0f, H - 40.0f));
		preview->SetBounds(vd::Rect(px + 16.0f, gy + 16.0f, paneW - 40.0f, 200.0f));
		nameL  ->SetBounds(vd::Rect(px + 16.0f, gy + 228.0f, paneW - 40.0f, 26.0f));
		dateL  ->SetBounds(vd::Rect(px + 16.0f, gy + 254.0f, paneW - 40.0f, 18.0f));
		ratingL->SetBounds(vd::Rect(px + 16.0f, gy + 284.0f, 100.0f, 18.0f));
		rating ->SetBounds(vd::Rect(px + 16.0f, gy + 304.0f, 130.0f, 22.0f));
		tintL  ->SetBounds(vd::Rect(px + 16.0f, gy + 340.0f, 100.0f, 18.0f));
		tint   ->SetBounds(vd::Rect(px + 16.0f, gy + 362.0f, paneW - 40.0f, 224.0f));
		clearTint->SetBounds(vd::Rect(px + 16.0f, gy + 592.0f, 120.0f, 20.0f));
		slideL ->SetBounds(vd::Rect(px + 16.0f, gy + 624.0f, 160.0f, 18.0f));
		slideAt->SetBounds(vd::Rect(px + 16.0f, gy + 644.0f, 120.0f, 32.0f));
		empty  ->SetBounds(vd::Rect(px + 16.0f, gy + 16.0f, paneW - 40.0f, 24.0f));
		status ->SetBounds(vd::Rect(gx, H - 30.0f, W - 32.0f, 20.0f));
		for (VFlyout* f : { (VFlyout*)menu, (VFlyout*)suggest, (VFlyout*)exportBtn, (VFlyout*)slideAt, (VFlyout*)tip }) f->Cover(W, H);
		repaint();
	};

	// --- wiring ---------------------------------------------------------------
	grid->OnSelection(showDetail);
	grid->OnActivate([&](int i) { status->Text(L"Opening " + photos[(size_t)view[(size_t)i]].name + L" in the viewer (not really)."); repaint(); });
	favBtn->OnChange([&](bool on) { favOnly = on; menu->At(2, 1).checked = on; rebuild(); });
	capBtn->OnChange([&](bool on) { grid->Captions(on); menu->At(2, 0).checked = on; repaint(); });
	search->OnTextChanged([&] {
		rebuild();
		std::wstring q = Lower(search->GetText());
		std::vector<std::wstring> hits;
		if (!q.empty()) for (const wchar_t* t : kTags) if (std::wstring(t).find(q) != std::wstring::npos) hits.push_back(t);
		suggest->Show(search->GetBounds(), hits);
	});
	suggest->OnPick([&](int i) { search->SetText(suggest->At(i)); rebuild(); });
	win.OnKeyHook([&](UINT vk) { return suggest->HandleKey(vk); });
	exportBtn->OnClick([&] { status->Text(L"Exported " + std::to_wstring((std::max)(1, grid->SelectedCount())) + L" photo(s) as PNG."); repaint(); });
	exportBtn->OnPick([&](int i) { status->Text(L"Exported " + std::to_wstring((std::max)(1, grid->SelectedCount())) + L" photo(s) as " + exportBtn->At(i).label + L"."); repaint(); });
	rating->OnChange([&](int v) { if (current >= 0) { photos[(size_t)current].rating = v; photos[(size_t)current].favourite = v >= 4; status->Text(L"Rated " + std::to_wstring(v) + L"."); } });
	tint->OnChange([&](D2D1_COLOR_F c) { if (current >= 0) { photos[(size_t)current].tint = vd::Alpha(c, 0.35f); repaint(); } });
	clearTint->OnClick([&] { if (current >= 0) { photos[(size_t)current].tint = D2D1::ColorF(0, 0, 0, 0); status->Text(L"Tint cleared."); repaint(); } });
	slideAt->OnChange([&](VTime t) { status->Text(L"Slideshow starts at " + slideAt->Format() + L"."); (void)t; repaint(); });
	tip->OnAction([&] { status->Text(L"Tip dismissed."); repaint(); });
	menu->OnPick([&](int m, int i) {
		const VMenuItem& it = menu->At(m, i);
		if (m == 0 && i == 3) { PostMessage(hwnd, WM_CLOSE, 0, 0); return; }
		if (m == 1 && i == 0) { grid->SelectAll(true); }
		else if (m == 1 && i == 1) { grid->Clear(); }
		else if (m == 2 && i == 0) { capBtn->Set(it.checked); grid->Captions(it.checked); }
		else if (m == 2 && i == 1) { favBtn->Set(it.checked); favOnly = it.checked; rebuild(); }
		else if (m == 2 && i == 3) { sortByDate = it.checked; rebuild(); }
		else if (m == 3 && i == 0) { tip->Show(grid->GetBounds()); }
		status->Text(L"Menu: " + it.label + (it.checkable ? (it.checked ? L" (on)" : L" (off)") : L""));
		repaint();
	});
	win.OnTick([&](float dt) {
		if (tipIn <= 0.0f) return;
		tipIn -= dt;
		if (tipIn <= 0.0f) { D2D1_RECT_F g = grid->GetBounds(); tip->Show(vd::Rect(vd::CX(g) - 40.0f, g.top + 40.0f, 80.0f, 1.0f)); repaint(); }
	});

	win.OnResize(layout);
	layout();
	rebuild();
	grid->Select(0);
	win.SetFocusWidget(grid);
	return win.RunMessageLoop();
}
