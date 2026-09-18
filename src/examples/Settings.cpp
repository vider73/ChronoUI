// =============================================================================
// Settings.cpp — an app shell in the WinUI 3 vocabulary: side navigation,
// tabs, expanders, an info bar, a menu, a date picker, a list and a tree.
//
// What this example teaches:
//   * VNavView switches pages. A page is a plain vector of widgets the app
//     shows or hides; there is no container tree, and none is needed.
//   * VExpander and VInfoBar change their own height and say so through
//     OnLayout; the app's single layout() places everything again.
//   * VFlyout is the light-dismiss popup: VMenu and VDatePicker build on it.
//     Flyouts are chrome, added last, so they paint on top and get the click.
//   * VListView keeps a selection (click, Ctrl, Shift, Ctrl+A); VTreeView
//     keeps open/closed state per node. Both scroll on their own.
//
// Build target: Settings. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VNavigation.hpp"
#include "VCollections.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg     = vd::Col(0xF3F3F3);
static const D2D1_COLOR_F kText   = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted  = vd::Col(0x6B7280);
static const D2D1_COLOR_F kBorder = vd::Col(0xE5E7EB);

// A plain white card: the surface the tab content and the tree detail sit on.
class VCard : public VirtualWidgetImpl {
public:
	const char* GetTypeName() const override { return "VCard"; }
	void OnDraw(ID2D1RenderTarget* rt) override {
		vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f);
		vd::Stroke(rt, m_bounds, kBorder, 8.0f, 1.0f);
	}
};

// A page is the set of widgets that show while it is selected.
struct Page { std::wstring title; std::vector<IVirtualWidget*> widgets; };

static VTreeView::Node Branch(const wchar_t* label, std::vector<VTreeView::Node> kids, bool open = false) {
	VTreeView::Node n; n.label = label; n.kids = std::move(kids); n.open = open; return n;
}
static VTreeView::Node Leaf(const wchar_t* label) { VTreeView::Node n; n.label = label; return n; }

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Settings", 1180, 780)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };
	auto label = [&](const std::wstring& text, float size = 13.0f, D2D1_COLOR_F col = kText) {
		VLabel* l = win.Add<VLabel>(); l->Text(text).FontSize(size).Color(col); return l;
	};

	// --- shell: navigation, page title, the "..." menu button, a status line ---
	auto* nav = win.Add<VNavView>(
		std::vector<VNavView::Item>{ { L"\xE80F", L"Home" }, { L"\xE790", L"Appearance" }, { L"\xE77B", L"Accounts" }, { L"\xE8B7", L"Storage" } },
		std::vector<VNavView::Item>{ { L"\xE946", L"About" } });
	auto* title  = label(L"", 26.0f);
	auto* more   = win.Add<VButton>();
	more->Text(L"\x2022\x2022\x2022").Face(vd::Col(0xE9E9E9)).FaceHover(vd::Col(0xDCDCDC)).FacePress(vd::Col(0xCFCFCF)).TextColor(kText);
	more->SetTooltip(L"Page menu");
	auto* status = label(L"Ready.", 12.0f, kMuted);

	std::vector<Page> pages(5);
	pages[0].title = L"Home"; pages[1].title = L"Appearance"; pages[2].title = L"Accounts";
	pages[3].title = L"Storage"; pages[4].title = L"About";

	// --- Home: an info bar and three expanders -------------------------------
	auto* bar = win.Add<VInfoBar>();
	bar->Set(VSeverity::Info, L"Update available", L"ChronoUI 2.1 is ready to install.").Action(L"Install");

	auto* expDisplay = win.Add<VExpander>(L"\xE7F4", L"Display", L"Brightness and night light");
	expDisplay->Content(100.0f).Open(true);
	auto* brightL = label(L"Brightness");   auto* bright = win.Add<VSlider>(); bright->Set(0.7f);
	auto* nightL  = label(L"Night light");  auto* night  = win.Add<VToggle>();

	auto* expSound = win.Add<VExpander>(L"\xE767", L"Sound", L"Output device and volume");
	expSound->Content(100.0f);
	auto* outL = label(L"Output");  auto* out = win.Add<VSegment>(std::vector<std::wstring>{ L"Speakers", L"Headphones", L"Monitor" });
	auto* volL = label(L"Volume");  auto* vol = win.Add<VSlider>(); vol->Set(0.4f);

	auto* expPower = win.Add<VExpander>(L"\xE945", L"Power", L"Plan and scheduled restart");
	expPower->Content(150.0f);
	auto* planL    = label(L"Plan");
	auto* plan     = win.Add<VRadio>(std::vector<std::wstring>{ L"Balanced", L"Best performance", L"Best efficiency" });
	auto* restartL = label(L"Scheduled restart");
	pages[0].widgets = { bar, expDisplay, brightL, bright, nightL, night, expSound, outL, out, volL, vol, expPower, planL, plan, restartL };

	// --- Appearance: a tab view over three cards ------------------------------
	auto* tabs    = win.Add<VTabView>(std::vector<std::wstring>{ L"Colors", L"Fonts", L"Wallpaper" });
	auto* tabCard = win.Add<VCard>();
	std::vector<std::vector<IVirtualWidget*>> tabContent(3);

	auto* themeL  = label(L"Theme");   auto* theme  = win.Add<VRadio>(std::vector<std::wstring>{ L"Light", L"Dark" }, true);
	auto* accentL = label(L"Accent");  auto* accent = win.Add<VSegment>(std::vector<std::wstring>{ L"Blue", L"Violet", L"Green", L"Rose" });
	tabContent[0] = { themeL, theme, accentL, accent };

	auto* sizeL   = label(L"Text size");
	auto* size    = win.Add<VSlider>();
	size->Set(0.3f).Format([](float v) { return std::to_wstring((int)(10.0f + v * 14.0f + 0.5f)) + L" px"; });
	auto* preview = label(L"The quick brown fox jumps over the lazy dog.", 14.0f);
	tabContent[1] = { sizeL, size, preview };

	auto* fit     = win.Add<VCheck>(L"Fit to screen");   fit->Set(true);
	auto* shuffle = win.Add<VCheck>(L"Shuffle pictures");
	auto* everyL  = label(L"Change every");  auto* every = win.Add<VStepper>(); every->Range(1, 120).Set(30);
	auto* minL    = label(L"minutes", 13.0f, kMuted);
	tabContent[2] = { fit, shuffle, everyL, every, minL };

	pages[1].widgets = { tabs, tabCard };
	for (auto& tc : tabContent) for (auto* w : tc) pages[1].widgets.push_back(w);

	// --- Accounts: a multi-select list ---------------------------------------
	auto* people = win.Add<VListView>();
	people->Items({
		{ L"Grace Hopper",     L"Administrator \x00B7 grace@example.com",   L"\xE77B" },
		{ L"Alan Turing",      L"Developer \x00B7 alan@example.com",        L"\xE77B" },
		{ L"Ada Lovelace",     L"Developer \x00B7 ada@example.com",         L"\xE77B" },
		{ L"Margaret Hamilton",L"Release manager \x00B7 margaret@example.com", L"\xE77B" },
		{ L"Linus Torvalds",   L"Guest \x00B7 linus@example.com",           L"\xE77B" },
		{ L"Barbara Liskov",   L"Reviewer \x00B7 barbara@example.com",      L"\xE77B" },
		{ L"Dennis Ritchie",   L"Developer \x00B7 dennis@example.com",      L"\xE77B" },
		{ L"Radia Perlman",    L"Network \x00B7 radia@example.com",         L"\xE77B" },
		{ L"Ken Thompson",     L"Guest \x00B7 ken@example.com",             L"\xE77B" },
	});
	auto* countL    = label(L"", 13.0f, kMuted);
	auto* allBtn    = win.Add<VButton>(); allBtn->Text(L"Select all");
	allBtn->Face(vd::Col(0xE9E9E9)).FaceHover(vd::Col(0xDCDCDC)).FacePress(vd::Col(0xCFCFCF)).TextColor(kText);
	auto* removeBtn = win.Add<VButton>(); removeBtn->Text(L"Remove");
	removeBtn->Face(vd::Col(0xDC2626)).FaceHover(vd::Col(0xB91C1C)).FacePress(vd::Col(0x991B1B));
	pages[2].widgets = { people, countL, allBtn, removeBtn };

	// --- Storage: a tree with a detail card ----------------------------------
	auto* tree = win.Add<VTreeView>();
	tree->Set({
		Branch(L"This PC", {
			Branch(L"Documents", {
				Branch(L"Invoices",  { Leaf(L"2026-01.pdf"), Leaf(L"2026-02.pdf"), Leaf(L"2026-03.pdf") }),
				Branch(L"Reports",   { Leaf(L"Q1.docx"), Leaf(L"Q2.docx"), Leaf(L"Q3.docx") }),
				Branch(L"Contracts", { Leaf(L"Lease.pdf"), Leaf(L"Supplier.pdf") }),
			}, true),
			Branch(L"Pictures", { Branch(L"Camera", { Leaf(L"IMG_0001.jpg"), Leaf(L"IMG_0002.jpg") }), Branch(L"Screenshots", { Leaf(L"desktop.png") }) }),
			Branch(L"Music",    { Leaf(L"playlist.m3u") }),
			Branch(L"Projects", {
				Branch(L"ChronoUI", { Branch(L"include", { Leaf(L"VirtualWidget.hpp"), Leaf(L"VNavigation.hpp"), Leaf(L"VCollections.hpp") }),
				                      Branch(L"src", { Leaf(L"Settings.cpp") }), Branch(L"docs", { Leaf(L"EXAMPLES.md") }) }, true),
				Branch(L"Website",  { Leaf(L"index.html"), Leaf(L"style.css") }),
			}, true),
		}, true),
	});
	auto* detailCard  = win.Add<VCard>();
	auto* detailTitle = label(L"Nothing selected", 18.0f);
	auto* detailInfo  = label(L"Pick a folder or a file on the left.", 13.0f, kMuted);
	auto* detailHint  = label(L"Arrows walk the tree, Right opens a branch, Left closes it.", 12.0f, kMuted);
	pages[3].widgets = { tree, detailCard, detailTitle, detailInfo, detailHint };

	// --- About ---------------------------------------------------------------
	auto* aboutCard = win.Add<VCard>();
	auto* about1 = label(L"ChronoUI Settings", 18.0f);
	auto* about2 = label(L"The WinUI 3 vocabulary as ChronoUI virtual widgets: VNavView, VTabView, VExpander, VInfoBar,", 13.0f, kMuted);
	auto* about3 = label(L"VMenu, VDatePicker, VListView and VTreeView, from VNavigation.hpp and VCollections.hpp.", 13.0f, kMuted);
	auto* about4 = label(L"One HWND, one render target, C++ objects with OnDraw. No XAML was harmed.", 13.0f, kMuted);
	pages[4].widgets = { aboutCard, about1, about2, about3, about4 };

	// --- flyouts: chrome, added last so they paint over everything ------------
	auto* restart = win.AddChrome<VDatePicker>(); restart->Attach(&win);
	restart->Set(vdate::AddDays(vdate::Today(), 3));
	pages[0].widgets.push_back(restart);
	auto* menu = win.AddChrome<VMenu>(); menu->Attach(&win);
	menu->Add(L"Rename page", L"\xE8AC").Add(L"Duplicate page", L"\xE8C8").Separator()
	    .Check(L"Show advanced settings", false).Separator().Add(L"Reset to defaults", L"\xE777", L"Ctrl+R");

	// --- state + layout -------------------------------------------------------
	int current = 0;
	std::function<void()> layout;

	auto showTab = [&](int t) {
		for (size_t k = 0; k < tabContent.size(); ++k)
			for (auto* w : tabContent[k]) w->SetVisible(current == 1 && (int)k == t);
	};
	auto showPage = [&](int i) {
		current = i;
		for (size_t p = 0; p < pages.size(); ++p)
			for (auto* w : pages[p].widgets) w->SetVisible((int)p == i);
		title->Text(pages[(size_t)i].title);
		showTab(tabs->Value());
		layout();
	};
	auto updateCount = [&] {
		int n = people->SelectedCount();
		countL->Text(std::to_wstring(n) + L" of " + std::to_wstring(people->Size()) + L" selected \x00B7 click, Ctrl+click, Shift+click, Ctrl+A");
		removeBtn->Enabled(n > 0);
		repaint();
	};

	layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		float navW = nav->CurrentWidth();
		nav->SetBounds(vd::Rect(0.0f, 0.0f, navW, H));
		float x = navW + 36.0f, w = W - x - 36.0f, top = 78.0f;
		title ->SetBounds(vd::Rect(x, 22.0f, w - 60.0f, 36.0f));
		more  ->SetBounds(vd::Rect(W - 76.0f, 24.0f, 40.0f, 32.0f));
		status->SetBounds(vd::Rect(x, H - 30.0f, w, 20.0f));
		restart->Cover(W, H); menu->Cover(W, H);

		// Home: stack the bar and the cards; each card's children sit in its content rect.
		bool home = current == 0;
		float y = top;
		bar->SetBounds(vd::Rect(x, y, w, bar->ShownHeight()));
		y += bar->ShownHeight() + (bar->ShownHeight() > 0.5f ? 16.0f : 0.0f);
		auto card = [&](VExpander* e, std::vector<IVirtualWidget*> kids) {
			e->SetBounds(vd::Rect(x, y, w, e->ShownHeight()));
			y += e->ShownHeight() + 12.0f;
			for (auto* k : kids) k->SetVisible(home && e->ContentVisible());
			return e->ContentRect();
		};
		D2D1_RECT_F c = card(expDisplay, { brightL, bright, nightL, night });
		brightL->SetBounds(vd::Rect(c.left, c.top + 6.0f, 140.0f, 26.0f));   bright->SetBounds(vd::Rect(c.left + 150.0f, c.top + 6.0f, 330.0f, 26.0f));
		nightL ->SetBounds(vd::Rect(c.left, c.top + 50.0f, 140.0f, 28.0f));  night ->SetBounds(vd::Rect(c.left + 150.0f, c.top + 50.0f, 60.0f, 28.0f));
		c = card(expSound, { outL, out, volL, vol });
		outL->SetBounds(vd::Rect(c.left, c.top + 6.0f, 140.0f, 30.0f));      out->SetBounds(vd::Rect(c.left + 150.0f, c.top + 6.0f, 340.0f, 30.0f));
		volL->SetBounds(vd::Rect(c.left, c.top + 50.0f, 140.0f, 26.0f));     vol->SetBounds(vd::Rect(c.left + 150.0f, c.top + 50.0f, 330.0f, 26.0f));
		c = card(expPower, { planL, plan, restartL, restart });
		planL   ->SetBounds(vd::Rect(c.left, c.top + 4.0f, 140.0f, 28.0f));  plan   ->SetBounds(vd::Rect(c.left + 150.0f, c.top + 2.0f, 260.0f, 84.0f));
		restartL->SetBounds(vd::Rect(c.left, c.top + 102.0f, 140.0f, 34.0f)); restart->SetBounds(vd::Rect(c.left + 150.0f, c.top + 102.0f, 240.0f, 34.0f));

		// Appearance: the strip, then a card, then the current tab's content on it.
		tabs   ->SetBounds(vd::Rect(x, top, w, 40.0f));
		tabCard->SetBounds(vd::Rect(x, top + 40.0f, w, H - top - 40.0f - 48.0f));
		float cx = x + 28.0f, cy = top + 40.0f + 28.0f;
		themeL ->SetBounds(vd::Rect(cx, cy, 140.0f, 28.0f));          theme ->SetBounds(vd::Rect(cx + 150.0f, cy, 240.0f, 28.0f));
		accentL->SetBounds(vd::Rect(cx, cy + 48.0f, 140.0f, 30.0f));  accent->SetBounds(vd::Rect(cx + 150.0f, cy + 48.0f, 360.0f, 30.0f));
		sizeL  ->SetBounds(vd::Rect(cx, cy, 140.0f, 26.0f));          size  ->SetBounds(vd::Rect(cx + 150.0f, cy, 330.0f, 26.0f));
		preview->SetBounds(vd::Rect(cx, cy + 56.0f, w - 56.0f, 40.0f));
		fit    ->SetBounds(vd::Rect(cx, cy, 300.0f, 26.0f));          shuffle->SetBounds(vd::Rect(cx, cy + 36.0f, 300.0f, 26.0f));
		everyL ->SetBounds(vd::Rect(cx, cy + 80.0f, 140.0f, 34.0f));  every  ->SetBounds(vd::Rect(cx + 150.0f, cy + 80.0f, 130.0f, 34.0f));
		minL   ->SetBounds(vd::Rect(cx + 290.0f, cy + 80.0f, 100.0f, 34.0f));
		for (size_t k = 3; k < tabContent.size(); ++k)
			for (auto* wdg : tabContent[k]) wdg->SetBounds(vd::Rect(cx, cy, w - 56.0f, 26.0f));

		// Accounts
		countL   ->SetBounds(vd::Rect(x, top, w - 260.0f, 34.0f));
		allBtn   ->SetBounds(vd::Rect(x + w - 250.0f, top, 110.0f, 34.0f));
		removeBtn->SetBounds(vd::Rect(x + w - 130.0f, top, 130.0f, 34.0f));
		people   ->SetBounds(vd::Rect(x, top + 48.0f, w, H - top - 48.0f - 48.0f));

		// Storage
		float tw = (std::min)(420.0f, w * 0.45f);
		tree       ->SetBounds(vd::Rect(x, top, tw, H - top - 48.0f));
		detailCard ->SetBounds(vd::Rect(x + tw + 20.0f, top, w - tw - 20.0f, 150.0f));
		detailTitle->SetBounds(vd::Rect(x + tw + 44.0f, top + 22.0f, w - tw - 68.0f, 30.0f));
		detailInfo ->SetBounds(vd::Rect(x + tw + 44.0f, top + 58.0f, w - tw - 68.0f, 22.0f));
		detailHint ->SetBounds(vd::Rect(x + tw + 44.0f, top + 92.0f, w - tw - 68.0f, 22.0f));

		// About
		aboutCard->SetBounds(vd::Rect(x, top, w, 170.0f));
		about1->SetBounds(vd::Rect(x + 24.0f, top + 22.0f, w - 48.0f, 30.0f));
		about2->SetBounds(vd::Rect(x + 24.0f, top + 62.0f, w - 48.0f, 22.0f));
		about3->SetBounds(vd::Rect(x + 24.0f, top + 86.0f, w - 48.0f, 22.0f));
		about4->SetBounds(vd::Rect(x + 24.0f, top + 120.0f, w - 48.0f, 22.0f));
		repaint();
	};

	// --- wiring ---------------------------------------------------------------
	nav->OnChange(showPage);
	nav->OnLayout([&](float) { layout(); });
	expDisplay->OnLayout(layout); expSound->OnLayout(layout); expPower->OnLayout(layout);
	bar->OnLayout(layout);
	bar->OnAction([&] {
		bar->Set(VSeverity::Success, L"Installed", L"ChronoUI 2.1 is in place; restart when convenient.").Action(L"");
		status->Text(L"Update installed."); repaint();
	});
	bar->OnClose([&] { status->Text(L"Update dismissed."); });
	more->OnClick([&] { menu->Open(more->GetBounds()); });
	menu->OnPick([&](int i) {
		const VMenu::Item& it = menu->At(i);
		std::wstring s = L"Menu: " + it.label;
		if (it.checkable) s += it.checked ? L" (on)" : L" (off)";
		if (i == menu->Count() - 1) {           // Reset to defaults
			bright->Set(0.7f); night->Set(false); out->Set(0); vol->Set(0.4f); plan->Set(0);
			theme->Set(0); accent->Set(0); size->Set(0.3f); preview->FontSize(14.0f);
			fit->Set(true); shuffle->Set(false); every->Set(30);
			restart->Set(vdate::AddDays(vdate::Today(), 3));
		}
		status->Text(s); repaint();
	});
	restart->OnChange([&](VDate d) { status->Text(L"Restart scheduled for " + vdate::Format(d) + L"."); repaint(); });
	bright->OnChange([&](float v) { status->Text(L"Brightness " + vd::Num(v * 100.0) + L"%."); });
	night ->OnChange([&](bool on) { status->Text(on ? L"Night light on." : L"Night light off."); });
	size  ->OnChange([&](float v) { preview->FontSize(10.0f + v * 14.0f); });
	theme ->OnChange([&](int i)   { status->Text(i ? L"Dark theme picked (the widgets stay light in this example)." : L"Light theme."); });

	tabs->OnChange(showTab);
	tabs->OnAdd([&] {
		int i = tabs->Add(L"Tab " + std::to_wstring(tabs->Count() + 1));
		VLabel* l = label(L"A new tab. Close it with the \x00D7 on its header, or press Delete.", 13.0f, kMuted);
		tabContent.push_back({ l }); pages[1].widgets.push_back(l);
		showTab(i); layout();
	});
	tabs->OnClose([&](int i) {
		for (auto* w : tabContent[(size_t)i]) {
			pages[1].widgets.erase(std::remove(pages[1].widgets.begin(), pages[1].widgets.end(), w), pages[1].widgets.end());
			win.RemoveWidget(w);
		}
		tabContent.erase(tabContent.begin() + i);
		tabs->Remove(i);
		showTab(tabs->Value()); repaint();
	});

	people->OnSelection(updateCount);
	people->OnActivate([&](int i) { status->Text(L"Opened " + people->At(i).title + L"."); });
	allBtn->OnClick([&] { people->SelectAll(true); });
	removeBtn->OnClick([&] { people->Erase(people->Selected()); status->Text(L"Removed."); });

	tree->OnSelect([&](VTreeView::Node& n) {
		detailTitle->Text(n.label);
		detailInfo->Text(n.kids.empty() ? L"File" : std::to_wstring(n.kids.size()) + (n.kids.size() == 1 ? L" item" : L" items"));
		status->Text(L"Selected " + n.label + L"."); repaint();
	});

	win.OnResize(layout);
	updateCount();
	showPage(0);
	win.SetFocusWidget(nav);
	return win.RunMessageLoop();
}
