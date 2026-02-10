#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h> 
#include <string>
#include <algorithm>
#include <vector>
#include <sstream>
#include <commctrl.h>

#include "WidgetImpl.hpp"

// Link DWrite, D2D and Window Codecs
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comctl32.lib")

using namespace ChronoUI;

class EditBox : public WidgetImpl {
	// Inner Child Control
	HWND m_hEdit = NULL;
	WNDPROC m_oldEditProc = nullptr;
	HWND m_hwndTT = nullptr;

	// State Properties
	std::string m_placeholder;
	std::string m_allowedChars = "";
	bool m_isPassword = false;

	// Image Properties
	ID2D1Bitmap* m_pBitmap = nullptr;
	std::string m_imagePath = "";
	std::string m_imageBase64 = "";
	std::string m_imageAlign = "right";
	bool m_imageBorder = false;
	float m_imagePadding = 4.0f;

	// Style Properties 
	float m_paddingX = 4.0f;
	float m_paddingY = 4.0f;

	// Change Tracking
	std::string m_initialValue;

	// GDI Objects
	HBRUSH m_hBackBrush = NULL;

public:
	EditBox() {
		m_validated = true;
		m_imageBorder = false; // Default to false for cleaner look
	}

	virtual ~EditBox() {
		if (m_hBackBrush) DeleteObject(m_hBackBrush);
		SafeRelease(&m_pBitmap);
	}

	const char* __stdcall GetControlName() override { return "EditBox"; }

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 3,
            "description": "A text input field with CSS styling, validation, and icon support (Direct2D)",
            "properties": [
                { "name": "value", "type": "string", "description": "Current text value" },
                { "name": "placeholder", "type": "string", "description": "Hint text when empty" },
                { "name": "password", "type": "boolean", "description": "Mask characters" },
                { "name": "allowed_chars", "type": "string", "description": "Whitelist of allowed characters" },
                { "name": "validation_error", "type": "string", "description": "Error message when invalid" },
                { "name": "image_path", "type": "string", "description": "Path to the icon/image" },
                { "name": "image_base64", "type": "string", "description": "Base64 encoded image" },
                { "name": "image_align", "type": "string", "description": "'left' or 'right'" },
                { "name": "border-radius", "type": "float", "description": "Corner radius" },
                { "name": "border-color", "type": "color", "description": "Normal border color" },
                { "name": "focus-color", "type": "color", "description": "Border color when focused" },
                { "name": "error-color", "type": "color", "description": "Border color when invalid" }
            ],
            "events": [
                { "name": "onChange", "description": "Fired when focus is lost and text changed" },
                { "name": "onInput", "description": "Fired on every character change" },
                { "name": "onFocus", "description": "Fired when control gains focus" },
                { "name": "onBlur", "description": "Fired when control loses focus" }
            ]
        })json";
	}

	// --- Helpers ---

	std::string GetEditText() {
		if (!m_hEdit) return "";
		int len = GetWindowTextLengthA(m_hEdit);
		if (len <= 0) return "";
		std::string buf;
		buf.resize(len);
		GetWindowTextA(m_hEdit, &buf[0], len + 1);
		return buf;
	}

	// --- Initialization ---

	void __stdcall Create(HWND parent) override {
		WidgetImpl::Create(parent);

		// CRITICAL: 
		// 1. WS_BORDER is OMITTED to prevent the native 3D border.
		// 2. We explicitly use 0 for ExStyles to avoid ClientEdge.
		// 3. WS_CLIPSIBLINGS ensures it plays nice with D2D drawing.
		m_hEdit = CreateWindowExA(
			0, "EDIT", "",
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT | WS_CLIPSIBLINGS,
			0, 0, 0, 0,
			m_hwnd, (HMENU)101, GetModuleHandle(NULL), NULL
		);

		HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		SendMessage(m_hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

		SetWindowLongPtr(m_hEdit, GWLP_USERDATA, (LONG_PTR)this);
		m_oldEditProc = (WNDPROC)SetWindowLongPtr(m_hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

		if (m_hEdit) {
			const char* v = GetProperty("value", "");
			std::wstring val = v ? NarrowToWide(v) : L"";
			SetWindowTextW(m_hEdit, val.c_str());
		}
	}

	// --- Message Handling ---

	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_SIZE:
			RearrangeChild();
			InvalidateRect(m_hwnd, NULL, FALSE); // Redraw border on resize
			return false;

		case WM_SETFOCUS:
			if (m_hEdit) ::SetFocus(m_hEdit);
			return false;

		case WM_MOUSEMOVE:
			if (!m_isHovered) {
				m_isHovered = true;
				TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
				TrackMouseEvent(&tme);
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			break;

		case WM_MOUSELEAVE:
			m_isHovered = false;
			InvalidateRect(m_hwnd, NULL, FALSE);
			break;

		case WM_CTLCOLOREDIT:
		{
			// Propagate CSS background color to the GDI Edit Control
			// This ensures the native control "blends" into our D2D background
			LRESULT brush = HandleCtlColor((HDC)wp);
			return brush;
		}

		case WM_COMMAND:
			if ((HWND)lp == m_hEdit) {
				switch (HIWORD(wp)) {
				case EN_CHANGE: {
					std::string m_text = GetEditText();
					SetProperty("value", m_text.c_str());

					// Trigger validation logic (WidgetImpl usually handles this, 
					// but we ensure m_validated is updated)
					PerformValidation(m_text.c_str());

					UpdateBind("value", m_text);
					FireEvent("onInput", "{}");
					UpdateTooltip();

					// Repaint to update border color if validation changed
					InvalidateRect(m_hwnd, NULL, FALSE);
				} break;
				case EN_SETFOCUS:
					m_focused = true;
					m_initialValue = GetEditText();
					FireEvent("onFocus", "{}");
					InvalidateRect(m_hwnd, NULL, FALSE); // Redraw for focus color
					break;
				case EN_KILLFOCUS:
					m_focused = false;
					FireEvent("onBlur", "{}");
					InvalidateRect(m_hwnd, NULL, FALSE); // Redraw for blur color
					std::string nt = GetEditText();
					if (nt != m_initialValue) {
						m_initialValue = nt;
						SetProperty("value", nt.c_str());
						FireEvent("onChange", "{}");
					}
					break;
				}
			}
			break;
		}
		return false;
	}

	// --- Layout ---

	void RearrangeChild() {
		if (!m_hEdit || !m_hwnd) return;

		RECT rc;
		GetClientRect(m_hwnd, &rc);
		int totalW = rc.right - rc.left;
		int totalH = rc.bottom - rc.top;

		HDC hdc = GetDC(m_hEdit);
		HFONT hFont = (HFONT)SendMessage(m_hEdit, WM_GETFONT, 0, 0);
		HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
		TEXTMETRIC tm;
		GetTextMetrics(hdc, &tm);
		SelectObject(hdc, oldFont);
		ReleaseDC(m_hEdit, hdc);

		int editH = tm.tmHeight + 2;
		int y = (totalH - editH) / 2;
		if (y < (int)m_paddingY) y = (int)m_paddingY;

		int padX = (int)m_paddingX;
		int imgSpace = 0;
		bool hasImage = (!m_imagePath.empty() || !m_imageBase64.empty());

		if (hasImage) {
			int iconSize = totalH - ((int)m_paddingY * 2);
			if (iconSize < 16) iconSize = 16;
			imgSpace = iconSize + (int)m_imagePadding;
		}

		int x = padX;
		int w = totalW - (padX * 2);

		if (hasImage) {
			if (m_imageAlign == "left") {
				x += imgSpace;
				w -= imgSpace;
			}
			else { // right
				w -= imgSpace;
			}
		}

		if (w < 0) w = 0;
		::MoveWindow(m_hEdit, x, y, w, editH, TRUE);
	}

	// --- Drawing (Direct2D) ---

	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		// Shrink rect slightly (0.5) to ensure 1px border is drawn cleanly inside bounds
		D2D1_RECT_F rect = D2D1::RectF(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f);

		std::string backColor = m_parent->GetProperty("background-color");
		// 1. Cell draw Background (parent color)
		pRT->Clear(CSSColorToD2D(backColor));

		// 1. Setup Resources
		if (!m_pBitmap) LoadBitmapResources(pRT);

		// 2. Determine Styling
		D2D1_COLOR_F bgColor = CSSColorToD2D(GetProperty("background-color", "#FFFFFF"));
		D2D1_COLOR_F borderColor;
		float borderThickness = 1.0f;

		// --- BORDER COLOR LOGIC ---
		if (!m_validated) {
			// INVALID: Red (or custom error color)
			borderColor = CSSColorToD2D(GetProperty("error-color", "#FF0000"));
			borderThickness = 2.0f; // Highlight error with thicker border
		}
		else if (m_focused) {
			// FOCUSED: Blue (or custom focus color)
			borderColor = CSSColorToD2D(GetProperty("focus-color", "#0078D7"));
			borderThickness = 2.0f;
		}
		else {
			// NORMAL: Gray (or custom border color)
			borderColor = CSSColorToD2D(GetProperty("border-color", "#7a7a7a"));
			if (m_isHovered) {
				// Slightly darker on hover if desired
				 // borderColor = ...
			}
		}

		// 3. Draw Background & Border
		ComPtr<ID2D1SolidColorBrush> pBrush;

		// -- Radius
		float radius = (float)atof(GetProperty("border-radius", "0"));
		D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(rect, radius, radius);

		// -- Fill
		pRT->CreateSolidColorBrush(bgColor, &pBrush);
		pRT->FillRoundedRectangle(roundedRect, pBrush.Get());

		// -- Stroke (The Border)
		pRT->CreateSolidColorBrush(borderColor, &pBrush);
		pRT->DrawRoundedRectangle(roundedRect, pBrush.Get(), borderThickness);

		// 4. Draw Image (if exists)
		if (m_pBitmap) {
			float padY = m_paddingY;
			float contentH = size.height - (padY * 2);
			float iconSize = contentH;
			if (iconSize > size.height) iconSize = size.height;
			float iconTop = (size.height - iconSize) / 2.0f;
			float paddingInner = iconSize * 0.05f;
			float drawSize = iconSize - (paddingInner * 2);
			float padX = m_paddingX;

			D2D1_RECT_F imgRect;
			if (m_imageAlign == "left") {
				imgRect = D2D1::RectF(
					padX + paddingInner, iconTop + paddingInner,
					padX + paddingInner + drawSize, iconTop + paddingInner + drawSize
				);
			}
			else {
				float rightEdge = size.width - padX;
				imgRect = D2D1::RectF(
					rightEdge - iconSize + paddingInner, iconTop + paddingInner,
					rightEdge - paddingInner, iconTop + paddingInner + drawSize
				);
			}

			if (m_imageBorder) {
				// Reuse the existing brush for the image border if needed, or create new
				pRT->DrawRectangle(imgRect, pBrush.Get(), 1.0f);
			}

			float opacity = m_isEnabled ? 1.0f : 0.4f;
			pRT->DrawBitmap(m_pBitmap, imgRect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		}
	}

private:
	void LoadBitmapResources(ID2D1RenderTarget* pRT) {
		SafeRelease(&m_pBitmap);
		if (!m_imageBase64.empty()) {
			ImageFromBase64(pRT, m_imageBase64.c_str(), &m_pBitmap);
		}
		else if (!m_imagePath.empty()) {
			LoadBitmapFromFile(pRT, m_imagePath, &m_pBitmap);
		}
	}

	void LoadBitmapFromFile(ID2D1RenderTarget* pRT, const std::string& path, ID2D1Bitmap** ppBitmap) {
		// (WIC Loading code same as original)
		// ...
		ComPtr<IWICImagingFactory> pWICFactory;
		CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWICFactory));
		if (!pWICFactory) return;

		ComPtr<IWICBitmapDecoder> pDecoder;
		std::wstring wpath = NarrowToWide(path);
		pWICFactory->CreateDecoderFromFilename(wpath.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
		if (!pDecoder) return;

		ComPtr<IWICBitmapFrameDecode> pSource;
		pDecoder->GetFrame(0, &pSource);

		ComPtr<IWICFormatConverter> pConverter;
		pWICFactory->CreateFormatConverter(&pConverter);
		if (pConverter && pSource) {
			pConverter->Initialize(pSource.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
			pRT->CreateBitmapFromWicBitmap(pConverter.Get(), NULL, ppBitmap);
		}
	}

	// --- GDI Support ---

	LRESULT HandleCtlColor(HDC hdc) {
		// Match the inner GDI Edit control background to the D2D Background
		COLORREF crBack = GetCSSColorRefStyle("background-color");
		COLORREF crText = GetCSSColorRefStyle("foreground-color");

		SetTextColor(hdc, crText);
		SetBkColor(hdc, crBack);

		if (m_hBackBrush) DeleteObject(m_hBackBrush);
		m_hBackBrush = CreateSolidBrush(crBack);

		return (LRESULT)m_hBackBrush;
	}

	// --- Tooltip & Validation ---

	void UpdateTooltip() {
		if (!IsWindow(m_hEdit)) return;

		// Create Tooltip if missing
		if (!m_hwndTT) {
			m_hwndTT = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
				WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
				0, 0, 0, 0, m_hEdit, NULL, GetModuleHandle(NULL), NULL);
			TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hEdit, (UINT_PTR)m_hEdit };
			ti.lpszText = (LPSTR)"";
			SendMessage(m_hwndTT, TTM_ADDTOOLA, 0, (LPARAM)&ti);
		}

		std::string m_ttTitle = GetProperty("tt_title");
		std::string m_ttDesc = GetProperty("tt_description");

		// Override title if validation fails
		if (!m_validated) {
			std::string err = GetProperty("validation_error", "");
			if (err.empty()) err = "Invalid Input";
			m_ttTitle = err;
			// Set error icon
			SendMessage(m_hwndTT, TTM_SETTITLEA, 2, (LPARAM)"Error");
		}
		else {
			// Standard Title
			SendMessage(m_hwndTT, TTM_SETTITLEA, 1, (LPARAM)m_ttDesc.c_str());
		}

		TOOLINFOA ti = { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, m_hEdit, (UINT_PTR)m_hEdit };
		ti.lpszText = (LPSTR)m_ttTitle.c_str();
		SendMessage(m_hwndTT, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);
	}

	// --- Properties ---

	void OnPropertyChanged(const char* key, const char* value) override {
		std::string k = key;
		std::string v = value ? value : "";

		if (k == "validated") {
			// Key Fix: Redraw when validation status changes programmatically
			UpdateTooltip();
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (k == "value") {
			if (IsWindow(m_hEdit)) {
				std::string current = GetEditText();
				if (current != v) {
					SetWindowTextW(m_hEdit, NarrowToWide(v).c_str());
				}
			}
		}
		else if (k == "placeholder") {
			if (IsWindow(m_hEdit)) {
				m_placeholder = v;
				std::wstring wPlaceholder = NarrowToWide(m_placeholder);
				SendMessage(m_hEdit, EM_SETCUEBANNER, 0, (LPARAM)wPlaceholder.c_str());
			}
		}
		else if (k == "password") {
			if (IsWindow(m_hEdit)) {
				m_isPassword = (v == "true");
				SendMessage(m_hEdit, EM_SETPASSWORDCHAR, m_isPassword ? (WPARAM)'*' : 0, 0);
			}
		}
		else if (k == "allowed_chars") {
			m_allowedChars = v;
		}
		else if (k == "image_path" || k == "image_base64") {
			if (k == "image_path") m_imagePath = v; else m_imageBase64 = v;
			SafeRelease(&m_pBitmap);
			RearrangeChild();
			InvalidateRect(m_hwnd, NULL, FALSE);
		}
		else if (k == "image_align") {
			m_imageAlign = v;
			RearrangeChild();
			InvalidateRect(m_hwnd, NULL, FALSE);
		}

		WidgetImpl::OnPropertyChanged(key, value);
	}

	static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
		EditBox* pThis = (EditBox*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		if (msg == WM_CHAR) {
			if (pThis && !pThis->m_allowedChars.empty() && wp >= 32) {
				if (pThis->m_allowedChars.find((char)wp) == std::string::npos) {
					MessageBeep(MB_ICONWARNING);
					return 0;
				}
			}
		}
		return CallWindowProc(pThis->m_oldEditProc, hwnd, msg, wp, lp);
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new EditBox();
}