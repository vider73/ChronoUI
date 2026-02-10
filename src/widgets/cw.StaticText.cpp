#include <windows.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h> // IWICImagingFactory for Base64 images

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib") // For image loading

#include "WidgetImpl.hpp"
#include "ChronoStyles.hpp"

using namespace ChronoUI;

// --------------------------------------------------------------------------------------
// StaticText Implementation (Direct2D Port)
// --------------------------------------------------------------------------------------

class StaticText : public WidgetImpl {
	bool m_trackingMouse = false;

public:
	StaticText() {
		StyleManager::AddClass(this->GetContextNode(), "text");
	}

	const char* __stdcall GetControlName() override {
		return "StaticText";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
					"version": 1,
					"description": "A control with CSS state support (:hover, :disabled)",
					"properties": [
						{ "name": "title", "type": "string", "description": "Label text" }
					],
					"events": [
						{ "name": "onClick", "type": "action" }
					]
				})json";
	}

	// 1. Drawing Logic: Ported to Direct2D
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// Get dimensions directly from the Render Target
		D2D1_SIZE_F size = pRT->GetSize();

		// Create the bounding rectangle (D2D1_RECT_F)
		D2D1_RECT_F drawRect = D2D1::RectF(0.0f, 0.0f, size.width, size.height);

		// Draw Background using the D2D helper
		// Preserving original logic: hovereffect = false
		DrawWidgetBackground(pRT, drawRect, false);

		// Draw Text using the D2D helper
		std::string label = GetProperty("title");
		if (!label.empty()) {
			// Preserving original logic: allowHover = false
			DrawTextStyled(pRT, label, drawRect, false);
		}
	}

	// 2. Message Logic: Remains mostly standard Win32, 
	// though WM_PAINT is now handled by the engine calling OnDrawWidget
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
			case WM_LBUTTONDOWN: {
					SetFocus(m_hwnd);
					if (IsWindowEnabled(m_hwnd)) {
						FireEvent("onClick", "{}");
					}
				}
				return true; // Consumed
			case WM_ENABLE:
				// Force repaint when enabled state changes to render :disabled styles
				InvalidateRect(m_hwnd, NULL, FALSE);
				return true;
			}
		return false;
	}

	// 3. Focus Logic: Maintain parent notification behavior
	void OnFocus(bool f) override {
		// Old StaticText behavior: notify parent of focus change via custom message
		HWND parent = GetParent(m_hwnd);
		if (f && parent) {
			SendMessage(parent, WM_USER + 200, 0, (LPARAM)m_hwnd);
		}
		FireEvent(f ? "onFocus" : "onBlur", "{}");
	}

	virtual void OnPropertyChanged(const char* key, const char* value) override {
		WidgetImpl::OnPropertyChanged(key, value);
		// Specific property handling if needed
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new StaticText();
}