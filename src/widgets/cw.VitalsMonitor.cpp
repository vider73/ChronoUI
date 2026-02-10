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
#pragma comment(lib, "user32.lib")

using namespace ChronoUI;

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

#define NOMINMAX
#include <windows.h>

// =============================================================
// Widget Class Declaration
// =============================================================
class VitalsMonitor : public WidgetImpl {
private:
	// --- 1. Internal State ---
	bool m_isHovered = false;
	UINT_PTR m_timerId = 0;
	static const int TIMER_ID = 202;

	// Buffer for signal history (Normalised 0.0 to 1.0)
	std::vector<float> m_dataBuffer;
	int m_scanHeadX = 0;         // Current X position of the "write head"

	// ECG Simulation vars
	float m_simTime = 0.0f;

public:
	VitalsMonitor() {
		// --- Initialize Properties ---

		// Standard Colors (CSS Hex format)
		SetProperty("background-color", "#000A05"); // Deep Black/Green
		SetProperty("foreground-color", "#ffffff");

		// Custom Control Properties
		SetProperty("label", "ECG-V1");
		SetProperty("mode", "sim");             // 'sim' or 'data'
		SetProperty("value", "0.5");            // Input value
		SetProperty("trace_color", "#00FF40");  // CRT Green
		SetProperty("grid_color", "#003214");   // Dim Green
		SetProperty("speed", "4");              // Pixels per update
		SetProperty("active", "1");             // bool true

		// Initialize buffer with defaults
		m_dataBuffer.resize(100, 0.5f);
	}

	virtual ~VitalsMonitor() {
		if (m_hwnd && m_timerId) KillTimer(m_hwnd, m_timerId);
	}

	// --- 2. Metadata ---
	const char* __stdcall GetControlName() override {
		return "VitalsMonitor";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "Direct2D Retro CRT monitor for ECG or real-time signal plotting.",
            "properties": [
                { "name": "label", "type": "string", "description": "Monitor Label" },
                { "name": "mode", "type": "string", "description": "'sim' for heartbeat, 'data' for manual input" },
                { "name": "value", "type": "float", "description": "Input value (0.0 - 1.0) for data mode" },
                { "name": "trace_color", "type": "color", "description": "Signal line color" },
                { "name": "grid_color", "type": "color", "description": "Background grid color" },
                { "name": "background-color", "type": "color", "description": "Monitor background" },
                { "name": "speed", "type": "int", "description": "Scan speed (pixels per frame)" },
                { "name": "active", "type": "bool", "description": "Pause/Resume scanning" }
            ],
            "events": [
                { "name": "onClick", "type": "action" }
            ]
        })json";
	}

	// --- 3. Initialization ---
	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);

		if (m_hwnd) {
			// Run at ~60 FPS (16ms)
			m_timerId = SetTimer(m_hwnd, TIMER_ID, 16, NULL);
		}
	}

	// --- 4. Message Handling ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_TIMER:
			if (wp == TIMER_ID && GetBoolProperty("active")) {
				UpdateSimulation();
				// Invalidate to trigger OnDrawWidget
				InvalidateRect(m_hwnd, NULL, FALSE);
				return true;
			}
			break;

		case WM_MOUSEMOVE:
			if (!m_isHovered) {
				m_isHovered = true;
				TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
				TrackMouseEvent(&tme);
				InvalidateRect(m_hwnd, NULL, FALSE); // Redraw for hover effects
			}
			return false;

		case WM_MOUSELEAVE:
			m_isHovered = false;
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;

		case WM_LBUTTONDOWN:
			SetFocus(m_hwnd);
			FireEvent("onClick", "{}");

			// Toggle active state
			{
				bool currentState = GetBoolProperty("active");
				SetProperty("active", currentState ? "0" : "1");
			}
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		}
		return false;
	}

	// --- Internal Logic: Simulation & Buffer Update ---
	void UpdateSimulation() {
		if (m_dataBuffer.empty()) return;

		int speed = GetIntProperty("speed");
		std::string mode = GetStringProperty("mode");

		// Perform 'speed' number of updates per frame
		for (int i = 0; i < speed; i++) {
			float newValue = 0.5f;

			if (mode == "sim") {
				// Heartbeat approximation
				m_simTime += 0.04f;
				if (m_simTime > 6.28f) m_simTime -= 6.28f;

				// Baseline noise
				newValue = 0.5f + ((float)(rand() % 100) / 10000.0f);

				// QRS Spike
				if (m_simTime > 2.8f && m_simTime < 3.2f) {
					newValue -= static_cast<float>(sin((m_simTime - 2.8f) * 15.0f)) * 0.4f;
				}
				// T Wave
				else if (m_simTime > 3.5f && m_simTime < 4.2f) {
					newValue -= static_cast<float>(sin((m_simTime - 3.5f) * 4.0f)) * 0.15f;
				}
			}
			else {
				// Real data mode
				float val = GetFloatProperty("value");
				val = (std::max)(0.0f, (std::min)(1.0f, val));
				newValue = 1.0f - val; // Invert for Y-axis (0 at top)
			}

			// Move Head
			m_scanHeadX++;
			if (m_scanHeadX >= (int)m_dataBuffer.size()) {
				m_scanHeadX = 0;
			}

			// Write to buffer
			m_dataBuffer[m_scanHeadX] = newValue;

			// Create Gap
			int gapSize = 10;
			for (int g = 1; g <= gapSize; g++) {
				int clearPos = (m_scanHeadX + g) % m_dataBuffer.size();
				m_dataBuffer[clearPos] = -1.0f; // -1 indicates "no signal"
			}
		}
	}

	// --- 5. Direct2D Drawing Logic ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		int width = static_cast<int>(size.width);
		int height = static_cast<int>(size.height);

		// Resize buffer if window size changes drastically
		// We cast to int to map 1 pixel = 1 buffer entry for simplicity
		if ((int)m_dataBuffer.size() != width && width > 0) {
			std::vector<float> newBuf(width, 0.5f);
			m_dataBuffer = newBuf;
			m_scanHeadX = 0;
		}

		// 1. Draw Background
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// Use helper for generic widget background logic (borders, hover), 
		// but then fill with our specific CRT color.
		DrawWidgetBackground(pRT, rect, m_isHovered);

		D2D1_COLOR_F backColor = CSSColorToD2D(GetStringProperty("background-color"));
		ComPtr<ID2D1SolidColorBrush> pBackBrush;
		pRT->CreateSolidColorBrush(backColor, &pBackBrush);

		// Slightly inset to account for border drawn by helper
		D2D1_RECT_F contentRect = D2D1::RectF(1, 1, size.width - 1, size.height - 1);
		pRT->FillRectangle(contentRect, pBackBrush.Get());

		// 2. Prepare Brushes
		D2D1_COLOR_F gridColor = CSSColorToD2D(GetStringProperty("grid_color"));
		D2D1_COLOR_F traceColor = CSSColorToD2D(GetStringProperty("trace_color"));

		ComPtr<ID2D1SolidColorBrush> pGridBrush;
		ComPtr<ID2D1SolidColorBrush> pTraceBrush;
		ComPtr<ID2D1SolidColorBrush> pGlowBrush;
		ComPtr<ID2D1SolidColorBrush> pHeadBrush;

		pRT->CreateSolidColorBrush(gridColor, &pGridBrush);
		pRT->CreateSolidColorBrush(traceColor, &pTraceBrush);

		// Glow is the trace color but with low alpha
		D2D1_COLOR_F glowCol = traceColor;
		glowCol.a = 0.3f;
		pRT->CreateSolidColorBrush(glowCol, &pGlowBrush);

		pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.5f), &pHeadBrush);

		// 3. Draw Grid
		float gridSize = 20.0f;

		// Vertical lines
		for (float x = 0; x < size.width; x += gridSize) {
			pRT->DrawLine(
				D2D1::Point2F(x, 0),
				D2D1::Point2F(x, size.height),
				pGridBrush.Get(),
				1.0f
			);
		}
		// Horizontal lines
		for (float y = 0; y < size.height; y += gridSize) {
			pRT->DrawLine(
				D2D1::Point2F(0, y),
				D2D1::Point2F(size.width, y),
				pGridBrush.Get(),
				1.0f
			);
		}

		// 4. Draw Signal Trace
		// We draw lines connecting adjacent points. 
		// For a "2026" look, we apply anti-aliasing (default in D2D) and a glow layer.

		for (int x = 0; x < width - 1; x++) {
			float y1_val = m_dataBuffer[x];
			float y2_val = m_dataBuffer[x + 1];

			// Skip gaps
			if (y1_val < 0.0f || y2_val < 0.0f) continue;

			float y1 = y1_val * size.height;
			float y2 = y2_val * size.height;

			D2D1_POINT_2F p1 = D2D1::Point2F((float)x, y1);
			D2D1_POINT_2F p2 = D2D1::Point2F((float)(x + 1), y2);

			// Draw Glow (Thicker, transparent)
			pRT->DrawLine(p1, p2, pGlowBrush.Get(), 4.0f);

			// Draw Core (Thinner, solid)
			pRT->DrawLine(p1, p2, pTraceBrush.Get(), 1.5f);
		}

		// 5. Draw Scan Head
		pRT->DrawLine(
			D2D1::Point2F((float)m_scanHeadX, 0.0f),
			D2D1::Point2F((float)m_scanHeadX, size.height),
			pHeadBrush.Get(),
			2.0f
		);

		// 6. UI Overlay (Label)
		// Using helper DrawTextStyled.
		std::string labelStr = GetStringProperty("label");
		D2D1_RECT_F textRect = D2D1::RectF(5, 5, 200, 30);
		DrawTextStyled(pRT, labelStr, textRect, false);

		// 7. "REC" Dot if active
		if (GetBoolProperty("active")) {
			ComPtr<ID2D1SolidColorBrush> pRedBrush;
			pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &pRedBrush);

			float dotSize = 10.0f;
			D2D1_ELLIPSE dot = D2D1::Ellipse(
				D2D1::Point2F(size.width - 20.0f, 15.0f),
				dotSize / 2.0f,
				dotSize / 2.0f
			);

			pRT->FillEllipse(dot, pRedBrush.Get());
		}
	}
};

// =============================================================
// Factory Export
// =============================================================
extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new VitalsMonitor();
}