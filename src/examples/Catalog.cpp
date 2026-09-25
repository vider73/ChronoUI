// =============================================================================
// Catalog.cpp — every virtual widget in one window, one page per family.
//
// This is the reference sheet: open it after touching a header and every
// control is on screen in its resting state, ready to be clicked. The pages
// are a VNavView; a page is a list of cells (a caption above a widget) that
// layout() flows into two columns.
//
// Build target: Catalog. Headers only.
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
#include "VText.hpp"
#include "VMedia.hpp"
#include "VLayout.hpp"
#include "VDraw.hpp"
#include "AppPaths.hpp"

using namespace ChronoUI;


class VCard : public VirtualWidgetImpl {
public:
	const char* GetTypeName() const override { return "VCard"; }
	void OnDraw(ID2D1RenderTarget* rt) override { vd::Fill(rt, m_bounds, vctl::Surface(), 8.0f); vd::Stroke(rt, m_bounds, vctl::Border(), 8.0f, 1.0f); }
};

struct Cell { VLabel* caption; std::vector<IVirtualWidget*> widgets; float h; bool wide; };
struct Page { std::wstring title; std::vector<Cell> cells; };

static VTreeView::Node Branch(const wchar_t* label, std::vector<VTreeView::Node> kids, bool open = false) {
	VTreeView::Node n; n.label = label; n.kids = std::move(kids); n.open = open; return n;
}
static VTreeView::Node Leaf(const wchar_t* label) { VTreeView::Node n; n.label = label; return n; }

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR cmdLine, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Catalog", 1280, 860)) return 1;
	// "dark" on the command line starts in the dark theme; the toggle at the top right switches live.
	vtheme::SetDark(cmdLine && wcsstr(cmdLine, L"dark") != nullptr);
	win.SetBackground(vtheme::Current().window);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };
	// Labels take their colour from the theme at draw time (ink, or muted), so they follow the toggle.
	auto label = [&](const std::wstring& text, float size = 13.0f, bool muted = false) {
		VLabel* l = win.Add<VLabel>(); l->Text(text).FontSize(size).Muted(muted); return l;
	};
	auto* darkLbl = win.AddChrome<VLabel>(); darkLbl->Text(L"Dark").FontSize(12.0f).Muted(true);
	auto* dark = win.AddChrome<VToggle>(); dark->Set(vtheme::IsDark());
	dark->OnChange([&](bool on) { vtheme::SetDark(on); win.SetBackground(vtheme::Current().window); repaint(); });

	auto* nav = win.Add<VNavView>(
		std::vector<VNavView::Item>{ { L"\xE8FD", L"Basics" }, { L"\xE70F", L"Input" }, { L"\xE946", L"Status" }, { L"\xE8A5", L"Navigation" },
		                             { L"\xE8FD", L"Collections" }, { L"\xE8D2", L"Text & media" }, { L"\xE8A1", L"Layout" } },
		std::vector<VNavView::Item>{ { L"\xE946", L"About" } });
	nav->Badge(2, 3);
	auto* title  = label(L"", 26.0f);
	auto* status = label(L"Click anything; this line says what happened.", 12.0f, true);
	std::vector<Page> pages(8);
	pages[0].title = L"Basics"; pages[1].title = L"Input"; pages[2].title = L"Status"; pages[3].title = L"Navigation";
	pages[4].title = L"Collections"; pages[5].title = L"Text & media"; pages[6].title = L"Layout"; pages[7].title = L"About";
	std::vector<VFlyout*> flyouts;
	auto say = [&](const std::wstring& s) { status->Text(s); repaint(); };

	// cell(page, caption, height, widgets...) — the caption label is created here.
	auto cell = [&](int page, const std::wstring& caption, float h, std::vector<IVirtualWidget*> ws, bool wide = false) {
		pages[(size_t)page].cells.push_back({ label(caption, 12.0f, true), std::move(ws), h, wide });
	};

	// --- Basics -------------------------------------------------------------
	auto* lbl = label(L"VLabel \x2014 one line of text, a size, a colour.", 14.0f);
	cell(0, L"VLabel", 24.0f, { lbl });
	auto* btn = win.Add<VButton>(); btn->Text(L"VButton");
	auto* btn2 = win.Add<VButton>(); btn2->Text(L"Secondary").Face(vctl::Track()).FaceHover(vctl::Outline()).FacePress(vctl::Dim()).TextColor(vctl::Ink());
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
	int held = 0;
	auto* rep = win.Add<VRepeatButton>(); rep->Text(L"Hold me");
	rep->OnClick([&] { say(L"Repeat button fired " + std::to_wstring(++held) + L" times."); });
	auto* tsplit = win.AddChrome<VToggleSplitButton>(L"Bullets"); tsplit->Attach(&win); flyouts.push_back(tsplit);
	tsplit->Add(L"Numbered", L"\xE8FD").Add(L"Checklist", L"\xE73A");
	tsplit->OnChange([&](bool on) { say(on ? L"Bullets on." : L"Bullets off."); });
	tsplit->OnPick([&](int i) { say(L"Toggle split button: " + tsplit->At(i).label); });
	cell(0, L"VRepeatButton (hold it) \x00B7 VToggleSplitButton", 36.0f, { rep, tsplit });
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
	auto* num = win.Add<VNumberBox>(); num->Range(0.0, 100.0).Step(5.0).Set(42.0);
	num->OnChange([&](double v) { say(L"Number: " + vd::Num(v)); });
	auto* num2 = win.Add<VNumberBox>(); num2->Decimals(2).Step(0.25).Set(3.5).Spin(false).Placeholder(L"2*(3+4)");
	num2->OnChange([&](double v) { say(L"Number: " + vd::Num(v, 2)); });
	auto* pwd = win.Add<VPasswordBox>();
	pwd->OnSubmit([&](const std::wstring& s) { say(L"Password of " + std::to_wstring(s.size()) + L" characters submitted."); });
	cell(1, L"VNumberBox (spin \x00B7 type an expression, Enter) \x00B7 VPasswordBox (hold the eye)", 34.0f, { num, num2, pwd }, true);
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
	auto* cal = win.Add<VCalendarView>();
	cal->OnChange([&](VDate d) { say(L"Calendar: " + vdate::Format(d)); });
	cell(1, L"VCalendarView (the header zooms out to months)", 290.0f, { cal });

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
	auto* selbar = win.Add<VSelectorBar>(std::vector<VSelectorBar::Item>{ { L"\xE823", L"Recent" }, { L"\xE8D6", L"Shared" }, { L"\xE734", L"Favourites" } });
	selbar->OnChange([&](int i) { say(L"Selector bar: " + selbar->Label(i)); });
	auto* pivot = win.Add<VPivot>(std::vector<std::wstring>{ L"Overview", L"Details", L"Reviews" });
	pivot->OnChange([&](int i) { say(L"Pivot: " + pivot->Label(i)); });
	cell(3, L"VSelectorBar \x00B7 VPivot", 40.0f, { selbar, pivot }, true);
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
	auto* cbfBtn = win.Add<VButton>(); cbfBtn->Text(L"Command flyout");
	auto* cbf = win.AddChrome<VCommandBarFlyout>(); cbf->Attach(&win); flyouts.push_back(cbf);
	cbf->Add(L"\xE8C8", L"Copy").Add(L"\xE77F", L"Paste").Add(L"\xE8C6", L"Cut").Add(L"\xE74D", L"Delete")
	   .AddSecondary(L"Select all", L"\xE8B3", L"Ctrl+A").AddSecondary(L"Rename\x2026", L"\xE8AC").Separator().AddSecondary(L"Properties", L"\xE946");
	cbfBtn->OnClick([&] { cbf->Show(cbfBtn->GetBounds()); });
	cbf->OnPick([&](int i) { say(L"Command bar flyout: " + cbf->At(i).label); });
	cell(3, L"VMenu \x00B7 VTeachingTip \x00B7 VDialog \x00B7 VCommandBarFlyout", 36.0f, { menuBtn, tipBtn, dlgBtn, cbfBtn }, true);
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
	cell(4, L"VListView (Ctrl / Shift / Ctrl+A)", 200.0f, { list });
	auto* tree = win.Add<VTreeView>();
	tree->Set({ Branch(L"include", { Leaf(L"VirtualWidget.hpp"), Leaf(L"VControls.hpp"), Leaf(L"VNavigation.hpp"), Leaf(L"VCollections.hpp"), Leaf(L"VActions.hpp"), Leaf(L"VText.hpp"), Leaf(L"VLayout.hpp") }, true),
	            Branch(L"src", { Branch(L"core", { Leaf(L"ChronoUI.cpp"), Leaf(L"ChronoStyles.cpp") }), Branch(L"examples", { Leaf(L"Settings.cpp"), Leaf(L"Mail.cpp"), Leaf(L"Booking.cpp"), Leaf(L"Catalog.cpp") }, true) }, true),
	            Branch(L"docs", { Leaf(L"EXAMPLES.md"), Leaf(L"WIDGETS.md") }) });
	tree->OnSelect([&](VTreeView::Node& n) { say(L"Tree: " + n.label); });
	cell(4, L"VTreeView", 200.0f, { tree });
	auto* grid = win.Add<VGridView>(); grid->TileSize(120.0f, 80.0f).Gap(10.0f);
	std::vector<VGridView::Tile> tiles;
	for (int i = 0; i < 12; ++i) tiles.push_back({ L"Tile " + std::to_wstring(i + 1), L"caption" });
	grid->Tiles(std::move(tiles));
	grid->Paint([](ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int i) {
		vd::Gradient(rt, r, vd::FromHSV((float)i * 30.0f, 0.5f, 0.95f), vd::FromHSV((float)i * 30.0f + 40.0f, 0.6f, 0.7f));
		vd::Text(rt, std::to_wstring(i + 1), r, vd::Col(0xFFFFFF, 0.9f), vd::Style().Size(24).Heavy().Center());
	});
	grid->OnSelection([&] { say(std::to_wstring(grid->SelectedCount()) + L" selected in the grid."); });
	cell(4, L"VGridView (painted tiles)", 190.0f, { grid }, true);
	auto* flip = win.Add<VFlipView>(); flip->Count(5);
	flip->Paint([](ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int i) {
		vd::Gradient(rt, r, vd::FromHSV((float)i * 72.0f, 0.55f, 0.85f), vd::FromHSV((float)i * 72.0f + 30.0f, 0.7f, 0.55f));
		vd::Text(rt, L"Page " + std::to_wstring(i + 1), r, vd::Col(0xFFFFFF), vd::Style().Size(28).Heavy().Center());
	});
	auto* pips = win.Add<VPipsPager>(); pips->Count(5).Arrows(true);
	flip->OnChange([&](int i) { pips->Select(i); say(L"Flip view: page " + std::to_wstring(i + 1)); });
	pips->OnChange([&](int i) { flip->Select(i); });
	cell(4, L"VFlipView (arrows, keys, wheel) \x00B7 VPipsPager", 190.0f, { flip, pips });
	auto* ann = win.Add<VAnnotatedScrollBar>();
	ann->Labels({ { 0.0f, L"2026" }, { 0.3f, L"2025" }, { 0.62f, L"2024" }, { 0.9f, L"Older" } }).ThumbSize(0.22f);
	ann->OnChange([&](float v) { say(L"Annotated scrollbar at " + vd::Num(v * 100.0) + L"%."); });
	cell(4, L"VAnnotatedScrollBar (drag the thumb)", 190.0f, { ann });

	// --- Text & media ----------------------------------------------------------
	auto* rich = win.Add<VRichText>();
	rich->Set(L"**VRichText** wraps a paragraph with *italic*, **bold**, `code` and a [link to the README](https://github.com/vider73/ChronoUI). "
	          L"Links underline, light up under the mouse and fire OnLink with their url; MeasureHeight(width) tells the layout how tall the text is.");
	rich->OnLink([&](const std::wstring& u) { say(L"Link: " + u); });
	cell(5, L"VRichText", 60.0f, { rich }, true);
	auto* ic1 = win.Add<VIcon>(L"\xE80F");
	auto* ic2 = win.Add<VIcon>(L"\xE8BD"); ic2->Size(22.0f).Color(vd::Col(0xDB2777));
	auto* ic3 = win.Add<VIcon>(L"\xE734"); ic3->Size(28.0f).Color(vd::Col(0xF59E0B));
	auto* ai1 = win.Add<VAnimatedIcon>(L"\xE8BD");
	auto* ai2 = win.Add<VAnimatedIcon>(L"\xE734"); ai2->Accent(vd::Col(0xF59E0B));
	auto* ai3 = win.Add<VAnimatedIcon>(L"\xE74D"); ai3->Accent(vd::Col(0xDC2626));
	for (auto* ai : { ai1, ai2, ai3 }) ai->OnClick([&] { say(L"Animated icon clicked."); });
	cell(5, L"VIcon \x00B7 VAnimatedIcon (hover, click)", 40.0f, { ic1, ic2, ic3, ai1, ai2, ai3 });
	auto* sh1 = win.Add<VShape>(VShape::Kind::Rectangle); sh1->Fill(vd::Col(0x4A90E2)).Radius(8.0f);
	auto* sh2 = win.Add<VShape>(VShape::Kind::Ellipse);   sh2->Fill(vd::Col(0xF59E0B, 0.25f)).Stroke(vd::Col(0xF59E0B), 3.0f);
	auto* sh3 = win.Add<VShape>(VShape::Kind::Polygon);   sh3->Points({ { 0.5f, 0.0f }, { 1.0f, 0.38f }, { 0.8f, 1.0f }, { 0.2f, 1.0f }, { 0.0f, 0.38f } }).Fill(vd::Col(0x16A34A)).Stroke(vd::Col(0x14532D), 2.0f);
	auto* sh4 = win.Add<VShape>(VShape::Kind::Line);      sh4->Points({ { 0.0f, 1.0f }, { 1.0f, 0.0f } }).Stroke(vd::Col(0xDC2626), 4.0f);
	cell(5, L"VShape: rectangle, ellipse, polygon, line", 70.0f, { sh1, sh2, sh3, sh4 });
	auto* img = win.Add<VImage>(); img->Source(AssetPathW(L"images\\example1.jpg")).Radius(8.0f);
	auto* img2 = win.Add<VImage>(); img2->Source(AssetPathW(L"images\\example1.jpg")).Mode(VImage::Stretch::UniformToFill).Radius(60.0f);
	auto* img3 = win.Add<VImage>(); img3->Source(L"missing.png").Radius(8.0f);
	cell(5, L"VImage: Uniform \x00B7 UniformToFill in a circle \x00B7 a missing file", 120.0f, { img, img2, img3 }, true);

	// --- Layout ------------------------------------------------------------------
	auto* scroll = win.Add<VScrollViewer>(); scroll->Content(900.0f, 700.0f);
	scroll->Paint([](ID2D1RenderTarget* rt, const D2D1_RECT_F&) {
		for (int y = 0; y < 7; ++y) for (int x = 0; x < 9; ++x)
			vd::Fill(rt, vd::Rect((float)x * 100.0f, (float)y * 100.0f, 100.0f, 100.0f), ((x + y) & 1) ? vctl::Subtle() : vd::Alpha(vctl::Blue(), 0.18f));
		vd::Text(rt, L"900 \x00D7 700 of content. Wheel scrolls, Shift+wheel sideways, Ctrl+wheel zooms about the cursor, drag pans.", vd::Rect(20.0f, 16.0f, 860.0f, 40.0f), vctl::Ink(), vd::Style().Size(16).Bold());
		for (int k = 0; k < 6; ++k) vd::Circle(rt, 150.0f + (float)k * 120.0f, 400.0f, 40.0f, vd::FromHSV((float)k * 60.0f, 0.6f, 0.9f));
	});
	cell(6, L"VScrollViewer (900 \x00D7 700 of painted content)", 180.0f, { scroll }, true);
	auto* splitCard = win.Add<VCard>();
	auto* splitv = win.Add<VSplitView>(); splitv->PaneWidth(200.0f);
	auto* paneLbl = label(L"The pane", 13.0f);
	auto* contentLbl = label(L"The content. PaneRect() and ContentRect() place the children.", 13.0f, true);
	auto* splitBtn = win.Add<VButton>(); splitBtn->Text(L"Toggle pane");
	splitBtn->OnClick([&] { splitv->Toggle(); say(splitv->IsOpen() ? L"Pane opening." : L"Pane closing."); });
	auto* splitMode = win.Add<VSegment>(std::vector<std::wstring>{ L"Inline", L"Overlay", L"Compact inline", L"Compact overlay" });
	splitMode->OnChange([&](int i) {
		static const VSplitView::Mode modes[4] = { VSplitView::Mode::Inline, VSplitView::Mode::Overlay, VSplitView::Mode::CompactInline, VSplitView::Mode::CompactOverlay };
		splitv->SetMode(modes[i]); say(L"Split view mode: " + std::wstring(i == 0 ? L"Inline" : i == 1 ? L"Overlay" : i == 2 ? L"Compact inline" : L"Compact overlay"));
	});
	cell(6, L"VSplitView (the mode below; Toggle opens and closes the pane)", 150.0f, { splitCard, splitv, paneLbl, contentLbl, splitBtn }, true);
	cell(6, L"", 32.0f, { splitMode }, true);
	auto* p1 = win.Add<VCard>(); auto* p2 = win.Add<VCard>();
	auto* two = win.Add<VTwoPaneView>(); two->Threshold(700.0f).Split(0.4f);
	auto* p1l = label(L"Pane 1 \x2014 drag the divider. Narrow the window: they stack.", 13.0f);
	auto* p2l = label(L"Pane 2", 13.0f);
	cell(6, L"VTwoPaneView", 140.0f, { p1, p2, two, p1l, p2l }, true);

	// --- About ----------------------------------------------------------------
	auto* aboutCard = win.Add<VCard>();
	auto* a1 = label(L"Every virtual widget in the framework, one page per family.", 14.0f);
	auto* a2 = label(L"Headers: VirtualWidget, VirtualChat, VControls, VNavigation, VCollections, VActions, VIndicators, VText, VMedia, VLayout, VDraw.", 13.0f, true);
	auto* a3 = label(L"Open this after changing a header: if it looks right here, it looks right everywhere.", 13.0f, true);
	cell(7, L"", 110.0f, { aboutCard, a1, a2, a3 }, true);

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
		darkLbl->SetBounds(vd::Rect(W - 116.0f, 24.0f, 40.0f, 24.0f)); dark->SetBounds(vd::Rect(W - 76.0f, 24.0f, 50.0f, 24.0f));
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
			D2D1_RECT_F body = vd::Rect(cx, wy, cw, c.h);
			// Widgets in a cell sit side by side, each given a sensible width.
			for (auto* wd : c.widgets) {
				std::string t = wd->GetTypeName();
				float ww = cw, wh = c.h;
				if (t == "VButton" || t == "VSplitButton" || t == "VRepeatButton" || t == "VToggleSplitButton") ww = 130.0f;
				else if (t == "VToggleButton") ww = wd == tb3 ? 36.0f : 100.0f;
				else if (t == "VLink") ww = 200.0f;
				else if (t == "VCheck") ww = 150.0f;
				else if (t == "VToggle") ww = 60.0f;
				else if (t == "VStepper") ww = 130.0f;
				else if (t == "VRating") { ww = 130.0f; wh = 22.0f; wy = y + 28.0f; }
				else if (t == "VDatePicker" || t == "VTimePicker") ww = t == "VDatePicker" ? 200.0f : 110.0f;
				else if (t == "VNumberBox") ww = 150.0f;
				else if (t == "VPasswordBox") ww = 200.0f;
				else if (t == "VProgressRing") ww = 32.0f;
				else if (t == "VBadge") { ww = 44.0f; wh = 20.0f; wy = y + 30.0f; }
				else if (t == "VPersonPicture") ww = 44.0f;
				else if (t == "VMenuBar") ww = 220.0f;
				else if (t == "VCommandBar") ww = cw - 232.0f;
				else if (t == "VSelectorBar" || t == "VPivot") ww = cw * 0.5f - 12.0f;
				else if (t == "VListView" || t == "VTreeView") ww = cw;
				else if (t == "VTabView") wh = 40.0f;
				else if (t == "VFlipView") wh = c.h - 30.0f;
				else if (t == "VAnnotatedScrollBar") ww = 140.0f;
				else if (t == "VIcon") ww = 36.0f;
				else if (t == "VAnimatedIcon") ww = 44.0f;
				else if (t == "VShape") ww = 70.0f;
				else if (t == "VImage") ww = 180.0f;
				if (wd == tabCard)  { wd->SetBounds(vd::Rect(cx, wy + 40.0f, cw, c.h - 40.0f)); continue; }
				if (wd == expInner) { wd->SetBounds(vd::Rect(cx + 16.0f, wy + VExpander::kHeader + 16.0f, cw - 32.0f, 24.0f)); wd->SetVisible(current == 3 && expander->ContentVisible()); continue; }
				if (wd == expander) { wd->SetBounds(vd::Rect(cx, wy, cw, expander->ShownHeight())); continue; }
				if (wd == pips)     { wd->SetBounds(vd::Rect(cx, wy + c.h - 24.0f, cw, 24.0f)); continue; }
				if (wd == splitCard || wd == splitv) { wd->SetBounds(body); continue; }
				if (wd == paneLbl)    { D2D1_RECT_F p = splitv->PaneRect(); wd->SetBounds(vd::Rect(p.left + 16.0f, p.top + 12.0f, vd::W(p) - 24.0f, 24.0f)); wd->SetVisible(current == 6 && splitv->PaneOpen()); continue; }
				if (wd == contentLbl) { D2D1_RECT_F r = splitv->ContentRect(); wd->SetBounds(vd::Rect(r.left + 16.0f, r.top + 12.0f, vd::W(r) - 24.0f, 24.0f)); continue; }
				if (wd == splitBtn)   { D2D1_RECT_F r = splitv->ContentRect(); wd->SetBounds(vd::Rect(r.left + 16.0f, r.bottom - 52.0f, 130.0f, 36.0f)); continue; }
				if (wd == two)        { wd->SetBounds(body); continue; }
				if (wd == p1)  { two->SetBounds(body); wd->SetBounds(two->Pane1Rect()); continue; }
				if (wd == p2)  { two->SetBounds(body); wd->SetBounds(two->Pane2Rect()); continue; }
				if (wd == p1l) { D2D1_RECT_F r = two->Pane1Rect(); wd->SetBounds(vd::Rect(r.left + 16.0f, r.top + 12.0f, vd::W(r) - 24.0f, 24.0f)); continue; }
				if (wd == p2l) { D2D1_RECT_F r = two->Pane2Rect(); wd->SetBounds(vd::Rect(r.left + 16.0f, r.top + 12.0f, vd::W(r) - 24.0f, 24.0f)); continue; }
				if (wd == aboutCard) { wd->SetBounds(body); continue; }
				if (wd == a1 || wd == a2 || wd == a3) { float k = wd == a1 ? 0.0f : (wd == a2 ? 1.0f : 2.0f); wd->SetBounds(vd::Rect(cx + 20.0f, wy + 18.0f + 26.0f * k, cw - 40.0f, 24.0f)); continue; }
				wd->SetBounds(vd::Rect(wx, wy, ww, wh));
				wx += ww + 14.0f;
			}
			float used = c.h + 22.0f + 22.0f;
			if (std::find(c.widgets.begin(), c.widgets.end(), expander) != c.widgets.end()) used = expander->ShownHeight() + 44.0f;
			y += used;
			if (c.wide) yR = yL; else col = 1 - col;      // a wide cell moves both columns down
		}
		repaint();
	};
	expander->OnLayout(layout);
	splitv->OnLayout(layout);
	two->OnLayout(layout);
	nav->OnChange(showPage);
	nav->OnLayout([&](float) { layout(); });
	win.OnResize(layout);
	// A page number on the command line opens that page (tools/shoot.ps1 -ExeArgs "5").
	int first = (cmdLine && *cmdLine) ? (std::max)(0, (std::min)((int)pages.size() - 1, _wtoi(cmdLine))) : 0;
	nav->Select(first);
	showPage(first);
	win.SetFocusWidget(nav);
	return win.RunMessageLoop();
}
