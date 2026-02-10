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
#pragma comment(lib, "comctl32.lib")

using namespace ChronoUI;

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

#include <windows.h>
#include <commctrl.h>

// Helper for Linear Interpolation
template <typename T>
T Lerp(T start, T end, float t) {
	return start + static_cast<T>((end - start) * t);
}

// Helper to Interpolate D2D Colors
D2D1_COLOR_F LerpColor(const D2D1_COLOR_F& c1, const D2D1_COLOR_F& c2, float t) {
	return D2D1::ColorF(
		Lerp(c1.r, c2.r, t),
		Lerp(c1.g, c2.g, t),
		Lerp(c1.b, c2.b, t),
		Lerp(c1.a, c2.a, t)
	);
}

class SwitchButton : public WidgetImpl {
	// Internal State
	HCURSOR m_handCursor = nullptr;
	HWND m_hTooltip = nullptr;
	bool m_isTextHidden = false;

	// Animation State
	float m_animProgress = 0.0f; // 0.0f (Off) to 1.0f (On)
	bool m_needsAnimation = false;

public:
	SwitchButton() {
		// Initialize Default Properties
		SetProperty("title", "");
		SetBoolProperty("checked", false);
		SetProperty("text-align", "left");
		SetProperty("font-size", "11"); // Slightly larger default for modern look

		// Modern 2026 Palette
		SetProperty("track-off-color", "#E0E0E0");
		SetProperty("track-on-color", "#1A73E8");
		SetProperty("thumb-color", "#FFFFFF");
		SetProperty("foreground-color", "#202124");

		m_handCursor = LoadCursor(NULL, IDC_HAND);
	}

	virtual ~SwitchButton() = default;

	const char* __stdcall GetControlName() override { return "SwitchButton"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A modern, animated toggle switch using Direct2D",
            "properties": [
                { "name": "title", "type": "string", "description": "Label text" },
                { "name": "checked", "type": "boolean", "description": "Is the switch on?" },
                { "name": "text-align", "type": "string", "description": "'left' or 'right' (relative to switch)" },
                { "name": "font-size", "type": "number", "description": "Font size in points" },
                { "name": "track-on-color", "type": "color", "description": "Track color when active" },
                { "name": "track-off-color", "type": "color", "description": "Track color when inactive" },
                { "name": "thumb-color", "type": "color", "description": "Switch thumb color" }
            ],
            "events": [
                { "name": "onChange", "type": "action", "description": "Fired when state changes" },
                { "name": "onFocus", "type": "action", "description": "Fired when control gains focus" },
                { "name": "onBlur", "type": "action", "description": "Fired when control loses focus" }
            ]
        })json";
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);

		INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
		InitCommonControlsEx(&icex);

		// Tooltip creation
		m_hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
			WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			m_hwnd, NULL, GetModuleHandle(NULL), NULL);

		if (m_hTooltip) {
			SetWindowPos(m_hTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			TOOLINFO ti = { 0 };
			ti.cbSize = sizeof(TOOLINFO);
			ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
			ti.hwnd = m_hwnd;
			ti.uId = 1;
			ti.lpszText = LPSTR_TEXTCALLBACK;
			GetClientRect(m_hwnd, &ti.rect);
			SendMessage(m_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
		}

		// Initialize animation state based on start property
		m_animProgress = GetBoolProperty("checked") ? 1.0f : 0.0f;
	}

	void UpdateTooltipState(bool activate) {
		if (!m_hTooltip) return;

		TOOLINFO ti = { 0 };
		ti.cbSize = sizeof(TOOLINFO);
		ti.hwnd = m_hwnd;
		ti.uId = 1;

		if (activate) {
			std::string label = GetStringProperty("title");
			std::wstring wLabel = NarrowToWide(label);
			ti.lpszText = const_cast<LPWSTR>(wLabel.c_str());
			SendMessage(m_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
			SendMessage(m_hTooltip, TTM_ACTIVATE, TRUE, 0);
		}
		else {
			SendMessage(m_hTooltip, TTM_ACTIVATE, FALSE, 0);
		}
	}

	// --- Message Handling ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		// Relay tooltip events
		if (m_hTooltip) {
			MSG msgTmp = { 0 };
			msgTmp.hwnd = m_hwnd;
			msgTmp.message = msg;
			msgTmp.wParam = wp;
			msgTmp.lParam = lp;
			GetCursorPos(&msgTmp.pt);
			SendMessage(m_hTooltip, TTM_RELAYEVENT, 0, (LPARAM)&msgTmp);
		}

		switch (msg) {
		case WM_SETCURSOR:
			if (LOWORD(lp) == HTCLIENT) {
				SetCursor(m_handCursor);
				return true;
			}
			break;

		case WM_LBUTTONDOWN:
			SetFocus(m_hwnd);
			if (IsWindowEnabled(m_hwnd)) {
				bool current = GetBoolProperty("checked");
				bool nextState = !current;
				SetBoolProperty("checked", nextState);

				// Fire Event
				FireEvent("onChange", nextState ? "true" : "false");
				TriggerOnChanged();

				// Trigger animation loop
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return true;
		}

		return false;
	}

	// --- Direct2D Drawing ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 1. Setup & Properties
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rBounds = D2D1::RectF(0, 0, size.width, size.height);

		bool checked = GetBoolProperty("checked");
		std::string label = GetStringProperty("title");
		std::string textAlign = GetStringProperty("text-align");
		float fontSize = GetFloatProperty("font-size");
		bool isRightAlign = (textAlign == "right");
		bool isEnabled = IsWindowEnabled(m_hwnd);

		// 2. Animation Logic (Smooth transition)
		float target = checked ? 1.0f : 0.0f;
		if (std::abs(m_animProgress - target) > 0.001f) {
			// Lerp speed
			m_animProgress += (target - m_animProgress) * 0.2f;
			// Continue painting until settled
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else {
			m_animProgress = target;
		}

		// 3. Colors
		D2D1_COLOR_F cTrackOff = CSSColorToD2D(GetStringProperty("track-off-color"));
		D2D1_COLOR_F cTrackOn = CSSColorToD2D(GetStringProperty("track-on-color"));
		D2D1_COLOR_F cThumb = CSSColorToD2D(GetStringProperty("thumb-color"));

		// Handle Disabled State
		if (!isEnabled) {
			cTrackOff = D2D1::ColorF(0.9f, 0.9f, 0.9f);
			cTrackOn = D2D1::ColorF(0.8f, 0.8f, 0.8f);
			cThumb = D2D1::ColorF(0.6f, 0.6f, 0.6f);
		}

		D2D1_COLOR_F cCurrentTrack = LerpColor(cTrackOff, cTrackOn, m_animProgress);

		// 4. Draw Background
		DrawWidgetBackground(pRT, rBounds, false); // Switch handles its own hover visual mostly

		// 5. Layout Metrics
		float fH = size.height;
		float fW = size.width;

		// Switch Geometry
		float swH = (std::min)(fH * 0.6f, 24.0f); // Max height 24px
		if (swH < 12.0f) swH = 12.0f;
		float swW = swH * 1.8f;
		float swY = (fH - swH) / 2.0f;
		float spacing = 8.0f;

		D2D1_RECT_F switchRect;
		D2D1_RECT_F textRect;
		bool showText = false;
		std::wstring wLabel = NarrowToWide(label);

		// Calculate Text Size
		float textWidth = 0.0f;
		ComPtr<IDWriteFactory> pDWrite = GetDWriteFactory();
		ComPtr<IDWriteTextFormat> pTextFormat;
		ComPtr<IDWriteTextLayout> pTextLayout;

		if (!wLabel.empty() && pDWrite) {
			pDWrite->CreateTextFormat(
				L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, fontSize * (96.0f / 72.0f), L"en-us", &pTextFormat);

			if (pTextFormat) {
				pDWrite->CreateTextLayout(wLabel.c_str(), (UINT32)wLabel.length(), pTextFormat.Get(), 5000.0f, fH, &pTextLayout);
			}

			if (pTextLayout) {
				DWRITE_TEXT_METRICS metrics;
				pTextLayout->GetMetrics(&metrics);
				textWidth = metrics.width;
			}

			// Determine Layout
			float totalRequired = swW + spacing + textWidth;

			if (totalRequired <= fW) {
				showText = true;
				if (isRightAlign) {
					// [Switch] [Text]
					switchRect = D2D1::RectF(0, swY, swW, swY + swH);
					textRect = D2D1::RectF(swW + spacing, 0, fW, fH);
				}
				else {
					// [Text] [Switch]
					float txtX = fW - swW - spacing - textWidth;
					// To keep it simple, we align text rect to start at 0 and end before switch
					switchRect = D2D1::RectF(fW - swW, swY, fW, swY + swH);
					textRect = D2D1::RectF(0, 0, fW - swW - spacing, fH);
				}
			}
			else {
				// Doesn't fit, hide text, center switch
				showText = false;
				switchRect = D2D1::RectF((fW - swW) / 2.0f, swY, (fW - swW) / 2.0f + swW, swY + swH);
			}
		}
		else {
			// No text
			if (isRightAlign) switchRect = D2D1::RectF(0, swY, swW, swY + swH);
			else switchRect = D2D1::RectF(fW - swW, swY, fW, swY + swH);
		}

		// Update Tooltip logic
		bool needTooltip = (!showText && !wLabel.empty());
		if (m_isTextHidden != needTooltip) {
			m_isTextHidden = needTooltip;
			UpdateTooltipState(needTooltip);
		}

		// 6. Draw Track
		ComPtr<ID2D1SolidColorBrush> pBrush;
		pRT->CreateSolidColorBrush(cCurrentTrack, &pBrush);

		D2D1_ROUNDED_RECT roundedTrack;
		roundedTrack.rect = switchRect;
		roundedTrack.radiusX = swH / 2.0f;
		roundedTrack.radiusY = swH / 2.0f;

		pRT->FillRoundedRectangle(roundedTrack, pBrush.Get());

		// Focus Ring (when focused and enabled)
		if (m_focused && isEnabled) {
			pBrush->SetColor(D2D1::ColorF(0.1f, 0.45f, 0.9f, 0.4f)); // Focus glow
			float focusPadding = 3.0f;
			D2D1_ROUNDED_RECT focusRect = roundedTrack;
			focusRect.rect.left -= focusPadding; focusRect.rect.top -= focusPadding;
			focusRect.rect.right += focusPadding; focusRect.rect.bottom += focusPadding;
			focusRect.radiusX += focusPadding; focusRect.radiusY += focusPadding;
			pRT->DrawRoundedRectangle(focusRect, pBrush.Get(), 2.0f);
		}

		// 7. Draw Thumb
		float thumbRadius = (swH / 2.0f) - 2.0f; // 2px padding
		float thumbDiameter = thumbRadius * 2.0f;
		float trackInnerWidth = swW - (4.0f + thumbDiameter); // Total travel distance

		// Calculate X offset based on animation progress
		float currentOffset = trackInnerWidth * m_animProgress;

		float thumbCx = switchRect.left + 2.0f + thumbRadius + currentOffset;
		float thumbCy = switchRect.top + (swH / 2.0f);

		D2D1_ELLIPSE thumbShape = D2D1::Ellipse(D2D1::Point2F(thumbCx, thumbCy), thumbRadius, thumbRadius);

		// Thumb Shadow (simulated)
		pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f));
		D2D1_ELLIPSE shadowShape = thumbShape;
		shadowShape.point.y += 1.0f; // Offset down
		pRT->FillEllipse(shadowShape, pBrush.Get());

		// Thumb Body
		pBrush->SetColor(cThumb);
		pRT->FillEllipse(thumbShape, pBrush.Get());

		// Subtle Thumb Border (for contrast against white backgrounds)
		pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.1f));
		pRT->DrawEllipse(thumbShape, pBrush.Get(), 0.5f);

		// 8. Draw Text
		if (showText) {
			// Re-use helper logic but pass specific rect
			// The helper handles alignment usually, but we want strict bounding box control here.
			DrawTextStyled(pRT, label, textRect, true);
		}
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string k = key;
		if (k == "title" && m_isTextHidden) {
			UpdateTooltipState(true);
		}

		// For 'checked', we don't snap m_animProgress here to allow
		// the OnDrawWidget to animate it from current position.

		WidgetImpl::OnPropertyChanged(key, value);
	}

	void OnFocus(bool f) override {
		HWND parent = GetParent(m_hwnd);
		if (f && parent) {
			SendMessage(parent, WM_USER + 200, 0, (LPARAM)m_hwnd);
		}
		FireEvent(f ? "onFocus" : "onBlur", "{}");
		if (IsWindow(m_hwnd)) InvalidateRect(m_hwnd, NULL, FALSE);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new SwitchButton();
}