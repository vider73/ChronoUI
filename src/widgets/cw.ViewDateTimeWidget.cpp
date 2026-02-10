#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <ctime>

#include "WidgetImpl.hpp"
#include "ChronoStyles.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
// Remove Gdiplus lib since we are porting to D2D
// #pragma comment(lib, "gdiplus.lib") 

using namespace ChronoUI;

class ViewDateTimeWidget : public WidgetImpl {
public:
	ViewDateTimeWidget() {
		StyleManager::AddClass(this->GetContextNode(), "text");
	}

	const char* __stdcall GetControlName() override { return "ViewDateTimeWidget"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
        "version": 2,
        "description": "Modern Direct2D system clock with date",
        "properties": [],
        "events": [
            { "name": "onClick", "type": "action" },
            { "name": "onFocus", "type": "action" },
            { "name": "onBlur", "type": "action" }
        ],
        "methods": []
    })json";
	}

	// Override OnMessage to handle input and timer setup specific to this widget
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_CREATE:
			// Start a 1-second timer for the clock updates.
			// The base WidgetImpl handles WM_TIMER by calling OnUpdate and InvalidateRect.
			if (m_hwnd) ::SetTimer(m_hwnd, 1, 1000, NULL);
			return false; // Let base continue processing

		case WM_DESTROY:
			if (m_hwnd) ::KillTimer(m_hwnd, 1);
			return false;

		case WM_LBUTTONDOWN:
			// Handle Click
			if (m_hwnd) ::SetFocus(m_hwnd);
			FireEvent("onClick", "{}");
			return true; // Message handled

		default:
			return false;
		}
	}

	// Override OnFocus to fire specific events expected by the manifest
	virtual void OnFocus(bool f) override {
		FireEvent(f ? "onFocus" : "onBlur", "{}");
	}

	// New Direct2D Painting Entry Point
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 0. Setup Bounds
		D2D1_SIZE_F size = pRT->GetSize();
		// Use parenthesis for min/max as requested
		float w = (std::max)(size.width, 0.0f);
		float h = (std::max)(size.height, 0.0f);
		D2D1_RECT_F rect = D2D1::RectF(0.0f, 0.0f, w, h);

		// 1. Draw Background
		// This helper handles the "2026" look (glassmorphism, rounded corners, borders)
		// based on CSS properties stored in the base class.
		DrawWidgetBackground(pRT, rect, true);

		// 2. Generate Time/Date String
		SYSTEMTIME st;
		::GetLocalTime(&st);
		wchar_t timeStr[64], dateStr[64];

		// 0 in flags ensures seconds are shown based on locale defaults
		::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, timeStr, 64);
		::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, dateStr, 64);

		// Combine logic: Time on top, Date below
		std::wstring wCombined = std::wstring(timeStr) + L"\n" + std::wstring(dateStr);

		// Convert Wide String to std::string using provided inline helper
		std::string combinedLabel = WideToNarrow(wCombined);

		// 3. Draw Text
		// DrawTextStyled handles font-family, font-size, alignment (center/center usually),
		// DPI scaling, and coloring.
		if (!combinedLabel.empty()) {
			DrawTextStyled(pRT, combinedLabel, rect, false);
		}
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new ViewDateTimeWidget();
}