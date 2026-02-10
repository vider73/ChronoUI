#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <algorithm>
#include <wrl/client.h>

#include "WidgetImpl.hpp"
#include "ChronoStyles.hpp"

// Libraries
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace ChronoUI;
using Microsoft::WRL::ComPtr;

// --- Data Structures ---
struct ListItem {
	std::string id;
	std::string title;
	std::string description;
	std::string imgId;
};

struct CachedImage {
	std::string base64;
	ComPtr<ID2D1Bitmap> pBitmap;
	ID2D1RenderTarget* pCreatorRT = nullptr;
};

class ListCards : public WidgetImpl {
	std::vector<std::unique_ptr<ListItem>> m_items;
	std::map<std::string, CachedImage> m_imageCache;

	// State
	int m_selectedIndex = -1;
	int m_hoverIndex = -1;
	float m_scrollY = 0.0f;

	// Layout Constants
	float m_defaultHeight = 100.0f; // Default if not styled
	float m_scrollbarWidth = 10.0f;

	// Resources
	ComPtr<IDWriteTextFormat> m_pFmtTitle;
	ComPtr<IDWriteTextFormat> m_pFmtDesc;

	// Interaction
	bool m_isDragging = false;
	int m_lastMouseY = 0;

public:
	ListCards() {
		// Default class so styles apply immediately		
		StyleManager::AddClass(this->GetContextNode(), "card");
	}

	const char* __stdcall GetControlName() override { return "ListCards"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 4,
            "description": "Fixed layout card list",
            "properties": [
                { "name": "addItem", "type": "string", "description": "id|title|desc|imgId" },
                { "name": "addImage", "type": "string", "description": "imgId|base64" },
                { "name": "clear", "type": "action" }
            ],
            "events": [ { "name": "onItemClick", "type": "event" } ]
        })json";
	}

	// --- Helpers ---

	// Helper to get float from CSS (e.g. "10px" -> 10.0)
	float GetStyleFloat(const char* prop, float def, const char* cls, bool sel, bool hov) {
		std::string v = GetStyle(prop, "", GetControlName(), cls, sel, IsWindowEnabled(m_hwnd), hov);
		if (v.empty()) return def;
		try { return std::stof(v); }
		catch (...) { return def; }
	}

	// --- Drawing ---

	void CreateTextFormats() {
		auto dw = GetDWriteFactory();
		if (!m_pFmtTitle) {
			// Title: 16px, Semi-Bold
			dw->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us", &m_pFmtTitle);
			if (m_pFmtTitle) {
				m_pFmtTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
				ComPtr<IDWriteInlineObject> ellipsis;
				dw->CreateEllipsisTrimmingSign(m_pFmtTitle.Get(), &ellipsis);
				DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
				m_pFmtTitle->SetTrimming(&trim, ellipsis.Get());
			}
		}
		if (!m_pFmtDesc) {
			// Description: 13px, Normal
			dw->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &m_pFmtDesc);
			if (m_pFmtDesc) {
				m_pFmtDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); // Allow wrap for desc
				ComPtr<IDWriteInlineObject> ellipsis;
				dw->CreateEllipsisTrimmingSign(m_pFmtDesc.Get(), &ellipsis);
				DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
				m_pFmtDesc->SetTrimming(&trim, ellipsis.Get());
			}
		}
	}

	ID2D1Bitmap* GetImage(ID2D1RenderTarget* rt, const std::string& id) {
		if (id.empty()) return nullptr;
		auto it = m_imageCache.find(id);
		if (it == m_imageCache.end()) return nullptr;

		if (!it->second.pBitmap || it->second.pCreatorRT != rt) {
			it->second.pBitmap.Reset();
			ID2D1Bitmap* bmp = nullptr;
			ImageFromBase64(rt, it->second.base64.c_str(), &bmp);
			if (bmp) {
				it->second.pBitmap.Attach(bmp);
				it->second.pCreatorRT = rt;
			}
		}
		return it->second.pBitmap.Get();
	}

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		CreateTextFormats();
		D2D1_SIZE_F size = pRT->GetSize();

		// Clear background
		pRT->Clear(D2D1::ColorF(D2D1::ColorF::White)); // Base canvas color

		std::string cls = GetProperty("class");
		if (cls.empty()) cls = "card";

		// Pre-calculate metrics to ensure performance
		float cardH = (GetStyleFloat("card-height", 100.0f, "", false, false));
		float margin = (GetStyleFloat("margin", 8.0f, "", false, false));
		float icon_size = (GetStyleFloat("icon-size", 32.0f, "", false, false));

		// Total row height = Card Height + Margin
		float rowTotal = cardH + margin;
		float totalListH = m_items.size() * rowTotal;

		pRT->PushAxisAlignedClip(D2D1::RectF(0, 0, size.width, size.height), D2D1_ANTIALIAS_MODE_ALIASED);

		for (int i = 0; i < (int)m_items.size(); ++i) {
			float yPos = (i * rowTotal) + (margin / 2.0f) - m_scrollY;

			// Visibility Culling
			if (yPos + cardH < 0) continue;
			if (yPos > size.height) break;

			bool isSel = (i == m_selectedIndex);
			bool isHov = (i == m_hoverIndex);

			// 1. Resolve Colors
			D2D1_COLOR_F cBg = CSSColorToD2D(GetStyle("background-color", "#ffffff", "", "", isSel, true, isHov));
			D2D1_COLOR_F cBorder = CSSColorToD2D(GetStyle("border-color", "#dddddd", "", "", isSel, true, isHov));
			D2D1_COLOR_F cTitle = CSSColorToD2D(GetStyle("color", "#111111", "", "", isSel, true, isHov));
			D2D1_COLOR_F cDesc = CSSColorToD2D(GetStyle("description-color", "#666666", "", "", isSel, true, isHov));

			// 2. Resolve Metrics
			float radius = (GetStyleFloat("border-radius", 6.0f, "", isSel, isHov));
			float borderW = (GetStyleFloat("border-width", 1.0f, "", isSel, isHov));
			float pad = (GetStyleFloat("padding", 12.0f, "", isSel, isHov));

			// 3. Create Brushes
			ComPtr<ID2D1SolidColorBrush> brBg, brBorder, brTitle, brDesc;
			pRT->CreateSolidColorBrush(cBg, &brBg);
			pRT->CreateSolidColorBrush(cBorder, &brBorder);
			pRT->CreateSolidColorBrush(cTitle, &brTitle);
			pRT->CreateSolidColorBrush(cDesc, &brDesc);

			// 4. Draw Card Box
			float scrollBarSpace = (totalListH > size.height) ? (m_scrollbarWidth) : 0;
			D2D1_RECT_F rcCard = D2D1::RectF(margin, yPos, size.width - margin - scrollBarSpace, yPos + cardH);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rcCard, radius, radius);

			if (brBg) pRT->FillRoundedRectangle(rr, brBg.Get());
			if (brBorder && borderW > 0) pRT->DrawRoundedRectangle(rr, brBorder.Get(), borderW);

			// 5. Layout Content
			//   [ Title ............. Image ]
			//   [ Description ............. ]

			float contentL = rcCard.left + pad;
			float contentT = rcCard.top + pad;
			float contentR = rcCard.right - pad;
			float contentB = rcCard.bottom - pad;
			float availW = contentR - contentL;

			// -- Image (Right Aligned) --
			float imgSize = (icon_size); // Fixed convenient size for avatar/thumb
			ID2D1Bitmap* pBmp = GetImage(pRT, m_items[i]->imgId);

			if (pBmp) {
				// Draw Image Top-Right
				D2D1_RECT_F rcImg = D2D1::RectF(contentR - imgSize, contentT, contentR, contentT + imgSize);
				pRT->DrawBitmap(pBmp, rcImg);

				// Reduce available width for text
				availW -= (imgSize + pad);
			}

			// -- Title (Top Left) --
			// Fixed height estimate for title line: 24px
			float titleH = (24);
			D2D1_RECT_F rcTitle = D2D1::RectF(contentL, contentT, contentL + availW, contentT + titleH);

			if (brTitle && m_pFmtTitle) {
				std::wstring wTitle = NarrowToWide(m_items[i]->title);
				pRT->DrawText(wTitle.c_str(), (UINT32)wTitle.length(), m_pFmtTitle.Get(), rcTitle, brTitle.Get());
			}

			// -- Description (Below Title) --
			float descTop = contentT + titleH + (14.0f); // Small gap
			D2D1_RECT_F rcDesc = D2D1::RectF(contentL, descTop, contentL + availW, contentB);

			if (brDesc && m_pFmtDesc) {
				std::wstring wDesc = NarrowToWide(m_items[i]->description);
				pRT->DrawText(wDesc.c_str(), (UINT32)wDesc.length(), m_pFmtDesc.Get(), rcDesc, brDesc.Get());
			}
		}

		pRT->PopAxisAlignedClip();

		// 6. Scrollbar
		if (totalListH > size.height) {
			float viewRatio = size.height / totalListH;
			float thumbH = (std::max)(50.0f, size.height * viewRatio);
			float maxTop = size.height - thumbH;
			float scrollPct = m_scrollY / (totalListH - size.height);
			float thumbTop = scrollPct * maxTop;

			ComPtr<ID2D1SolidColorBrush> brScroll;
			pRT->CreateSolidColorBrush(D2D1::ColorF(0.6f, 0.6f, 0.6f), &brScroll);

			float sbW = (m_scrollbarWidth);
			D2D1_RECT_F rcScroll = D2D1::RectF(size.width - sbW + 2, thumbTop, size.width - 2, thumbTop + thumbH);
			pRT->FillRoundedRectangle(D2D1::RoundedRect(rcScroll, 4, 4), brScroll.Get());
		}
	}

	// --- Events & Data ---

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		RECT rc; GetClientRect(m_hwnd, &rc);
		float h = (float)(rc.bottom - rc.top);

		// Recalculate basic height for hit testing
		float cardH = (GetStyleFloat("height", 100.0f, "card", false, false));
		float margin = (GetStyleFloat("margin", 8.0f, "card", false, false));
		float rowTotal = cardH + margin;
		float totalH = m_items.size() * rowTotal;

		switch (msg) {
		case WM_MOUSEWHEEL: {
			int d = GET_WHEEL_DELTA_WPARAM(wp);
			m_scrollY -= (d / 120.0f) * 60.0f; // Scroll speed
			if (m_scrollY < 0) m_scrollY = 0;
			if (totalH > h && m_scrollY > totalH - h) m_scrollY = totalH - h;
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		}
		case WM_LBUTTONDOWN: {
			int my = GET_Y_LPARAM(lp);
			float effY = (float)my + m_scrollY;
			int idx = (int)((effY - (margin / 2)) / rowTotal); // Approx hit test

			if (idx >= 0 && idx < (int)m_items.size()) {
				m_selectedIndex = idx;
				std::string s = "{\"index\":" + std::to_string(idx) + ",\"id\":\"" + m_items[idx]->id + "\"}";
				FireEvent("onItemClick", s.c_str());
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return true;
		}
		case WM_MOUSEMOVE: {
			int my = GET_Y_LPARAM(lp);
			float effY = (float)my + m_scrollY;
			int idx = (int)((effY - (margin / 2)) / rowTotal);
			if (idx < 0 || idx >= (int)m_items.size()) idx = -1;
			if (idx != m_hoverIndex) {
				m_hoverIndex = idx;
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return true;
		}
		case WM_MOUSELEAVE:
			m_hoverIndex = -1;
			InvalidateRect(m_hwnd, NULL, FALSE);
			return true;
		}
		return false;
	}

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string k = key;
		std::string v = value ? value : "";
		std::stringstream ss(v);

		if (k == "addItem") {
			std::string id, t, d, img;
			std::getline(ss, id, '|'); std::getline(ss, t, '|');
			std::getline(ss, d, '|'); std::getline(ss, img, '|');
			auto i = std::make_unique<ListItem>();
			i->id = id; i->title = t; i->description = d; i->imgId = img;
			m_items.push_back(std::move(i));
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (k == "addImage") {
			std::string id, b64;
			std::getline(ss, id, '|'); std::getline(ss, b64);
			AddImage(id, b64);
		}
		else if (k == "clear") {
			m_items.clear();
			m_scrollY = 0;
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		WidgetImpl::OnPropertyChanged(key, value);
	}

	void AddImage(const std::string& id, const std::string& b64) {
		CachedImage c; c.base64 = b64;
		m_imageCache[id] = std::move(c);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new ListCards();
}