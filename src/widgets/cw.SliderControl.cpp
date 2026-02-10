#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

using namespace ChronoUI;

class SliderControl : public WidgetImpl {
	// Interaction State
	bool m_isDragging = false;

public:
	SliderControl() {
		// Initialize Default Properties
		SetProperty("value", "0");
		SetProperty("min", "0");
		SetProperty("max", "100");

		// Style Properties
		SetProperty("track-color", "#e0e0e0");     // Inactive track
		SetProperty("active-color", "#1a73e8");    // Active track & Thumb
		SetProperty("track-height", "4");
		SetProperty("thumb-radius", "8");          // Base radius
		SetProperty("thumb-shadow", "true");       // Modern depth effect
	}

	virtual ~SliderControl() {}

	const char* __stdcall GetControlName() override { return "MaterialSlider"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A Modern Direct2D hardware-accelerated slider.",
            "properties": [
                { "name": "value", "type": "int", "description": "Current position" },
                { "name": "min", "type": "int", "description": "Minimum value" },
                { "name": "max", "type": "int", "description": "Maximum value" },
                { "name": "track-color", "type": "color", "description": "Color of the unfilled track" },
                { "name": "active-color", "type": "color", "description": "Color of the filled track and thumb" },
                { "name": "track-height", "type": "int", "description": "Height of the track line in pixels" },
                { "name": "thumb-radius", "type": "int", "description": "Radius of the thumb in pixels" }
            ],
            "events": [
                { "name": "onChange", "description": "Fired when the user stops dragging" },
                { "name": "onInput", "description": "Fired while dragging" },
                { "name": "onFocus", "description": "Fired when gained focus" },
                { "name": "onBlur", "description": "Fired when lost focus" }
            ]
        })json";
	}

	// --- Message Handling ---

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_ERASEBKGND:
			return true; // D2D handles background clearing

		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		case WM_ENABLE:
			InvalidateRect(m_hwnd, NULL, FALSE);
			return false;

		case WM_LBUTTONDOWN:
			OnMouseDown(GET_X_LPARAM(lp));
			return true;

		case WM_LBUTTONUP:
			OnMouseUp();
			return true;

		case WM_MOUSEMOVE:
			OnMouseMove(GET_X_LPARAM(lp));
			return true;

		case WM_KEYDOWN:
			OnKeyDown(wp);
			return true;
		}
		return false;
	}

	// --- Interaction Logic ---

	void OnMouseDown(int x) {
		if (!::IsWindowEnabled(m_hwnd)) return;

		m_isDragging = true;
		SetCapture(m_hwnd);
		SetFocus(m_hwnd);

		UpdateValueFromPos(x);
		InvalidateRect(m_hwnd, NULL, FALSE);
	}

	void OnMouseUp() {
		if (!m_isDragging) return;

		m_isDragging = false;
		ReleaseCapture();
		InvalidateRect(m_hwnd, NULL, FALSE);

		int val = GetIntProperty("value");
		std::string payload = "{ \"value\": " + std::to_string(val) + " }";
		FireEvent("onChange", payload.c_str());
	}

	void OnMouseMove(int x) {
		if (!m_isDragging) return;
		UpdateValueFromPos(x);
	}

	void OnKeyDown(WPARAM key) {
		if (!::IsWindowEnabled(m_hwnd)) return;

		int min = GetIntProperty("min");
		int max = GetIntProperty("max");
		int val = GetIntProperty("value");

		int step = (std::max)(1, (max - min) / 20);

		int newValue = val;

		if (key == VK_LEFT || key == VK_DOWN) newValue -= step;
		else if (key == VK_RIGHT || key == VK_UP) newValue += step;
		else return;

		SetValueInternal(newValue);
	}

	// --- Core Math ---

	void UpdateValueFromPos(int mouseX) {
		RECT rc;
		GetClientRect(m_hwnd, &rc);
		float width = (float)(rc.right - rc.left);

		int thumbR = GetCSSIntStyle("thumb-radius", 8);
		float padding = ScaleF((float)thumbR);
		float availableWidth = width - (padding * 2.0f);

		if (availableWidth <= 0) return;

		float relativeX = (float)mouseX - padding;
		float ratio = relativeX / availableWidth;

		// Parenthesis usage as requested
		ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));

		int min = GetIntProperty("min");
		int max = GetIntProperty("max");
		int distinctValues = max - min;

		int newValue = min + (int)std::round(ratio * distinctValues);

		SetValueInternal(newValue);
	}

	void SetValueInternal(int newVal) {
		int min = GetIntProperty("min");
		int max = GetIntProperty("max");
		int current = GetIntProperty("value");

		newVal = (std::max)(min, (std::min)(max, newVal));

		if (current != newVal) {
			SetProperty("value", std::to_string(newVal).c_str());

			std::string sVal = std::to_string(newVal);
			std::string payload = "{ \"value\": " + sVal + " }";

			UpdateBind("value", sVal);
			FireEvent("onInput", payload.c_str());

			InvalidateRect(m_hwnd, NULL, FALSE);
		}
	}

	// --- Direct2D Drawing ---

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 1. Setup Canvas
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// Draw basic background (transparent or CSS styled)
		DrawWidgetBackground(pRT, rect, false);

		// 2. Resolve Styles
		// Use helper to get D2D1_COLOR_F directly
		D2D1_COLOR_F activeColor = CSSColorToD2D(GetProperty("active-color"), 1.0f);
		D2D1_COLOR_F trackColor = CSSColorToD2D(GetProperty("track-color"), 1.0f);

		bool isEnabled = ::IsWindowEnabled(m_hwnd) != 0;
		if (!isEnabled) {
			// Greyscale for disabled state
			activeColor = D2D1::ColorF(0.7f, 0.7f, 0.7f);
			trackColor = D2D1::ColorF(0.9f, 0.9f, 0.9f);
		}

		// Layout Dimensions
		float rawThumbR = (float)GetCSSIntStyle("thumb-radius", 8);
		float rawTrackH = (float)GetCSSIntStyle("track-height", 4);

		float thumbRadius = ScaleF(rawThumbR);
		float trackHeight = ScaleF(rawTrackH);
		float cy = size.height / 2.0f;

		float padding = thumbRadius; // Keep thumb inside bounds
		float trackLeft = padding;
		float trackRight = size.width - padding;
		float trackWidth = trackRight - trackLeft;

		// Current Ratio
		int val = GetIntProperty("value");
		int min = GetIntProperty("min");
		int max = GetIntProperty("max");

		float ratio = 0.0f;
		if (max > min) {
			ratio = (float)(val - min) / (float)(max - min);
			ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
		}

		float thumbX = trackLeft + (ratio * trackWidth);

		// Prepare Brush
		ID2D1SolidColorBrush* pBrush = nullptr;
		HRESULT hr = pRT->CreateSolidColorBrush(trackColor, &pBrush);
		if (FAILED(hr)) return;

		// 3. Draw Inactive Track (Full Width)
		// Using RoundedRectangle for smooth caps
		D2D1_RECT_F trackRect = D2D1::RectF(trackLeft, cy - (trackHeight / 2.0f), trackRight, cy + (trackHeight / 2.0f));
		pRT->FillRoundedRectangle(D2D1::RoundedRect(trackRect, trackHeight / 2.0f, trackHeight / 2.0f), pBrush);

		// 4. Draw Active Track (Left to Thumb)
		if (ratio > 0.0f) {
			pBrush->SetColor(activeColor);
			D2D1_RECT_F activeRect = D2D1::RectF(trackLeft, cy - (trackHeight / 2.0f), thumbX, cy + (trackHeight / 2.0f));

			// Fix: ensure the active bar is at least a circle if very small to match roundness
			if (activeRect.right - activeRect.left < trackHeight) {
				// Not enough space for a rect, but D2D handles small rounded rects gracefully
			}
			pRT->FillRoundedRectangle(D2D1::RoundedRect(activeRect, trackHeight / 2.0f, trackHeight / 2.0f), pBrush);
		}

		// 5. Draw Halo (Interaction Feedback)
		if ((m_isDragging || m_isHovered) && isEnabled) {
			float haloR = thumbRadius * 2.0f;

			// Low alpha version of active color
			D2D1_COLOR_F haloColor = activeColor;
			haloColor.a = 0.15f;
			pBrush->SetColor(haloColor);

			D2D1_ELLIPSE haloShape = D2D1::Ellipse(D2D1::Point2F(thumbX, cy), haloR, haloR);
			pRT->FillEllipse(haloShape, pBrush);
		}

		// 6. Draw Thumb
		float currentThumbR = thumbRadius;
		if (m_isDragging) currentThumbR *= 1.1f; // Slight scale up on drag

		// 6a. Thumb Shadow (Fake Drop Shadow for 2026 depth)
		// We simulate a shadow by drawing a black, offset, blurry-looking circle underneath
		if (isEnabled) {
			D2D1_COLOR_F shadowColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.2f);
			pBrush->SetColor(shadowColor);

			float shadowOffset = ScaleF(2.0f);
			D2D1_ELLIPSE shadowShape = D2D1::Ellipse(D2D1::Point2F(thumbX, cy + shadowOffset * 0.5f), currentThumbR, currentThumbR);
			pRT->FillEllipse(shadowShape, pBrush);
		}

		// 6b. Main Thumb Body
		pBrush->SetColor(activeColor);
		D2D1_ELLIPSE thumbShape = D2D1::Ellipse(D2D1::Point2F(thumbX, cy), currentThumbR, currentThumbR);
		pRT->FillEllipse(thumbShape, pBrush);

		// 7. Focus Ring (Accessibility)
		if (m_focused && !m_isDragging) {
			pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f));

			// Draw ring slightly outside thumb
			D2D1_ELLIPSE focusRing = D2D1::Ellipse(D2D1::Point2F(thumbX, cy), currentThumbR + 2.0f, currentThumbR + 2.0f);
			pRT->DrawEllipse(focusRing, pBrush, 1.0f);
		}

		SafeRelease(&pBrush);
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		WidgetImpl::OnPropertyChanged(key, value);
		InvalidateRect(m_hwnd, NULL, FALSE);
	}

	void __stdcall OnFocus(bool f) override {
		WidgetImpl::OnFocus(f);
		FireEvent(f ? "onFocus" : "onBlur", "{}");
		InvalidateRect(m_hwnd, NULL, FALSE);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new SliderControl();
}