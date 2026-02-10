#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <windows.h>
#include <wrl/client.h> // For ComPtr

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "user32.lib")

using namespace ChronoUI;
using namespace Microsoft::WRL;

class AnalogClock : public WidgetImpl {
private:
	// --- Properties ---
	D2D1_COLOR_F m_secondHandColor = { 0.906f, 0.298f, 0.235f, 1.0f };
	const float PI = 3.1415926535f;

	// --- Resources (Cached for Performance) ---
	ComPtr<ID2D1SolidColorBrush> m_pBrushFace;
	ComPtr<ID2D1SolidColorBrush> m_pBrushFill;
	ComPtr<ID2D1SolidColorBrush> m_pBrushHand;
	ComPtr<ID2D1SolidColorBrush> m_pBrushSecond;
	ComPtr<ID2D1StrokeStyle> m_pRoundedStroke;

public:
	AnalogClock() {
		SetBoolProperty("show_seconds", true);
		SetBoolProperty("round_hover", true); // Used by background logic

		// Defaults
		SetProperty("face_color", "#212121");
		SetProperty("hand_color", "#121212");
		SetProperty("face_fill", "#fbfbfb");
	}

	virtual ~AnalogClock() {
		// Safety: Ensure timer is killed if the window is destroyed unexpectedly
		if (m_hwnd) ::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
	}

	// Release D2D resources when device is lost/reset
	void DiscardDeviceResources() {
		m_pBrushFace.Reset();
		m_pBrushFill.Reset();
		m_pBrushHand.Reset();
		m_pBrushSecond.Reset();
		m_pRoundedStroke.Reset();
		// Call base class to reset render target
		WidgetImpl::DiscardDeviceResources();
	}

	const char* __stdcall GetControlName() override { return "AnalogClock"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A modernized Direct2D analog clock",
            "properties": [
                { "name": "show_seconds", "type": "bool", "description": "Show the second hand" },
                { "name": "face_color", "type": "string", "description": "Color of the clock ring/ticks" },
                { "name": "face_fill", "type": "string", "description": "Background color of the clock dial" },
                { "name": "hand_color", "type": "string", "description": "Color of H/M hands" }
            ],
            "events": [
                { "name": "onClick", "type": "action", "description": "Triggered when the user clicks the control" }
            ]
        })json";
	}

	// Animation Loop: Called by WidgetImpl's WM_TIMER handler
	bool OnUpdateAnimation(float deltaTime) override {
		// Return true to trigger InvalidateRect (Redraw)
		return true;
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_CREATE:
			// Set 16ms timer (approx 60 FPS)
			::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 1000, NULL);
			return false; // Let base continue

		case WM_DESTROY:
			::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
			DiscardDeviceResources();
			return false;

		case WM_LBUTTONDOWN:
			::SetFocus(m_hwnd);
			FireEvent("onClick", "{}");
			// IMPORTANT: Return false so DefWindowProc can handle 
			// standard window activation/capture behavior.
			// Returning true might block the window from 'feeling' responsive.
			return false;
		}
		return false;
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		// If colors change, discard brushes so they get recreated in next Draw
		std::string k = key;
		if (k.find("color") != std::string::npos) {
			DiscardDeviceResources();
		}
		WidgetImpl::OnPropertyChanged(key, value);
	}

	// --- Direct2D Drawing Implementation ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 1. Setup Area
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// Draw standard widget background (handles margins/hover/selection)
		DrawWidgetBackground(pRT, rect, false);

		// 2. Geometry Calculation
		float fW = size.width;
		float fH = size.height;
		float minSide = (std::min)(fW, fH);
		float centerX = fW / 2.0f;
		float centerY = fH / 2.0f;

		float padding = minSide * 0.05f;
		float radius = (minSide / 2.0f) - padding;

		if (radius <= 1.0f) return;

		// 3. Create Resources (Only if missing)
		if (!m_pBrushFace) {
			pRT->CreateSolidColorBrush(CSSColorToD2D(GetStringProperty("face_color")), &m_pBrushFace);
			pRT->CreateSolidColorBrush(CSSColorToD2D(GetStringProperty("face_fill", "#222222")), &m_pBrushFill);
			pRT->CreateSolidColorBrush(CSSColorToD2D(GetStringProperty("hand_color")), &m_pBrushHand);
			pRT->CreateSolidColorBrush(m_secondHandColor, &m_pBrushSecond);

			// Create Stroke Style
			ComPtr<ID2D1Factory> pFactory;
			pRT->GetFactory(&pFactory);

			D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties();
			strokeProps.startCap = D2D1_CAP_STYLE_ROUND;
			strokeProps.endCap = D2D1_CAP_STYLE_ROUND;

			// Correct call on Factory
			pFactory->CreateStrokeStyle(strokeProps, nullptr, 0, &m_pRoundedStroke);
		}

		// Safety check if resource creation failed
		if (!m_pBrushFace || !m_pRoundedStroke) return;

		// 4. Draw Modern Clock Face
		D2D1_ELLIPSE clockFace = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius);

		// Fill background
		pRT->FillEllipse(clockFace, m_pBrushFill.Get());

		// Draw ring
		float strokeWidth = minSide / 35.0f;
		if (strokeWidth < 1.0f) strokeWidth = 1.0f;
		pRT->DrawEllipse(clockFace, m_pBrushFace.Get(), strokeWidth);

		// 5. Draw Ticks
		int ticks = 12;
		float tickLen = radius * 0.15f;

		for (int i = 0; i < ticks; i++) {
			float angle = (i * (360.0f / ticks)) * (PI / 180.0f);

			float x1 = centerX + cos(angle) * (radius - strokeWidth);
			float y1 = centerY + sin(angle) * (radius - strokeWidth);
			float x2 = centerX + cos(angle) * (radius - tickLen);
			float y2 = centerY + sin(angle) * (radius - tickLen);

			float tickStroke = (i % 3 == 0) ? strokeWidth * 1.5f : strokeWidth;

			pRT->DrawLine(
				D2D1::Point2F(x1, y1),
				D2D1::Point2F(x2, y2),
				m_pBrushFace.Get(),
				tickStroke,
				m_pRoundedStroke.Get()
			);
		}

		// 6. Draw Hands
		SYSTEMTIME st;
		GetLocalTime(&st);

		auto DrawHand = [&](float val, float maxVal, float lenScale, float widthScale, ID2D1Brush* brush) {
			float angleDeg = (val / maxVal) * 360.0f;
			float angleRad = (angleDeg - 90.0f) * (PI / 180.0f);

			float handLen = radius * lenScale;
			float handW = strokeWidth * widthScale;
			if (handW < 1.0f) handW = 1.0f;

			float tailLen = handLen * 0.15f;

			float xStart = centerX - cos(angleRad) * tailLen;
			float yStart = centerY - sin(angleRad) * tailLen;
			float xEnd = centerX + cos(angleRad) * handLen;
			float yEnd = centerY + sin(angleRad) * handLen;

			pRT->DrawLine(
				D2D1::Point2F(xStart, yStart),
				D2D1::Point2F(xEnd, yEnd),
				brush,
				handW,
				m_pRoundedStroke.Get()
			);
		};

		// Hour
		float hourVal = (float)(st.wHour % 12) + (st.wMinute / 60.0f);
		DrawHand(hourVal, 12.0f, 0.55f, 2.0f, m_pBrushHand.Get());

		// Minute
		DrawHand((float)st.wMinute + (st.wSecond / 60.0f), 60.0f, 0.85f, 1.5f, m_pBrushHand.Get());

		// Second
		if (GetBoolProperty("show_seconds")) {
			DrawHand((float)st.wSecond, 60.0f, 0.90f, 0.8f, m_pBrushSecond.Get());

			float dotSize = strokeWidth * 1.5f;
			D2D1_ELLIPSE centerDot = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), dotSize, dotSize);
			pRT->FillEllipse(centerDot, m_pBrushSecond.Get());

			D2D1_ELLIPSE innerDot = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), dotSize * 0.3f, dotSize * 0.3f);
			pRT->FillEllipse(innerDot, m_pBrushFill.Get());
		}
		else {
			float capSize = strokeWidth * 2.0f;
			D2D1_ELLIPSE centerCap = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), capSize, capSize);
			pRT->FillEllipse(centerCap, m_pBrushHand.Get());
		}
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new AnalogClock();
}