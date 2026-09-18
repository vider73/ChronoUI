// =============================================================================
// Catalog.cpp — every virtual widget in one window, one page per family.
//
// This is the reference sheet: open it after touching a header and every
// control is on screen in its resting state, ready to be clicked. The pages
// are a VNavView; a page is a list of cells (a caption above a widget) that
// layout() flows into two columns.
//
// Build target: Catalog. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
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

static const D2D1_COLOR_F kBg    = vd::Col(0xF3F3F3);
static const D2D1_COLOR_F kText  = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted = vd::Col(0x6B7280);

class VCard : public VirtualWidgetImpl {
public:
	const char* GetTypeName() const override { return "VCard"; }
	void OnDraw(ID2D1RenderTarget* rt) override { vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f); vd::Stroke(rt, m_bounds, vd::Col(0xE5E7EB), 8.0f, 1.0f); }
};

struct Cell { VLabel* caption; std::vector<IVirtualWidget*> widgets; float h; bool wide; };
struct Page { std::wstring title; std::vector<Cell> cells; };

static VTreeView::Node Branch(const wchar_t* label, std::vector<VTreeView::Node> kids, bool open = false) {
	VTreeView::Node n; n.label = label; n.kids = std::move(kids); n.open = open; return n;
}
static VTreeView::Node Leaf(const wchar_t* label) { VTreeView::Node n; n.label = label; return n; }

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Catalog", 1280, 860)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };
	auto label = [&](const std::wstring& text, float size = 13.0f, D2D1_COLOR_F col = kText) {
		VLabel* l = win.Add<VLabel>(); l->Text(text).FontSize(size).Color(col); return l;
	};

	auto* nav = win.Add<VNavView>(
		std::vector<VNavView::Item>{ { L"\xE8FD", L"Basics" }, { L"\xE70F", L"Input" }, { L"\xE946", L"Status" }, { L"\xE8A5", L"Navigation" }, { L"\xE8FD", L"Collections" } },
		std::vector<VNavView::Item>{ { L"\xE946", L"About" } });
	nav->Badge(2, 3);
	auto* title  = label(L"", 26.0f);
	auto* status = label(L"Click anything; this line says what happened.", 12.0f, kMuted);
	std::vector<Page> pages(6);
	pages[0].title = L"Basics"; pages[1].title = L"Input"; pages[2].title = L"Status";
	pages[3].title = L"Navigation"; pages[4].title = L"Collections"; pages[5].title = L"About";
	std::vector<VFlyout*> flyouts;
	auto say = [&](const std::wstring& s) { status->Text(s); repaint(); };

	// cell(page, caption, height, widgets...) — the caption label is created here.
	auto cell = [&](int page, const std::wstring& caption, float h, std::vector<IVirtualWidget*> ws, bool wide = false) {
		pages[(size_t)page].cells.push_back({ label(caption, 12.0f, kMuted), std::move(ws), h, wide });
	};

	// --- Basics -------------------------------------------------------------
	auto* lbl = label(L"VLabel \x2014 one line of text, a size, a colour.", 14.0f);
	cell(0, L"VLabel", 24.0f, { lbl });
	auto* btn = win.Add<VButton>(); btn->Text(L"VButton");
	auto* btn2 = win.Add<VButton>(); btn2->Text(L"Secondary").Face(vd::Col(0xE9E9E9)).FaceHover(vd::Col(0xDCDCDC)).FacePress(vd::Col(0xCFCFCF)).TextColor(kText);
	auto* btn3 = win.Add<VButton>(); btn3->Text(L"Disabled").Enabled(false);
	btn->OnClick([&] { say(L"VButton clicked."); }); btn2->OnClick([&] { say(L"Secondary clicked."); });
	cell(0, L"VButton \x00B7 secondary \x00B7 disabled", 36.0f, { btn, btn2, btn3 });
	auto* tb1 = win.Add<VToggleButton>(L"\xE8DD", L"Bold"); tb1->Set(true);
	auto* tb2 = win.Add<VToggleButton>(L"\xE8DB", L"Italic");
	auto* tb3 = win.Add<VToggleButton>(L"\xE8DC");
	cell(0, L"VToggleButton", 36.0f, { tb1, tb2, tb3 });
	auto* split = win.AddChrome<VSplitButton>(L"Save"); split->Attach(&win); flyouts.push_back(split);
	split->Add(L"Save as\x2026", L"\xE792").Add(L"Save a copy", L"\xE8C8");
	split->OnClick([&] { say(L"Saved."); }); split->OnPick([&](int i) { say(L"Split button: " + split->At(i).label); });
	auto* link = win.Add<VLink>(L"VLink \x2014 a hyperlink"); link->OnClick([&] { say(L"Link clicked."); });
	cell(0, L"VSplitButton \x00B7 VLink", 36.0f, { split, link });
	auto* chk1 = win.Add<VCheck>(L"VCheck, on"); chk1->Set(true);
	auto* chk2 = win.Add<VCheck>(L"VCheck, off");
	cell(0, L"VCheck", 28.0f, { chk1, chk2 });
	auto* radio = win.Add<VRadio>(std::vector<std::wstring>{ L"One", L"Two", L"Three" }, true);
	cell(0, L"VRadio (horizontal)", 28.0f, { radio });
	auto* tog = win.Add<VToggle>(); tog->Set(true);
	auto* tog2 = win.Add<VToggle>();
	cell(0, L"VToggle", 28.0f, { tog, tog2 });
	auto* seg = win.Add<VSegment>(std::vector<std::wstring>{ L"Day", L"Week", L"Month", L"Year" }); seg->Set(1);
	cell(0, L"VSegment", 32.0f, { seg });

	// --- Input ----------------------------------------------------------------
	auto* input = win.Add<VChatInput>(); input->SetPlaceholder(L"VChatInput as a text field").SetSingleLine(true);
	cell(1, L"VChatInput (single line)", 36.0f, { input });
	auto* slider = win.Add<VSlider>(); slider->Set(0.6f);
	cell(1, L"VSlider", 28.0f, { slider });
	auto* step = win.Add<VStepper>(); step->Range(0, 20).Set(4);
	auto* rate = win.Add<VRating>(); rate->Set(3);
	cell(1, L"VStepper \x00B7 VRating", 34.0f, { step, rate });
	auto* drop = win.AddChrome<VDropDown>(); drop->Attach(&win); flyouts.push_back(drop);
	drop->Items({ L"Segoe UI", L"Consolas", L"Cascadia Code", L"Georgia" }).Prefix(L"Font: ");
	drop->OnChange([&](int i) { say(L"Dropdown: " + drop->SelectedText()); });
	cell(1, L"VDropDown", 32.0f, { drop });
	auto* date = win.AddChrome<VDatePicker>(); date->Attach(&win); flyouts.push_back(date);
	auto* time = win.AddChrome<VTimePicker>(); time->Attach(&win); flyouts.push_back(time); time->Set({ 14, 30 });
	date->OnChange([&](VDate d) { say(L"Date: " + vdate::Format(d)); });
	time->OnChange([&](VTime) { say(L"Time: " + time->Format()); });
	cell(1, L"VDatePicker \x00B7 VTimePicker", 34.0f, { date, time });
	auto* color = win.Add<VColorPicker>(); color->Set(vd::Col(0x4A90E2));
	color->OnChange([&](D2D1_COLOR_F c) { say(L"Colour " + vd::Hex(c)); });
	cell(1, L"VColorPicker", 200.0f, { color });

	// --- Status ---------------------------------------------------------------
	auto* prog = win.Add<VProgress>(L"VProgress"); prog->Set(0.62f);
	cell(2, L"VProgress", 30.0f, { prog });
	auto* ring1 = win.Add<VProgressRing>();
	auto* ring2 = win.Add<VProgressRing>(); ring2->Set(0.7f);
	auto* badge1 = win.Add<VBadge>(); badge1->Count(12);
	auto* badge2 = win.Add<VBadge>(); badge2->Count(120).Color(vd::Col(0x2563EB));
	auto* badge3 = win.Add<VBadge>(); badge3->Dot(true).Color(vd::Col(0x16A34A));
	cell(2, L"VProgressRing (busy, 70%) \x00B7 VBadge", 36.0f, { ring1, ring2, badge1, badge2, badge3 });
	auto* pp1 = win.Add<VPersonPicture>(); pp1->Name(L"Grace Hopper");
	auto* pp2 = win.Add<VPersonPicture>(); pp2->Name(L"Alan Turing");
	auto* pp3 = win.Add<VPersonPicture>(); pp3->Name(L"Ada");
	auto* pp4 = win.Add<VPersonPicture>();
	cell(2, L"VPersonPicture", 44.0f, { pp1, pp2, pp3, pp4 });
	const wchar_t* sevNames[4] = { L"Info", L"Success", L"Warning", L"Error" };
	for (int k = 0; k < 4; ++k) {
		auto* bar = win.Add<VInfoBar>();
		bar->Set((VSeverity)k, sevNames[k], L"An inline message the user can close.").Action(k == 0 ? L"Action" : L"");
		bar->OnClose([&, k] { say(std::wstring(sevNames[k]) + L" bar closed."); });
		bar->OnAction([&] { say(L"Info bar action."); });
		cell(2, std::wstring(L"VInfoBar \x00B7 ") + sevNames[k], 56.0f, { bar }, true);
	}

	// --- Navigation -----------------------------------------------------------
	auto* tabs = win.Add<VTabView>(std::vector<std::wstring>{ L"General", L"Advanced", L"History" });
	auto* tabCard = win.Add<VCard>();
	tabs->OnChange([&](int i) { say(L"Tab: " + tabs->Title(i)); });
	tabs->OnAdd([&] { tabs->Add(L"Tab " + std::to_wstring(tabs->Count() + 1)); repaint(); });
	cell(3, L"VTabView (+ a card below)", 120.0f, { tabs, tabCard }, true);
	auto* crumb = win.Add<VBreadcrumb>(); crumb->Items({ L"Home", L"Documents", L"Reports", L"2026", L"Q3.docx" });
	crumb->OnPick([&](int i) { say(L"Breadcrumb: " + crumb->At(i)); });
	cell(3, L"VBreadcrumb", 28.0f, { crumb }, true);
	auto* expander = win.Add<VExpander>(L"\xE7F4", L"VExpander", L"Click the header; the card opens");
	expander->Content(60.0f);
	auto* expInner = label(L"The content area. The app places children here from ContentRect().", 13.0f);
	cell(3, L"VExpander", 64.0f, { expander, expInner }, true);
	auto* menuBtn = win.Add<VButton>(); menuBtn->Text(L"Open VMenu");
	auto* menu = win.AddChrome<VMenu>(); menu->Attach(&win); flyouts.push_back(menu);
	menu->Add(L"New", L"\xE8A5", L"Ctrl+N").Add(L"Open\x2026", L"\xE8E5", L"Ctrl+O").Separator().Check(L"Word wrap", true).Separator().Add(L"Exit");
	menuBtn->OnClick([&] { menu->Open(menuBtn->GetBounds()); });
	menu->OnPick([&](int i) { say(L"Menu: " + menu->At(i).label); });
	auto* tipBtn = win.Add<VButton>(); tipBtn->Text(L"Show VTeachingTip");
	auto* tip = win.AddChrome<VTeachingTip>(); tip->Attach(&win); flyouts.push_back(tip);
	tip->Title(L"A teaching tip").Body(L"It points at what it explains, wraps its text, and goes away with the action, the cross, Esc or a click elsewhere.").Action(L"Got it");
	tipBtn->OnClick([&] { tip->Show(tipBtn->GetBounds()); });
	auto* dlgBtn = win.Add<VButton>(); dlgBtn->Text(L"Open VDialog");
	auto* dialog = win.AddChrome<VDialog>(); dialog->Attach(&win);
	dialog->Title(L"Delete 3 files?").Body(L"They will be moved to the Recycle Bin. You can restore them from there.").Primary(L"Delete").Secondary(L"Cancel");
	dlgBtn->OnClick([&] { dialog->Open(); });
	dialog->OnResult([&](int r) { say(r == 0 ? L"Dialog: Delete." : L"Dialog: Cancel."); });
	cell(3, L"VMenu \x00B7 VTeachingTip \x00B7 VDialog", 36.0f, { menuBtn, tipBtn, dlgBtn }, true);
	auto* menubar = win.AddChrome<VMenuBar>(); menubar->Attach(&win); flyouts.push_back(menubar);
	menubar->Menu(L"File").Add(L"New", L"\xE8A5", L"Ctrl+N").Add(L"Open\x2026", L"\xE8E5", L"Ctrl+O").Separator().Add(L"Exit")
	       .Menu(L"Edit").Add(L"Undo", L"\xE7A7", L"Ctrl+Z").Add(L"Redo", L"\xE7A6", L"Ctrl+Y")
	       .Menu(L"View").Check(L"Status bar", true).Check(L"Word wrap", false);
	menubar->OnPick([&](int m, int i) { say(L"Menu bar: " + menubar->At(m, i).label); });
	auto* cmd = win.AddChrome<VCommandBar>(); cmd->Attach(&win); flyouts.push_back(cmd);
	cmd->Add(L"\xE8A5", L"New").Add(L"\xE8E5", L"Open").Add(L"\xE74E", L"Save", L"Ctrl+S").Separator()
	   .Add(L"\xE8C8", L"Copy").Add(L"\xE77F", L"Paste").Separator().Add(L"\xE7A7", L"Undo").Add(L"\xE7A6", L"Redo").Add(L"\xE749", L"Print").Add(L"\xE72E", L"Share").Add(L"\xE713", L"Settings");
	cmd->OnPick([&](int i) { say(L"Command bar: " + cmd->At(i).label); });
	cell(3, L"VMenuBar \x00B7 VCommandBar (narrow the window: the bar overflows)", 40.0f, { menubar, cmd }, true);

	// --- Collections ----------------------------------------------------------
	auto* list = win.Add<VListView>(); list->Avatars(true);
	list->Items({ { L"Grace Hopper", L"Compiler pioneer", L"", true }, { L"Alan Turing", L"Mathematician", L"" }, { L"Ada Lovelace", L"First programmer", L"" },
	              { L"Margaret Hamilton", L"Apollo software", L"", true }, { L"Barbara Liskov", L"Substitution principle", L"" }, { L"Dennis Ritchie", L"C", L"" } });
	list->OnSelection([&] { say(std::to_wstring(list->SelectedCount()) + L" selected in the list."); });
	cell(4, L"VListView (Ctrl / Shift / Ctrl+A)", 240.0f, { list });
	auto* tree = win.Add<VTreeView>();
	tree->Set({ Branch(L"include", { Leaf(L"VirtualWidget.hpp"), Leaf(L"VControls.hpp"), Leaf(L"VNavigation.hpp"), Leaf(L"VCollections.hpp"), Leaf(L"VActions.hpp"), Leaf(L"VIndicators.hpp"), Leaf(L"VDraw.hpp") }, true),
	            Branch(L"src", { Branch(L"core", { Leaf(L"ChronoUI.cpp"), Leaf(L"ChronoStyles.cpp") }), Branch(L"examples", { Leaf(L"Settings.cpp"), Leaf(L"Mail.cpp"), Leaf(L"Photos.cpp"), Leaf(L"Catalog.cpp") }, true) }, true),
	            Branch(L"docs", { Leaf(L"EXAMPLES.md"), Leaf(L"WIDGETS.md") }) });
	tree->OnSelect([&](VTreeView::Node& n) { say(L"Tree: " + n.label); });
	cell(4, L"VTreeView", 240.0f, { tree });
	auto* grid = win.Add<VGridView>(); grid->TileSize(120.0f, 80.0f).Gap(10.0f);
	std::vector<VGridView::Tile> tiles;
	for (int i = 0; i < 12; ++i) tiles.push_back({ L"Tile " + std::to_wstring(i + 1), L"caption" });
	grid->Tiles(std::move(tiles));
	grid->Paint([](ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int i) {
		vd::Gradient(rt, r, vd::FromHSV((float)i * 30.0f, 0.5f, 0.95f), vd::FromHSV((float)i * 30.0f + 40.0f, 0.6f, 0.7f));
		vd::Text(rt, std::to_wstring(i + 1), r, vd::Col(0xFFFFFF, 0.9f), vd::Style().Size(24).Heavy().Center());
	});
	grid->OnSelection([&] { say(std::to_wstring(grid->SelectedCount()) + L" selected in the grid."); });
	cell(4, L"VGridView (painted tiles)", 200.0f, { grid }, true);

	// --- About ----------------------------------------------------------------
	auto* aboutCard = win.Add<VCard>();
	auto* a1 = label(L"Every virtual widget in the framework, one page per family.", 14.0f);
	auto* a2 = label(L"Headers: VirtualWidget, VirtualChat, VControls, VNavigation, VCollections, VActions, VIndicators, VDraw.", 13.0f, kMuted);
	auto* a3 = label(L"Open this after changing a header: if it looks right here, it looks right everywhere.", 13.0f, kMuted);
	cell(5, L"", 110.0f, { aboutCard, a1, a2, a3 }, true);

	// --- pages + layout ---------------------------------------------------------
	int current = 0;
	std::function<void()> layout;
	auto showPage = [&](int i) {
		current = i;
		for (size_t p = 0; p < pages.size(); ++p)
			for (auto& c : pages[p].cells) { c.caption->SetVisible((int)p == i); for (auto* w : c.widgets) w->SetVisible((int)p == i); }
		title->Text(pages[(size_t)i].title);
		layout();
	};
	layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		float navW = nav->CurrentWidth();
		nav->SetBounds(vd::Rect(0.0f, 0.0f, navW, H));
		float x = navW + 36.0f, w = W - x - 36.0f, colW = (w - 24.0f) * 0.5f;
		title ->SetBounds(vd::Rect(x, 22.0f, w, 36.0f));
		status->SetBounds(vd::Rect(x, H - 30.0f, w, 20.0f));
		for (VFlyout* f : flyouts) f->Cover(W, H);
		dialog->Cover(W, H);

		// Flow the current page's cells: two columns, wide cells take the row.
		float yL = 78.0f, yR = 78.0f; int col = 0;
		for (auto& c : pages[(size_t)current].cells) {
			float& y = c.wide ? yL : (col == 0 ? yL : yR);
			if (c.wide) { yL = yR = (std::max)(yL, yR); col = 0; }
			float cx = (c.wide || col == 0) ? x : x + colW + 24.0f, cw = c.wide ? w : colW;
			c.caption->SetBounds(vd::Rect(cx, y, cw, 18.0f));
			float wy = y + 22.0f, wx = cx;
			// Widgets in a cell sit side by side, each given a sensible width.
			for (auto* wd : c.widgets) {
				std::string t = wd->GetTypeName();
				float ww = cw, wh = c.h;
				if (t == "VButton" || t == "VSplitButton") ww = 130.0f;
				else if (t == "VToggleButton") ww = wd == tb3 ? 36.0f : 100.0f;
				else if (t == "VLink") ww = 200.0f;
				else if (t == "VCheck") ww = 150.0f;
				else if (t == "VToggle") ww = 60.0f;
				else if (t == "VStepper") ww = 130.0f;
				else if (t == "VRating") { ww = 130.0f; wh = 22.0f; wy = y + 28.0f; }
				else if (t == "VDatePicker" || t == "VTimePicker") ww = t == "VDatePicker" ? 200.0f : 110.0f;
				else if (t == "VProgressRing") ww = 32.0f;
				else if (t == "VBadge") { ww = 44.0f; wh = 20.0f; wy = y + 30.0f; }
				else if (t == "VPersonPicture") ww = 44.0f;
				else if (t == "VMenuBar") ww = 220.0f;
				else if (t == "VCommandBar") ww = cw - 232.0f;
				else if (t == "VListView" || t == "VTreeView") ww = cw;
				else if (t == "VTabView") wh = 40.0f;
				if (wd == tabCard)  { wd->SetBounds(vd::Rect(cx, wy + 40.0f, cw, c.h - 40.0f)); continue; }
				if (wd == expInner) { wd->SetBounds(vd::Rect(cx + 16.0f, wy + VExpander::kHeader + 16.0f, cw - 32.0f, 24.0f)); wd->SetVisible(current == 3 && expander->ContentVisible()); continue; }
				if (wd == expander) { wd->SetBounds(vd::Rect(cx, wy, cw, expander->ShownHeight())); continue; }
				if (wd == aboutCard) { wd->SetBounds(vd::Rect(cx, wy, cw, c.h)); continue; }
				if (wd == a1 || wd == a2 || wd == a3) { float k = wd == a1 ? 0.0f : (wd == a2 ? 1.0f : 2.0f); wd->SetBounds(vd::Rect(cx + 20.0f, wy + 18.0f + 26.0f * k, cw - 40.0f, 24.0f)); continue; }
				wd->SetBounds(vd::Rect(wx, wy, ww, wh));
				wx += ww + 14.0f;
			}
			float used = c.h + 22.0f + 22.0f;
			if (std::find(c.widgets.begin(), c.widgets.end(), expander) != c.widgets.end()) used = expander->ShownHeight() + 44.0f;
			y += used;
			if (!c.wide) col = 1 - col;
		}
		repaint();
	};
	expander->OnLayout(layout);
	nav->OnChange(showPage);
	nav->OnLayout([&](float) { layout(); });
	win.OnResize(layout);
	showPage(0);
	win.SetFocusWidget(nav);
	return win.RunMessageLoop();
}
