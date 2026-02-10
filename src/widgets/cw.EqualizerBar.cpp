#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "user32.lib")

using namespace ChronoUI;

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif
#define NOMINMAX
#include <windows.h>

class EqualizerBar : public WidgetImpl {
private:
	// Physics Constants
	const float SMOOTH_FACTOR = 0.15f;
	const float PEAK_DECAY = 0.0075f;

	UINT_PTR m_timerId = 0;

public:
	EqualizerBar() {}

	virtual ~EqualizerBar() {
		if (m_hwnd && m_timerId) KillTimer(m_hwnd, m_timerId);
	}

	const char* __stdcall GetControlName() override { return "EqualizerBar"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 1,
            "description": "High-end segmented LED audio visualizer with smooth transitions",
            "properties": [
                { "name": "value", "type": "float", "description": "Target level (0.0 - 1.0)" },
                { "name": "vertical", "type": "bool", "description": "True for vertical bar, false for horizontal" },
                { "name": "segments", "type": "int", "description": "Number of LED segments" },
                { "name": "background-color", "type": "color", "description": "Background color" },
                { "name": "border-color", "type": "color", "description": "Dimmed segment outline color" }
            ]
        })json";
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);

		// Initialize Properties
		SetProperty("value", "0.0");
		SetProperty("visual-value", "0.0");
		SetProperty("peak-value", "0.0");

		SetProperty("vertical", "true");
		SetProperty("segments", "24");

		// Initialize Colors 
		// Deep CRT Black: #000500
		SetProperty("background-color", "#000500");
		// Dim Outline: Dark Green #003200
		SetProperty("border-color", "#003200");

		if (m_hwnd) {
			// 30 FPS timer for Physics
			m_timerId = SetTimer(m_hwnd, 1, 33, NULL);
		}
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_TIMER && wp == m_timerId) {
			bool needsRedraw = false;

			// 1. Retrieve State
			float targetVal = GetFloatProperty("value");
			float currentVal = GetFloatProperty("visual-value");
			float peakVal = GetFloatProperty("peak-value");

			// 2. Smooth Transition Logic
			float diff = targetVal - currentVal;

			if (std::abs(diff) > 0.001f) {
				currentVal += diff * SMOOTH_FACTOR;
				needsRedraw = true;
			}
			else if (currentVal != targetVal) {
				currentVal = targetVal;
				needsRedraw = true;
			}

			// 3. Peak Physics
			if (peakVal > targetVal) {
				peakVal -= PEAK_DECAY;
				if (peakVal < currentVal) peakVal = currentVal;
				needsRedraw = true;
			}

			// 4. Update Internal State
			if (needsRedraw) {
				SetProperty("visual-value", std::to_string(currentVal).c_str());
				SetProperty("peak-value", std::to_string(peakVal).c_str());
				InvalidateRect(m_hwnd, NULL, FALSE);
			}

			return true;
		}
		return false;
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		WidgetImpl::OnPropertyChanged(key, value);
		std::string t = key;
		std::string v = value ? value : "";

		if (t == "value") {
			try {
				float newVal = (std::clamp)(std::stof(v), 0.0f, 1.0f);
				float currentPeak = GetFloatProperty("peak-value");

				if (newVal > currentPeak) {
					SetProperty("peak-value", std::to_string(newVal).c_str());
				}
			}
			catch (...) {}
		}
		else if (t == "vertical" || t == "segments" || t == "background-color" || t == "border-color") {
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
	}

	// Helper: Get color based on intensity
	D2D1_COLOR_F GetSegmentColor(float intensity) {
		if (intensity > 0.85f) return D2D1::ColorF(1.0f, 0.15f, 0.15f); // Red
		if (intensity > 0.60f) return D2D1::ColorF(1.0f, 0.86f, 0.0f); // Yellow
		return D2D1::ColorF(0.0f, 1.0f, 0.31f);                         // Phosphor Green
	}

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		float fW = size.width;
		float fH = size.height;

		// 1. Get Properties
		// Note: Using alpha=1.0 for background, handled manually for transparency effects
		D2D1_COLOR_F colBg = CSSColorToD2D(GetStringProperty("background-color"));
		D2D1_COLOR_F colBorder = CSSColorToD2D(GetStringProperty("border-color"));

		bool isVertical = GetBoolProperty("vertical");
		int segments = GetIntProperty("segments");
		if (segments < 5) segments = 5;

		float visualValue = GetFloatProperty("visual-value");
		float peakValue = GetFloatProperty("peak-value");

		// 2. Background
		pRT->Clear(colBg);

		ID2D1SolidColorBrush* pBrush = nullptr;
		// Create a scratch brush
		HRESULT hr = pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pBrush);
		if (FAILED(hr)) return;

		// 3. Draw Grid (Subtle CRT effect)
		// Grid Color: approx GDI Color(40, 0, 80, 0) -> R=0, G=0.31, B=0, A=0.15
		pBrush->SetColor(D2D1::ColorF(0.0f, 0.31f, 0.0f, 0.15f));

		if (isVertical) {
			for (float y = 0; y < fH; y += 4.0f) {
				pRT->DrawLine(D2D1::Point2F(0.0f, y), D2D1::Point2F(fW, y), pBrush, 1.0f);
			}
		}
		else {
			for (float x = 0; x < fW; x += 4.0f) {
				pRT->DrawLine(D2D1::Point2F(x, 0.0f), D2D1::Point2F(x, fH), pBrush, 1.0f);
			}
		}

		// 4. Calculate Geometry
		float gap = 2.0f;
		float totalSpace = (isVertical ? fH : fW);
		float segSize = (totalSpace / (float)segments);
		float segFill = segSize - gap;
		if (segFill < 1.0f) segFill = 1.0f;

		// 5. Draw Segments
		float activeSegments = visualValue * (float)segments;

		// Pre-configure border color (using low alpha for the dim "off" state look)
		// GDI original was Color(20, 0, 50, 0). 20/255 ~= 0.08 alpha
		D2D1_COLOR_F dimBorderColor = colBorder;
		dimBorderColor.a = 0.08f;

		for (int i = 0; i < segments; i++) {
			float segmentFillFactor = (std::clamp)(activeSegments - (float)i, 0.0f, 1.0f);
			float intensity = (float)i / (float)(segments - 1);

			D2D1_RECT_F rect;
			if (isVertical) {
				float y = fH - ((i + 1) * segSize) + (gap / 2.0f);
				rect = D2D1::RectF(2.0f, y, fW - 2.0f, y + segFill);
			}
			else {
				float x = (i * segSize) + (gap / 2.0f);
				rect = D2D1::RectF(x, 2.0f, x + segFill, fH - 2.0f);
			}

			// Draw the background slot/outline
			pBrush->SetColor(dimBorderColor);
			pRT->DrawRectangle(rect, pBrush, 1.0f);

			if (segmentFillFactor > 0.0f) {
				D2D1_COLOR_F baseCol = GetSegmentColor(intensity);

				// Set alpha proportional to fill factor
				baseCol.a = segmentFillFactor;
				pBrush->SetColor(baseCol);
				pRT->FillRectangle(rect, pBrush);

				// Add Glow/Bloom proportional to fill
				if (segmentFillFactor > 0.1f) {
					D2D1_COLOR_F bloomCol = baseCol;
					// Bloom alpha approx 60/255 * factor
					bloomCol.a = (60.0f / 255.0f) * segmentFillFactor;
					pBrush->SetColor(bloomCol);

					D2D1_RECT_F bloomRect = D2D1::RectF(
						rect.left - 1.0f, rect.top - 1.0f,
						rect.right + 1.0f, rect.bottom + 1.0f
					);
					pRT->FillRectangle(bloomRect, pBrush);
				}
			}
		}

		// 6. Draw Peak Indicator
		pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));

		if (isVertical) {
			float y = fH - (peakValue * fH);
			y = (std::clamp)(y, 1.0f, fH - 1.0f);
			pRT->DrawLine(D2D1::Point2F(0.0f, y), D2D1::Point2F(fW, y), pBrush, 2.0f);
		}
		else {
			float x = peakValue * fW;
			x = (std::clamp)(x, 0.0f, fW - 1.0f);
			pRT->DrawLine(D2D1::Point2F(x, 0.0f), D2D1::Point2F(x, fH), pBrush, 2.0f);
		}

		SafeRelease(&pBrush);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new EqualizerBar();
}