// =============================================================================
// VirtualHello.cpp — first proof of the virtual-widget model.
//
// What this demonstrates:
//   * One HWND for the whole window (no per-widget child windows).
//   * Three widgets — a title label, a counter label, and a button — all
//     drawn onto the same D2D render target by the host.
//   * Mouse hover, press, and click routed in C++ via VirtualWindow's
//     internal hit-testing.
//
// Compare against HelloWorld.cpp (which uses the legacy HWND-per-widget
// stack). The visible output is similar; the internal architecture is
// fundamentally different.
// =============================================================================

#include <windows.h>
#include <string>

#include "VirtualWidget.hpp"

using namespace ChronoUI;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	// Initialize the controller so D2D / DWrite factories exist. (Touching the
	// instance is enough — its constructor runs Initialize() with refcount=1.)
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"Virtual Hello", 480, 280)) return 1;

	// Hero label, top.
	auto* title = win.Add<VLabel>();
	title->Text(L"Virtual widgets are running.")
	      .FontSize(18.0f)
	      .Color(D2D1::ColorF(0x111111));
	title->SetBounds(D2D1::RectF(24, 20, 456, 60));

	// Counter label, center. Updates when the button is clicked.
	auto* counter = win.Add<VLabel>();
	counter->Text(L"Clicks: 0")
	        .FontSize(14.0f)
	        .Color(D2D1::ColorF(0x444444));
	counter->SetBounds(D2D1::RectF(24, 80, 456, 130));

	// Click counter state captured into the button lambda.
	auto count = std::make_shared<int>(0);

	auto* btn = win.Add<VButton>();
	btn->Text(L"Click me");
	btn->SetBounds(D2D1::RectF(180, 180, 300, 220));
	btn->OnClick([count, counter, &win]() {
		(*count)++;
		std::wstring s = L"Clicks: " + std::to_wstring(*count);
		counter->Text(s);
		InvalidateRect(win.GetHWND(), NULL, FALSE);
	});

	return win.RunMessageLoop();
}
