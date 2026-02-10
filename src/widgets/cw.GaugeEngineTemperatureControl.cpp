#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h> // Required for loading images from paths
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>

#include "WidgetImpl.hpp"

// Link DWrite, D2D and WindowCodecs
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace ChronoUI;

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

#define NOMINMAX
#include <windows.h>
#include <commctrl.h> 

// Utility for PI
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --- Helper to convert Degrees to Radians ---
inline float DegToRad(float degrees) {
	return degrees * (M_PI / 180.0f);
}

// --- Helper to calculate point on circle for D2D ---
inline D2D1_POINT_2F GetCirclePoint(D2D1_POINT_2F center, float radius, float angleDegrees) {
	float rads = DegToRad(angleDegrees);
	return D2D1::Point2F(
		center.x + radius * cosf(rads),
		center.y + radius * sinf(rads)
	);
}

class GaugeEngineTemperatureControl : public WidgetImpl {
	// Data State
	float m_currentValue = 50.0f;
	float m_targetValue = 50.0f;
	float m_minValue = 10.0f;
	float m_maxValue = 130.0f;
	float m_warningValue = 110.0f;
	std::string m_label = "TEMP";
	std::string m_unit = "C";

	// Animation state
	UINT_PTR m_timerId = 0;

	// Geometry Constants
	const float START_ANGLE = 150.0f;
	const float SWEEP_ANGLE = 240.0f;

public:
	GaugeEngineTemperatureControl() {}

	virtual ~GaugeEngineTemperatureControl() {
		if (m_hwnd && m_timerId) {
			KillTimer(m_hwnd, m_timerId);
		}
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);
		m_timerId = SetTimer(m_hwnd, 1, 16, NULL);
	}

	const char* __stdcall GetControlName() override { return "GaugeEngineTemperatureControl"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "An analog engine temperature gauge with warning zones",
            "properties": [
                { "name": "value", "type": "float", "description": "Current temperature value" },
                { "name": "min", "type": "float", "description": "Minimum scale value" },
                { "name": "max", "type": "float", "description": "Maximum scale value" },
                { "name": "warning", "type": "float", "description": "Threshold for red warning zone" },
                { "name": "label", "type": "string", "description": "Gauge label text" },
                { "name": "unit", "type": "string", "description": "Unit of measurement (e.g., C or F)" }
            ],
            "events": [
                { "name": "onOverheat", "type": "action" }
            ]
        })json";
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		if (t == "value") {
			if (val == "") val = "0.0";
			m_targetValue = std::clamp(std::stof(val), m_minValue, m_maxValue);
		} else if (t == "min") {
			if (val == "") val = "0.0";
			m_minValue = std::stof(val);
		} else if (t == "max") {
			if (val == "") val = "0.0";
			m_maxValue = std::stof(val);
		} 
		else if (t == "warning")
			m_warningValue = std::stof(val);
		else if (t == "label")
			m_label = val;
		else if (t == "unit")
			m_unit = val;

		WidgetImpl::OnPropertyChanged(key, value);
		InvalidateRect(m_hwnd, NULL, FALSE);
	}

private:
	D2D1_COLOR_F GetTemperatureColor(float value) {
		if (value >= m_warningValue) return D2D1::ColorF(1.0f, 0.15f, 0.15f); // Danger Red
		if (value < (m_minValue + (m_maxValue - m_minValue) * 0.25f)) return D2D1::ColorF(0.0f, 0.7f, 1.0f); // Cold Blue
		return D2D1::ColorF(0.4f, 1.0f, 0.4f); // Normal Green
	}

	// Helper to draw an arc path
	void DrawArc(ID2D1RenderTarget* pRT, D2D1_POINT_2F center, float radius, float startAngle, float sweepAngle, ID2D1Brush* pBrush, float strokeWidth) {
		ComPtr<ID2D1Factory> pFactory;
		pRT->GetFactory(&pFactory);

		ComPtr<ID2D1PathGeometry> pGeo;
		pFactory->CreatePathGeometry(&pGeo);

		ComPtr<ID2D1GeometrySink> pSink;
		pGeo->Open(&pSink);

		D2D1_POINT_2F startPoint = GetCirclePoint(center, radius, startAngle);
		D2D1_POINT_2F endPoint = GetCirclePoint(center, radius, startAngle + sweepAngle);

		pSink->BeginFigure(startPoint, D2D1_FIGURE_BEGIN_HOLLOW);
		pSink->AddArc(D2D1::ArcSegment(
			endPoint,
			D2D1::SizeF(radius, radius),
			0.0f,
			D2D1_SWEEP_DIRECTION_CLOCKWISE,
			sweepAngle > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
		));
		pSink->EndFigure(D2D1_FIGURE_END_OPEN);
		pSink->Close();

		pRT->DrawGeometry(pGeo.Get(), pBrush, strokeWidth);
	}

public:
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		float width = size.width;
		float height = size.height;

		// Theme Logic
		std::string bgStr = GetStyle("background-color", "#F0F0F5", GetControlName(), "", m_focused, IsWindowEnabled(m_hwnd), m_isHovered);
		D2D1_COLOR_F styleBgColor = CSSColorToD2D(bgStr);

		// Simple luminance check
		float lum = 0.299f * styleBgColor.r + 0.587f * styleBgColor.g + 0.114f * styleBgColor.b;
		bool isDarkMode = (lum < 0.5f);

		// Define Colors
		D2D1_COLOR_F trackColor = isDarkMode ? D2D1::ColorF(0.18f, 0.18f, 0.18f) : D2D1::ColorF(0.82f, 0.82f, 0.82f);
		D2D1_COLOR_F textColor = isDarkMode ? D2D1::ColorF(0.9f, 0.9f, 0.9f) : D2D1::ColorF(0.12f, 0.12f, 0.12f);
		D2D1_COLOR_F tickColor = isDarkMode ? D2D1::ColorF(0.31f, 0.31f, 0.31f) : D2D1::ColorF(0.7f, 0.7f, 0.7f);

		pRT->Clear(styleBgColor);

		float centerX = width / 2.0f;
		float centerY = height / 2.0f + (height * 0.15f);
		float radius = (std::min)(width, height) * 0.45f;
		D2D1_POINT_2F center = D2D1::Point2F(centerX, centerY);

		ComPtr<ID2D1SolidColorBrush> pBrush;
		pRT->CreateSolidColorBrush(trackColor, &pBrush);

		// 1. Draw Background Track
		DrawArc(pRT, center, radius, START_ANGLE, SWEEP_ANGLE, pBrush.Get(), ScaleF(6.0f));

		// 2. Draw Warning Zone
		float warningStartPercent = (m_warningValue - m_minValue) / (m_maxValue - m_minValue);
		pBrush->SetColor(D2D1::ColorF(0.7f, 0.2f, 0.2f));
		DrawArc(pRT, center, radius,
			START_ANGLE + (SWEEP_ANGLE * warningStartPercent),
			SWEEP_ANGLE * (1.0f - warningStartPercent),
			pBrush.Get(), ScaleF(6.0f));

		// 3. Draw Ticks
		pBrush->SetColor(tickColor);
		for (int i = 0; i <= 8; ++i) {
			float angle = START_ANGLE + (SWEEP_ANGLE * (i / 8.0f));
			float tLen = (i % 4 == 0) ? ScaleF(12.0f) : ScaleF(6.0f);
			D2D1_POINT_2F p1 = GetCirclePoint(center, radius - tLen, angle);
			D2D1_POINT_2F p2 = GetCirclePoint(center, radius, angle);
			pRT->DrawLine(p1, p2, pBrush.Get(), ScaleF(1.5f));
		}

		// 4. Calculate Needle
		float progress = (m_currentValue - m_minValue) / (m_maxValue - m_minValue);
		float needleAngle = START_ANGLE + (SWEEP_ANGLE * progress);
		D2D1_COLOR_F statusColor = GetTemperatureColor(m_currentValue);

		// 5. Draw Needle
		pBrush->SetColor(statusColor);

		// Create a stroke style for the triangle cap if desired, or build a geometry
		// For D2D, drawing a geometry is cleaner for a needle
		D2D1_POINT_2F needleTip = GetCirclePoint(center, radius - ScaleF(8.0f), needleAngle);
		D2D1_POINT_2F needleBase = GetCirclePoint(center, -ScaleF(12.0f), needleAngle);

		// Simple line for porting speed, adding a small circle at tip could emulate cap, 
		// but D2D1_CAP_STYLE_TRIANGLE requires ID2D1StrokeStyle.
		ComPtr<ID2D1Factory> pFactory;
		pRT->GetFactory(&pFactory);
		ComPtr<ID2D1StrokeStyle> pStrokeStyle;
		D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties();
		props.endCap = D2D1_CAP_STYLE_TRIANGLE;
		pFactory->CreateStrokeStyle(&props, nullptr, 0, &pStrokeStyle);

		pRT->DrawLine(needleBase, needleTip, pBrush.Get(), ScaleF(3.5f), pStrokeStyle.Get());

		// 6. Hub
		D2D1_COLOR_F hubColor = isDarkMode ? D2D1::ColorF(0.86f, 0.86f, 0.86f) : D2D1::ColorF(0.16f, 0.16f, 0.16f);
		pBrush->SetColor(hubColor);
		float hubSize = ScaleF(10.0f);
		D2D1_ELLIPSE hub = D2D1::Ellipse(center, hubSize / 2.0f, hubSize / 2.0f);
		pRT->FillEllipse(hub, pBrush.Get());

		// 7. Value & Labels
		// Re-use styled text helper, but we need rects.
		// Construct rects based on original logic offsets.

		// Value
		std::string valStr = std::to_string((int)m_currentValue) + " " + m_unit;
		D2D1_RECT_F valRect = D2D1::RectF(0, centerY - (radius * 0.6f), width, centerY);
		// Note: DrawTextStyled usually centers text.
		// To strictly match font sizes, we might need a custom text format, 
		// but DrawTextStyled is the requested API. We assume it handles "basic" styling.
		// For strict port, we construct specific layouts below:

		// We will manually use DWrite to match the specific font sizes from the GDI+ code
		ComPtr<IDWriteFactory> pDWrite = GetDWriteFactory();
		ComPtr<IDWriteTextFormat> pValFmt;
		pDWrite->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, radius * 0.15f, L"en-us", &pValFmt);
		pValFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

		std::wstring wVal = NarrowToWide(valStr);
		pBrush->SetColor(textColor);
		pRT->DrawText(wVal.c_str(), (UINT32)wVal.length(), pValFmt.Get(),
			D2D1::RectF(0, centerY - (radius * 0.6f), width, centerY), pBrush.Get());

		// Label
		ComPtr<IDWriteTextFormat> pLblFmt;
		pDWrite->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, radius * 0.09f, L"en-us", &pLblFmt);
		pLblFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

		std::wstring wLbl = NarrowToWide(m_label);
		pRT->DrawText(wLbl.c_str(), (UINT32)wLbl.length(), pLblFmt.Get(),
			D2D1::RectF(0, centerY + (radius * 0.2f), width, height), pBrush.Get());
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		if (msg == WM_TIMER) {
			if (wp == m_timerId) {
				float diff = m_targetValue - m_currentValue;
				if (std::abs(diff) > 0.05f) {
					float oldVal = m_currentValue;
					m_currentValue += diff * 0.08f;

					if (oldVal < m_warningValue && m_currentValue >= m_warningValue) {
						FireEvent("onOverheat", "{}");
					}
					InvalidateRect(m_hwnd, NULL, FALSE);
				}
				return true;
			}
		}
		return false;
	}
};

enum class ButtonAlign {
	Center, Top, Bottom, Left, Right
};

class Button : public WidgetImpl {
	std::string m_title = "";
	float m_fontSize = 10.0f;

	// Image State
	ID2D1Bitmap* m_pBitmap = nullptr;
	std::string m_imagePath;
	std::string m_imageBase64;
	bool m_imageDirty = false;

	ButtonAlign m_imageAlign = ButtonAlign::Left;

	float m_cornerRadius = 6.0f;
	bool m_isPill = false;
	bool m_activeButton = false;
	bool m_arrow = false;

	HWND m_hwndTT = nullptr;
	std::string m_ttTitle;
	std::string m_ttDesc;

	HCURSOR m_handCursor = nullptr;
	static constexpr float kBaseSpacing = 6.0f;

public:
	Button() {
		m_handCursor = LoadCursor(NULL, IDC_HAND);
	}

	virtual ~Button() {
		SafeRelease(&m_pBitmap);
	}

	const char* __stdcall GetControlName() override { return "Button"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A clickable button with reactive layout and rounded styles",
            "properties": [
                { "name": "title", "type": "string", "description": "Button text" },
                { "name": "font_size", "type": "float", "description": "Font size in pt" },
                { "name": "align", "type": "string", "description": "Image alignment (left, right, top, bottom, center)" },
                { "name": "image_path", "type": "string", "description": "File path to image" },
                { "name": "image_base64", "type": "string", "description": "Base64 encoded image" },
                { "name": "active_button", "type": "boolean", "description": "Draw active accent strip" },
                { "name": "arrow", "type": "boolean", "description": "Draw chevron arrow" },
                { "name": "corner_radius", "type": "float", "description": "Border radius in points" },
                { "name": "is_pill", "type": "boolean", "description": "Enable fully rounded capsule shape" },
                { "name": "tt_title", "type": "string", "description": "Tooltip Title" },
                { "name": "tt_desc", "type": "string", "description": "Tooltip Description" }
            ],
            "events": [
                { "name": "onClick", "type": "action" }
            ]
        })json";
	}

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);
		if (m_hwnd) {
			m_hwndTT = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
				WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
				0, 0, 0, 0, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

			TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hwnd, (UINT_PTR)m_hwnd };
			ti.lpszText = (LPSTR)"";
			SendMessage(m_hwndTT, TTM_ADDTOOLA, 0, (LPARAM)&ti);
		}
	}

	void UpdateTooltip() {
		if (!m_hwndTT) return;
		TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hwnd, (UINT_PTR)m_hwnd };
		if (m_ttDesc.empty()) {
			ti.lpszText = (LPSTR)m_ttTitle.c_str();
			SendMessage(m_hwndTT, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
			SendMessage(m_hwndTT, TTM_SETTITLEA, 0, (LPARAM)NULL);
		}
		else {
			ti.lpszText = (LPSTR)m_ttDesc.c_str();
			SendMessage(m_hwndTT, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
			SendMessage(m_hwndTT, TTM_SETTITLEA, 1, (LPARAM)m_ttTitle.c_str());
		}
	}

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_ENABLE:
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		case WM_LBUTTONDOWN:
			SetFocus(m_hwnd);
			if (IsWindowEnabled(m_hwnd)) FireEvent("onClick", "{}");
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

	// Load WIC bitmap from file
	void LoadImageFromFile(ID2D1RenderTarget* pRT, const std::wstring& path) {
		SafeRelease(&m_pBitmap);
		if (path.empty()) return;

		ComPtr<IWICImagingFactory> pWIC;
		CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWIC));
		if (!pWIC) return;

		ComPtr<IWICBitmapDecoder> pDecoder;
		pWIC->CreateDecoderFromFilename(path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
		if (!pDecoder) return;

		ComPtr<IWICBitmapFrameDecode> pSource;
		pDecoder->GetFrame(0, &pSource);
		if (!pSource) return;

		ComPtr<IWICFormatConverter> pConverter;
		pWIC->CreateFormatConverter(&pConverter);
		pConverter->Initialize(pSource.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);

		pRT->CreateBitmapFromWicBitmap(pConverter.Get(), NULL, &m_pBitmap);
	}

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		float fW = size.width;
		float fH = size.height;
		D2D1_RECT_F clientRect = D2D1::RectF(0, 0, fW, fH);

		// Manage Bitmap Resource (Device Dependent)
		if (m_imageDirty) {
			SafeRelease(&m_pBitmap);
			if (!m_imageBase64.empty()) {
				ImageFromBase64(pRT, m_imageBase64.c_str(), &m_pBitmap);
			}
			else if (!m_imagePath.empty()) {
				LoadImageFromFile(pRT, NarrowToWide(m_imagePath));
			}
			m_imageDirty = false;
		}

		// 1. Draw Background
		// Use the corner radius properties
		float radius = m_isPill ? (std::min)(fW, fH) / 2.0f : ScaleF(m_cornerRadius);
		// We temporarily override the widget rect for the helper if needed, but the helper draws logic for us.
		// We'll reimplement specific DrawWidgetBackground logic here to support dynamic radius from property
		{
			std::string subclass = GetProperty("subclass");
			bool isEnabled = IsWindowEnabled(m_hwnd);
			bool hovered = m_isHovered && isEnabled;

			std::string bgKey = hovered ? "background-color-hover" : "background-color";
			std::string bgStr = GetStyle(bgKey.c_str(), hovered ? "#E0E0E0" : "#F0F0F0", GetControlName(), subclass.c_str(), m_focused, isEnabled, hovered);
			D2D1_COLOR_F bgCol = CSSColorToD2D(bgStr);

			std::string borderStr = GetStyle("border-color", "#CCCCCC", GetControlName(), subclass.c_str(), m_focused, isEnabled, hovered);
			D2D1_COLOR_F borderCol = CSSColorToD2D(borderStr);
			float borderWidth = ScaleF(1.0f);

			ComPtr<ID2D1SolidColorBrush> pBrush;
			pRT->CreateSolidColorBrush(bgCol, &pBrush);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(clientRect, radius, radius);
			pRT->FillRoundedRectangle(rr, pBrush.Get());

			pBrush->SetColor(borderCol);
			pRT->DrawRoundedRectangle(rr, pBrush.Get(), borderWidth);
		}

		// 2. Resolve Colors
		std::string subclass = GetProperty("subclass");
		bool isEnabled = IsWindowEnabled(m_hwnd);
		std::string bgFaceS = GetStyle("face-color", "", GetControlName(), subclass.c_str(), false, isEnabled, false);
		D2D1_COLOR_F bgColor = CSSColorToD2D(bgFaceS);
		float luminance = 0.299f * bgColor.r + 0.587f * bgColor.g + 0.114f * bgColor.b;
		bool isDarkBg = luminance < 0.5f;

		std::string textColorHex = GetStyle("foreground-color", "", GetControlName(), subclass.c_str(), false, isEnabled, false);
		D2D1_COLOR_F textColor = CSSColorToD2D(textColorHex);
		if (!isEnabled) {
			textColor = isDarkBg ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f) : D2D1::ColorF(0.6f, 0.6f, 0.6f);
		}

		// 3. Prepare Text Layout
		std::wstring wTitle = NarrowToWide(m_title);
		ComPtr<IDWriteFactory> pDWrite = GetDWriteFactory();
		ComPtr<IDWriteTextFormat> pTextFormat;
		float scaledFontSize = ScaleF(m_fontSize > 0 ? m_fontSize : 10.0f);

		pDWrite->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, scaledFontSize, L"en-us", &pTextFormat);

		float arrowSpace = m_arrow ? ScaleF(20.0f) : 0.0f;
		float availW = (std::max)(1.0f, fW - arrowSpace);
		float availH = (std::max)(1.0f, fH);

		ComPtr<IDWriteTextLayout> pTextLayout;
		DWRITE_TEXT_METRICS textMetrics = { 0 };
		if (!wTitle.empty()) {
			pDWrite->CreateTextLayout(wTitle.c_str(), (UINT32)wTitle.length(), pTextFormat.Get(), availW, availH, &pTextLayout);
			pTextLayout->GetMetrics(&textMetrics);
		}

		// 4. Layout Logic
		float spacing = ScaleF(kBaseSpacing);
		bool drawText = !wTitle.empty();
		bool drawImg = (m_pBitmap != nullptr);

		float rawW = 0, rawH = 0;
		if (drawImg) {
			D2D1_SIZE_F sz = m_pBitmap->GetSize();
			rawW = sz.width;
			rawH = sz.height;
		}

		if (drawText && drawImg) {
			float minImgSize = ScaleF(16.0f);
			bool fits = true;
			if (m_imageAlign == ButtonAlign::Top || m_imageAlign == ButtonAlign::Bottom) {
				if ((minImgSize + spacing + textMetrics.height) > availH) fits = false;
			}
			else {
				if ((minImgSize + spacing + textMetrics.width) > availW) fits = false;
			}
			if (!fits) drawText = false;
		}

		float imgW = 0, imgH = 0;
		if (drawImg) {
			float maxImgW = availW;
			float maxImgH = availH;

			if (drawText) {
				switch (m_imageAlign) {
				case ButtonAlign::Left:
				case ButtonAlign::Right:
				case ButtonAlign::Center:
					maxImgW = availW - textMetrics.width - spacing;
					maxImgH = (std::min)(maxImgH, fH * 0.65f);
					break;
				case ButtonAlign::Top:
				case ButtonAlign::Bottom:
					maxImgH = availH - textMetrics.height - spacing;
					maxImgH = (std::min)(maxImgH, fH * 0.50f);
					break;
				}
			}
			else {
				maxImgW = availW * 0.85f;
				maxImgH = availH * 0.85f;
			}

			if (rawW > 0 && rawH > 0 && maxImgW > 0 && maxImgH > 0) {
				float ratio = (std::min)(maxImgW / rawW, maxImgH / rawH);
				imgW = rawW * ratio * 0.6f;
				imgH = rawH * ratio * 0.6f;
			}
		}

		D2D1_RECT_F imgRectFinal = { 0 };
		D2D1_RECT_F textRectFinal = { 0 };

		if (drawImg && !drawText) {
			imgRectFinal.left = (availW - imgW) / 2.0f;
			imgRectFinal.top = (availH - imgH) / 2.0f;
			imgRectFinal.right = imgRectFinal.left + imgW;
			imgRectFinal.bottom = imgRectFinal.top + imgH;
		}
		else if (!drawImg && drawText) {
			textRectFinal.left = (availW - textMetrics.width) / 2.0f;
			textRectFinal.top = (availH - textMetrics.height) / 2.0f;
			textRectFinal.right = textRectFinal.left + textMetrics.width;
			textRectFinal.bottom = textRectFinal.top + textMetrics.height;
		}
		else if (drawImg && drawText) {
			float contentW, contentH, startX, startY;
			switch (m_imageAlign) {
			case ButtonAlign::Left:
			case ButtonAlign::Center:
				contentW = imgW + spacing + textMetrics.width;
				startX = (availW - contentW) / 2.0f;
				imgRectFinal = D2D1::RectF(startX, (availH - imgH) / 2.0f, startX + imgW, (availH - imgH) / 2.0f + imgH);
				textRectFinal = D2D1::RectF(imgRectFinal.right + spacing, (availH - textMetrics.height) / 2.0f, imgRectFinal.right + spacing + textMetrics.width, (availH - textMetrics.height) / 2.0f + textMetrics.height);
				break;
			case ButtonAlign::Right:
				contentW = imgW + spacing + textMetrics.width;
				startX = (availW - contentW) / 2.0f;
				textRectFinal = D2D1::RectF(startX, (availH - textMetrics.height) / 2.0f, startX + textMetrics.width, (availH - textMetrics.height) / 2.0f + textMetrics.height);
				imgRectFinal = D2D1::RectF(textRectFinal.right + spacing, (availH - imgH) / 2.0f, textRectFinal.right + spacing + imgW, (availH - imgH) / 2.0f + imgH);
				break;
			case ButtonAlign::Top:
				contentH = imgH + spacing + textMetrics.height;
				startY = (availH - contentH) / 2.0f;
				imgRectFinal = D2D1::RectF((availW - imgW) / 2.0f, startY, (availW - imgW) / 2.0f + imgW, startY + imgH);
				textRectFinal = D2D1::RectF((availW - textMetrics.width) / 2.0f, imgRectFinal.bottom + spacing, (availW - textMetrics.width) / 2.0f + textMetrics.width, imgRectFinal.bottom + spacing + textMetrics.height);
				break;
			case ButtonAlign::Bottom:
				contentH = imgH + spacing + textMetrics.height;
				startY = (availH - contentH) / 2.0f;
				textRectFinal = D2D1::RectF((availW - textMetrics.width) / 2.0f, startY, (availW - textMetrics.width) / 2.0f + textMetrics.width, startY + textMetrics.height);
				imgRectFinal = D2D1::RectF((availW - imgW) / 2.0f, textRectFinal.bottom + spacing, (availW - imgW) / 2.0f + imgW, textRectFinal.bottom + spacing + imgH);
				break;
			}
		}

		// 5. Draw Content
		if (drawImg && m_pBitmap) {
			float opacity = isEnabled ? 1.0f : 0.4f; // Simple disabled simulation
			pRT->DrawBitmap(m_pBitmap, imgRectFinal, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		}

		if (drawText) {
			ComPtr<ID2D1SolidColorBrush> pTextBrush;
			pRT->CreateSolidColorBrush(textColor, &pTextBrush);
			// DrawLayout renders at the origin, so we translate the point
			D2D1_POINT_2F pt = { textRectFinal.left, textRectFinal.top };
			pRT->DrawTextLayout(pt, pTextLayout.Get(), pTextBrush.Get());
		}
		else if (m_ttTitle.empty() && !wTitle.empty()) {
			m_ttTitle = WideToNarrow(wTitle);
			UpdateTooltip();
		}

		// 6. Draw Arrow
		if (m_arrow) {
			D2D1_COLOR_F arrowColor = isEnabled ? (isDarkBg ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.3f, 0.3f, 0.3f))
				: (isDarkBg ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f) : D2D1::ColorF(0.6f, 0.6f, 0.6f));

			ComPtr<ID2D1SolidColorBrush> pArrowBrush;
			pRT->CreateSolidColorBrush(arrowColor, &pArrowBrush);

			float ax = availW + (arrowSpace / 2) - ScaleF(5.0f);
			float ay = fH / 2.0f;
			float as = ScaleF(3.0f);

			pRT->DrawLine(D2D1::Point2F(ax - as, ay - as), D2D1::Point2F(ax, ay), pArrowBrush.Get(), ScaleF(1.5f));
			pRT->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(ax - as, ay + as), pArrowBrush.Get(), ScaleF(1.5f));
		}

		// 7. Active Strip
		if (m_activeButton) {
			ComPtr<ID2D1SolidColorBrush> pActiveBrush;
			pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.84f), &pActiveBrush); // VS Blue
			float stripHeight = ScaleF(3.0f);

			// For simple rounded strip effect, we can intersect the rounded rect or just draw bottom area
			// Constructing a path like the GDI+ version:
			ComPtr<ID2D1Factory> pFact;
			pRT->GetFactory(&pFact);
			ComPtr<ID2D1PathGeometry> pPath;
			pFact->CreatePathGeometry(&pPath);
			ComPtr<ID2D1GeometrySink> pSink;
			pPath->Open(&pSink);

			float d = radius;
			// Simplified "strip" path logic compatible with rounded corners
			// Start left-above-bottom-curve
			pSink->BeginFigure(D2D1::Point2F(0, fH - d), D2D1_FIGURE_BEGIN_FILLED);
			// Arc down-right
			pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(d, fH), D2D1::SizeF(d, d), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			// Line across
			pSink->AddLine(D2D1::Point2F(fW - d, fH));
			// Arc up-right
			pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(fW, fH - d), D2D1::SizeF(d, d), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			// Line up to strip height top
			pSink->AddLine(D2D1::Point2F(fW, fH - stripHeight));
			// Line back left
			pSink->AddLine(D2D1::Point2F(0, fH - stripHeight));
			pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
			pSink->Close();

			pRT->FillGeometry(pPath.Get(), pActiveBrush.Get());
		}
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string t = key;
		std::string val = value ? value : "";

		if (t == "title" || t == "text") {
			m_title = val;
		}
		else if (t == "image_path") {
			m_imagePath = val;
			m_imageDirty = true;
		}
		else if (t == "image-base-64" || t == "image_base64") {
			m_imageBase64 = val;
			m_imageDirty = true;
		}
		else if (t == "align") {
			if (val == "right") m_imageAlign = ButtonAlign::Right;
			else if (val == "top") m_imageAlign = ButtonAlign::Top;
			else if (val == "bottom") m_imageAlign = ButtonAlign::Bottom;
			else if (val == "left") m_imageAlign = ButtonAlign::Left;
			else m_imageAlign = ButtonAlign::Center;
		}
		else if (t == "corner_radius" || t == "border_radius") {
			m_cornerRadius = val.empty() ? 6.0f : std::stof(val);
		}
		else if (t == "is_pill") {
			m_isPill = (val == "true");
		}
		else if (t == "active_button") {
			m_activeButton = (val == "true");
		}
		else if (t == "arrow") {
			m_arrow = (val == "true");
		}
		else if (t == "tt_title") {
			m_ttTitle = val;
			UpdateTooltip();
		}
		else if (t == "tt_desc") {
			m_ttDesc = val;
			UpdateTooltip();
		}
		else if (t == "font_size") {
			if (!val.empty()) m_fontSize = std::stof(val);
		}

		WidgetImpl::OnPropertyChanged(key, value);
	}

	void __stdcall OnFocus(bool f) override {
		FireEvent(f ? "onFocus" : "onBlur", "{}");
		InvalidateRect(m_hwnd, NULL, FALSE);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new GaugeEngineTemperatureControl();
}