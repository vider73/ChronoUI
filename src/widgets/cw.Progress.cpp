#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <iomanip>
#include <sstream>
#include <wrl/client.h> // For ComPtr

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "comctl32.lib") // For Tooltips

using namespace ChronoUI;
using Microsoft::WRL::ComPtr;

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

class Progress : public WidgetImpl {
	// Runtime State for Tooltip
	HWND m_hwndTT = nullptr;

public:
	Progress() {
		// Initialize Default Properties
		SetProperty("value", "0.0");
		SetProperty("min", "0.0");
		SetProperty("max", "100.0");
		SetProperty("show_text", "true");
		SetProperty("text", ""); // Empty means auto-percentage

		// Modern Defaults (CSS)
		SetProperty("border-radius", "4");
		SetProperty("font_size", "10");
	}

	virtual ~Progress() {
		// Tooltip window is destroyed by parent hierarchy usually, 
		// but no specific cleanup needed for D2D resources as we use ComPtr/Stack
	}

	const char* __stdcall GetControlName() override { return "Progress"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
        "version": 1,
        "description": "A modern Direct2D progress bar",
        "properties": [
            { "name": "value", "type": "float", "description": "Current progress value" },
            { "name": "min", "type": "float", "description": "Minimum value" },
            { "name": "max", "type": "float", "description": "Maximum value" },
            { "name": "show_text", "type": "boolean", "description": "Show percentage text overlay" },
            { "name": "text", "type": "string", "description": "Custom text override" },
            { "name": "tt_title", "type": "string", "description": "Tooltip Title" },
            { "name": "tt_desc", "type": "string", "description": "Tooltip Description" }
        ]
    })json";
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);

		// Initialize Tooltip Control (Standard Win32 Control)
		if (m_hwnd) {
			m_hwndTT = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
				WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
				0, 0, 0, 0, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

			if (m_hwndTT) {
				TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hwnd, (UINT_PTR)m_hwnd };
				ti.lpszText = (LPSTR)"";
				SendMessage(m_hwndTT, TTM_ADDTOOLA, 0, (LPARAM)&ti);
			}
		}
	}

	void UpdateTooltip() {
		if (!m_hwndTT) return;

		std::string title = GetStringProperty("tt_title");
		std::string desc = GetStringProperty("tt_desc");

		TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hwnd, (UINT_PTR)m_hwnd };
		if (desc.empty()) {
			ti.lpszText = (LPSTR)title.c_str();
			SendMessage(m_hwndTT, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
			SendMessage(m_hwndTT, TTM_SETTITLEA, 0, (LPARAM)NULL);
		}
		else {
			ti.lpszText = (LPSTR)desc.c_str();
			SendMessage(m_hwndTT, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
			SendMessage(m_hwndTT, TTM_SETTITLEA, 1, (LPARAM)title.c_str());
		}
	}

	// --- Direct2D Rendering ---

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 1. Setup & Background
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// Draw standard widget background (handles generic CSS bg-color, border, etc.)
		DrawWidgetBackground(pRT, rect, false);

		// 2. Fetch Style & Logic Properties
		float mt = ScaleF((float)GetCSSIntStyle("margin-top"));
		float mb = ScaleF((float)GetCSSIntStyle("margin-bottom"));
		float ml = ScaleF((float)GetCSSIntStyle("margin-left"));
		float mr = ScaleF((float)GetCSSIntStyle("margin-right"));

		D2D1_COLOR_F trackColor = GetCSSColorStyle("background-color");
		D2D1_COLOR_F fillColor = GetCSSColorStyle("face-color");
		D2D1_COLOR_F borderColor = GetCSSColorStyle("border-color");

		float borderWidth = (float)GetCSSIntStyle("border-width", 1);
		float cssRadius = (float)GetCSSIntStyle("border-radius", 4);

		float minVal = GetFloatProperty("min");
		float maxVal = GetFloatProperty("max");
		float curVal = GetFloatProperty("value");

		// Clamp Logic
		curVal = (std::max)(minVal, (std::min)(curVal, maxVal));
		float range = maxVal - minVal;
		float ratio = (range > 0.0001f) ? (curVal - minVal) / range : 0.0f;

		// 3. Define Geometry Areas
		// Center the bar vertically, typically taking up ~50-70% height or fixed by margins
		float barHeight = (std::max)(1.0f, size.height - (mt + mb));
		// If height is very large, maybe constrain it? For now, respect CSS margins.
		if (barHeight > size.height * 0.6f) barHeight = size.height * 0.6f;

		float barY = (size.height - barHeight) / 2.0f;

		D2D1_RECT_F trackRect = D2D1::RectF(
			ml,
			barY,
			size.width - mr,
			barY + barHeight
		);

		// Adjust for border inset
		float halfStroke = borderWidth / 2.0f;
		trackRect.left += halfStroke;
		trackRect.top += halfStroke;
		trackRect.right -= halfStroke;
		trackRect.bottom -= halfStroke;

		if (trackRect.right < trackRect.left) trackRect.right = trackRect.left;
		if (trackRect.bottom < trackRect.top) trackRect.bottom = trackRect.top;

		// Radius Logic
		float radiusX = (std::min)(cssRadius, (trackRect.right - trackRect.left) / 2.0f);
		float radiusY = (std::min)(cssRadius, (trackRect.bottom - trackRect.top) / 2.0f);

		D2D1_ROUNDED_RECT roundedTrack = D2D1::RoundedRect(trackRect, radiusX, radiusY);

		// 4. Create Brushes
		ComPtr<ID2D1SolidColorBrush> brushTrack;
		ComPtr<ID2D1SolidColorBrush> brushFill;
		ComPtr<ID2D1SolidColorBrush> brushBorder;

		pRT->CreateSolidColorBrush(trackColor, &brushTrack);
		pRT->CreateSolidColorBrush(fillColor, &brushFill);
		if (borderWidth > 0) {
			pRT->CreateSolidColorBrush(borderColor, &brushBorder);
		}

		// 5. Draw Track (Background)
		if (trackColor.a > 0.0f) {
			pRT->FillRoundedRectangle(roundedTrack, brushTrack.Get());
		}

		// 6. Draw Fill Bar (Using Layers for correct corner clipping)
		if (ratio > 0.0f) {
			// Define the rectangle that represents the fill amount
			float totalW = trackRect.right - trackRect.left;
			float fillW = totalW * ratio;

			D2D1_RECT_F fillRect = trackRect;
			fillRect.right = fillRect.left + fillW;

			// To ensure the fill does not bleed outside the rounded corners of the track,
			// we use a geometry mask or a Layer. 
			// A Layer is cleanest: Clip to the Track Geometry, then draw the Fill Rect.

			ComPtr<ID2D1Factory> d2dFactory = GetD2DFactory();
			ComPtr<ID2D1RoundedRectangleGeometry> trackGeo;
			d2dFactory->CreateRoundedRectangleGeometry(roundedTrack, &trackGeo);

			ComPtr<ID2D1Layer> pLayer;
			pRT->CreateLayer(NULL, &pLayer);

			// Push Layer: Clip to the rounded track shape
			D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters();
			layerParams.geometricMask = trackGeo.Get();
			layerParams.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
			// Only bound the layer to the fill area for performance
			layerParams.contentBounds = fillRect;

			pRT->PushLayer(layerParams, pLayer.Get());

			// Draw the fill
			pRT->FillRectangle(fillRect, brushFill.Get());

			pRT->PopLayer();
		}

		// 7. Draw Border (on top of fill)
		if (borderWidth > 0 && borderColor.a > 0.0f) {
			pRT->DrawRoundedRectangle(roundedTrack, brushBorder.Get(), borderWidth);
		}

		// 8. Draw Text (With Contrast logic)
		bool showText = GetBoolProperty("show_text");
		std::string customText = GetStringProperty("text");

		if (showText || !customText.empty()) {
			// Prepare String
			std::string displayStr;
			if (!customText.empty()) displayStr = customText;
			else {
				std::stringstream ss;
				ss << (int)(ratio * 100.0f) << "%";
				displayStr = ss.str();
			}
			std::wstring wText = NarrowToWide(displayStr);

			// Prepare Format
			float fontSize = GetFloatProperty("font_size");
			if (fontSize <= 0.0f) fontSize = 12.0f;

			ComPtr<IDWriteFactory> dwFactory = GetDWriteFactory();
			ComPtr<IDWriteTextFormat> textFormat;

			// "Segoe UI" is standard, maybe "Segoe UI Variable" for 2026? Stick to safe choice.
			dwFactory->CreateTextFormat(
				L"Segoe UI",
				NULL,
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				fontSize,
				L"en-us",
				&textFormat
			);

			// Center alignment
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

			// Calculate Contrast Colors
			auto getContrastColor = [](const D2D1_COLOR_F& c) -> D2D1_COLOR_F {
				// Simple luminance formula
				float lum = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
				return (lum > 0.5f) ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
			};

			D2D1_COLOR_F outerColor = getContrastColor(trackColor);
			D2D1_COLOR_F innerColor = getContrastColor(fillColor);

			ComPtr<ID2D1SolidColorBrush> brushTextOuter;
			ComPtr<ID2D1SolidColorBrush> brushTextInner;
			pRT->CreateSolidColorBrush(outerColor, &brushTextOuter);
			pRT->CreateSolidColorBrush(innerColor, &brushTextInner);

			// PASS 1: Draw text for the "Empty" part (Clip logic reversed implicitly by drawing first)
			// Actually, we draw the "Outer" color everywhere first.
			pRT->DrawText(
				wText.c_str(),
				(UINT32)wText.length(),
				textFormat.Get(),
				trackRect, // Draw centered in track
				brushTextOuter.Get()
			);

			// PASS 2: Draw text for the "Filled" part (Clipped to fill rect)
			// Only needed if we have fill
			if (ratio > 0.0f) {
				float totalW = trackRect.right - trackRect.left;
				float fillW = totalW * ratio;
				D2D1_RECT_F fillClipRect = trackRect;
				fillClipRect.right = fillClipRect.left + fillW;

				// Push Axis Aligned Clip (Faster than geometric layer)
				pRT->PushAxisAlignedClip(fillClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

				pRT->DrawText(
					wText.c_str(),
					(UINT32)wText.length(),
					textFormat.Get(),
					trackRect, // Same rect, just clipped
					brushTextInner.Get()
				);

				pRT->PopAxisAlignedClip();
			}
		}
	}

	// --- Standard Event Handling ---

	void OnPropertyChanged(const char* key, const char* value) override {
		WidgetImpl::OnPropertyChanged(key, value);

		std::string t = key;
		if (t == "tt_title" || t == "tt_desc") {
			UpdateTooltip();
		}
		// Base class handles invalidation for visual updates
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_ENABLE) {
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		}
		return false;
	}

	void __stdcall OnFocus(bool f) override {
		WidgetImpl::OnFocus(f);
		FireEvent(f ? "onFocus" : "onBlur", "{}");
		InvalidateRect(m_hwnd, NULL, FALSE);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new Progress();
}