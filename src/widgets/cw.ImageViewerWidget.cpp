#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h> // Required for loading images in D2D
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h> // For ComPtr

#include "WidgetImpl.hpp"

// Link DWrite, D2D, and WIC
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace ChronoUI;
using Microsoft::WRL::ComPtr;

class ImageViewerWidget : public WidgetImpl {
private:
	// WIC Factory for loading images
	ComPtr<IWICImagingFactory> m_pWICFactory;

	// The device-independent image source (loaded from file)
	ComPtr<IWICBitmapSource> m_pWICBitmapSource;
	ComPtr<IWICBitmapDecoder> m_pCurrentDecoder; // Kept for frame switching

	// The device-dependent bitmap (created from pWICBitmapSource on the GPU)
	ComPtr<ID2D1Bitmap> m_pD2DBitmap;

	// View State
	double m_scale = 1.0;
	double m_offsetX = 0;
	double m_offsetY = 0;

	// Animation Targets
	double m_targetScale = 1.0;
	double m_targetOffsetX = 0;
	double m_targetOffsetY = 0;

	// Interaction State
	D2D1_POINT_2F m_mousePos = { 0, 0 };
	bool m_isDragging = false;
	D2D1_POINT_2F m_lastMouse = { 0, 0 };

	// Multi-frame / GIF Support
	int m_currentFrame = 0;
	int m_frameCount = 1;

	const UINT_PTR ID_ANIM_TIMER = 9001;
	const int ANIM_INTERVAL = 16;

public:
	ImageViewerWidget() {
		// Initialize WIC Factory
		HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&m_pWICFactory)
		);

		// Initialize default properties
		SetProperty("image-path", "");
		SetProperty("frame", "0");
		SetProperty("width", "auto");
		SetProperty("height", "auto");
	}

	~ImageViewerWidget() {
		if (m_hwnd) ::KillTimer(m_hwnd, ID_ANIM_TIMER);
		// ComPtrs handle release automatically
	}

	const char* __stdcall GetControlName() override {
		return "ImageViewerWidget";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "High-performance D2D image viewer with zoom, pan and magnifier",
            "properties": [
                {"name": "image-path", "type": "string"},
                {"name": "frame", "type": "int"},
                {"name": "zoom-fit", "type": "action"},
                {"name": "zoom-width", "type": "action"},
                {"name": "zoom-height", "type": "action"}
            ],
            "events": [
                {"name": "onImageLoaded", "type": "action"},
                {"name": "onFocus", "type": "action"},
                {"name": "onBlur", "type": "action"}
            ]
        })json";
	}

	// --- Core Logic Methods ---

	void StartAnimation() {
		if (m_hwnd) ::SetTimer(m_hwnd, ID_ANIM_TIMER, ANIM_INTERVAL, NULL);
	}

	// Load image using WIC (Device Independent)
	void LoadImageFromPath(const std::wstring& path) {
		if (!m_pWICFactory || path.empty()) return;

		// Reset state
		m_pWICBitmapSource.Reset();
		m_pD2DBitmap.Reset(); // Will force recreation in OnDraw
		m_pCurrentDecoder.Reset();
		m_frameCount = 0;
		m_currentFrame = 0;

		HRESULT hr = m_pWICFactory->CreateDecoderFromFilename(
			path.c_str(),
			NULL,
			GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,
			&m_pCurrentDecoder
		);

		if (SUCCEEDED(hr)) {
			UINT count = 0;
			m_pCurrentDecoder->GetFrameCount(&count);
			m_frameCount = (int)count;
			LoadFrame(0);
		}
	}

	void LoadFrame(int index) {
		if (!m_pCurrentDecoder || index < 0 || index >= m_frameCount) return;

		ComPtr<IWICBitmapFrameDecode> pFrame;
		if (SUCCEEDED(m_pCurrentDecoder->GetFrame(index, &pFrame))) {
			// Convert to format compatible with D2D (32bppPBGRA)
			ComPtr<IWICFormatConverter> pConverter;
			m_pWICFactory->CreateFormatConverter(&pConverter);

			if (pConverter) {
				pConverter->Initialize(
					pFrame.Get(),
					GUID_WICPixelFormat32bppPBGRA,
					WICBitmapDitherTypeNone,
					NULL,
					0.f,
					WICBitmapPaletteTypeMedianCut
				);

				// Store the converted source. 
				// We don't create the D2D Bitmap here; we do it in OnDraw 
				// because it requires the specific RenderTarget.
				m_pWICBitmapSource = pConverter;
				m_pD2DBitmap.Reset(); // Invalidate GPU bitmap so it reloads

				// Reset zoom on first load, but not on frame change if we want stability
				if (m_currentFrame == 0 && index == 0) {
					// We need the size to calculate zoom, but D2D bitmap isn't ready.
					// We can get size from WIC source.
					UINT w, h;
					m_pWICBitmapSource->GetSize(&w, &h);
					// Defer zoom fit to first draw or calculate now
					// Trigger immediate redraw
				}
				m_currentFrame = index;
				FireEvent("onImageLoaded", "{}");
			}
		}
		if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
	}

	void ZoomToFit() {
		// Can't calc if we don't have dimensions. Use WIC source if D2D not ready.
		if (!m_pWICBitmapSource || !m_hwnd) return;

		RECT rc; GetClientRect(m_hwnd, &rc);
		double vw = (double)(rc.right - rc.left);
		double vh = (double)(rc.bottom - rc.top);

		UINT iw_u, ih_u;
		m_pWICBitmapSource->GetSize(&iw_u, &ih_u);
		double iw = (double)iw_u;
		double ih = (double)ih_u;

		if (vw <= 0 || vh <= 0 || iw <= 0 || ih <= 0) return;

		double scale = (std::min)(vw / iw, vh / ih);

		m_targetScale = scale;
		m_targetOffsetX = (vw - (iw * scale)) / 2.0;
		m_targetOffsetY = (vh - (ih * scale)) / 2.0;

		StartAnimation();
	}

	// --- WidgetImpl Overrides ---

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		// Mouse coordinates
		int sx_i = GET_X_LPARAM(lp);
		int sy_i = GET_Y_LPARAM(lp);
		float sx = (float)sx_i;
		float sy = (float)sy_i;

		switch (msg) {
		case WM_TIMER:
			if (wp == ID_ANIM_TIMER) {
				const double lerp = 0.20;
				bool moving = false;
				auto Interp = [&](double& cur, double tar) {
					if (std::abs(tar - cur) < 0.001) cur = tar;
					else { cur += (tar - cur) * lerp; moving = true; }
				};

				Interp(m_scale, m_targetScale);
				Interp(m_offsetX, m_targetOffsetX);
				Interp(m_offsetY, m_targetOffsetY);

				InvalidateRect(m_hwnd, NULL, FALSE);

				if (!moving) ::KillTimer(m_hwnd, ID_ANIM_TIMER);
				return true;
			}
			return false;

		case WM_MOUSEWHEEL: {
			int delta = GET_WHEEL_DELTA_WPARAM(wp);
			double zoomFactor = (delta > 0) ? 1.2 : 0.8;

			POINT pt; GetCursorPos(&pt);
			ScreenToClient(m_hwnd, &pt);

			double oldScale = m_targetScale;
			// Clamp Zoom (0.01x to 50x)
			m_targetScale = (std::max)(0.01, (std::min)(m_targetScale * zoomFactor, 50.0));

			// Zoom towards mouse pointer logic
			m_targetOffsetX = pt.x - (pt.x - m_targetOffsetX) * (m_targetScale / oldScale);
			m_targetOffsetY = pt.y - (pt.y - m_targetOffsetY) * (m_targetScale / oldScale);

			StartAnimation();
			return true;
		}

		case WM_LBUTTONDOWN:
			::SetFocus(m_hwnd);
			::KillTimer(m_hwnd, ID_ANIM_TIMER);
			m_isDragging = true;
			m_lastMouse = { sx, sy };
			::SetCapture(m_hwnd);
			return true;

		case WM_MOUSEMOVE:
			m_mousePos = { sx, sy };

			if (m_isDragging) {
				m_offsetX += (sx - m_lastMouse.x);
				m_offsetY += (sy - m_lastMouse.y);
				m_targetOffsetX = m_offsetX;
				m_targetOffsetY = m_offsetY;
				m_lastMouse = { sx, sy };

				::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
				InvalidateRect(m_hwnd, NULL, FALSE);
				return true;
			}

			// Magnifier update check (Ctrl key)
			if (::GetAsyncKeyState(VK_CONTROL) & 0x8000) {
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return false;

		case WM_LBUTTONUP:
			if (m_isDragging) {
				m_isDragging = false;
				::ReleaseCapture();
				return true;
			}
			return false;

		case WM_KEYDOWN:
		case WM_KEYUP:
			if (wp == VK_CONTROL) InvalidateRect(m_hwnd, NULL, FALSE);
			return false;
		}

		return false;
	}

	// --- Drawing Entry Point ---
	// --- Drawing Entry Point ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		// 1. Get Control Bounds
		RECT rc; GetClientRect(m_hwnd, &rc);
		D2D1_RECT_F bounds = D2D1::RectF(0, 0, (float)(rc.right), (float)(rc.bottom));

		// 2. Draw Background
		DrawWidgetBackground(pRT, bounds, true);

		// 3. Create Device Bitmap if needed (Bridge between WIC and D2D)
		if (m_pWICBitmapSource && !m_pD2DBitmap) {
			pRT->CreateBitmapFromWicBitmap(m_pWICBitmapSource.Get(), nullptr, &m_pD2DBitmap);

			// If just loaded and never scaled, zoom to fit now
			if (m_scale == 1.0 && m_targetScale == 1.0 && m_frameCount > 0) {
				ZoomToFit();
				m_scale = m_targetScale; m_offsetX = m_targetOffsetX; m_offsetY = m_targetOffsetY;
			}
		}

		if (m_pD2DBitmap) {
			// Save Transform State
			D2D1_MATRIX_3X2_F oldTransform;
			pRT->GetTransform(&oldTransform);

			D2D1_SIZE_F imgSize = m_pD2DBitmap->GetSize();

			// Apply Pan & Zoom
			// Matrix Order: Scale -> Translate
			D2D1_MATRIX_3X2_F transform =
				D2D1::Matrix3x2F::Scale((float)m_scale, (float)m_scale) *
				D2D1::Matrix3x2F::Translation((float)m_offsetX, (float)m_offsetY);

			pRT->SetTransform(transform);

			// Draw Image
			D2D1_RECT_F destRect = D2D1::RectF(0, 0, imgSize.width, imgSize.height);

			// FIXED: Use LINEAR for ID2D1RenderTarget (D2D 1.0 compatibility)
			pRT->DrawBitmap(
				m_pD2DBitmap.Get(),
				destRect,
				1.0f,
				D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
			);

			// Restore Transform for UI overlays (Magnifier)
			pRT->SetTransform(oldTransform);

			// Draw Magnifier
			if (::GetAsyncKeyState(VK_CONTROL) & 0x8000) {
				DrawMagnifier(pRT, bounds);
			}
		}
		else {
			// No Image Loaded Placeholder
			D2D1_RECT_F txtRect = bounds;
			txtRect.left += 10; txtRect.top += 10;
			DrawTextStyled(pRT, "No Image Loaded", txtRect, false);
		}
	}

	void DrawMagnifier(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds) {
		if (!m_pD2DBitmap) return;

		float width = bounds.right - bounds.left;
		float height = bounds.bottom - bounds.top;

		float zoomBoxSize = (std::max)(150.0f, width / 5.0f);
		float magnifierScale = (float)m_scale * 3.0f;

		// Calculate the "Source" rectangle in the original image coordinate space
		// Inverse transform logic: (Mouse - Offset) / Scale
		float ix = (m_mousePos.x - (float)m_offsetX) / (float)m_scale;
		float iy = (m_mousePos.y - (float)m_offsetY) / (float)m_scale;

		// How much of the original image fits in the magnifier?
		float srcArea = zoomBoxSize / magnifierScale * (float)m_scale; // Correct relative sizing

		// Actually, logic is simpler: 
		// We want to draw the image at (Mouse - ix*MagScale, Mouse - iy*MagScale) scaled by MagScale.
		// But D2D makes clipping easy. Let's calculate Dest Rect logic.

		float gap = 50.0f;
		float destX = m_mousePos.x + gap;
		float destY = m_mousePos.y - (zoomBoxSize / 2.0f);

		// Keep inside screen
		if (destX + zoomBoxSize > width) destX = m_mousePos.x - zoomBoxSize - gap;
		if (destY < 0) destY = 0;
		if (destY + zoomBoxSize > height) destY = height - zoomBoxSize;

		D2D1_RECT_F magRect = D2D1::RectF(destX, destY, destX + zoomBoxSize, destY + zoomBoxSize);

		// 1. Draw Shadow
		ComPtr<ID2D1SolidColorBrush> pShadowBrush;
		pRT->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.3f), &pShadowBrush);
		D2D1_RECT_F shadowRect = magRect;
		shadowRect.left += 4; shadowRect.top += 4; shadowRect.right += 4; shadowRect.bottom += 4;
		if (pShadowBrush) pRT->FillRectangle(shadowRect, pShadowBrush.Get());

		// 2. Clip to Magnifier Box
		pRT->PushAxisAlignedClip(magRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

		// 3. Fill Background White
		ComPtr<ID2D1SolidColorBrush> pWhiteBrush;
		pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pWhiteBrush);
		if (pWhiteBrush) pRT->FillRectangle(magRect, pWhiteBrush.Get());

		// 4. Draw Zoomed Image
		// Logic: We translate the image so the pixel under mouse (ix, iy) is at the center of magRect,
		// and we scale it by magnifierScale.

		float centerX = destX + zoomBoxSize / 2.0f;
		float centerY = destY + zoomBoxSize / 2.0f;

		D2D1_MATRIX_3X2_F magTransform =
			D2D1::Matrix3x2F::Scale(magnifierScale, magnifierScale) *
			D2D1::Matrix3x2F::Translation(
				centerX - (ix * magnifierScale),
				centerY - (iy * magnifierScale)
			);

		pRT->SetTransform(magTransform);

		pRT->DrawBitmap(m_pD2DBitmap.Get(),
			D2D1::RectF(0, 0, m_pD2DBitmap->GetSize().width, m_pD2DBitmap->GetSize().height),
			1.0f,
			D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR // Pixelated look for high zoom
		);

		// Reset Transform & Clip
		pRT->SetTransform(D2D1::Matrix3x2F::Identity());
		pRT->PopAxisAlignedClip();

		// 5. Draw Border
		ComPtr<ID2D1SolidColorBrush> pBorderBrush;
		// Windows 10/11 Accent Blue-ish
		pRT->CreateSolidColorBrush(CSSColorToD2D("#0078D7"), &pBorderBrush);
		if (pBorderBrush) pRT->DrawRectangle(magRect, pBorderBrush.Get(), 2.0f);
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		if (t == "image-path") {
			if (val.empty()) {
				m_pWICBitmapSource.Reset();
				m_pD2DBitmap.Reset();
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			else {
				std::wstring wpath = NarrowToWide(val);
				LoadImageFromPath(wpath);
			}
		}
		else if (t == "zoom-fit") {
			ZoomToFit();
		}
		else if (t == "frame") {
			LoadFrame(atoi(val.c_str()));
		}

		WidgetImpl::OnPropertyChanged(key, value);
	}

	void __stdcall OnFocus(bool f) override {
		FireEvent(f ? "onFocus" : "onBlur", "{}");
		InvalidateRect(m_hwnd, NULL, FALSE);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new ImageViewerWidget();
}