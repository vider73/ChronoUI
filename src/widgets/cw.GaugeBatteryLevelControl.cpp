#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib") // Often needed for image loading helpers

#define NOMINMAX
#include <windows.h>

// Assuming WidgetImpl.hpp defines the base class and structure. 
// For this port, we assume the user has the header, but we define the 
// internal helpers locally as requested.
#include "WidgetImpl.hpp"

using namespace ChronoUI;


class GaugeBatteryLevelControl : public WidgetImpl {
	// Animation internal state
	float m_currentValue = 12.6f;
	UINT_PTR m_timerId = 0;

	// Geometry Constants
	const float START_ANGLE = 150.0f;
	const float SWEEP_ANGLE = 240.0f;
	const float PI = 3.1415926535f;

public:
	GaugeBatteryLevelControl() {
		SetProperty("label", "BATT");
		SetProperty("unit", "V");
		SetProperty("value", "12.6");
		SetProperty("min", "10.0");
		SetProperty("max", "16.0");
		SetProperty("lowWarning", "11.5");
		SetProperty("highWarning", "15.0");
		
		SetProperty("track-color", "#DCDCE1");
		SetProperty("tick-color", "#BEBEC3");
		SetProperty("hub-color", "#323237");
	}

	virtual ~GaugeBatteryLevelControl() {
		if (m_hwnd && m_timerId) {
			KillTimer(m_hwnd, m_timerId);
		}
	}

	const char* __stdcall GetControlName() override { return "GaugeBatteryLevel"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A battery voltage gauge with animated needle and status zones (Direct2D)",
            "properties": [
                { "name": "label", "type": "string", "description": "Gauge Label" },
                { "name": "unit", "type": "string", "description": "Unit Label" },
                { "name": "value", "type": "float", "description": "Current voltage target" },
                { "name": "min", "type": "float", "description": "Minimum scale value" },
                { "name": "max", "type": "float", "description": "Maximum scale value" },
                { "name": "lowWarning", "type": "float", "description": "Low voltage threshold" },
                { "name": "highWarning", "type": "float", "description": "Overcharge threshold" },
                { "name": "background-color", "type": "color", "description": "Background color" },
                { "name": "color", "type": "color", "description": "Main text/foreground color" },
                { "name": "track-color", "type": "color", "description": "Gauge track color" },
                { "name": "tick-color", "type": "color", "description": "Tick marks color" },
                { "name": "hub-color", "type": "color", "description": "Center needle hub color" }
            ]
        })json";
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);
		if (m_hwnd) {
			m_timerId = SetTimer(m_hwnd, 1, 16, NULL);
		}
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		InvalidateRect(m_hwnd, NULL, FALSE);
	}

public:
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_TIMER && wp == m_timerId) {
			float targetValue = GetFloatProperty("value");
			float minVal = GetFloatProperty("min");
			float maxVal = GetFloatProperty("max");

			targetValue = std::clamp(targetValue, minVal, maxVal);

			float diff = targetValue - m_currentValue;
			if (std::abs(diff) > 0.005f) {
				m_currentValue += diff * 0.1f;
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return true;
		}
		return false;
	}

	// --- Direct2D Drawing Logic ---

private:
	D2D1_POINT_2F GetPointOnArc(D2D1_POINT_2F center, float radius, float angleDegrees) {
		float radians = (angleDegrees) * (PI / 180.0f);
		return D2D1::Point2F(
			center.x + radius * cosf(radians),
			center.y + radius * sinf(radians)
		);
	}

	D2D1_COLOR_F GetBatteryColor(float value) {
		float lowWarn = GetFloatProperty("lowWarning");
		float highWarn = GetFloatProperty("highWarning");

		if (value <= lowWarn) return CSSColorToD2D("#FF3C3C");   // Red
		if (value >= highWarn) return CSSColorToD2D("#FF9600");  // Orange
		return CSSColorToD2D("#00DC64"); // Green
	}

	// Helper to draw an arc since D2D doesn't have a simple DrawArc method like GDI+
	void DrawArcSegment(ID2D1RenderTarget* pRT, D2D1_POINT_2F center, float radius, float startAngle, float sweepAngle, ID2D1Brush* pBrush, float strokeWidth) {
		// D2D Geometry creation
		ID2D1Factory* pFactory = NULL;
		pRT->GetFactory(&pFactory); // Get factory from RT
		if (!pFactory) return;

		ID2D1PathGeometry* pGeo = NULL;
		pFactory->CreatePathGeometry(&pGeo);

		if (pGeo) {
			ID2D1GeometrySink* pSink = NULL;
			pGeo->Open(&pSink);

			if (pSink) {
				D2D1_POINT_2F startPoint = GetPointOnArc(center, radius, startAngle);
				D2D1_POINT_2F endPoint = GetPointOnArc(center, radius, startAngle + sweepAngle);

				pSink->BeginFigure(startPoint, D2D1_FIGURE_BEGIN_HOLLOW);
				pSink->AddArc(D2D1::ArcSegment(
					endPoint,
					D2D1::SizeF(radius, radius),
					0.0f,
					(sweepAngle > 180.0f) ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_CLOCKWISE,
					(sweepAngle > 180.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
				));
				pSink->EndFigure(D2D1_FIGURE_END_OPEN);
				pSink->Close();
				pSink->Release();
			}

			pRT->DrawGeometry(pGeo, pBrush, strokeWidth);
			SafeRelease(&pGeo);
		}
		SafeRelease(&pFactory);
	}

	void DrawBatteryIcon(ID2D1RenderTarget* pRT, float x, float y, float size, D2D1_COLOR_F color) {
		ID2D1SolidColorBrush* pBrush = NULL;
		pRT->CreateSolidColorBrush(color, &pBrush);
		if (!pBrush) return;

		// Main Body
		D2D1_RECT_F bodyRect = D2D1::RectF(x - size / 2, y - size / 4, x + size / 2, y + size / 4);
		pRT->DrawRectangle(bodyRect, pBrush, 1.5f);

		// Terminal Nub
		D2D1_RECT_F nubRect = D2D1::RectF(x + size / 2, y - size / 8, x + size / 2 + size / 8, y + size / 8);
		pRT->FillRectangle(nubRect, pBrush);

		// +/- Symbols (Draw manually with lines for crispness at small sizes)
		float symSize = size / 4;

		// Minus (Left)
		float mx = x - size / 4;
		pRT->DrawLine(D2D1::Point2F(mx - symSize / 2, y), D2D1::Point2F(mx + symSize / 2, y), pBrush, 1.5f);

		// Plus (Right)
		float px = x + size / 4;
		pRT->DrawLine(D2D1::Point2F(px - symSize / 2, y), D2D1::Point2F(px + symSize / 2, y), pBrush, 1.5f);
		pRT->DrawLine(D2D1::Point2F(px, y - symSize / 2), D2D1::Point2F(px, y + symSize / 2), pBrush, 1.5f);

		SafeRelease(&pBrush);
	}

public:
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// 1. Draw Background
		DrawWidgetBackground(pRT, rect, true);

		// 2. Setup Layout
		float centerX = size.width / 2.0f;
		float centerY = size.height / 2.0f + (size.height * 0.12f);
		float radius = (std::min)(size.width, size.height) * 0.42f;

		// Retrieve Colors
		D2D1_COLOR_F trackColor = CSSColorToD2D(GetStringProperty("track-color"));
		D2D1_COLOR_F textColor = CSSColorToD2D(GetStringProperty("color"));
		D2D1_COLOR_F tickColor = CSSColorToD2D(GetStringProperty("tick-color"));
		D2D1_COLOR_F hubColor = CSSColorToD2D(GetStringProperty("hub-color"));

		// Create Brushes (Scope managed)
		ID2D1SolidColorBrush* pTrackBrush = NULL;
		ID2D1SolidColorBrush* pZoneBrush = NULL;
		ID2D1SolidColorBrush* pTickBrush = NULL;
		ID2D1SolidColorBrush* pNeedleBrush = NULL;
		ID2D1SolidColorBrush* pHubBrush = NULL;
		ID2D1SolidColorBrush* pTextBrush = NULL;

		pRT->CreateSolidColorBrush(trackColor, &pTrackBrush);
		pRT->CreateSolidColorBrush(tickColor, &pTickBrush);
		pRT->CreateSolidColorBrush(hubColor, &pHubBrush);
		pRT->CreateSolidColorBrush(textColor, &pTextBrush);

		D2D1_POINT_2F center = D2D1::Point2F(centerX, centerY);

		// 3. Draw Background Track
		if (pTrackBrush) {
			DrawArcSegment(pRT, center, radius, START_ANGLE, SWEEP_ANGLE, pTrackBrush, 5.0f);
		}

		// 4. Draw Color Zones
		float minVal = GetFloatProperty("min");
		float maxVal = GetFloatProperty("max");
		float lowWarn = GetFloatProperty("lowWarning");
		float highWarn = GetFloatProperty("highWarning");

		float range = maxVal - minVal;
		if (range <= 0) range = 1.0f;

		float lowEndPercent = (lowWarn - minVal) / range;
		float highStartPercent = (highWarn - minVal) / range;

		// Low Zone (Red)
		if (lowEndPercent > 0) {
			pRT->CreateSolidColorBrush(CSSColorToD2D("#962828"), &pZoneBrush); // Dim Red
			if (pZoneBrush) {
				DrawArcSegment(pRT, center, radius, START_ANGLE, SWEEP_ANGLE * lowEndPercent, pZoneBrush, 5.0f);
				SafeRelease(&pZoneBrush);
			}
		}

		// High Zone (Orange)
		if (highStartPercent < 1.0f) {
			pRT->CreateSolidColorBrush(CSSColorToD2D("#B46400"), &pZoneBrush); // Dim Orange
			if (pZoneBrush) {
				DrawArcSegment(pRT, center, radius,
					START_ANGLE + (SWEEP_ANGLE * highStartPercent),
					SWEEP_ANGLE * (1.0f - highStartPercent),
					pZoneBrush, 5.0f);
				SafeRelease(&pZoneBrush);
			}
		}

		// 5. Ticks
		if (pTickBrush) {
			for (int i = 0; i <= 12; ++i) {
				float angle = START_ANGLE + (SWEEP_ANGLE * (i / 12.0f));
				float tLen = (i % 3 == 0) ? 10.0f : 5.0f;
				D2D1_POINT_2F p1 = GetPointOnArc(center, radius - tLen, angle);
				D2D1_POINT_2F p2 = GetPointOnArc(center, radius, angle);
				pRT->DrawLine(p1, p2, pTickBrush, 1.0f);
			}
		}

		// 6. Needle Calculation
		float progress = (m_currentValue - minVal) / range;
		float needleAngle = START_ANGLE + (SWEEP_ANGLE * progress);
		D2D1_COLOR_F statusColor = GetBatteryColor(m_currentValue);

		// 7. Draw Needle
		pRT->CreateSolidColorBrush(statusColor, &pNeedleBrush);
		if (pNeedleBrush) {
			D2D1_POINT_2F needleTip = GetPointOnArc(center, radius - 5.0f, needleAngle);
			D2D1_POINT_2F needleBase = GetPointOnArc(center, -10.0f, needleAngle);

			// Draw simple line for needle (Triangle cap requires Geometry, using thick line for simplicity)
			pRT->DrawLine(needleBase, needleTip, pNeedleBrush, 3.0f);
		}

		// 8. Hub
		if (pHubBrush) {
			pRT->FillEllipse(D2D1::Ellipse(center, 6.0f, 6.0f), pHubBrush);
		}

		// 9. Text Labels
		// Unit & Value
		std::string unitStr = GetStringProperty("unit");
		char valBuf[64];
		sprintf_s(valBuf, "%.1f %s", m_currentValue, unitStr.c_str());

		// Define areas for text. Using Helpers as requested.
		// Value Area
		D2D1_RECT_F valRect = D2D1::RectF(0, centerY - (radius * 0.60f), size.width, centerY);
		DrawTextStyled(pRT, valBuf, valRect, false); // Assuming helper centers text if not specified otherwise in base

		// Label Area
		std::string labelStr = GetStringProperty("label");
		D2D1_RECT_F labelRect = D2D1::RectF(0, centerY + (radius * 0.15f), size.width, centerY + radius);
		DrawTextStyled(pRT, labelStr, labelRect, false);

		// 10. Battery Icon
		DrawBatteryIcon(pRT, centerX, centerY + (radius * 0.55f), radius * 0.25f, statusColor);

		// Cleanup
		SafeRelease(&pTrackBrush);
		SafeRelease(&pZoneBrush);
		SafeRelease(&pTickBrush);
		SafeRelease(&pNeedleBrush);
		SafeRelease(&pHubBrush);
		SafeRelease(&pTextBrush);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new GaugeBatteryLevelControl();
}