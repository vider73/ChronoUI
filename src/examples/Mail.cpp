// =============================================================================
// Mail.cpp — a three-pane mail client: folders, messages, and a reading pane
// with a command bar. Nothing is deleted without a dialog.
//
// What this example teaches:
//   * VCommandBar measures itself: the actions that fit are buttons, the rest
//     go behind "..." into the same kind of flyout the menus use.
//   * VDialog is modal. Open() shows it over a scrim and the answer arrives
//     through OnResult; nothing else in the window reacts meanwhile.
//   * VDropDown is a drawn combo box, so the whole window keeps one look.
//   * The message list is a VIEW: a vector of indices into the store, rebuilt
//     on every folder, search or sort change. The open message is remembered
//     by id, so it survives a re-sort.
//   * Small indicators — a progress ring while syncing, a rating for
//     importance, avatars from initials, an unread badge on the navigation.
//
// Build target: Mail. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
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

enum Folder { Inbox = 0, Starred, Sent, Drafts, Archive, Trash };
static const wchar_t* kFolderNames[] = { L"Inbox", L"Starred", L"Sent", L"Drafts", L"Archive", L"Trash" };

struct Msg {
	int id;
	std::wstring from, subject, preview, body, when;
	bool unread;
	int  stars;        // importance 0..3
	int  folder;
};

static std::vector<Msg> Seed() {
	std::vector<Msg> v = {
		{ 0, L"Grace Hopper",  L"Orbit launch checklist", L"Three items left before Friday.", L"Hi,\n\nThree items are still open on the launch checklist: the API rate limits, the status page, and the rollback script. I can take the first two if someone owns the rollback.\n\nGrace", L"09:42", true, 3, Inbox },
		{ 1, L"Alan Turing",   L"Q3 numbers", L"Revenue is up 4.0% on the quarter; the forecast attached.", L"Revenue is up 4.0% on the quarter, orders slightly down. The forecast is attached; the interesting column is churn, which halved.\n\nAlan", L"08:15", true, 2, Inbox },
		{ 2, L"Ada Lovelace",  L"Lunch Friday?", L"The new place by the river, 13:00?", L"The new place by the river opened last week. 13:00 on Friday? They take bookings for six.\n\nAda", L"Yesterday", false, 0, Inbox },
		{ 3, L"Billing",       L"Invoice INV-1186 paid", L"Tessera Tiles paid \x20AC" L"1,938.60.", L"Tessera Tiles paid invoice INV-1186 (\x20AC" L"1,938.60) on 2026-04-28. No action needed.", L"Yesterday", false, 0, Inbox },
		{ 4, L"Margaret Hamilton", L"Design review notes", L"Kanban cards should slide, not jump.", L"Notes from the review:\n\n- Kanban cards should slide, not jump, when one is dragged over them.\n- The gallery needs keyboard navigation.\n- Table headers sort on click; show the arrow.\n\nMargaret", L"Tue", true, 1, Inbox },
		{ 5, L"Travel desk",   L"Your flight to Lisbon", L"LIS, 14 Oct, 07:55. Check-in opens 24 h before.", L"Your flight to Lisbon is confirmed: 14 October, 07:55, gate to be announced. Check-in opens 24 hours before departure.", L"Tue", false, 0, Inbox },
		{ 6, L"Linus Torvalds", L"New ChronoUI release", L"The Settings example is out; have a look at the tree view.", L"The Settings example is out. The tree view is the part worth reading: state lives in the node, rows are flattened on every fold.\n\nLinus", L"Mon", false, 2, Inbox },
		{ 7, L"People team",   L"Welcome to the team", L"Your first week, in one page.", L"Welcome! Your first week in one page: badge on Monday, laptop on Monday, lunch with your team on Tuesday, and a walkthrough of the codebase on Wednesday.", L"Mon", false, 0, Inbox },
		{ 8, L"Ops",           L"Server maintenance window", L"Saturday 02:00-04:00 UTC, expect a short outage.", L"The API will be unavailable on Saturday between 02:00 and 04:00 UTC while we move to the new database host. Status page will be updated.", L"Sun", false, 1, Inbox },
		{ 9, L"Barbara Liskov", L"Re: Kanban board colours", L"Violet for bugs, green for docs, blue for features.", L"I would go with violet for bugs, green for docs and blue for features. Rose for anything urgent.\n\nBarbara", L"Sat", false, 0, Inbox },
		{ 10, L"Me",           L"Photos from the offsite", L"Uploaded to the shared album.", L"All photos from the offsite are in the shared album now. Feel free to add yours.", L"Fri", false, 0, Sent },
		{ 11, L"Me",           L"Contract renewal", L"Draft attached for your review.", L"Please find the draft renewal attached. Two changes from last year: the SLA and the payment terms.", L"Thu", false, 0, Sent },
		{ 12, L"Me",           L"Notes for the standup", L"(draft)", L"- Table example done\n- Settings example done\n- Mail example: today", L"Now", false, 0, Drafts },
		{ 13, L"Newsletter",   L"Ten things about Direct2D", L"Number seven will not surprise you.", L"A round-up of Direct2D tips. Number seven: create brushes on the fly, it is cheap.", L"Aug 30", false, 0, Archive },
	};
	return v;
}

static std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

// Wrapped body text. VLabel is one line; this is the paragraph version.
class VBody : public VirtualWidgetImpl {
	std::wstring m_text;
public:
	const char* GetTypeName() const override { return "VBody"; }
	VBody& Text(std::wstring t) { m_text = std::move(t); return *this; }
	void OnDraw(ID2D1RenderTarget* rt) override { vd::Text(rt, m_text, m_bounds, kText, vd::Style().Size(13).Wrap().Top()); }
};
class VCard : public VirtualWidgetImpl {
public:
	const char* GetTypeName() const override { return "VCard"; }
	void OnDraw(ID2D1RenderTarget* rt) override { vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f); vd::Stroke(rt, m_bounds, kBorder, 8.0f, 1.0f); }
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Mail", 1280, 820)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };
	auto label = [&](const std::wstring& text, float size = 13.0f, D2D1_COLOR_F col = kText) {
		VLabel* l = win.Add<VLabel>(); l->Text(text).FontSize(size).Color(col); return l;
	};

	std::vector<Msg> msgs = Seed();
	std::vector<int> view;              // indices into msgs, in list order
	int   folder = Inbox, currentId = -1;
	float syncLeft = 0.0f;

	// --- folders --------------------------------------------------------------
	auto* nav = win.Add<VNavView>(
		std::vector<VNavView::Item>{ { L"\xE715", L"Inbox" }, { L"\xE734", L"Starred" }, { L"\xE724", L"Sent" }, { L"\xE70F", L"Drafts" }, { L"\xE7B8", L"Archive" }, { L"\xE74D", L"Trash" } },
		std::vector<VNavView::Item>{ { L"\xE713", L"Settings" } });

	// --- the list column ------------------------------------------------------
	auto* search = win.Add<VChatInput>();
	search->SetPlaceholder(L"Search mail").SetSingleLine(true);
	auto* list = win.Add<VListView>();
	list->Multi(false).Avatars(true);
	auto* ring       = win.Add<VProgressRing>(); ring->Active(false);
	auto* listStatus = label(L"", 12.0f, kMuted);

	// --- the reading pane ----------------------------------------------------
	auto* crumb    = win.Add<VBreadcrumb>();
	auto* card     = win.Add<VCard>();
	auto* avatar   = win.Add<VPersonPicture>();
	auto* fromL    = label(L"", 14.0f);
	auto* whenL    = label(L"", 12.0f, kMuted);
	auto* subjectL = label(L"", 20.0f);
	auto* starsL   = label(L"Importance", 12.0f, kMuted);
	auto* stars    = win.Add<VRating>(); stars->Max(3);
	auto* body     = win.Add<VBody>();
	auto* link     = win.Add<VLink>(L"View attachment \x00B7 orbit-checklist.pdf");
	auto* empty    = label(L"Select a message to read it.", 14.0f, kMuted);
	auto* status   = label(L"Ready.", 12.0f, kMuted);
	std::vector<IVirtualWidget*> reading = { avatar, fromL, whenL, subjectL, starsL, stars, body, link };

	// --- chrome: dropdown, command bar, dialog (painted on top) -----------------
	auto* sort = win.AddChrome<VDropDown>(); sort->Attach(&win);
	sort->Items({ L"Newest first", L"Oldest first", L"Unread first", L"Sender A\x2013Z" }).Prefix(L"Sort: ");
	auto* bar = win.AddChrome<VCommandBar>(); bar->Attach(&win);
	bar->Add(L"\xE72B", L"Reply").Add(L"\xE72A", L"Forward").Separator()
	   .Add(L"\xE7B8", L"Archive").Add(L"\xE74D", L"Delete", L"Del").Separator()
	   .Add(L"\xE734", L"Flag").Add(L"\xE715", L"Mark unread").Add(L"\xE8DE", L"Move to\x2026").Add(L"\xE749", L"Print", L"Ctrl+P").Separator()
	   .Add(L"\xE895", L"Sync");
	enum Cmd { Reply = 0, Forward = 1, ArchiveCmd = 3, Delete = 4, Flag = 6, MarkUnread = 7, Move = 8, Print = 9, Sync = 11 };
	auto* dialog = win.AddChrome<VDialog>(); dialog->Attach(&win);
	dialog->Title(L"Delete message?").Primary(L"Delete").Secondary(L"Cancel");

	// --- state ----------------------------------------------------------------
	std::function<void()> layout;
	auto msgById = [&](int id) -> Msg* { for (auto& m : msgs) if (m.id == id) return &m; return nullptr; };
	auto rowOfId = [&](int id) { for (size_t i = 0; i < view.size(); ++i) if (msgs[(size_t)view[i]].id == id) return (int)i; return -1; };

	auto unreadCount = [&] { int n = 0; for (auto& m : msgs) if (m.folder == Inbox && m.unread) ++n; return n; };
	auto showMessage = [&](int id) {
		currentId = id;
		Msg* m = msgById(id);
		for (auto* w : reading) w->SetVisible(m != nullptr);
		empty->SetVisible(m == nullptr);
		if (!m) { crumb->Items({ L"Mail", kFolderNames[folder] }); repaint(); return; }
		if (m->unread) { m->unread = false; int r = rowOfId(id); if (r >= 0) list->Edit(r).unread = false; nav->Badge(Inbox, unreadCount()); }
		avatar->Name(m->from);
		fromL->Text(m->from); whenL->Text(m->when + L" \x00B7 to me"); subjectL->Text(m->subject);
		stars->Set(m->stars); body->Text(m->body);
		link->SetVisible(m->id == 0);
		crumb->Items({ L"Mail", kFolderNames[folder], m->subject });
		bar->Enable(ArchiveCmd, m->folder != Archive).Enable(Delete, true);
		repaint();
	};
	auto rebuild = [&] {
		std::wstring q = Lower(search->GetText());
		view.clear();
		for (size_t i = 0; i < msgs.size(); ++i) {
			const Msg& m = msgs[i];
			bool in = folder == Starred ? (m.stars > 0 && m.folder != Trash) : m.folder == folder;
			if (!in) continue;
			if (!q.empty() && Lower(m.from + L" " + m.subject + L" " + m.preview).find(q) == std::wstring::npos) continue;
			view.push_back((int)i);
		}
		switch (sort->SelectedIndex()) {
			case 1: std::reverse(view.begin(), view.end()); break;
			case 2: std::stable_partition(view.begin(), view.end(), [&](int i) { return msgs[(size_t)i].unread; }); break;
			case 3: std::stable_sort(view.begin(), view.end(), [&](int a, int b) { return msgs[(size_t)a].from < msgs[(size_t)b].from; }); break;
			default: break;
		}
		std::vector<VListView::Item> rows;
		for (int i : view) { const Msg& m = msgs[(size_t)i]; rows.push_back({ m.from, m.subject + L"  \x00B7  " + m.preview, L"", m.unread }); }
		list->Items(std::move(rows));
		int unread = 0; for (int i : view) unread += msgs[(size_t)i].unread ? 1 : 0;
		listStatus->Text(std::to_wstring(view.size()) + (view.size() == 1 ? L" message" : L" messages") + (unread ? L" \x00B7 " + std::to_wstring(unread) + L" unread" : L""));
		nav->Badge(Inbox, unreadCount());
		int r = rowOfId(currentId);
		if (r >= 0) list->Select(r); else showMessage(-1);
		repaint();
	};

	// --- layout ---------------------------------------------------------------
	layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		float navW = nav->CurrentWidth();
		nav->SetBounds(vd::Rect(0.0f, 0.0f, navW, H));
		float x1 = navW + 16.0f, w1 = 380.0f;
		search    ->SetBounds(vd::Rect(x1, 16.0f, w1, 36.0f));
		sort      ->SetBounds(vd::Rect(x1, 60.0f, 200.0f, 32.0f));
		list      ->SetBounds(vd::Rect(x1, 102.0f, w1, H - 102.0f - 48.0f));
		ring      ->SetBounds(vd::Rect(x1 + 2.0f, H - 36.0f, 20.0f, 20.0f));
		listStatus->SetBounds(vd::Rect(x1 + 30.0f, H - 38.0f, w1 - 30.0f, 24.0f));

		float x2 = x1 + w1 + 16.0f, w2 = W - x2 - 16.0f;
		crumb->SetBounds(vd::Rect(x2, 20.0f, w2, 28.0f));
		bar  ->SetBounds(vd::Rect(x2, 56.0f, w2, 40.0f));
		float cardTop = 104.0f, cardBot = H - 48.0f;
		card    ->SetBounds(D2D1::RectF(x2, cardTop, x2 + w2, cardBot));
		avatar  ->SetBounds(vd::Rect(x2 + 24.0f, cardTop + 24.0f, 44.0f, 44.0f));
		fromL   ->SetBounds(vd::Rect(x2 + 80.0f, cardTop + 22.0f, w2 - 300.0f, 24.0f));
		whenL   ->SetBounds(vd::Rect(x2 + 80.0f, cardTop + 46.0f, w2 - 300.0f, 20.0f));
		starsL  ->SetBounds(vd::Rect(x2 + w2 - 180.0f, cardTop + 22.0f, 100.0f, 20.0f));
		stars   ->SetBounds(vd::Rect(x2 + w2 - 180.0f, cardTop + 44.0f, 80.0f, 22.0f));
		subjectL->SetBounds(vd::Rect(x2 + 24.0f, cardTop + 86.0f, w2 - 48.0f, 30.0f));
		body    ->SetBounds(D2D1::RectF(x2 + 24.0f, cardTop + 130.0f, x2 + w2 - 24.0f, cardBot - 64.0f));
		link    ->SetBounds(vd::Rect(x2 + 24.0f, cardBot - 48.0f, w2 - 48.0f, 24.0f));
		empty   ->SetBounds(vd::Rect(x2 + 24.0f, cardTop + 24.0f, w2 - 48.0f, 24.0f));
		status  ->SetBounds(vd::Rect(x2, H - 38.0f, w2, 24.0f));
		sort->Cover(W, H); bar->Cover(W, H); dialog->Cover(W, H);
		repaint();
	};

	// --- wiring ---------------------------------------------------------------
	nav->OnChange([&](int i) {
		if (i == 6) { status->Text(L"Settings: see the Settings example."); nav->Select(folder); repaint(); return; }
		folder = i; currentId = -1; rebuild();
	});
	nav->OnLayout([&](float) { layout(); });
	search->OnTextChanged([&] { rebuild(); });
	sort->OnChange([&](int) { rebuild(); });
	list->OnSelection([&] {
		std::vector<int> sel = list->Selected();
		if (sel.empty()) return;
		showMessage(msgs[(size_t)view[(size_t)sel[0]]].id);
	});
	crumb->OnPick([&](int i) { if (i == 1) { list->Clear(); showMessage(-1); } else status->Text(L"Mail is the root."); });
	stars->OnChange([&](int v) { if (Msg* m = msgById(currentId)) { m->stars = v; status->Text(L"Importance set to " + std::to_wstring(v) + L"."); } });
	link->OnClick([&] { status->Text(L"Opening orbit-checklist.pdf (not really)."); repaint(); });
	dialog->OnResult([&](int r) {
		Msg* m = msgById(currentId);
		if (r == 0 && m) {
			if (m->folder == Trash) msgs.erase(std::find_if(msgs.begin(), msgs.end(), [&](const Msg& x) { return x.id == currentId; }));
			else m->folder = Trash;
			status->Text(L"Deleted \x201C" + std::wstring(m ? m->subject : L"") + L"\x201D.");
			currentId = -1; rebuild();
		} else status->Text(L"Kept.");
		repaint();
	});
	bar->OnPick([&](int i) {
		Msg* m = msgById(currentId);
		if (i == Sync) { ring->Active(true); syncLeft = 2.5f; status->Text(L"Syncing\x2026"); repaint(); return; }
		if (!m) { status->Text(L"Select a message first."); repaint(); return; }
		switch (i) {
			case Reply:      status->Text(L"Reply to " + m->from + L" (a compose window would open)."); break;
			case Forward:    status->Text(L"Forward \x201C" + m->subject + L"\x201D."); break;
			case ArchiveCmd: m->folder = Archive; status->Text(L"Archived."); currentId = -1; rebuild(); break;
			case Delete:     dialog->Body(L"\x201C" + m->subject + L"\x201D will move to Trash. You can bring it back from there." ); dialog->Open(); break;
			case Flag:       m->stars = m->stars ? 0 : 2; stars->Set(m->stars); status->Text(m->stars ? L"Flagged." : L"Flag removed."); break;
			case MarkUnread: m->unread = true; { int r = rowOfId(m->id); if (r >= 0) list->Edit(r).unread = true; } nav->Badge(Inbox, unreadCount()); status->Text(L"Marked as unread."); break;
			case Move:       status->Text(L"Move to\x2026 (a folder picker would open)."); break;
			case Print:      status->Text(L"Printing \x201C" + m->subject + L"\x201D."); break;
			default: break;
		}
		repaint();
	});
	win.OnTick([&](float dt) {
		if (syncLeft <= 0.0f) return;
		syncLeft -= dt;
		if (syncLeft <= 0.0f) { ring->Active(false); status->Text(L"Up to date."); repaint(); }
	});

	win.OnResize(layout);
	layout();
	rebuild();
	list->Select(0);
	win.SetFocusWidget(list);
	return win.RunMessageLoop();
}
