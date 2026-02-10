#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>

#include "WidgetImpl.hpp"
#include "ChronoStyles.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

using namespace ChronoUI;

enum class ButtonAlign {
	Center, Top, Bottom, Left, Right
};

class Button : public WidgetImpl {
	// Resources
	ID2D1Bitmap* m_pBitmap = nullptr;

	// Logic for lazy loading
	std::string m_pendingImagePath;
	std::string m_pendingImageBase64;
	bool m_imageDirty = false;

	// Tooltip State
	HWND m_hwndTT = nullptr;
	std::string m_ttTitle;
	std::string m_ttDesc;

	// Internal State
	bool m_trackingMouse = false;
	HCURSOR m_handCursor = nullptr;

	// Layout Constants
	static constexpr float kBaseSpacing = 6.0f;
	static constexpr float kOuterPadding = 8.0f; // Padding from border

public:
	Button() {
		m_hoverActive = true;
		m_handCursor = LoadCursor(NULL, IDC_HAND);

		SetBoolProperty("is_pill", false);
		SetBoolProperty("arrow", false);
		SetBoolProperty("checked", false);

		// Badge Defaults
		SetProperty("badge_text", "");
		SetProperty("badge_shape", "round");
		SetProperty("badge_align", "top-right");
		SetProperty("badge_bg_color", "#FF2D2D");
		SetProperty("badge_text_color", "#FFFFFF");

		SetProperty("image-align", "left"); // Changed default to left for better standard alignment
		SetProperty("text-align", "center");

		StyleManager::AddClass(this->GetContextNode(), "btn");
	}

	virtual ~Button() {
		SafeRelease(&m_pBitmap);
	}

	const char* __stdcall GetControlName() override { return "Button"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 4,
            "description": "A stable, layout-consistent button (Direct2D)",
            "properties": [
                { "name": "title", "type": "string", "description": "Button text" },
                { "name": "image-align", "type": "string", "enum": ["left", "right", "top", "bottom", "center"], "description": "Image alignment" },
                { "name": "image_path", "type": "string", "description": "File path to image" },
                { "name": "image_base64", "type": "string", "description": "Base64 encoded image" },
                { "name": "checked", "type": "boolean", "description": "Draw checked accent strip" },
                { "name": "arrow", "type": "boolean", "description": "Draw chevron arrow" },
                { "name": "is_pill", "type": "boolean", "description": "Enable fully rounded capsule shape" },
                { "name": "tt_title", "type": "string", "description": "Tooltip Title" },
                { "name": "tt_desc", "type": "string", "description": "Tooltip Description" },
                { "name": "badge_text", "type": "string", "description": "Badge content" }
            ],
            "events": [
                { "name": "onClick", "type": "action" },
                { "name": "onFocus", "type": "action" },
                { "name": "onBlur", "type": "action" }
            ]
        })json";
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);
	}

	// --- Helpers ---

	void ReloadImage(ID2D1RenderTarget* pRT) {
		SafeRelease(&m_pBitmap);
		if (!m_pendingImageBase64.empty()) {
			ImageFromBase64(pRT, m_pendingImageBase64.c_str(), &m_pBitmap);
		}
		// Add File load logic here if needed
		m_imageDirty = false;
	}

	// --- Rendering Core ---

	void DrawBackground(ID2D1RenderTarget* pRT, const D2D1_RECT_F& rect) {
		const char* ctrl = GetControlName();
		std::string sub = GetProperty("subclass");
		bool en = ::IsWindowEnabled(m_hwnd);
		bool hov = m_isHovered && m_hoverActive;

		// Fetch Styles
		std::string backCol = m_parent->GetProperty("background-color");
		std::string faceColStr = GetStyle("background-color", "", ctrl, sub.c_str(), m_focused, en, hov);
		std::string borderColStr = GetStyle("border-color", "", ctrl, sub.c_str(), m_focused, en, hov);

		float borderWidth = std::stof(GetStyle("border-width", "0", ctrl, sub.c_str(), m_focused, en, hov));
		float radius = std::stof(GetStyle("border-radius", "4", ctrl, sub.c_str(), m_focused, en, hov));
		if (GetBoolProperty("is_pill")) radius = (rect.bottom - rect.top) / 2.0f;

		// 1. Clear Parent Background
		pRT->Clear(CSSColorToD2D(backCol));

		D2D1_RECT_F drawRect = rect;
		// Apply slight inset for border so it doesn't clip
		float inset = borderWidth / 2.0f;
		D2D1_RECT_F borderRect = D2D1::RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset);

		// 2. Fill Face
		if (!faceColStr.empty()) {
			D2D1_COLOR_F col = CSSColorToD2D(faceColStr);
			if (col.a > 0) {
				ID2D1SolidColorBrush* pBrush = nullptr;
				pRT->CreateSolidColorBrush(col, &pBrush);
				if (radius > 0)
					pRT->FillRoundedRectangle(D2D1::RoundedRect(borderRect, radius, radius), pBrush);
				else
					pRT->FillRectangle(borderRect, pBrush);
				SafeRelease(&pBrush);
			}
		}

		// 3. Draw Border
		if (borderWidth > 0 && !borderColStr.empty()) {
			D2D1_COLOR_F col = CSSColorToD2D(borderColStr);
			ID2D1SolidColorBrush* pBrush = nullptr;
			pRT->CreateSolidColorBrush(col, &pBrush);
			if (radius > 0)
				pRT->DrawRoundedRectangle(D2D1::RoundedRect(borderRect, radius, radius), pBrush, borderWidth);
			else
				pRT->DrawRectangle(borderRect, pBrush, borderWidth);
			SafeRelease(&pBrush);
		}
	}

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		if (m_imageDirty) ReloadImage(pRT);

		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F clientRect = D2D1::RectF(0, 0, size.width, size.height);

		// 1. Draw Background
		DrawBackground(pRT, clientRect);

		// 2. Setup Resources & Colors
		bool isEnabled = ::IsWindowEnabled(m_hwnd);
		D2D1_COLOR_F textColor = CSSColorToD2D(GetProperty("color"));
		if (!isEnabled) textColor = D2D1::ColorF(0.6f, 0.6f, 0.6f);

		ID2D1SolidColorBrush* pTextBrush = nullptr;
		pRT->CreateSolidColorBrush(textColor, &pTextBrush);

		// 3. Prepare Content
		std::wstring wTitle = NarrowToWide(GetProperty("title"));
		float fontSize = ScaleF((float)GetCSSIntStyle("font-size", 10));

		// Prepare Text Format
		IDWriteTextFormat* pTextFormat = nullptr;
		GetDWriteFactory()->CreateTextFormat(
			L"Segoe UI", NULL,
			DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			fontSize, L"en-us", &pTextFormat
		);

		// Measure Text
		float maxTextW = size.width - (kOuterPadding * 2);
		float maxTextH = size.height;
		D2D1_SIZE_F textSize = { 0, 0 };

		if (!wTitle.empty()) {
			// Center alignment for measurement
			pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

			IDWriteTextLayout* pLayout = nullptr;
			GetDWriteFactory()->CreateTextLayout(wTitle.c_str(), (UINT32)wTitle.length(), pTextFormat, maxTextW, maxTextH, &pLayout);
			if (pLayout) {
				DWRITE_TEXT_METRICS metrics;
				pLayout->GetMetrics(&metrics);
				textSize.width = metrics.width;
				textSize.height = metrics.height;
				pLayout->Release();
			}
		}

		// 4. Layout Calculation
		bool drawImg = (m_pBitmap != nullptr);
		bool drawText = !wTitle.empty();

		D2D1_RECT_F imgRect = { 0 };
		D2D1_RECT_F textRect = { 0 };

		std::string alignProp = GetStringProperty("image-align");
		ButtonAlign align = ButtonAlign::Left; // Default to Left (Common UI pattern)
		if (alignProp == "right") align = ButtonAlign::Right;
		else if (alignProp == "center") align = ButtonAlign::Center;
		else if (alignProp == "top") align = ButtonAlign::Top;
		else if (alignProp == "bottom") align = ButtonAlign::Bottom;

		float pad = ScaleF(kOuterPadding);
		float spacing = ScaleF(kBaseSpacing);

		// --- IMAGE LOGIC ---
		if (drawImg) {
			D2D1_SIZE_F bmpSize = m_pBitmap->GetSize();
			float targetH = size.height * 0.6f; // Max 60% of button height
			if (targetH > ScaleF(24.0f)) targetH = ScaleF(24.0f); // Cap size

			float ratio = targetH / bmpSize.height;
			float targetW = bmpSize.width * ratio;

			float cX = size.width / 2.0f;
			float cY = size.height / 2.0f;

			// Alignment Logic
			switch (align) {
			case ButtonAlign::Left:
				// Image aligned to Left Border (plus padding)
				imgRect = D2D1::RectF(pad, cY - (targetH / 2), pad + targetW, cY + (targetH / 2));
				break;
			case ButtonAlign::Right:
				// Image aligned to Right Border
				imgRect = D2D1::RectF(size.width - pad - targetW, cY - (targetH / 2), size.width - pad, cY + (targetH / 2));
				break;
			case ButtonAlign::Center:
				// Part of the packed group logic below
				break;
			case ButtonAlign::Top:
				// Stacked logic
				break;
			case ButtonAlign::Bottom:
				// Stacked logic
				break;
			}

			// --- PACKED GROUP LOGIC (Center/Top/Bottom) ---
			if (align == ButtonAlign::Center) {
				float totalW = targetW + (drawText ? spacing + textSize.width : 0);
				float startX = (size.width - totalW) / 2.0f;
				imgRect = D2D1::RectF(startX, cY - (targetH / 2), startX + targetW, cY + (targetH / 2));
				// Set text rect relative to image
				textRect = D2D1::RectF(imgRect.right + spacing, 0, imgRect.right + spacing + textSize.width, size.height);
			}
			else if (align == ButtonAlign::Top) {
				float totalH = targetH + (drawText ? spacing + textSize.height : 0);
				float startY = (size.height - totalH) / 2.0f;
				imgRect = D2D1::RectF(cX - (targetW / 2), startY, cX + (targetW / 2), startY + targetH);
				textRect = D2D1::RectF(0, imgRect.bottom + spacing, size.width, imgRect.bottom + spacing + textSize.height);
			}
			else if (align == ButtonAlign::Bottom) {
				float totalH = targetH + (drawText ? spacing + textSize.height : 0);
				float startY = (size.height - totalH) / 2.0f;
				// Text first
				textRect = D2D1::RectF(0, startY, size.width, startY + textSize.height);
				imgRect = D2D1::RectF(cX - (targetW / 2), textRect.bottom + spacing, cX + (targetW / 2), textRect.bottom + spacing + targetH);
			}
		}

		// --- TEXT LOGIC (Stabilized) ---
		if (drawText) {
			// If TextRect wasn't set by Packing logic (Center/Top/Bottom), set it now.
			// For Left/Right Image alignment, Text stays CENTERED in the control to avoid jitter.
			if (align == ButtonAlign::Left || align == ButtonAlign::Right || !drawImg) {
				// Stabilize: Text uses full width (minus safety padding) but centers itself.
				// It does NOT shift based on image presence unless strictly necessary (overlap).

				// Check collision if image is massive? usually no need for buttons.
				textRect = D2D1::RectF(0, 0, size.width, size.height);
			}
		}

		// 5. Draw Operations
		if (drawImg) {
			pRT->DrawBitmap(m_pBitmap, imgRect, isEnabled ? 1.0f : 0.4f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		}

		if (drawText) {
			// We use the calculated textRect which is either the packed area or the full control area.
			// We rely on DWrite to center it within that rect.
			pRT->DrawText(wTitle.c_str(), (UINT32)wTitle.length(), pTextFormat, textRect, pTextBrush);
		}

		// Update Tooltip if title is truncated? (Simplified logic here)
		if (m_ttTitle.empty() && !wTitle.empty()) {
			m_ttTitle = WideToNarrow(wTitle);
			UpdateTooltip();
		}

		SafeRelease(&pTextFormat);
		SafeRelease(&pTextBrush);

		// 6. Draw Arrow (Chevron)
		if (GetBoolProperty("arrow")) {
			DrawChevron(pRT, clientRect, isEnabled);
		}

		// 7. Draw Checked Strip
		if (GetBoolProperty("checked")) {
			DrawCheckStrip(pRT, clientRect);
		}

		// 8. Draw Badge
		DrawBadge(pRT, clientRect);
	}

	// Extracted Geometry Drawing for cleanliness
	void DrawChevron(ID2D1RenderTarget* pRT, const D2D1_RECT_F& r, bool enabled) {
		float size = ScaleF(4.0f);
		float x = r.right - ScaleF(16.0f);
		float y = (r.bottom + r.top) / 2.0f;

		ID2D1SolidColorBrush* b = nullptr;
		pRT->CreateSolidColorBrush(enabled ? D2D1::ColorF(0.4f, 0.4f, 0.4f) : D2D1::ColorF(0.7f, 0.7f, 0.7f), &b);

		pRT->DrawLine(D2D1::Point2F(x - size, y - size), D2D1::Point2F(x, y), b, 1.5f);
		pRT->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x - size, y + size), b, 1.5f);

		SafeRelease(&b);
	}

	void DrawCheckStrip(ID2D1RenderTarget* pRT, const D2D1_RECT_F& r) {
		ID2D1SolidColorBrush* pBrush = nullptr;
		pRT->CreateSolidColorBrush(D2D1::ColorF(0, 0.47f, 0.84f), &pBrush);

		float h = ScaleF(3.0f);
		D2D1_RECT_F strip = D2D1::RectF(r.left + ScaleF(2), r.bottom - h, r.right - ScaleF(2), r.bottom);
		pRT->FillRectangle(strip, pBrush); // Simple strip, path geometry overhead removed for performance unless rounded needed

		SafeRelease(&pBrush);
	}

	void DrawBadge(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds) {
		std::string badgeText = GetStringProperty("badge_text");
		if (badgeText.empty()) return;

		D2D1_COLOR_F bgCol = CSSColorToD2D(GetStringProperty("badge_bg_color"));
		D2D1_COLOR_F txtCol = CSSColorToD2D(GetStringProperty("badge_text_color"));

		ID2D1SolidColorBrush* pBgBrush = nullptr;
		ID2D1SolidColorBrush* pTextBrush = nullptr;
		pRT->CreateSolidColorBrush(bgCol, &pBgBrush);
		pRT->CreateSolidColorBrush(txtCol, &pTextBrush);

		IDWriteTextFormat* pTF = nullptr;
		GetDWriteFactory()->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, ScaleF(9.0f), L"en-us", &pTF);

		pTF->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		pTF->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

		std::wstring wText = NarrowToWide(badgeText);

		// Quick measure
		IDWriteTextLayout* pTL = nullptr;
		GetDWriteFactory()->CreateTextLayout(wText.c_str(), (UINT32)wText.length(), pTF, 100.0f, 100.0f, &pTL);
		DWRITE_TEXT_METRICS tm;
		pTL->GetMetrics(&tm);

		float pad = ScaleF(4.0f);
		float dim = (std::max)(tm.width + pad, tm.height + ScaleF(2));

		float m = ScaleF(4.0f);
		D2D1_RECT_F badgeR = D2D1::RectF(bounds.right - dim - m, bounds.top + m, bounds.right - m, bounds.top + m + dim); // Top Right default

		pRT->FillRoundedRectangle(D2D1::RoundedRect(badgeR, dim / 2, dim / 2), pBgBrush);
		badgeR.top -= ScaleF(1.0f); // Visual centering tweak
		pRT->DrawText(wText.c_str(), (UINT32)wText.length(), pTF, badgeR, pTextBrush);

		SafeRelease(&pTL);
		SafeRelease(&pTF);
		SafeRelease(&pBgBrush);
		SafeRelease(&pTextBrush);
	}

	// --- Message Handling ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_ENABLE:
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		case WM_MOUSEMOVE:
			if (!m_trackingMouse) {
				TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
				TrackMouseEvent(&tme);
				m_trackingMouse = true;
				m_isHovered = true;
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return false;
		case WM_MOUSELEAVE:
			m_trackingMouse = false;
			m_isHovered = false;
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		case WM_LBUTTONDOWN:
			SetFocus(m_hwnd);
			if (IsWindowEnabled(m_hwnd)) {
				FireEvent("onClick", "{}");
			}
			return true;
		case WM_SETCURSOR:
			if (LOWORD(lp) == HTCLIENT) {
				SetCursor(m_handCursor);
				return true;
			}
			break;
		}
		return false;
	}

	// --- Property Changes ---
	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		if (t == "image_path") {
			m_pendingImagePath = val;
			m_imageDirty = true;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "image-base-64" || t == "image_base64") {
			m_pendingImageBase64 = val;
			m_imageDirty = true;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (t == "tt_title" || t == "tt_desc") {
			if (t == "tt_title") m_ttTitle = val;
			if (t == "tt_desc") m_ttDesc = val;
			UpdateTooltip();
		}

		WidgetImpl::OnPropertyChanged(key, value);
	}

	void UpdateTooltip() {
		if (!m_hwndTT) {
			m_hwndTT = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
				WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
				0, 0, 0, 0, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
		}

		TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hwnd, (UINT_PTR)m_hwnd };
		ti.lpszText = (LPSTR)(m_ttDesc.empty() ? m_ttTitle.c_str() : m_ttDesc.c_str());
		SendMessage(m_hwndTT, TTM_ADDTOOLA, 0, (LPARAM)&ti);
		SendMessage(m_hwndTT, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
		SendMessage(m_hwndTT, TTM_SETTITLEA, m_ttDesc.empty() ? 0 : 1, (LPARAM)(m_ttDesc.empty() ? NULL : m_ttTitle.c_str()));
	}

	void __stdcall OnFocus(bool f) override {
		FireEvent(f ? "onFocus" : "onBlur", "{}");
		InvalidateRect(m_hwnd, NULL, FALSE);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new Button();
}