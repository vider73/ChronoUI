#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

using namespace ChronoUI;

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

// Helper to load file content to string (for converting path to base64/data internally)
static std::string ReadFileToString(const std::wstring& path) {
	std::ifstream f(path, std::ios::in | std::ios::binary);
	if (!f) return "";
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// Simple Base64 encoder helper (needed if we read from file path to use the ImageFromBase64 helper)
static std::string BytesToBase64(const std::string& data) {
	static const char* s_b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	int val = 0, valb = -6;
	for (unsigned char c : data) {
		val = (val << 8) + c;
		valb += 8;
		while (valb >= 0) {
			out.push_back(s_b64[(val >> valb) & 0x3F]);
			valb -= 6;
		}
	}
	if (valb > -6) out.push_back(s_b64[((val << 8) >> (valb + 8)) & 0x3F]);
	while (out.size() % 4) out.push_back('=');
	return out;
}

class TextSlider : public WidgetImpl {
private:
	// Properties
	std::wstring m_text = L"Smooth Scrolling Status Message";
	float m_speed = 2.0f;
	float m_scrollPos = 0.0f;

	// Image Data (Source)
	std::string m_imgData; // Stores Base64 string to recreate bitmap if device is lost
	bool m_hasImage = false;

	// Direct2D / DirectWrite Resources
	ID2D1Bitmap* m_pBitmap = nullptr;
	IDWriteTextLayout* m_pTextLayout = nullptr;
	IDWriteTextFormat* m_pTextFormat = nullptr;

	// Layout metrics
	float m_contentWidth = 0.0f;
	float m_iconPadding = 8.0f;
	bool m_recalcNeeded = true;

	// Animation
	UINT_PTR m_timerId = 0;
	static const UINT_PTR TIMER_ID = 9001;

public:
	TextSlider() {
		SetProperty("text", "Smooth Scrolling Status Message");
	}

	~TextSlider() {
		if (m_hwnd && m_timerId) {
			KillTimer(m_hwnd, m_timerId);
		}
		ReleaseResources();
	}

	// -------------------------------------------------------------------------
	// Resource Management
	// -------------------------------------------------------------------------

	void ReleaseResources() {
		SafeRelease(&m_pBitmap);
		SafeRelease(&m_pTextLayout);
		SafeRelease(&m_pTextFormat);
	}

	void CreateTextResources() {
		SafeRelease(&m_pTextLayout);
		SafeRelease(&m_pTextFormat);

		// Define Font (Segoe UI, 14px equivalent)
		// In a real app, you might parse "font-family" from CSS styles here
		HRESULT hr = GetDWriteFactory()->CreateTextFormat(
			L"Segoe UI",
			NULL,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			14.0f,
			L"en-us",
			&m_pTextFormat
		);

		if (SUCCEEDED(hr)) {
			// Center vertically
			m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			m_pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

			// Create Layout
			hr = GetDWriteFactory()->CreateTextLayout(
				m_text.c_str(),
				(UINT32)m_text.length(),
				m_pTextFormat,
				5000.0f, // Max width (large enough to fit text)
				50.0f,   // Arbitrary height, will be constrained by draw rect
				&m_pTextLayout
			);
		}

		if (SUCCEEDED(hr) && m_pTextLayout) {
			DWRITE_TEXT_METRICS metrics;
			m_pTextLayout->GetMetrics(&metrics);
			m_contentWidth = metrics.width;
		}
		else {
			m_contentWidth = 0;
		}
	}

	// -------------------------------------------------------------------------
	// Metadata
	// -------------------------------------------------------------------------

	const char* __stdcall GetControlName() override {
		return "TextSlider";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A hardware-accelerated smooth scrolling text ticker.",
            "properties": [
                { "name": "text", "type": "string", "description": "The message to scroll" },
                { "name": "speed", "type": "number", "description": "Scroll speed (px/frame)" },
                { "name": "image_path", "type": "string", "description": "Path to icon image" },
                { "name": "image_base64", "type": "string", "description": "Base64 encoded image data" }
            ],
            "events": [
                { "name": "onCycleComplete", "type": "notify" }
            ]
        })json";
	}

	// -------------------------------------------------------------------------
	// Property Handling
	// -------------------------------------------------------------------------

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string k = key;

		if (k == "text") {
			m_text = NarrowToWide(value);
			m_recalcNeeded = true;
		}
		else if (k == "speed") {
			try {
				float v = std::stof(value);
				m_speed = (std::max)(0.1f, (std::min)(v, 50.0f));
			}
			catch (...) { m_speed = 2.0f; }
		}
		else if (k == "image_base64") {
			m_imgData = value;
			m_hasImage = !m_imgData.empty();
			SafeRelease(&m_pBitmap); // Invalidate D2D bitmap so it rebuilds next frame
			m_recalcNeeded = true;
		}
		else if (k == "image_path") {
			std::wstring wPath = NarrowToWide(value);
			std::string rawBytes = ReadFileToString(wPath);
			if (!rawBytes.empty()) {
				// Convert to B64 to use the common ImageFromBase64 helper
				m_imgData = BytesToBase64(rawBytes);
				m_hasImage = true;
			}
			else {
				m_hasImage = false;
			}
			SafeRelease(&m_pBitmap);
			m_recalcNeeded = true;
		}

		WidgetImpl::OnPropertyChanged(key, value);
	}

	// -------------------------------------------------------------------------
	// Direct2D Drawing
	// -------------------------------------------------------------------------

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		if (!pRT) return;

		// 1. Get Geometry
		RECT rc;
		GetClientRect(m_hwnd, &rc);
		D2D1_RECT_F rect = D2D1::RectF((float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom);
		float width = rect.right - rect.left;
		float height = rect.bottom - rect.top;

		// 2. Resolve Colors
		std::string subclass = GetProperty("subclass");
		bool isEnabled = IsWindowEnabled(m_hwnd);

		D2D1_COLOR_F bgColor = CSSColorToD2D(GetStyle("background-color", "rgb(20,20,20)", GetControlName(), subclass.c_str(), m_focused, isEnabled, m_isHovered));
		D2D1_COLOR_F fgColor = CSSColorToD2D(GetStyle("foreground-color", "rgb(220,220,220)", GetControlName(), subclass.c_str(), m_focused, isEnabled, m_isHovered));

		// 3. Draw Background
		DrawWidgetBackground(pRT, rect, m_isHovered);

		// 4. Resource Initialization (Lazy Loading)
		if (m_recalcNeeded || !m_pTextLayout) {
			CreateTextResources();
			// Calculate total content width (Icon + Text + Padding)
			float iconWidth = m_hasImage ? (height * 0.6f + m_iconPadding) : 0.0f;
			// Add 50% screen width gap for loop
			float loopGap = width * 0.5f;

			DWRITE_TEXT_METRICS textMetrics;
			if (m_pTextLayout) {
				m_pTextLayout->GetMetrics(&textMetrics);
			}
			m_contentWidth = iconWidth + (textMetrics.width) + loopGap;
			m_recalcNeeded = false;
		}

		// Initialize Bitmap if needed
		if (m_hasImage && !m_pBitmap && !m_imgData.empty()) {
			ImageFromBase64(pRT, m_imgData.c_str(), &m_pBitmap);
		}

		// 5. Setup Brushes
		ID2D1SolidColorBrush* pTextBrush = nullptr;
		pRT->CreateSolidColorBrush(fgColor, &pTextBrush);

		// 6. Draw Content Loop
		if (pTextBrush) {
			// Clip to widget bounds so scrolling text disappears cleanly
			pRT->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

			auto DrawSingleInstance = [&](float offsetX) {
				float currentX = offsetX;

				// Draw Icon
				if (m_pBitmap) {
					float iconSize = height * 0.6f;
					float iconY = rect.top + (height - iconSize) / 2.0f;
					D2D1_RECT_F iconDest = D2D1::RectF(currentX, iconY, currentX + iconSize, iconY + iconSize);

					pRT->DrawBitmap(m_pBitmap, iconDest);
					currentX += iconSize + m_iconPadding;
				}

				// Draw Text
				if (m_pTextLayout) {
					// Offset the text vertically to center
					DWRITE_TEXT_METRICS tm;
					m_pTextLayout->GetMetrics(&tm);
					float textY = rect.top + (height - tm.height) / 2.0f;

					pRT->DrawTextLayout(
						D2D1::Point2F(currentX, textY),
						m_pTextLayout,
						pTextBrush,
						D2D1_DRAW_TEXT_OPTIONS_CLIP // Optimize for clipping
					);
				}
			};

			// Draw primary instance
			DrawSingleInstance(rect.left + m_scrollPos);

			// Draw looping instance if the gap is visible
			if (m_scrollPos + m_contentWidth < width) {
				DrawSingleInstance(rect.left + m_scrollPos + m_contentWidth);
			}

			pRT->PopAxisAlignedClip();
			SafeRelease(&pTextBrush);
		}
	}

	// -------------------------------------------------------------------------
	// Logic / Messaging
	// -------------------------------------------------------------------------

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_CREATE:
			// High precision timer desireable, but standard SetTimer is okay for UI
			m_timerId = SetTimer(m_hwnd, TIMER_ID, 16, NULL); // ~60fps target
			return false;

		case WM_TIMER:
			if (wp == TIMER_ID) {
				m_scrollPos -= m_speed;

				// Loop reset logic
				// Once the first instance is completely off-screen, reset position
				// Note: m_contentWidth includes the gap
				if (m_scrollPos <= -m_contentWidth) {
					m_scrollPos += m_contentWidth; // Smooth wrap
					FireEvent("onCycleComplete", "{}");
				}

				// Trigger repaint
				InvalidateRect(m_hwnd, NULL, FALSE);
				return true;
			}
			break;

		case WM_DESTROY:
			if (m_timerId) KillTimer(m_hwnd, m_timerId);
			ReleaseResources();
			break;

		case WM_SIZE:
			m_recalcNeeded = true; // Recalc gap based on new width
			break;
		}

		return WidgetImpl::OnMessage(msg, wp, lp);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new TextSlider();
}