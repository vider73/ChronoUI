#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <iomanip>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "user32.lib")

using namespace ChronoUI;

class EyesControl : public WidgetImpl {
private:
	// State (Using D2D types for coordinates)
	D2D1_POINT_2F m_currentLookPos = { 0.0f, 0.0f };
	D2D1_POINT_2F m_targetLookPos = { 0.0f, 0.0f };
	UINT_PTR m_timerId = 0;

	// Animation State (Blinking)
	float m_blinkFactor = 0.0f;   // 0 = open, 1 = closed
	bool m_isBlinking = false;

	// Resources (Created on draw, or cached if optimization is needed)
	// For this lightweight widget, creating brushes in OnDraw is acceptable,
	// but caching them is "2026 style" for performance.
	// However, to keep code simple and robust against device loss, we'll create them in OnDraw 
	// or use the lightweight approach standard for simple widgets.

public:
	EyesControl() {
		SetProperty("sclera_color", "#ffffff");
		SetProperty("pupil_color", "#000000");
		SetProperty("speed", "0.15");
	}

	virtual ~EyesControl() {
		if (m_hwnd && m_timerId) {
			KillTimer(m_hwnd, m_timerId);
		}
	}

	const char* __stdcall GetControlName() override { return "EyesControl"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "Animated eyes that follow the mouse cursor (Direct2D)",
            "properties": [
                { "name": "speed", "type": "float", "description": "Tracking speed (0.01 - 1.0)" },
                { "name": "sclera_color", "type": "string", "description": "Color of the eye white (hex)" },
                { "name": "pupil_color", "type": "string", "description": "Color of the pupil (hex)" }
            ]
        })json";
	}

	void __stdcall Create(HWND parent) override {
		// Base creation
		WidgetImpl::Create(parent);

		if (m_hwnd) {
			// 60 FPS update for smooth animation
			m_timerId = SetTimer(m_hwnd, 101, 16, NULL);

			// Initialize look position to center
			RECT cr;
			GetClientRect(m_hwnd, &cr);
			m_currentLookPos = { (float)cr.right / 2.0f, (float)cr.bottom / 2.0f };
		}
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_TIMER && wp == m_timerId) {
			// 1. Global Mouse Tracking
			POINT pt;
			GetCursorPos(&pt);           // Get global screen coords
			ScreenToClient(m_hwnd, &pt); // Convert to control-relative coords

			m_targetLookPos = { (float)pt.x, (float)pt.y };

			float m_lerpSpeed = GetFloatProperty("speed");

			// LERP interpolation
			m_currentLookPos.x += (m_targetLookPos.x - m_currentLookPos.x) * m_lerpSpeed;
			m_currentLookPos.y += (m_targetLookPos.y - m_currentLookPos.y) * m_lerpSpeed;

			// 2. Blinking Logic
			UpdateBlink();

			// Trigger redraw
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		}
		return false;
	}

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// 1. Draw modern widget background (handles rounded corners, transparency, etc.)
		DrawWidgetBackground(pRT, rect, false);

		float fW = size.width;
		float fH = size.height;

		float eyeWidth = (fW / 2.0f) * 0.75f;
		float eyeHeight = fH * 0.85f;
		float centerX = fW / 2.0f;
		float centerY = fH / 2.0f;
		float horizontalGap = fW * 0.22f;

		// Prepare colors using Helper
		D2D1_COLOR_F scleraColor = CSSColorToD2D(GetProperty("sclera_color"));
		D2D1_COLOR_F pupilColor = CSSColorToD2D(GetProperty("pupil_color"));

		// Create Brushes
		ID2D1SolidColorBrush* pScleraBrush = nullptr;
		ID2D1SolidColorBrush* pPupilBrush = nullptr;
		ID2D1SolidColorBrush* pOutlineBrush = nullptr;
		ID2D1SolidColorBrush* pGlintBrush = nullptr;

		pRT->CreateSolidColorBrush(scleraColor, &pScleraBrush);
		pRT->CreateSolidColorBrush(pupilColor, &pPupilBrush);
		pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &pOutlineBrush);
		pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f), &pGlintBrush); // 220/255 alpha

		if (pScleraBrush && pPupilBrush && pOutlineBrush && pGlintBrush) {
			// Draw Left Eye
			DrawSingleEye(pRT, D2D1::Point2F(centerX - horizontalGap, centerY), eyeWidth, eyeHeight, centerX, horizontalGap,
				pScleraBrush, pPupilBrush, pOutlineBrush, pGlintBrush);

			// Draw Right Eye
			DrawSingleEye(pRT, D2D1::Point2F(centerX + horizontalGap, centerY), eyeWidth, eyeHeight, centerX, horizontalGap,
				pScleraBrush, pPupilBrush, pOutlineBrush, pGlintBrush);
		}

		// Cleanup local brushes
		SafeRelease(&pScleraBrush);
		SafeRelease(&pPupilBrush);
		SafeRelease(&pOutlineBrush);
		SafeRelease(&pGlintBrush);
	}

private:
	void UpdateBlink() {
		if (m_isBlinking) {
			m_blinkFactor += 0.2f; // Close speed
			if (m_blinkFactor >= 1.0f) {
				m_blinkFactor = 1.0f;
				m_isBlinking = false; // Start opening next frame
			}
		}
		else if (m_blinkFactor > 0.0f) {
			m_blinkFactor -= 0.2f; // Open speed
			if (m_blinkFactor < 0.0f) m_blinkFactor = 0.0f;
		}
		else {
			// Random chance to start a blink
			if (rand() % 250 == 1) m_isBlinking = true;
		}
	}

	void DrawSingleEye(ID2D1RenderTarget* pRT,
		D2D1_POINT_2F eyeCenter,
		float w,
		float h,
		float controlCenterX,
		float horizontalGap,
		ID2D1SolidColorBrush* pScleraBrush,
		ID2D1SolidColorBrush* pPupilBrush,
		ID2D1SolidColorBrush* pOutlineBrush,
		ID2D1SolidColorBrush* pGlintBrush)
	{
		float currentH = h * (1.0f - m_blinkFactor);

		// If eye is essentially closed, draw a line
		if (currentH < 2.0f) {
			pRT->DrawLine(
				D2D1::Point2F(eyeCenter.x - w / 2, eyeCenter.y),
				D2D1::Point2F(eyeCenter.x + w / 2, eyeCenter.y),
				pOutlineBrush,
				3.0f
			);
			return;
		}

		// 1. Sclera
		// D2D uses Radius, GDI+ used Diameter(Width/Height)
		D2D1_ELLIPSE eyeEllipse = D2D1::Ellipse(eyeCenter, w / 2.0f, currentH / 2.0f);

		pRT->FillEllipse(eyeEllipse, pScleraBrush);
		pRT->DrawEllipse(eyeEllipse, pOutlineBrush, 2.5f);

		// 2. Pupil Calculation
		float dx = m_currentLookPos.x - eyeCenter.x;
		float dy = m_currentLookPos.y - eyeCenter.y;

		// --- Neutralize horizontal look when mouse is between eyes ---
		float distFromFaceCenter = std::abs(m_currentLookPos.x - controlCenterX);
		float neutralZone = horizontalGap * 0.7f;

		if (distFromFaceCenter < neutralZone) {
			float factor = distFromFaceCenter / neutralZone;
			factor *= factor; // Ease-in
			dx *= factor;
		}
		// -------------------------------------------------------------

		float angle = std::atan2(dy, dx);
		float dist = std::sqrt(dx * dx + dy * dy);

		float pupilW = w * 0.35f;
		float pupilH = currentH * 0.45f;

		// Constrain pupil inside sclera
		// Radius constraint calculation
		float limitX = (w / 2.0f) - (pupilW / 2.0f) - 4.0f;
		float limitY = (currentH / 2.0f) - (pupilH / 2.0f) - 4.0f;

		// Calculate offset (use std::min with parenthesis as requested)
		float travelX = std::cos(angle) * (std::min)(dist * 0.1f, limitX);
		float travelY = std::sin(angle) * (std::min)(dist * 0.1f, limitY);

		// 3. Draw Pupil
		D2D1_POINT_2F pupilCenter = D2D1::Point2F(
			eyeCenter.x + travelX,
			eyeCenter.y + travelY
		);

		D2D1_ELLIPSE pupilEllipse = D2D1::Ellipse(pupilCenter, pupilW / 2.0f, pupilH / 2.0f);
		pRT->FillEllipse(pupilEllipse, pPupilBrush);

		// 4. Highlight (Glint) - Offset slightly relative to pupil size
		// Pupil top-left + percentage offset
		float glintOffsetX = pupilW * 0.2f;
		float glintOffsetY = -(pupilH * 0.2f); // Move up/right relative to pupil center

		D2D1_POINT_2F glintCenter = D2D1::Point2F(
			pupilCenter.x + (pupilW * 0.15f),
			pupilCenter.y - (pupilH * 0.15f)
		);

		float glintRadiusX = (pupilW * 0.3f) / 2.0f;
		float glintRadiusY = (pupilH * 0.3f) / 2.0f;

		D2D1_ELLIPSE glintEllipse = D2D1::Ellipse(glintCenter, glintRadiusX, glintRadiusY);
		pRT->FillEllipse(glintEllipse, pGlintBrush);
	}
};

// Factory export
extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new EyesControl();
}