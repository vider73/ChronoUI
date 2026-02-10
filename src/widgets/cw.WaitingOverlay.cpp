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

#ifndef NOMINMAX
#define NOMINMAX
#endif

class WaitingOverlay : public ChronoUI::WidgetImpl
{
	// --- Animation State ---
	bool isWaiting = false;
	float m_rotationAngle;
	const float m_rotationSpeed = 12.0f; // Degrees per frame

	// --- Cached D2D Resources ---
	ID2D1StrokeStyle* m_pStrokeStyleRound = nullptr;

public:
	WaitingOverlay() {
		m_rotationAngle = 0.0f;

		// Default property state
		SetProperty("waiting", "true");
		isWaiting = true;
	}

	virtual ~WaitingOverlay() {
		SafeRelease(&m_pStrokeStyleRound);
	}

	virtual void __stdcall SetBounds(int x, int y, int w, int h) override {
		// Update local bounds
		if (m_hwnd) {
			SetWindowPos(m_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

			// Logic to keep window on top/bottom based on state
			if (isWaiting)
				SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
			else
				SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
		}
	}

	const char* __stdcall GetControlName() override {
		return "WaitingOverlay";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 1,
            "description": "An overlay that dims content and shows a modern spinner.",
            "properties": [
                { "name": "waiting", "type": "boolean", "description": "If true, shows the overlay." }
            ],
            "events": []
        })json";
	}

	// --- Helper: Create Arc Geometry ---
	void CreateArcGeometry(ID2D1Factory* pFactory, float radius, float sweepAngle, ID2D1PathGeometry** ppGeometry) {
		if (!pFactory) return;

		pFactory->CreatePathGeometry(ppGeometry);
		if (*ppGeometry) {
			ID2D1GeometrySink* pSink = nullptr;
			(*ppGeometry)->Open(&pSink);
			if (pSink) {
				// Start at top (relative to rotation 0)
				D2D1_POINT_2F startPoint = D2D1::Point2F(radius, 0);

				float radians = sweepAngle * (3.14159265f / 180.0f);
				D2D1_POINT_2F endPoint = D2D1::Point2F(
					radius * cos(radians),
					radius * sin(radians)
				);

				pSink->BeginFigure(startPoint, D2D1_FIGURE_BEGIN_HOLLOW);
				pSink->AddArc(D2D1::ArcSegment(
					endPoint,
					D2D1::SizeF(radius, radius),
					0.0f,
					D2D1_SWEEP_DIRECTION_CLOCKWISE,
					(sweepAngle > 180.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
				));
				pSink->EndFigure(D2D1_FIGURE_END_OPEN);
				pSink->Close();
				pSink->Release();
			}
		}
	}

	// --- Drawing Logic ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		if (!isWaiting || !pRT) {
			return;
		}

		// *** FIX 1: Removed the premature 'return;' that was here ***

		// Retrieve Factory from RT to create geometry/strokes safely
		ID2D1Factory* pFactory = nullptr;
		pRT->GetFactory(&pFactory);
		if (!pFactory) return;
		
		// Initialize Stroke Style for rounded caps if not already done
		if (!m_pStrokeStyleRound) {
			D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
				D2D1_CAP_STYLE_ROUND,
				D2D1_CAP_STYLE_ROUND,
				D2D1_CAP_STYLE_ROUND,
				D2D1_LINE_JOIN_ROUND,
				10.0f,
				D2D1_DASH_STYLE_SOLID,
				0.0f
			);
			pFactory->CreateStrokeStyle(&props, nullptr, 0, &m_pStrokeStyleRound);
		}

		D2D1_SIZE_F size = pRT->GetSize();
		float width = size.width;
		float height = size.height;

		// 1. Draw the Dimming Layer (Black with ~35% opacity)
		D2D1_RECT_F fullRect = D2D1::RectF(0, 0, width, height);
		ID2D1SolidColorBrush* pDimBrush = nullptr;
		pRT->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.35f), &pDimBrush);

		if (pDimBrush) {
			pRT->FillRectangle(&fullRect, pDimBrush);
			SafeRelease(&pDimBrush);
		}

		// 2. Calculate Spinner Metrics
		float cx = width * 0.5f;
		float cy = height * 0.5f;
		float margin = 10.0f;
		float maxRadius = (std::min)(width, height) * 0.5f - margin;
		float desiredRadius = (std::min)(maxRadius, 40.0f);
		float radius = (std::min)(desiredRadius, maxRadius);
		float strokeWidth = (std::min)(5.0f, radius * 0.2f);

		if (radius > 0) {
			// 3. Prepare Colors
			// Google Blue: #4285F4
			ID2D1SolidColorBrush* pMainBrush = nullptr;
			ID2D1SolidColorBrush* pTrackBrush = nullptr;

			pRT->CreateSolidColorBrush(D2D1::ColorF(0x4285F4), &pMainBrush);
			pRT->CreateSolidColorBrush(D2D1::ColorF(0x4285F4, 0.2f), &pTrackBrush);

			if (pMainBrush && pTrackBrush) {
				// 4. Draw Geometry

				// Draw Track (Full circle)
				D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
				pRT->DrawEllipse(&ellipse, pTrackBrush, strokeWidth);

				// Create the arc geometry (270 degrees)
				ID2D1PathGeometry* pArcGeo = nullptr;
				CreateArcGeometry(pFactory, radius, 270.0f, &pArcGeo);

				if (pArcGeo) {
					// Apply rotation and translation
					D2D1_MATRIX_3X2_F oldTransform;
					pRT->GetTransform(&oldTransform);

					// Rotate around center (0,0 of the arc) then translate to screen center
					D2D1_MATRIX_3X2_F transform =
						D2D1::Matrix3x2F::Rotation(m_rotationAngle, D2D1::Point2F(0, 0)) *
						D2D1::Matrix3x2F::Translation(cx, cy);

					pRT->SetTransform(transform * oldTransform);

					// Draw the spinner
					pRT->DrawGeometry(pArcGeo, pMainBrush, strokeWidth, m_pStrokeStyleRound);

					// Restore transform
					pRT->SetTransform(oldTransform);

					SafeRelease(&pArcGeo);
				}
			}
			SafeRelease(&pMainBrush);
			SafeRelease(&pTrackBrush);
		}
		
		// Cleanup factory reference (GetFactory calls AddRef)
		SafeRelease(&pFactory);
	}

	// --- Update Logic ---
	bool OnUpdateAnimation(float deltaTime) override {
		if (isWaiting) {
			m_rotationAngle += m_rotationSpeed;
			if (m_rotationAngle >= 360.0f) {
				m_rotationAngle -= 360.0f;
			}
			return true;
		}
		return false;
	}

	// --- Message Handling ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override
	{
		switch (msg) {
		case WM_PAINT:
			return false; // Let host call OnDrawWidget

		case WM_ERASEBKGND:
			return 1; // Prevent GDI flicker

		case WM_CREATE:
			// Ensure timer is running if created in waiting state
			if (isWaiting) {
				::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
			}
			SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			return false;

		case WM_DESTROY:
			::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
			return false;

		case WM_NCHITTEST:
			// If waiting, we want to capture clicks (return HTCLIENT = 1)
			// If not waiting, we shouldn't be here (hidden), but safety return HTTRANSPARENT (-1) or false
			if (isWaiting) 
				return true;
			return false;

		case WM_SETCURSOR:
			if (isWaiting) {
				::SetCursor(::LoadCursor(NULL, IDC_WAIT));
				return true;
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
			// Consume input
			if (isWaiting) return true;
			break;
		}
		return false;
	}

	void OnFocus(bool f) override {
		// Pass focus back to parent if we accidentally get it, or just ignore
	}

	void OnPropertyChanged(const char* _key, const char* value) override {
		std::string key = _key;
		std::string val = value;
		if (key == "waiting") {
			bool newVal = (val == "true" || val == "1" || val == "yes");
			if (newVal != isWaiting) {
				isWaiting = newVal;
				if (isWaiting) {
					SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
					::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
				}
				else {
					::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
					SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
					// Force parent repaint if needed to clear overlay artifacts
					if (m_hwnd) {
						HWND parent = GetParent(m_hwnd);
						InvalidateRect(parent, NULL, TRUE);
					}
				}
			}
		}
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new WaitingOverlay();
}