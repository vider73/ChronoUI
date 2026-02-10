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

#ifndef CHRONOUI_GAUGE_EXPORTS
#define CHRONOUI_GAUGE_EXPORTS
#endif

// Ensure M_PI is defined
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Convert Degrees to Radians
#define DEG2RAD(angle) ((angle) * (M_PI / 180.0f))

class GaugeSpeedOmeter : public WidgetImpl {
	// Data State
	float m_currentValue = 0.0f;
	float m_targetValue = 0.0f;
	float m_minValue = 0.0f;
	float m_maxValue = 240.0f;
	std::string m_label = "SPEED";
	std::string m_unit = "km/h";

	// Visual Customization State
	std::string m_accentColorHex = "#0096FF"; // Default Neon Blue
	std::string m_needleColorHex = "#FF3232"; // Default Red

	// Animation state
	UINT_PTR m_timerId = 0;
	static const UINT_PTR TIMER_ID = 101;

	// Geometry Constants
	const float START_ANGLE = 135.0f; // Start at bottom-left
	const float SWEEP_ANGLE = 270.0f; // Wrap around to bottom-right

public:
	GaugeSpeedOmeter() {}

	virtual ~GaugeSpeedOmeter() {
		if (m_hwnd && m_timerId) {
			KillTimer(m_hwnd, m_timerId);
		}
	}

	const char* __stdcall GetControlName() override { return "GaugeSpeedOmeter"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "Animated D2D Gauge Speedometer",
            "properties": [
                { "name": "value", "type": "float", "description": "Target value to animate towards" },
                { "name": "min", "type": "float", "description": "Minimum scale value" },
                { "name": "max", "type": "float", "description": "Maximum scale value" },
                { "name": "label", "type": "string", "description": "Label text (e.g. SPEED)" },
                { "name": "unit", "type": "string", "description": "Unit text (e.g. km/h)" },
                { "name": "accent_color", "type": "string", "description": "Hex color for the active arc" },
                { "name": "needle_color", "type": "string", "description": "Hex color for the needle" }
            ],
            "events": [
                { "name": "onLimitReached", "type": "event" }
            ]
        })json";
	}

	// --- Initialization ---

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);
		// Start animation timer (~60fps)
		if (m_hwnd) {
			m_timerId = SetTimer(m_hwnd, TIMER_ID, 16, NULL);
		}
	}

	// --- Message Handling ---

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_TIMER && wp == TIMER_ID) {
			// Smoothly interpolate current value toward target
			float diff = m_targetValue - m_currentValue;

			// If difference is significant, update physics
			if (std::abs(diff) > 0.01f) {
				// Damping factor: 0.15f provides a nice springy feel
				m_currentValue += diff * 0.15f;

				// Snap if very close
				if (std::abs(m_targetValue - m_currentValue) < 0.05f) {
					m_currentValue = m_targetValue;
				}

				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return true;
		}
		return false;
	}

	// --- Rendering Helpers ---

private:
	D2D1_POINT_2F GetPointOnArc(D2D1_POINT_2F center, float radius, float angleDegrees) {
		float radians = DEG2RAD(angleDegrees);
		return D2D1::Point2F(
			center.x + radius * cosf(radians),
			center.y + radius * sinf(radians)
		);
	}

	// Create a path geometry for an arc to enable DrawGeometry (cleaner than simplified lines)
	void CreateArcGeometry(ID2D1Factory* pFactory, D2D1_POINT_2F center, float radius, float startAngle, float sweepAngle, ID2D1PathGeometry** ppGeometry) {
		if (!pFactory || !ppGeometry) return;

		pFactory->CreatePathGeometry(ppGeometry);
		if (*ppGeometry) {
			ID2D1GeometrySink* pSink = nullptr;
			(*ppGeometry)->Open(&pSink);
			if (pSink) {
				D2D1_POINT_2F startPoint = GetPointOnArc(center, radius, startAngle);
				D2D1_POINT_2F endPoint = GetPointOnArc(center, radius, startAngle + sweepAngle);

				pSink->BeginFigure(startPoint, D2D1_FIGURE_BEGIN_HOLLOW);

				// Determine arc size: > 180 degrees is a large arc
				D2D1_ARC_SIZE arcSize = (sweepAngle > 180.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
				// Sweep direction: Clockwise
				D2D1_SWEEP_DIRECTION sweepDir = D2D1_SWEEP_DIRECTION_CLOCKWISE;

				pSink->AddArc(D2D1::ArcSegment(endPoint, D2D1::SizeF(radius, radius), 0.0f, sweepDir, arcSize));

				pSink->EndFigure(D2D1_FIGURE_END_OPEN);
				pSink->Close();
				pSink->Release();
			}
		}
	}

public:
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 0. Resources
		HRESULT hr = S_OK;
		ID2D1Factory* pFactory = nullptr;
		pRT->GetFactory(&pFactory); // Weak reference, don't release pFactory from GetFactory

		// 1. Setup Geometry
		D2D1_SIZE_F size = pRT->GetSize();
		float width = size.width;
		float height = size.height;
		float centerX = width / 2.0f;
		float centerY = height / 2.0f + (height * 0.1f);
		float radius = (std::min)(width, height) * 0.4f;

		D2D1_POINT_2F center = D2D1::Point2F(centerX, centerY);

		// 2. Resolve Styles
		const char* cName = GetControlName();
		std::string sub = GetProperty("subclass");
		bool isEnabled = IsWindowEnabled(m_hwnd);

		D2D1_COLOR_F cBg = CSSColorToD2D(GetStyle("background-color", "", cName, sub.c_str(), false, isEnabled, false));
		D2D1_COLOR_F cFace = CSSColorToD2D(GetStyle("face-color", "#F5F5F5", cName, sub.c_str(), false, isEnabled, false));
		D2D1_COLOR_F cText = CSSColorToD2D(GetStyle("foreground-color", "#202020", cName, sub.c_str(), false, isEnabled, false));

		// Custom logic for empty styles to mimic original dark/light detection
		float lum = 0.299f * cFace.r + 0.587f * cFace.g + 0.114f * cFace.b;
		bool isDark = lum < 0.5f;

		std::string borderHex = GetStyle("border-color", "", cName, sub.c_str(), false, isEnabled, false);
		D2D1_COLOR_F cTrack;
		if (borderHex.empty()) cTrack = isDark ? D2D1::ColorF(0.23f, 0.23f, 0.23f) : D2D1::ColorF(0.78f, 0.78f, 0.78f);
		else cTrack = CSSColorToD2D(borderHex);

		D2D1_COLOR_F cAccent = CSSColorToD2D(m_accentColorHex);
		D2D1_COLOR_F cNeedle = CSSColorToD2D(m_needleColorHex);

		// 3. Draw Base
		// Use helper for standardized background
		DrawWidgetBackground(pRT, D2D1::RectF(0, 0, width, height), false);

		// 4. Create Brushes & Styles
		ID2D1SolidColorBrush* pTrackBrush = nullptr;
		ID2D1SolidColorBrush* pAccentBrush = nullptr;
		ID2D1SolidColorBrush* pNeedleBrush = nullptr;
		ID2D1SolidColorBrush* pTextBrush = nullptr;
		ID2D1SolidColorBrush* pTickBrush = nullptr;
		ID2D1StrokeStyle* pRoundStroke = nullptr;

		pRT->CreateSolidColorBrush(cTrack, &pTrackBrush);
		pRT->CreateSolidColorBrush(cAccent, &pAccentBrush);
		pRT->CreateSolidColorBrush(cNeedle, &pNeedleBrush);
		pRT->CreateSolidColorBrush(cText, &pTextBrush);
		pRT->CreateSolidColorBrush(isDark ? D2D1::ColorF(0.4f, 0.4f, 0.4f) : D2D1::ColorF(0.6f, 0.6f, 0.6f), &pTickBrush);

		// Create Round Stroke Style for modern 2026 look
		D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
			D2D1_CAP_STYLE_ROUND, // Start
			D2D1_CAP_STYLE_ROUND, // End
			D2D1_CAP_STYLE_ROUND, // Dash
			D2D1_LINE_JOIN_ROUND,
			10.0f,
			D2D1_DASH_STYLE_SOLID,
			0.0f
		);
		pFactory->CreateStrokeStyle(&strokeProps, nullptr, 0, &pRoundStroke);

		float trackThickness = 8.0f * (radius / 100.0f); // Scale thickness relative to radius
		trackThickness = (std::max)(4.0f, trackThickness);

		// 5. Draw Background Track
		ID2D1PathGeometry* pTrackGeo = nullptr;
		CreateArcGeometry(pFactory, center, radius, START_ANGLE, SWEEP_ANGLE, &pTrackGeo);

		if (pTrackGeo && pTrackBrush) {
			pRT->DrawGeometry(pTrackGeo, pTrackBrush, trackThickness, pRoundStroke);
		}

		// 6. Draw Active Range
		if (m_maxValue < m_minValue) m_maxValue = m_minValue + 0.01f;
		float safeCurrent = (std::max)(m_minValue, (std::min)(m_currentValue, m_maxValue));
		float progress = (safeCurrent - m_minValue) / (m_maxValue - m_minValue);

		if (progress > 0.001f) {
			ID2D1PathGeometry* pActiveGeo = nullptr;
			CreateArcGeometry(pFactory, center, radius, START_ANGLE, SWEEP_ANGLE * progress, &pActiveGeo);

			if (pActiveGeo && pAccentBrush) {
				pRT->DrawGeometry(pActiveGeo, pAccentBrush, trackThickness, pRoundStroke);
			}
			SafeRelease(&pActiveGeo);
		}

		// 7. Draw Ticks
		// We draw ticks manually as lines
		float innerTickR = radius - (trackThickness * 1.5f);
		float outerTickR = radius - (trackThickness * 0.2f);

		for (int i = 0; i <= 10; ++i) {
			float angle = START_ANGLE + (SWEEP_ANGLE * (i / 10.0f));
			D2D1_POINT_2F p1 = GetPointOnArc(center, innerTickR, angle);
			D2D1_POINT_2F p2 = GetPointOnArc(center, outerTickR, angle);

			pRT->DrawLine(p1, p2, pTickBrush, (std::max)(1.0f, trackThickness * 0.25f));
		}

		// 8. Draw Needle
		float needleAngle = START_ANGLE + (SWEEP_ANGLE * progress);
		D2D1_POINT_2F needleTip = GetPointOnArc(center, radius - 5.0f, needleAngle);
		D2D1_POINT_2F needleBase = GetPointOnArc(center, -10.0f, needleAngle); // Overhang

		if (pNeedleBrush) {
			// Main needle line
			pRT->DrawLine(needleBase, needleTip, pNeedleBrush, (std::max)(2.0f, trackThickness * 0.4f), pRoundStroke);

			// Center Hub
			float hubSize = (std::max)(10.0f, radius * 0.1f);
			D2D1_ELLIPSE hub = D2D1::Ellipse(center, hubSize / 2, hubSize / 2);

			// Fill hub with track/dark color, stroke with needle color
			ID2D1SolidColorBrush* pHubBrush = nullptr;
			pRT->CreateSolidColorBrush(isDark ? D2D1::ColorF(0.8f, 0.8f, 0.8f) : D2D1::ColorF(0.2f, 0.2f, 0.2f), &pHubBrush);
			if (pHubBrush) {
				pRT->FillEllipse(hub, pHubBrush);
				SafeRelease(&pHubBrush);
			}
			pRT->DrawEllipse(hub, pNeedleBrush, 2.0f);
		}

		// 9. Draw Text
		// We use GetDWriteFactory() helper.
		// For the Value, we need dynamic sizing which DrawTextStyled might not fully cover if it uses fixed sizes,
		// so we use raw DWrite for the big number, and DrawTextStyled for the label.

		auto dwFactory = GetDWriteFactory();
		if (dwFactory && pTextBrush) {
			// A. Draw Value (Large)
			IDWriteTextFormat* pValueFmt = nullptr;
			float fontSize = radius * 0.4f;

			dwFactory->CreateTextFormat(
				L"Segoe UI",
				NULL,
				DWRITE_FONT_WEIGHT_BOLD,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				fontSize,
				L"en-us",
				&pValueFmt
			);

			if (pValueFmt) {
				pValueFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				pValueFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

				std::wstring valStr = std::to_wstring((int)std::round(m_currentValue));

				// Define area for text below center
				D2D1_RECT_F valRect = D2D1::RectF(
					centerX - radius,
					centerY + (radius * 0.1f),
					centerX + radius,
					centerY + (radius * 0.8f)
				);

				pRT->DrawText(
					valStr.c_str(),
					(UINT32)valStr.length(),
					pValueFmt,
					valRect,
					pTextBrush
				);
				SafeRelease(&pValueFmt);
			}

			// B. Draw Label (Small) via Helper
			std::string fullLabel = m_label + " (" + m_unit + ")";

			// Calculate a rect at the bottom
			D2D1_RECT_F labelRect = D2D1::RectF(
				centerX - radius,
				centerY + (radius * 0.6f), // Push down below value
				centerX + radius,
				centerY + radius + 20.0f
			);

			// Use the requested helper for the label text
			DrawTextStyled(pRT, fullLabel, labelRect, false);
		}

		// Cleanup
		SafeRelease(&pTrackGeo);
		SafeRelease(&pTrackBrush);
		SafeRelease(&pAccentBrush);
		SafeRelease(&pNeedleBrush);
		SafeRelease(&pTextBrush);
		SafeRelease(&pTickBrush);
		SafeRelease(&pRoundStroke);
	}

	// --- Property Logic ---

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		if (t == "value") {
			try { m_targetValue = std::clamp(std::stof(val), m_minValue, m_maxValue); }
			catch (...) {}
			// Animation handles the rest
		}
		else if (t == "min") {
			try { m_minValue = std::stof(val); }
			catch (...) {}
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "max") {
			try { m_maxValue = std::stof(val); }
			catch (...) {}
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "label") {
			m_label = val;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "unit") {
			m_unit = val;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "accent_color") {
			m_accentColorHex = val;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "needle_color") {
			m_needleColorHex = val;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}

		WidgetImpl::OnPropertyChanged(key, value);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new GaugeSpeedOmeter();
}