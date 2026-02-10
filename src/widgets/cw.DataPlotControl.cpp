#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cmath>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "user32.lib")

using namespace ChronoUI;
using namespace Microsoft::WRL; // Assuming ComPtr is in this namespace based on helper signature

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

#define NOMINMAX
#include <windows.h>

class DataPlotControl : public WidgetImpl {
private:
	// Data State
	std::vector<float> m_history;

public:
	DataPlotControl() {
		// Initialize Properties with defaults
		SetProperty("label_x", "Time");
		SetProperty("label_y", "Usage");
		SetProperty("units", "%");
		SetProperty("steps", "100");

		SetProperty("min", "0.0");
		SetProperty("max", "100.0");

		// Initialize Colors
		SetProperty("color", "#64FF64");              // Bright Green
		SetProperty("background-color", "#000A00");   // Deep Green/Black
		SetProperty("grid-color", "#28003C00");       // Dim Grid
		SetProperty("text-color", "#B464FF64");       // Pale Green Text

		m_history.reserve(100);
	}

	virtual ~DataPlotControl() {}

	const char* __stdcall GetControlName() override { return "DataPlotControl"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 1,
            "description": "Real-time scrolling data plot (Direct2D)",
            "properties": [
                { "name": "label_x", "type": "string", "description": "Label for X axis" },
                { "name": "label_y", "type": "string", "description": "Label for Y axis" },
                { "name": "units", "type": "string", "description": "Unit suffix (e.g. %)" },
                { "name": "steps", "type": "int", "description": "Max data points to keep" },
                { "name": "min", "type": "float", "description": "Minimum Y value" },
                { "name": "max", "type": "float", "description": "Maximum Y value" },
                { "name": "add_value", "type": "float", "description": "Push a new value to the plot" },
                { "name": "color", "type": "string", "description": "Plot line color (hex)" },
                { "name": "background-color", "type": "string", "description": "Background color (hex)" },
                { "name": "grid-color", "type": "string", "description": "Grid line color (hex)" },
                { "name": "text-color", "type": "string", "description": "Text label color (hex)" }
            ]
        })json";
	}

	void AddValue(float val) {
		size_t maxSteps = (size_t)GetIntProperty("steps");
		if (maxSteps < 2) maxSteps = 2;

		m_history.push_back(val);

		if (m_history.size() > maxSteps) {
			m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - maxSteps));
		}

		// Trigger redraw
		if (m_hwnd) {
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		WidgetImpl::OnPropertyChanged(key, value);

		std::string t = key;
		std::string val = value ? value : "";

		if (t == "steps") {
			try {
				size_t newSteps = (size_t)(std::max)(10, std::stoi(val));
				if (m_history.size() > newSteps) {
					m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - newSteps));
				}
			}
			catch (...) {}
		}
		else if (t == "add_value") {
			try { AddValue(std::stof(val)); }
			catch (...) {}
		}
	}

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		float fW = size.width;
		float fH = size.height;
		D2D1_RECT_F rFull = D2D1::RectF(0, 0, fW, fH);

		// 0. Retrieve Properties & Convert Colors
		// Background handles its own color via DrawWidgetBackground property lookup usually, 
		// but here we override the specific background color using the helper logic if needed.
		// The helper `DrawWidgetBackground` usually reads specific props or styles.
		// For this specific widget, we want the specific "background-color" property.

		// Let's paint the custom background manually if the helper doesn't support the specific property override
		// or we use the helper and overlay. Assuming DrawWidgetBackground handles the standard widget box:

		// Manual background fill to ensure we use the "background-color" property specifically
		D2D1_COLOR_F bgColor = CSSColorToD2D(GetStringProperty("background-color"));
		ComPtr<ID2D1SolidColorBrush> pBgBrush;
		pRT->CreateSolidColorBrush(bgColor, &pBgBrush);
		if (pBgBrush) {
			pRT->FillRectangle(rFull, pBgBrush.Get());
		}

		// Alternatively, use the helper if it adapts to CSS automatically:
		// DrawWidgetBackground(pRT, rFull, false); 

		// 1. Setup Resources
		D2D1_COLOR_F gridColor = CSSColorToD2D(GetStringProperty("grid-color"));
		D2D1_COLOR_F plotColor = CSSColorToD2D(GetStringProperty("color"));

		ComPtr<ID2D1SolidColorBrush> pGridBrush;
		ComPtr<ID2D1SolidColorBrush> pPlotBrush;
		ComPtr<ID2D1SolidColorBrush> pGlowBrush;

		pRT->CreateSolidColorBrush(gridColor, &pGridBrush);
		pRT->CreateSolidColorBrush(plotColor, &pPlotBrush);

		// Create Glow Color (Plot color with lower alpha)
		D2D1_COLOR_F glowColor = plotColor;
		glowColor.a = 0.2f; // ~50/255
		pRT->CreateSolidColorBrush(glowColor, &pGlowBrush);

		size_t maxSteps = (size_t)GetIntProperty("steps");
		float minRange = GetFloatProperty("min");
		float maxRange = GetFloatProperty("max");

		// 2. Draw Grid
		int gridDivs = 10;
		if (pGridBrush) {
			// Vertical lines
			for (int i = 1; i < gridDivs; ++i) {
				float x = (fW / gridDivs) * i;
				// Snap to pixel for sharpness
				x = floor(x) + 0.5f;
				pRT->DrawLine(D2D1::Point2F(x, 0.0f), D2D1::Point2F(x, fH), pGridBrush.Get(), 1.0f);
			}
			// Horizontal lines
			for (int i = 1; i < gridDivs; ++i) {
				float y = (fH / gridDivs) * i;
				y = floor(y) + 0.5f;
				pRT->DrawLine(D2D1::Point2F(0.0f, y), D2D1::Point2F(fW, y), pGridBrush.Get(), 1.0f);
			}
		}

		// 3. Draw Data Line
		if (m_history.size() > 1 && maxSteps > 1 && pPlotBrush && pGlowBrush) {
			float stepW = fW / (float)(maxSteps - 1);
			float range = maxRange - minRange;
			if (range == 0) range = 1.0f;

			auto getPos = [&](size_t index) -> D2D1_POINT_2F {
				float val = m_history[index];
				float x = index * stepW;

				// Clamp for display
				if (val < minRange) val = minRange;
				if (val > maxRange) val = maxRange;

				float normalizedY = (val - minRange) / range;
				float y = fH - (normalizedY * fH);
				return D2D1::Point2F(x, y);
			};

			for (size_t i = 1; i < m_history.size(); ++i) {
				D2D1_POINT_2F p1 = getPos(i - 1);
				D2D1_POINT_2F p2 = getPos(i);

				// Draw Glow (Thick)
				pRT->DrawLine(p1, p2, pGlowBrush.Get(), 4.0f);
				// Draw Core (Thin)
				pRT->DrawLine(p1, p2, pPlotBrush.Get(), 1.5f);
			}
		}

		// 4. Labels & HUD
		std::string labelX = GetStringProperty("label_x");
		std::string labelY = GetStringProperty("label_y");
		std::string units = GetStringProperty("units");

		// Y Label (Top Left) - Current Value
		std::stringstream ss;
		if (!m_history.empty()) {
			ss << labelY << ": " << std::fixed << std::setprecision(1) << m_history.back() << units;
		}
		else {
			ss << labelY;
		}

		// Define Rect for Top Left text
		D2D1_RECT_F rTopLeft = D2D1::RectF(5.0f, 5.0f, fW, 25.0f);
		DrawTextStyled(pRT, ss.str(), rTopLeft, false);

		// X Label (Bottom Right)
		// DrawTextStyled usually left-aligns, so we pass a rect at the bottom.
		// To mimic GDI+ "Far" alignment, we'd need DWrite layouts, but we use the requested helper here.
		// We position the rect at the bottom to at least get the vertical placement right.
		D2D1_RECT_F rBottom = D2D1::RectF(5.0f, fH - 20.0f, fW - 5.0f, fH);
		DrawTextStyled(pRT, labelX, rBottom, false);

		// Range Indicator (Max) - Top Right
		std::stringstream maxSS;
		maxSS << (int)maxRange;
		// Position rect approx where top-right text should be. 
		// Note: Without explicit right-align in DrawTextStyled, this draws left-aligned at the specified X.
		D2D1_RECT_F rTopRight = D2D1::RectF(fW - 40.0f, 5.0f, fW, 25.0f);
		DrawTextStyled(pRT, maxSS.str(), rTopRight, false);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new DataPlotControl();
}