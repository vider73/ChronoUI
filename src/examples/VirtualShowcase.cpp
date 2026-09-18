// =============================================================================
// VirtualShowcase.cpp — a tour of the virtual-widget model in one window.
//
// This is the example to read after VirtualHello.cpp. It uses only the
// framework (ChronoUI.dll + VirtualWidget.hpp + VirtualChat.hpp), no LLM, no
// tools, and shows every host feature an app built on VirtualWindow leans on:
//
//   * CUSTOM CHROME — the OS title bar is gone; the header is painted by us
//     (a VLabel, a VCombo, VButtons) and still drags / resizes / maximizes.
//   * TWO LAYERS — chrome widgets (AddChrome) are pinned in viewport space;
//     content widgets (Add) live in a scrollable layer clipped to the safe
//     area set with SetSafeAreaInsets. The sidebar, header and input strip
//     never move; the conversation scrolls under them.
//   * CHAT WIDGETS — VChatBubble (user / assistant, wrapped text, inline
//     ```code``` fences, streamed reveal), VCodeBlock, VTypingIndicator and
//     VChatInput (multi-line editor with caret, selection, clipboard).
//   * ANIMATION — a custom widget (VPulse) that asks for frames from
//     OnUpdate(dt); the host's 60 Hz heartbeat drives it and the streaming.
//   * TOOLTIPS — SetTooltip on any VirtualWidgetImpl; the host renders them.
//   * FILE DROP — EnableFileDrop + OnFilesDropped: drop files from Explorer
//     onto the window and they land in the conversation.
//   * SCROLL AWARENESS — a "scroll to bottom" FAB that only shows when the
//     user has scrolled up (IsAtBottom), re-pinned from OnScroll / OnTick.
//   * LAYOUT PASS — one Relayout() computes every bound from the client
//     size, measuring each item with MeasureHeight(width). Called on resize,
//     on every list change, and while a bubble is still streaming (its
//     height grows as text is revealed).
//
// Build: target VirtualShowcase (links ChronoUI only). Run it, type, press
// Enter, drop a file, resize the window, scroll up.
// =============================================================================

#include <windows.h>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

#include "VirtualWidget.hpp"
#include "VirtualChat.hpp"

using namespace ChronoUI;

// -----------------------------------------------------------------------------
// VPulse — the smallest possible animated custom widget. Three expanding rings
// fading out, restarting every 1.6 s. OnUpdate returns true = "give me another
// frame"; the host invalidates the window when any widget says so.
// -----------------------------------------------------------------------------
class VPulse : public VirtualWidgetImpl {
	float m_t = 0.0f;
	bool  m_running = true;
public:
	const char* GetTypeName() const override { return "VPulse"; }
	void  SetRunning(bool v) { m_running = v; }
	bool  IsRunning() const  { return m_running; }

	bool OnUpdate(float dt) override {
		if (!m_running) return false;
		m_t = std::fmod(m_t + dt, 1.6f);
		return true;
	}
	void OnDraw(ID2D1RenderTarget* pRT) override {
		float cx = (m_bounds.left + m_bounds.right) * 0.5f;
		float cy = (m_bounds.top + m_bounds.bottom) * 0.5f;
		float maxR = (std::min)(Width(), Height()) * 0.5f - 2.0f;
		for (int i = 0; i < 3; ++i) {
			float phase = std::fmod(m_t / 1.6f + i / 3.0f, 1.0f);   // 0..1
			float r = 6.0f + phase * (maxR - 6.0f);
			float a = (1.0f - phase) * 0.55f;
			ComPtr<ID2D1SolidColorBrush> b;
			pRT->CreateSolidColorBrush(D2D1::ColorF(0x4A90E2, a), &b);
			if (b) pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b.Get(), 2.0f);
		}
		ComPtr<ID2D1SolidColorBrush> core;
		pRT->CreateSolidColorBrush(D2D1::ColorF(m_running ? 0x4A90E2 : 0x9CA3AF), &core);
		if (core) pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), core.Get());
	}
};

// -----------------------------------------------------------------------------
// VPanel — a flat rounded rectangle used as the sidebar / header / input-strip
// background. Decorative chrome: CanFocus() is false, so dragging the header
// over it still moves the window (see the WM_NCHITTEST rule in VirtualWindow).
// -----------------------------------------------------------------------------
class VPanel : public VirtualWidgetImpl {
	D2D1_COLOR_F m_fill   = D2D1::ColorF(0xF3F4F6);
	D2D1_COLOR_F m_border = D2D1::ColorF(0xE5E7EB);
	float        m_radius = 0.0f;
public:
	const char* GetTypeName() const override { return "VPanel"; }
	VPanel& Fill(D2D1_COLOR_F c)   { m_fill = c; return *this; }
	VPanel& Border(D2D1_COLOR_F c) { m_border = c; return *this; }
	VPanel& Radius(float r)        { m_radius = r; return *this; }
	void OnDraw(ID2D1RenderTarget* pRT) override {
		ComPtr<ID2D1SolidColorBrush> f, b;
		pRT->CreateSolidColorBrush(m_fill, &f);
		pRT->CreateSolidColorBrush(m_border, &b);
		D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_bounds, m_radius, m_radius);
		if (f) pRT->FillRoundedRectangle(rr, f.Get());
		if (b) pRT->DrawRoundedRectangle(rr, b.Get(), 1.0f);
	}
};

// -----------------------------------------------------------------------------
// The app.
// -----------------------------------------------------------------------------
namespace {

	constexpr float kTitleH   = 44.0f;
	constexpr float kSidebarW = 240.0f;
	constexpr float kInputH   = 76.0f;
	constexpr float kGap      = 10.0f;

	struct Item {
		IVirtualWidget*               w;
		std::function<float(float)>   measure;   // width -> height
	};

	struct App {
		VirtualWindow win;

		// chrome
		VPanel*  header   = nullptr;
		VLabel*  title    = nullptr;
		VCombo*  speed    = nullptr;
		VButton* btnClear = nullptr;
		VButton* btnMin   = nullptr;
		VButton* btnMax   = nullptr;
		VButton* btnClose = nullptr;

		VPanel*  sidebar  = nullptr;
		VLabel*  sbTitle  = nullptr;
		VLabel*  sbHint   = nullptr;
		VButton* btnHello = nullptr;
		VButton* btnCode  = nullptr;
		VButton* btnLong  = nullptr;
		VButton* btnPulse = nullptr;
		VButton* btnOff   = nullptr;
		VPulse*  pulse    = nullptr;
		VLabel*  stats    = nullptr;

		VPanel*     strip = nullptr;
		VChatInput* input = nullptr;
		VButton*    send  = nullptr;
		VButton*    fab   = nullptr;

		// content
		std::vector<Item>  items;
		VTypingIndicator*  typing   = nullptr;   // content widget, toggled
		VChatBubble*       streaming = nullptr;
		float              cps = 90.0f;
		int                replies = 0;

		// ---- content helpers -------------------------------------------------
		VChatBubble* AddBubble(VChatBubble::Role role, const std::wstring& text, bool stream) {
			auto* b = win.Add<VChatBubble>();
			b->SetRole(role);
			if (stream) b->StreamFrom(text, cps); else b->SetText(text);
			items.push_back({ b, [b](float w) { return b->MeasureHeight(w); } });
			return b;
		}
		VCodeBlock* AddCode(const std::wstring& lang, const std::wstring& code) {
			auto* c = win.Add<VCodeBlock>();
			c->SetLanguage(lang).SetCode(code);
			items.push_back({ c, [c](float w) { return c->MeasureHeight(w); } });
			return c;
		}
		void Reply(const std::wstring& text) {
			if (streaming && streaming->IsStreaming()) return;   // one at a time
			streaming = AddBubble(VChatBubble::Assistant, text, /*stream*/ true);
			++replies;
			Relayout();
			win.ScrollToBottom();
		}
		void Clear() {
			for (auto& it : items) win.RemoveWidget(it.w);
			items.clear();
			streaming = nullptr;
			Relayout();
		}

		std::wstring CannedReply(const std::wstring& userText) {
			switch (replies % 4) {
			case 0: return L"You typed **" + userText + L"**. Every bubble here is a VChatBubble: "
				L"wrapped by DirectWrite, revealed at " + std::to_wstring((int)cps) +
				L" chars/s by the host heartbeat, selectable with the mouse, Ctrl+C to copy.";
			case 1: return L"Bubbles parse ```code fences``` inline, so a reply can carry code:\n"
				L"```cpp\nauto* b = win.Add<VChatBubble>();\nb->SetRole(VChatBubble::Assistant);\n"
				L"b->StreamFrom(L\"hello\", 90.0f);\n```\nand the bubble grows as the text streams in.";
			case 2: return L"The sidebar, header and this input strip are CHROME (AddChrome): they sit in "
				L"viewport space and never scroll. The conversation is CONTENT (Add): it scrolls "
				L"under them and is clipped to the safe area. Try the wheel, then the ↓ button.";
			default: return L"Drop a file from Explorer anywhere on this window — EnableFileDrop hands "
				L"the paths to OnFilesDropped. Hover any button on the left for its tooltip.";
			}
		}

		// ---- layout ----------------------------------------------------------
		void Relayout() {
			RECT rc; GetClientRect(win.GetHWND(), &rc);
			float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);

			// Header (viewport space).
			header->SetBounds(D2D1::RectF(0, 0, W, kTitleH));
			title->SetBounds(D2D1::RectF(16, 0, 360, kTitleH));
			float x = W - 40.0f;
			btnClose->SetBounds(D2D1::RectF(x, 8, x + 32, kTitleH - 8)); x -= 38;
			btnMax->SetBounds(D2D1::RectF(x, 8, x + 32, kTitleH - 8));   x -= 38;
			btnMin->SetBounds(D2D1::RectF(x, 8, x + 32, kTitleH - 8));   x -= 46;
			btnClear->SetBounds(D2D1::RectF(x - 70, 8, x, kTitleH - 8)); x -= 80;
			speed->SetBounds(D2D1::RectF(x - 150, 8, x, kTitleH - 8));

			// Sidebar (viewport space).
			sidebar->SetBounds(D2D1::RectF(0, kTitleH, kSidebarW, H));
			float y = kTitleH + 16;
			sbTitle->SetBounds(D2D1::RectF(16, y, kSidebarW - 16, y + 24)); y += 32;
			sbHint->SetBounds(D2D1::RectF(16, y, kSidebarW - 16, y + 40));  y += 48;
			for (VButton* b : { btnHello, btnCode, btnLong, btnPulse, btnOff }) {
				b->SetBounds(D2D1::RectF(16, y, kSidebarW - 16, y + 36)); y += 44;
			}
			y += 8;
			pulse->SetBounds(D2D1::RectF(16, y, kSidebarW - 16, y + 110)); y += 118;
			stats->SetBounds(D2D1::RectF(16, y, kSidebarW - 16, y + 40));

			// Input strip (viewport space).
			strip->SetBounds(D2D1::RectF(kSidebarW, H - kInputH, W, H));
			float inputRight = W - 110.0f;
			float inputH = (std::min)(input->MeasureHeight(inputRight - kSidebarW - 32.0f), kInputH - 20.0f);
			float inputTop = H - kInputH + (kInputH - inputH) * 0.5f;
			input->SetBounds(D2D1::RectF(kSidebarW + 16, inputTop, inputRight, inputTop + inputH));
			send->SetBounds(D2D1::RectF(W - 96, H - kInputH + 18, W - 16, H - 18));
			fab->SetBounds(D2D1::RectF(W - 56, H - kInputH - 52, W - 16, H - kInputH - 12));

			// Content (content space): stack every item with its measured height.
			win.SetSafeAreaInsets(kTitleH, 0, kInputH, kSidebarW);
			float colLeft = kSidebarW + 24.0f, colW = W - kSidebarW - 48.0f;
			std::vector<VStackItem> stack;
			stack.reserve(items.size() + 1);
			for (auto& it : items) stack.push_back({ it.w, it.measure(colW) });
			bool showTyping = streaming && streaming->IsStreaming();
			typing->SetVisible(showTyping);
			if (showTyping) stack.push_back({ typing, 28.0f });
			float bottom = StackVertical(stack, colLeft, kTitleH + 16.0f, colW, kGap);
			win.SetContentHeight(bottom + 16.0f);

			fab->SetVisible(!win.IsAtBottom());
			stats->Text(std::to_wstring(items.size()) + L" items · " + std::to_wstring(replies) + L" replies");
			InvalidateRect(win.GetHWND(), NULL, FALSE);
		}
	};

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();   // D2D / DWrite factories

	App app;
	if (!app.win.Create(hInstance, L"ChronoUI — Virtual Showcase", 1100, 720,
	                    /*customChrome*/ true, kTitleH, /*deferShow*/ true)) return 1;

	// ---- chrome: header --------------------------------------------------------
	app.header = app.win.AddChrome<VPanel>();
	app.header->Fill(D2D1::ColorF(0xFFFFFF)).Border(D2D1::ColorF(0xE5E7EB));
	app.title = app.win.AddChrome<VLabel>();
	app.title->Text(L"ChronoUI · Virtual Showcase").FontSize(16.0f).Color(D2D1::ColorF(0x111827));

	app.speed = app.win.AddChrome<VCombo>();
	app.speed->Items({ L"Reply: slow", L"Reply: normal", L"Reply: fast", L"Reply: instant" }).Select(1);
	app.speed->SetTooltip(L"Characters per second the assistant bubble reveals at");
	app.speed->OnChange([&](size_t i) {
		static const float cps[] = { 30.0f, 90.0f, 300.0f, 100000.0f };
		app.cps = cps[i < 4 ? i : 1];
	});

	auto ghost = [](VButton* b) {
		b->Face(D2D1::ColorF(0xF3F4F6)).FaceHover(D2D1::ColorF(0xE5E7EB)).FacePress(D2D1::ColorF(0xD1D5DB))
		 .TextColor(D2D1::ColorF(0x374151)).CornerRadius(6.0f);
	};
	app.btnClear = app.win.AddChrome<VButton>(); app.btnClear->Text(L"Clear"); ghost(app.btnClear);
	app.btnClear->SetTooltip(L"Remove every bubble (RemoveWidget keeps hover/focus consistent)");
	app.btnClear->OnClick([&] { app.Clear(); });
	app.btnMin   = app.win.AddChrome<VButton>(); app.btnMin->Text(L"–"); ghost(app.btnMin);
	app.btnMin->OnClick([&] { ShowWindow(app.win.GetHWND(), SW_MINIMIZE); });
	app.btnMax   = app.win.AddChrome<VButton>(); app.btnMax->Text(L"□"); ghost(app.btnMax);
	app.btnMax->OnClick([&] {
		HWND h = app.win.GetHWND();
		ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
	});
	app.btnClose = app.win.AddChrome<VButton>(); app.btnClose->Text(L"×");
	app.btnClose->Face(D2D1::ColorF(0xF3F4F6)).FaceHover(D2D1::ColorF(0xEF4444)).FacePress(D2D1::ColorF(0xB91C1C))
	             .TextColor(D2D1::ColorF(0x374151)).CornerRadius(6.0f);
	app.btnClose->OnClick([&] { PostMessage(app.win.GetHWND(), WM_CLOSE, 0, 0); });

	// ---- chrome: sidebar -------------------------------------------------------
	app.sidebar = app.win.AddChrome<VPanel>();
	app.sidebar->Fill(D2D1::ColorF(0xF9FAFB)).Border(D2D1::ColorF(0xE5E7EB));
	app.sbTitle = app.win.AddChrome<VLabel>();
	app.sbTitle->Text(L"Try things").FontSize(15.0f).Color(D2D1::ColorF(0x111827));
	app.sbHint = app.win.AddChrome<VLabel>();
	app.sbHint->Text(L"Chrome never scrolls; content does.").FontSize(12.0f).Color(D2D1::ColorF(0x6B7280));

	app.btnHello = app.win.AddChrome<VButton>(); app.btnHello->Text(L"Assistant says hello");
	app.btnHello->SetTooltip(L"Adds a streamed assistant bubble");
	app.btnHello->OnClick([&] { app.Reply(L"Hello! I am a **VChatBubble** streamed by the heartbeat. "
		L"Resize the window while I type — the layout pass re-measures me every frame."); });

	app.btnCode = app.win.AddChrome<VButton>(); app.btnCode->Text(L"Add a code block"); ghost(app.btnCode);
	app.btnCode->SetTooltip(L"VCodeBlock: monospace, dark, selectable, Ctrl+C");
	app.btnCode->OnClick([&] {
		app.AddCode(L"cpp",
			L"// one HWND, many widgets\n"
			L"VirtualWindow win;\n"
			L"win.Create(hInst, L\"Demo\", 900, 600, /*customChrome*/ true);\n"
			L"auto* label = win.Add<VLabel>();\n"
			L"label->Text(L\"hello\").FontSize(14.0f);\n"
			L"label->SetBounds(D2D1::RectF(16, 16, 300, 40));\n"
			L"return win.RunMessageLoop();");
		app.Relayout(); app.win.ScrollToBottom();
	});

	app.btnLong = app.win.AddChrome<VButton>(); app.btnLong->Text(L"Fill the page"); ghost(app.btnLong);
	app.btnLong->SetTooltip(L"Adds a dozen bubbles so there is something to scroll");
	app.btnLong->OnClick([&] {
		for (int i = 1; i <= 12; ++i) {
			bool user = (i % 3 == 0);
			app.AddBubble(user ? VChatBubble::User : VChatBubble::Assistant,
				user ? L"Message " + std::to_wstring(i) + L" from the user."
				     : L"Filler bubble " + std::to_wstring(i) + L". Scroll up: the ↓ button appears "
				       L"(IsAtBottom is false) and stays pinned in the corner because it is chrome.",
				/*stream*/ false);
		}
		app.Relayout(); app.win.ScrollToTop();
	});

	app.btnPulse = app.win.AddChrome<VButton>(); app.btnPulse->Text(L"Pause animation"); ghost(app.btnPulse);
	app.btnPulse->SetTooltip(L"VPulse::OnUpdate returns false while paused — no frames are requested");
	app.btnPulse->OnClick([&] {
		app.pulse->SetRunning(!app.pulse->IsRunning());
		app.btnPulse->Text(app.pulse->IsRunning() ? L"Pause animation" : L"Resume animation");
		InvalidateRect(app.win.GetHWND(), NULL, FALSE);
	});

	app.btnOff = app.win.AddChrome<VButton>(); app.btnOff->Text(L"Disabled button"); app.btnOff->Enabled(false);
	app.btnOff->SetTooltip(L"Enabled(false): muted face, no hover, clicks swallowed");

	app.pulse = app.win.AddChrome<VPulse>();
	app.pulse->SetTooltip(L"A 40-line custom widget: OnUpdate(dt) + OnDraw");
	app.stats = app.win.AddChrome<VLabel>();
	app.stats->FontSize(12.0f).Color(D2D1::ColorF(0x6B7280));

	// ---- chrome: input strip + FAB --------------------------------------------
	app.strip = app.win.AddChrome<VPanel>();
	app.strip->Fill(D2D1::ColorF(0xFFFFFF)).Border(D2D1::ColorF(0xE5E7EB));
	app.input = app.win.AddChrome<VChatInput>();
	app.input->SetPlaceholder(L"Type a message and press Enter (Shift+Enter for a newline)…").SetMaxLines(3);
	auto submit = [&](const std::wstring& text) {
		if (text.empty()) return;
		app.AddBubble(VChatBubble::User, text, false);
		app.input->SetText(L"");
		app.Reply(app.CannedReply(text));
	};
	app.input->OnSubmit(submit);
	app.input->OnTextChanged([&] { app.Relayout(); });   // the strip grows up to 3 lines
	app.send = app.win.AddChrome<VButton>(); app.send->Text(L"Send");
	app.send->OnClick([&] { submit(app.input->GetText()); });
	app.fab = app.win.AddChrome<VButton>(); app.fab->Text(L"↓"); ghost(app.fab); app.fab->CornerRadius(20.0f);
	app.fab->SetTooltip(L"Scroll to bottom");
	app.fab->OnClick([&] { app.win.ScrollToBottom(); app.Relayout(); });

	// ---- content: typing indicator (toggled by Relayout) + opening message -----
	app.typing = app.win.Add<VTypingIndicator>();
	app.typing->SetVisible(false);
	app.AddBubble(VChatBubble::Assistant,
		L"Welcome to the **virtual-widget** showcase. One HWND, one render target, and "
		L"everything you see is a C++ object painted by the host. Type below, press the "
		L"buttons on the left, drop a file on the window, resize, scroll.", false);

	// ---- host hooks ------------------------------------------------------------
	app.win.OnResize([&] { app.Relayout(); });
	app.win.OnScroll([&] { app.fab->SetVisible(!app.win.IsAtBottom()); });
	app.win.OnTick([&](float) {
		// While a reply streams its height changes every frame: re-stack and
		// keep the view glued to the bottom. When it finishes, hide the dots.
		if (app.streaming) {
			bool wasAtBottom = app.win.IsAtBottom();
			app.Relayout();
			if (wasAtBottom) app.win.ScrollToBottom();
			if (!app.streaming->IsStreaming()) app.streaming = nullptr;
		}
	});
	app.win.EnableFileDrop();
	app.win.OnFilesDropped([&](const std::vector<std::wstring>& paths) {
		std::wstring msg = L"Dropped " + std::to_wstring(paths.size()) + L" file(s):";
		for (auto& p : paths) msg += L"\n• " + p;
		app.AddBubble(VChatBubble::User, msg, false);
		app.Reply(L"Got them. In ChronoChat this is where the files would become attachments "
		          L"and show up in the Assets panel; here they are just a bubble.");
	});

	app.Relayout();
	app.win.SetFocusWidget(app.input);
	ShowWindow(app.win.GetHWND(), SW_SHOW);
	UpdateWindow(app.win.GetHWND());
	return app.win.RunMessageLoop();
}
