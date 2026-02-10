#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h> 
#include <string>
#include <algorithm>
#include <vector>

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

#include "WidgetImpl.hpp"

using namespace ChronoUI;

// --- Helper: WIC Image Loader (Kept from your snippet) ---
HRESULT LoadBitmapFromFile(ID2D1RenderTarget* pRT, const std::wstring& uri, ID2D1Bitmap** ppBitmap) {
	HRESULT hr = S_OK;
	ComPtr<IWICImagingFactory> pWICFactory;
	hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWICFactory));

	ComPtr<IWICBitmapDecoder> pDecoder;
	if (SUCCEEDED(hr)) {
		hr = pWICFactory->CreateDecoderFromFilename(uri.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
	}

	ComPtr<IWICBitmapFrameDecode> pSource;
	if (SUCCEEDED(hr)) {
		hr = pDecoder->GetFrame(0, &pSource);
	}

	ComPtr<IWICFormatConverter> pConverter;
	if (SUCCEEDED(hr)) {
		hr = pWICFactory->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr)) {
		hr = pConverter->Initialize(pSource.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
	}

	if (SUCCEEDED(hr)) {
		hr = pRT->CreateBitmapFromWicBitmap(pConverter.Get(), NULL, ppBitmap);
	}
	return hr;
}

class TitleDescCard : public WidgetImpl {
	// Direct2D Resources
	ID2D1Bitmap* m_pBitmap = nullptr;

	// State Tracking
	std::string m_lastImagePath;
	std::string m_lastImageBase64;
	bool m_bImageDirty = true;

	// Modern Layout Constants (2026 Look)
	const float kPadding = 16.0f;           // Generous padding
	const float kImageSize = 24.0f;         // Icon size
	const float kPillPadding = 8.0f;        // Space around icon inside the pill
	const float kTitleGap = 4.0f;           // Gap between title and desc

	HCURSOR m_handCursor = nullptr;

public:
	TitleDescCard() {
		m_handCursor = LoadCursor(NULL, IDC_HAND);

		// Properties
		SetProperty("title", "Card Title");
		SetProperty("description", "This is a description of the card item.");
		SetProperty("top_text", "");
		SetProperty("image_path", "");
		SetProperty("image_base64", "");

		// Modern 2026 Palette Defaults
		SetProperty("background-color", "#FFFFFF");        // Card Face
		SetProperty("border-color", "#E5E7EB");            // Subtle gray border
		SetProperty("foreground-color", "#111827");        // Dark Slate (Title)
		SetProperty("description-color", "#6B7280");       // Cool Gray (Text)
		SetProperty("pill-color", "#F3F4F6");              // Light Gray Pill background
		SetProperty("dimmed-color", "#9CA3AF");            // Light text
	}

	virtual ~TitleDescCard() {
		SafeRelease(&m_pBitmap);
	}

	const char* __stdcall GetControlName() override { return "TitleDescCard"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "Modern card with pill-styled icon",
            "properties": [
                { "name": "title", "type": "string" },
                { "name": "description", "type": "string" },
                { "name": "image_path", "type": "string" },
                { "name": "image_base64", "type": "string" },
                { "name": "pill-color", "type": "color" }
            ],
            "events": [ { "name": "onClick", "type": "action" } ]
        })json";
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		if (t == "image_path") {
			if (m_lastImagePath != val) {
				m_lastImagePath = val;
				m_bImageDirty = true;
			}
		}
		else if (t == "image_base64") {
			if (m_lastImageBase64 != val) {
				m_lastImageBase64 = val;
				m_bImageDirty = true;
			}
		}
		WidgetImpl::OnPropertyChanged(key, value);
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_LBUTTONDOWN) {
			SetFocus(m_hwnd);
			FireEvent("onClick", "{}");
			return true;
		}
		if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
			SetCursor(m_handCursor);
			return true;
		}
		return WidgetImpl::OnMessage(msg, wp, lp);
	}

	void EnsureBitmap(ID2D1RenderTarget* pRT) {
		if (!m_bImageDirty && m_pBitmap) return;
		SafeRelease(&m_pBitmap);
		m_bImageDirty = false;

		if (!m_lastImageBase64.empty()) {
			ImageFromBase64(pRT, m_lastImageBase64.c_str(), &m_pBitmap);
		}
		else if (!m_lastImagePath.empty()) {
			LoadBitmapFromFile(pRT, NarrowToWide(m_lastImagePath), &m_pBitmap);
		}
	}

	// -------------------------------------------------------------------------
	// Modern Painting Entry Point
	// -------------------------------------------------------------------------
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F bounds = D2D1::RectF(0, 0, size.width, size.height);

		// 1. Draw Background (Using Base Class Helper)
		// This handles the background color, border, and hover state automatically.
		DrawWidgetBackground(pRT, bounds, true);

		// 2. Resolve Colors
		D2D1_COLOR_F titleColor = CSSColorToD2D(GetStringProperty("foreground-color"));
		D2D1_COLOR_F descColor = CSSColorToD2D(GetStringProperty("description-color"));
		D2D1_COLOR_F pillColor = CSSColorToD2D(GetStringProperty("pill-color"));

		// Load Bitmap if needed
		EnsureBitmap(pRT);

		// 3. Layout Calculation
		// Define the content area inside padding
		float left = bounds.left + kPadding;
		float top = bounds.top + kPadding;
		float right = bounds.right - kPadding;
		float bottom = bounds.bottom - kPadding;

		// 4. Draw Image with Pill Background (Top Right)
		float contentRightEdge = right; // Text stops here

		if (m_pBitmap) {
			float pillTotalSize = kImageSize + (kPillPadding * 2.0f);

			// Define Pill Rect
			D2D1_RECT_F pillRect;
			pillRect.right = right;
			pillRect.top = top;
			pillRect.left = right - pillTotalSize;
			pillRect.bottom = top + pillTotalSize;

			// Draw Pill Background
			// "Looks like 2026": Smooth squircle/rounded rect
			D2D1_ROUNDED_RECT roundedPill = D2D1::RoundedRect(pillRect, pillTotalSize / 2.0f, pillTotalSize / 2.0f);

			ComPtr<ID2D1SolidColorBrush> pPillBrush;
			pRT->CreateSolidColorBrush(pillColor, &pPillBrush);
			pRT->FillRoundedRectangle(roundedPill, pPillBrush.Get());

			// Draw Icon Centered in Pill
			D2D1_RECT_F iconRect = D2D1::RectF(
				pillRect.left + kPillPadding,
				pillRect.top + kPillPadding,
				pillRect.right - kPillPadding,
				pillRect.bottom - kPillPadding
			);

			pRT->DrawBitmap(m_pBitmap, iconRect);

			// Adjust text width so it doesn't hit the pill
			contentRightEdge = pillRect.left - kPadding;
		}

		// 5. Typography (Title & Description)
		// We use DirectWrite directly here for fine-tuned hierarchy (Bold Title vs Regular Desc)
		// which generic helpers might not handle as elegantly.

		ComPtr<IDWriteFactory> pDWrite = GetDWriteFactory();
		float currentY = top;

		// -- Title --
		std::string titleStr = GetStringProperty("title");
		if (!titleStr.empty()) {
			ComPtr<IDWriteTextFormat> pTitleFmt;
			pDWrite->CreateTextFormat(
				L"Segoe UI", NULL, // Or "Segoe UI Variable Display" if available in newer SDKs
				DWRITE_FONT_WEIGHT_BOLD,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				15.0f, L"en-us", &pTitleFmt
			);

			if (pTitleFmt) {
				// Ensure text clips if it hits the pill
				pTitleFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
				DWRITE_TRIMMING t{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
				pTitleFmt->SetTrimming(&t, NULL);

				ComPtr<ID2D1SolidColorBrush> pTitleBrush;
				pRT->CreateSolidColorBrush(titleColor, &pTitleBrush);

				std::wstring wTitle = NarrowToWide(titleStr);

				// Measure to advance Y
				ComPtr<IDWriteTextLayout> pLayout;
				pDWrite->CreateTextLayout(wTitle.c_str(), (UINT32)wTitle.size(), pTitleFmt.Get(),
					(std::max)(0.0f, contentRightEdge - left), // Std::max with parenthesis
					1000.0f, &pLayout);

				DWRITE_TEXT_METRICS metrics;
				pLayout->GetMetrics(&metrics);

				D2D1_POINT_2F origin = D2D1::Point2F(left, currentY);
				pRT->DrawTextLayout(origin, pLayout.Get(), pTitleBrush.Get());

				currentY += metrics.height + kTitleGap;
			}
		}

		// -- Description --
		std::string descStr = GetStringProperty("description");
		if (!descStr.empty()) {
			ComPtr<IDWriteTextFormat> pDescFmt;
			pDWrite->CreateTextFormat(
				L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				13.0f, L"en-us", &pDescFmt
			);

			if (pDescFmt) {
				pDescFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

				// Description can go under the pill if it wraps long enough, 
				// but for clean layout, let's keep it in the column.
				D2D1_RECT_F descRect = D2D1::RectF(left, currentY, contentRightEdge, bottom);

				// Use Helper if strictly required, but manual DWrite is better for multiline clipping
				// DrawTextStyled(pRT, descStr, descRect, false); <- This assumes single line usually.

				ComPtr<ID2D1SolidColorBrush> pDescBrush;
				pRT->CreateSolidColorBrush(descColor, &pDescBrush);
				std::wstring wDesc = NarrowToWide(descStr);

				pRT->DrawText(wDesc.c_str(), (UINT32)wDesc.size(), pDescFmt.Get(), descRect, pDescBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
			}
		}
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new TitleDescCard();
}