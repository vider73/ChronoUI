#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

#include "ChronoUI.hpp"
#include <vector>
#include <string>
#include <map>
#include <windowsx.h>
#include <algorithm>
#include <dwmapi.h>
#include <gdiplus.h>
#include <mutex>
#include <regex>
#include <sstream>
#include <debugapi.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h> // IWICImagingFactory for Base64 images

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib") // For image loading

// Smart pointer helper is highly recommended for COM
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

#include "WidgetImpl.hpp"
#include "ChronoStyles.hpp"

namespace ChronoUI {
	const int SPLITTER_SIZE = 6;
	const UINT WM_CHRONO_SPLIT = WM_USER + 101;

	static int Scale(HWND hwnd, int px) { return MulDiv((int)px, (int)GetDpiForWindow(hwnd), 96); }
	static int Scale(HWND hwnd, float px) { return MulDiv((int)px, (int)GetDpiForWindow(hwnd), 96); }

	
	// Window properties used for cross-window hit-test behavior
	static constexpr wchar_t kPropCustomTitleBar[] = L"Chrono.CustomTitleBar";
	static constexpr wchar_t kPropTitleRowHeight[] = L"Chrono.TitleRowHeight";

	// --- Splitter ---
	struct SplitterData { HWND hwnd; bool isVert; int index; void* owner; bool dragging = false; };

	static LRESULT CALLBACK SplitterWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
		auto* sd = (SplitterData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		switch (msg) {
		case WM_SETCURSOR: SetCursor(LoadCursor(NULL, sd->isVert ? IDC_SIZEWE : IDC_SIZENS)); return TRUE;
		case WM_LBUTTONDOWN: SetCapture(hwnd); sd->dragging = true; return 0;
		case WM_LBUTTONUP: ReleaseCapture(); sd->dragging = false; return 0;
		case WM_MOUSEMOVE:
			if (sd->dragging && (wp & MK_LBUTTON))
				SendMessage(GetParent(hwnd), WM_CHRONO_SPLIT, (WPARAM)sd, lp); return 0;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			RECT r;
			GetClientRect(hwnd, &r);
			COLORREF bg = RGB(200, 200, 200);
			HBRUSH hbr = CreateSolidBrush(bg);
			FillRect(hdc, &r, hbr);
			DeleteObject(hbr);
			EndPaint(hwnd, &ps);
			return 0;
		}
		}
		return DefWindowProc(hwnd, msg, wp, lp);
	}

	// --- Cell ---
	class LayoutImpl;
	class CellImpl : public ICell, public ContextNodeImpl {
	public:
		HWND m_hwnd = nullptr;
		std::vector<IWidget*> widgets;
		ILayout* nested = nullptr;
		bool scrollEnabled = false; int scrollPos = 0;
		StackMode m_mode = StackMode::Vertical;
		int m_activeTab = 0;

		std::vector<IWidget*> overflowItems;
		IWidget* overflowButton = nullptr;
		bool measurementsDirty = true;
		IContainer* parentContainer;

		CellImpl(IContainer* _parentContainer) : parentContainer(_parentContainer) {
		}
		~CellImpl();

		void Clean();


		virtual void __stdcall RemoveAllWidgets() override {
			Clean();
		}
		
		void __stdcall UpdateWidgets();

		// Helper: Parses dimensions like "200", "200px", "25%"
		// Returns -1 if the string is empty so the caller can apply defaults.
		int ParseCssDimension(const std::string& input, int refTotalSize) {
			if (input.empty()) return -1;

			std::string val = input;
			bool isPercent = false;

			if (val == "auto")
				return refTotalSize;

			// Check for percentage
			if (val.back() == '%') {
				isPercent = true;
				val.pop_back();
			}
			// Check for units to strip (px, lu) - treat both as scalable units
			else if (val.length() > 2) {
				std::string suffix = val.substr(val.length() - 2);
				if (suffix == "px" || suffix == "lu") {
					val = val.substr(0, val.length() - 2);
				}
			}

			try {
				float fVal = std::stof(val);
				if (isPercent) {
					return (int)(refTotalSize * (fVal / 100.0f));
				}
				else {
					// We assume inputs are logical units (DPI independent) unless otherwise specified,
					// so we scale them.
					return Scale(m_hwnd, (int)(fVal));
				}
			}
			catch (...) {
				return -1;
			}
		}

		IContainer* SetParentContainer(IContainer* _parentContainer) { parentContainer = _parentContainer; };
		IContainer* GetParentContainer() { return parentContainer; };

		virtual void __stdcall SetParentNode(IContextNode* parent) override { ContextNodeImpl::SetParentNode(parent); }
		virtual IContextNode* __stdcall GetParentNode() override { return ContextNodeImpl::GetParentNode(); }
		virtual IContextNode* __stdcall GetContextNode() override { return ContextNodeImpl::GetContextNode(); }

		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override { return ContextNodeImpl::GetProperty(key, def); }
		virtual ICell* __stdcall SetProperty(const char* key, const char* value) {
			m_properties[key] = (value) ? value : "";
			InvalidateRect(m_hwnd, NULL, TRUE);
			return this;
		}
		virtual COLORREF GetColor(const char* key, COLORREF defaultColor = RGB(0, 0, 0)) override { return ContextNodeImpl::GetColor(key, defaultColor); }
		virtual IContextNode* SetColor(const char* key, COLORREF color) override { return ContextNodeImpl::SetColor(key, color); };
		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active) override { return ContextNodeImpl::GetStyle(_prop, _def, _classid, _subclass, selected, enabled, hovered, active); }

		void DrawTextStyled(Gdiplus::Graphics& g, const std::string& text, const Gdiplus::Color& color, const Gdiplus::RectF& r) {
			Gdiplus::SolidBrush textBrush(color);
			std::string subclass = GetProperty("subclass");

			std::string fontNameStr = GetStyle("font-family", "Segoe UI", "cell", subclass.c_str(), false, true, false, false);
			std::wstring fontNameW(fontNameStr.begin(), fontNameStr.end());
			Gdiplus::FontFamily fontFamily(fontNameW.c_str());

			std::string styleStr = GetStyle("font-style", "normal", "cell", subclass.c_str(), false, true, false, false);
			INT fontStyle = Gdiplus::FontStyleRegular;
			if (styleStr == "bold")            fontStyle = Gdiplus::FontStyleBold;
			else if (styleStr == "italic")     fontStyle = Gdiplus::FontStyleItalic;
			else if (styleStr == "bolditalic") fontStyle = Gdiplus::FontStyleBold | Gdiplus::FontStyleItalic;
			else if (styleStr == "underline")  fontStyle = Gdiplus::FontStyleUnderline;

			int baseFontSize = std::stoi(GetStyle("font-size", "12", "cell", subclass.c_str(), false, true, false, false));
			float scaledFontSize = ScaleSize((float)baseFontSize);
			Gdiplus::Font font(&fontFamily, scaledFontSize, fontStyle, Gdiplus::UnitPixel);

			Gdiplus::StringFormat format;
			std::string alignStr = GetStyle("text-align", "center", "cell", subclass.c_str(), false, true, false, false);

			if (alignStr == "left")       format.SetAlignment(Gdiplus::StringAlignmentNear);
			else if (alignStr == "right") format.SetAlignment(Gdiplus::StringAlignmentFar);
			else                          format.SetAlignment(Gdiplus::StringAlignmentCenter);
			format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

			std::wstring wText(text.begin(), text.end());
			g.DrawString(wText.c_str(), -1, &font, r, &format, &textBrush);
		}

		void ScrollWidgetIntoView(HWND widgetHwnd) {
			if (!scrollEnabled || widgets.empty()) return;
			RECT cellRect, widgetRect; GetClientRect(m_hwnd, &cellRect); GetWindowRect(widgetHwnd, &widgetRect);
			MapWindowPoints(HWND_DESKTOP, m_hwnd, (LPPOINT)&widgetRect, 2);
			bool isHoriz = (m_mode == StackMode::Horizontal);
			int widgetSize = Scale(m_hwnd, 80), viewportSize = isHoriz ? cellRect.right : cellRect.bottom;
			int widgetStart = isHoriz ? widgetRect.left : widgetRect.top, widgetEnd = widgetStart + widgetSize;
			int newScrollPos = scrollPos;
			if (widgetStart < 0) newScrollPos = scrollPos + widgetStart;
			else if (widgetEnd > viewportSize) newScrollPos = scrollPos + (widgetEnd - viewportSize);
			if (newScrollPos < 0) newScrollPos = 0;
			if (newScrollPos != scrollPos) {
				scrollPos = newScrollPos;
				SCROLLINFO si = { sizeof(si), SIF_POS }; si.nPos = scrollPos;
				SetScrollInfo(m_hwnd, isHoriz ? SB_HORZ : SB_VERT, &si, TRUE);
				UpdateWidgets();
			}
		}

		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
			CellImpl* self = (CellImpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			switch (msg) {
			case WM_SIZE:
				if (self) {
					if (self->m_mode == StackMode::CommandBar) {
						self->measurementsDirty = true;
					}
					self->UpdateWidgets();
				}
				return 0;
			case WM_ERASEBKGND: {
				HDC hdc = (HDC)wp;
				RECT r;
				GetClientRect(hwnd, &r);
				COLORREF bg = self ? self->GetColor("background-color", RGB(255, 255, 255)) : RGB(64, 64, 64);
				HBRUSH hbr = CreateSolidBrush(bg);
				FillRect(hdc, &r, hbr);
				DeleteObject(hbr);
				return 1;
			}
			case WM_PAINT: {
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT r;
				GetClientRect(hwnd, &r);
				COLORREF bg = self ? self->GetColor("background-color", RGB(255, 255, 255)) : RGB(64, 64, 64);
				HBRUSH hbr = CreateSolidBrush(bg);
				FillRect(hdc, &r, hbr);
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_NCHITTEST: {
				LRESULT base = DefWindowProc(hwnd, msg, wp, lp);
				if (base != HTCLIENT) return base;
				HWND root = GetAncestor(hwnd, GA_ROOT);
				if (!root) return base;

				if (GetPropW(root, kPropCustomTitleBar)) {
					UINT dpi = GetDpiForWindow(root);
					int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
					int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

					POINT ptScreen = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
					RECT rcRoot;
					GetWindowRect(root, &rcRoot);

					bool onLeft = ptScreen.x < rcRoot.left + frameX;
					bool onRight = ptScreen.x >= rcRoot.right - frameX;
					bool onBottom = ptScreen.y >= rcRoot.bottom - frameY;
					bool onTop = ptScreen.y < rcRoot.top + frameY;

					if (onLeft || onRight || onBottom || onTop) return HTTRANSPARENT;

					int titleH = (int)(INT_PTR)GetPropW(root, kPropTitleRowHeight);
					if (titleH > 0) {
						POINT rpt = ptScreen;
						ScreenToClient(root, &rpt);
						if (rpt.y >= 0 && rpt.y <= titleH) return HTTRANSPARENT;
					}
				}
				return base;
			}
			case WM_CHRONO_SPLIT:
				return SendMessage(GetParent(hwnd), msg, wp, lp);

			case WM_USER + 200:
				if (self)
					self->ScrollWidgetIntoView((HWND)lp); return 0;

			case WM_VSCROLL:
			case WM_HSCROLL: {
				int bar = (msg == WM_VSCROLL) ? SB_VERT : SB_HORZ;
				SCROLLINFO si = { sizeof(si), SIF_ALL }; GetScrollInfo(hwnd, bar, &si);
				int oldPos = si.nPos;
				switch (LOWORD(wp)) {
				case SB_LINEUP: si.nPos -= 20; break;
				case SB_LINEDOWN: si.nPos += 20; break;
				case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
				}
				si.fMask = SIF_POS; 
				SetScrollInfo(hwnd, bar, &si, TRUE); 
				GetScrollInfo(hwnd, bar, &si);

				if (si.nPos != oldPos) { 
					self->scrollPos = si.nPos; 
					self->UpdateWidgets(); 
				}
				return 0;
			}

			}
			return DefWindowProc(hwnd, msg, wp, lp);
		}

		void Create(HWND p) {
			WNDCLASSW wc = { 0 }; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandle(NULL);
			wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpszClassName = L"ChronoCell";
			wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
			if (!GetClassInfoW(wc.hInstance, wc.lpszClassName, &wc)) RegisterClassW(&wc);

			m_hwnd = CreateWindowExW(0, L"ChronoCell", nullptr,
				WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
				0, 0, 0, 0, p, nullptr, wc.hInstance, nullptr);

			SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
		}

		// Helper: Removes widget from this cell's management BUT keeps the HWND alive
		void __stdcall DetachWidget(IWidget* w) override;
		// Helper: Takes an existing widget from another cell and moves it here
		void __stdcall AdoptWidget(IWidget* w) override;

		IWidget* __stdcall CreateOverflowButton();

		// --- Update CellImpl::ShowOverflowPopup ---

		void ShowOverflowPopup() {
			if (overflowItems.empty()) return;

			auto* container = GetParentContainer();
			if (!container) return;

			// Create popup container
			// Adjust sizing logic as needed
			
			float buttonWidth = (float)Scale(m_hwnd, getStandardMetric(StandardMetric::ToolbarButtonHeight));

			int popupWidth = Scale(m_hwnd, getStandardMetric(StandardMetric::ToolbarButtonLabeledWidth));
			int popupHeight = Scale(m_hwnd, ((int)overflowItems.size()+1) * buttonWidth );

			IContainer* popup = CreateChronoPopupContainer(m_hwnd, popupWidth, popupHeight);

			if (!popup) return;

			// Setup popup layout
			auto* root = popup->CreateRootLayout(1, 1);

			CellImpl* popupCell = (CellImpl*)root->GetCell(0, 0);

			popupCell->EnableScroll(true);
			popupCell->SetStackMode(StackMode::Vertical);

			std::vector<IWidget*> movedItems = overflowItems;
			overflowItems.clear();

			for (auto* item : movedItems) {
				this->DetachWidget(item);
				popupCell->AdoptWidget(item);
			}

			popupCell->UpdateWidgets();

			// --- 2. SHOW AND RUN ---
			// Position popup aligned with the overflow button or cursor
			RECT rBtn;
			if (overflowButton)
				GetWindowRect(overflowButton->GetHWND(), &rBtn);
			else
				GetWindowRect(m_hwnd, &rBtn);

			popup->ShowPopup(popupWidth, popupHeight);

			// This blocks until the popup is closed
			popup->RunMessageLoop();

			// --- 3. RESTORE WIDGETS ---
			for (auto* item : movedItems) {
				this->AdoptWidget(item);
			}

			delete (popup);

			// Force layout update on main bar
			this->UpdateWidgets();
		}

		void MeasureCommandBarWidgets(int availableWidth, std::vector<IWidget*>& visible, std::vector<IWidget*>& overflow, bool allowOverflow = true) {
			visible.clear();
			overflow.clear();

			if (widgets.empty()) return;

			int overflowBtnWidth = Scale(m_hwnd, 40);
			int spacing = Scale(m_hwnd, 4);
			int usedWidth = 0;

			for (auto* w : widgets) {
				if (w == overflowButton) continue;

				int widgetWidth = 0;
				const char* wProp = w->GetProperty("width");

				// 1. Check for "auto"
				if (wProp && strcmp(wProp, "auto") == 0) {
					widgetWidth = Scale(m_hwnd, 40);
				}
				else {
					// 2. Parse explicit dimension
					widgetWidth = ParseCssDimension(wProp, availableWidth);
					// 3. Default if missing
					if (widgetWidth < 0) widgetWidth = Scale(m_hwnd, 80);
				}

				// Calculate maxWidth based on whether overflow logic is active
				int maxWidth = availableWidth;

				// Only check for overflow button reservation if overflow is actually allowed
				if (allowOverflow) {
					// Check if we need to reserve space for the overflow button.
					// 1. If we already have items in overflow, we are in "overflow mode".
					// 2. If adding this item + spacing + overflow button exceeds total, we restrict limit.
					bool needsOverflowRoom = !overflow.empty() ||
						(usedWidth + widgetWidth + spacing + overflowBtnWidth > availableWidth);

					if (needsOverflowRoom) {
						maxWidth = availableWidth - overflowBtnWidth - spacing;
					}
				}

				// Check fit
				// Note: If allowOverflow is false, 'overflow' vector is always empty, 
				// so we effectively just check: (usedWidth + widgetWidth <= availableWidth)
				if (overflow.empty() && (usedWidth + widgetWidth <= maxWidth)) {
					visible.push_back(w);
					usedWidth += widgetWidth + spacing;
				}
				else {
					// It doesn't fit in the remaining visible space.
					if (allowOverflow) {
						overflow.push_back(w);
					}
					// If allowOverflow is false, we do nothing. 
					// The widget is neither visible nor in overflow (effectively clipped/hidden).
				}
			}
		}


		IWidget* __stdcall AddWidget(IWidget* w) override;
		void __stdcall RemoveWidget(IWidget* w) override;

		IWidget* __stdcall GetWidget(int idx) override;
		ILayout* __stdcall CreateLayout(int r, int c) override;
		ILayout* __stdcall GetNestedLayout() override;

		void __stdcall SetStackMode(StackMode mode) override { m_mode = mode; UpdateWidgets(); }
		void __stdcall SetActiveTab(int index) override { m_activeTab = index; UpdateWidgets(); }
		void __stdcall EnableScroll(bool e) override {
			scrollEnabled = e; bool isHoriz = (m_mode == StackMode::Horizontal);
			LONG style = GetWindowLong(m_hwnd, GWL_STYLE);
			SetWindowLong(m_hwnd, GWL_STYLE, e ? (style | (isHoriz ? WS_HSCROLL : WS_VSCROLL)) : (style & ~(WS_HSCROLL | WS_VSCROLL)));
			UpdateWidgets();
		}
	};

	struct DimPlan { 
		WidgetSize size; 
		WidgetSize min; 
		int pos = 0; 
		int actual = 0; 
		bool splitter = false; 
		SplitterData* sd = nullptr; 

		// --- ADD THESE ---
		WidgetSize savedSize = WidgetSize::Fixed(0);
		bool isCollapsed = false;     // State flag
	};

	class LayoutImpl : public ILayout, public ContextNodeImpl {
		HWND m_parentNode;
		int m_rCount,
			m_cCount;
		std::vector<DimPlan> m_rows, m_cols;
		std::vector<CellImpl*> m_cells;
		RECT m_lastRect = { 0,0,0,0 };
		std::map<std::string, std::pair<int, int>> named_cells;

	public:
		LayoutImpl(IContainer* _parentContainer, HWND p, int r, int c) : parentContainer(_parentContainer), m_parentNode(p), m_rCount(r), m_cCount(c) {
			m_rows.resize(r, { WidgetSize::Fill(), WidgetSize::Fixed(20) });
			m_cols.resize(c, { WidgetSize::Fill(), WidgetSize::Fixed(20) });
			for (int i = 0; i < r * c; ++i) {
				CellImpl* cell = new CellImpl(GetParentContainer());
				cell->Create(p);
				m_cells.push_back(cell);
			}
		}
		~LayoutImpl() {
			// Destroy all cells created by this layout
			for (CellImpl* cell : m_cells) {
				if (cell) {
					// Destroying the cell will trigger the recursive 
					// destruction of widgets inside it (via ~CellImpl)
					if (IsWindow(cell->m_hwnd)) {
						DestroyWindow(cell->m_hwnd);
					}
					delete cell;
				}
			}
			m_cells.clear();

			// Clean up splitters
			for (auto& r : m_rows) {
				if (r.sd) {
					if (IsWindow(r.sd->hwnd)) DestroyWindow(r.sd->hwnd);
					delete r.sd;
				}
			}
			for (auto& c : m_cols) {
				if (c.sd) {
					if (IsWindow(c.sd->hwnd)) DestroyWindow(c.sd->hwnd);
					delete c.sd;
				}
			}
		}
		// --- ADD THIS METHOD TO LayoutImpl ---
		void CalculateMinimumSize(int& w, int& h) {
			int sSize = Scale(m_parentNode, SPLITTER_SIZE);
			w = 0; h = 0;

			// Calculate Total Minimum Height (Rows)
			for (auto& r : m_rows) {
				if (r.splitter) h += sSize;

				if (r.size.unit == SizeUnit::Pixels) {
					h += Scale(m_parentNode, (int)r.size.value);
				}
				else if (r.size.unit == SizeUnit::System) {
					h += GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(m_parentNode));
				}
				else {
					// For Fill/Percent, we rely on the defined Minimum size
					// If Min is percent, we can't reliably calculate total height without context, 
					// so we assume 0 or the pixel value.
					int minPx = (r.min.unit == SizeUnit::Pixels) ? Scale(m_parentNode, (int)r.min.value) : 0;
					h += minPx;
				}
			}

			// Calculate Total Minimum Width (Cols)
			for (auto& c : m_cols) {
				if (c.splitter) w += sSize;

				if (c.size.unit == SizeUnit::Pixels) {
					w += Scale(m_parentNode, (int)c.size.value);
				}
				else if (c.size.unit == SizeUnit::System) {
					// System width isn't standard, usually ignored, treat as 0 or valid metric
					w += 0;
				}
				else {
					int minPx = (c.min.unit == SizeUnit::Pixels) ? Scale(m_parentNode, (int)c.min.value) : 0;
					w += minPx;
				}
			}
		}
		IContainer* parentContainer = nullptr;
		IContainer* SetParentContainer(IContainer* _parentContainer) { parentContainer = _parentContainer; };
		IContainer* GetParentContainer() { return parentContainer; };

		virtual void __stdcall SetParentNode(IContextNode* parent) override {
			ContextNodeImpl::SetParentNode(parent);
		}
		virtual IContextNode* __stdcall GetParentNode() override { return ContextNodeImpl::GetParentNode(); }
		virtual IContextNode* __stdcall GetContextNode() override { return ContextNodeImpl::GetContextNode(); }
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override { return ContextNodeImpl::GetProperty(key, def); }
		virtual ILayout* __stdcall SetProperty(const char* key, const char* value) { 
			m_properties[key] = (value) ? value : ""; 
			return this; 
		}
		virtual COLORREF GetColor(const char* key, COLORREF defaultColor = RGB(0, 0, 0)) override { return ContextNodeImpl::GetColor(key, defaultColor); }
		virtual IContextNode* SetColor(const char* key, COLORREF color) override { return SetColor(key, color); };
		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active) override { return ContextNodeImpl::GetStyle(_prop, _def, _classid, _subclass, selected, enabled, hovered, active); }

		void __stdcall SetRow(int i, WidgetSize s, bool sp, WidgetSize min) override {
			m_rows[i].size = s; 
			m_rows[i].splitter = sp; 
			m_rows[i].min = min;

			if (sp && !m_rows[i].sd) {
				m_rows[i].sd = new SplitterData{ nullptr, false, i, this };
				WNDCLASSW wc = { 0 }; wc.lpfnWndProc = SplitterWndProc; wc.lpszClassName = L"ChronoSplit";
				if (!GetClassInfoW(GetModuleHandle(NULL), wc.lpszClassName, &wc)) RegisterClassW(&wc);
				m_rows[i].sd->hwnd = CreateWindowW(L"ChronoSplit", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_parentNode, nullptr, GetModuleHandle(NULL), nullptr);
				SetWindowLongPtr(m_rows[i].sd->hwnd, GWLP_USERDATA, (LONG_PTR)m_rows[i].sd);
			}
		}

		void __stdcall SetCol(int i, WidgetSize s, bool sp, WidgetSize min) override {
			m_cols[i].size = s; m_cols[i].splitter = sp; m_cols[i].min = min;
			if (sp && !m_cols[i].sd) {
				m_cols[i].sd = new SplitterData{ nullptr, true, i, this };
				WNDCLASSW wc = { 0 }; wc.lpfnWndProc = SplitterWndProc; wc.lpszClassName = L"ChronoSplit";
				if (!GetClassInfoW(GetModuleHandle(NULL), wc.lpszClassName, &wc)) RegisterClassW(&wc);
				m_cols[i].sd->hwnd = CreateWindowW(L"ChronoSplit", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_parentNode, nullptr, GetModuleHandle(NULL), nullptr);
				SetWindowLongPtr(m_cols[i].sd->hwnd, GWLP_USERDATA, (LONG_PTR)m_cols[i].sd);
			}
		}

		void __stdcall SetCellName(int row, int col, const char* name) {
			named_cells[std::string(name)] = std::make_pair(row, col);
		}
		ICell* __stdcall GetCell(const char* name) {
			std::string n(name);
			if (named_cells.count(n) == 0) {
				return nullptr;
			}
			return GetCell(named_cells[n].first, named_cells[n].second);
		}

		ICell* __stdcall GetCell(int r, int c) override {
			ICell* cell = m_cells[r * m_cCount + c];

			if (cell && cell->GetParentNode() == nullptr) {
				cell->SetParentNode((ContextNodeImpl*)this);
			}

			return cell;
		}

		void OnSplitter(SplitterData* sd, LPARAM lp) {
			POINT pt; GetCursorPos(&pt);
			ScreenToClient(m_parentNode, &pt);
			UINT dpiVal = GetDpiForWindow(m_parentNode);
			float dpi = dpiVal / 96.0f;

			DWORD pStyle = GetWindowLongW(m_parentNode, GWL_STYLE);
			int sbWidth = (pStyle & WS_VSCROLL) ? GetSystemMetricsForDpi(SM_CXVSCROLL, dpiVal) : 0;
			int sbHeight = (pStyle & WS_HSCROLL) ? GetSystemMetricsForDpi(SM_CYHSCROLL, dpiVal) : 0;

			int sSize = Scale(m_parentNode, SPLITTER_SIZE);

			if (sd->isVert) {
				int layoutWidth = (m_lastRect.right - m_lastRect.left) - sbWidth;
				int totalRightMinWidth = 0;
				for (int i = sd->index + 1; i < m_cCount; ++i) {
					int minPx = (m_cols[i].min.unit == SizeUnit::Percent) ? (int)(layoutWidth * m_cols[i].min.value / 100.0f) : Scale(m_parentNode, (int)m_cols[i].min.value);
					totalRightMinWidth += minPx;
					if (m_cols[i].splitter) totalRightMinWidth += sSize;
				}

				int minCurrentWidth = (m_cols[sd->index].min.unit == SizeUnit::Percent) ? (int)(layoutWidth * m_cols[sd->index].min.value / 100.0f) : Scale(m_parentNode, (int)m_cols[sd->index].min.value);
				int newWidth = pt.x - m_cols[sd->index].pos;
				newWidth = (std::max)(newWidth, minCurrentWidth);
				newWidth = (std::min)(newWidth, (layoutWidth - totalRightMinWidth) - m_cols[sd->index].pos);
				m_cols[sd->index].size = WidgetSize::Fixed(newWidth / dpi);
			}
			else {
				int layoutHeight = (m_lastRect.bottom - m_lastRect.top) - sbHeight;
				int totalBottomMinHeight = 0;
				for (int i = sd->index + 1; i < m_rCount; ++i) {
					int minPx = (m_rows[i].min.unit == SizeUnit::Percent) ? (int)(layoutHeight * m_rows[i].min.value / 100.0f) : Scale(m_parentNode, (int)m_rows[i].min.value);
					totalBottomMinHeight += minPx;
					if (m_rows[i].splitter) totalBottomMinHeight += sSize;
				}

				int minCurrentHeight = (m_rows[sd->index].min.unit == SizeUnit::Percent) ? (int)(layoutHeight * m_rows[sd->index].min.value / 100.0f) : Scale(m_parentNode, (int)m_rows[sd->index].min.value);
				int newHeight = pt.y - m_rows[sd->index].pos;
				newHeight = (std::max)(newHeight, minCurrentHeight);
				newHeight = (std::min)(newHeight, (layoutHeight - totalBottomMinHeight) - m_rows[sd->index].pos);
				m_rows[sd->index].size = WidgetSize::Fixed(newHeight / dpi);
			}
			Arrange(m_lastRect.left, m_lastRect.top, m_lastRect.right - m_lastRect.left, m_lastRect.bottom - m_lastRect.top);
		}

		void __stdcall Arrange(int x, int y, int w, int h) override {
			m_lastRect = { x, y, x + w, y + h };

			auto solve = [&](std::vector<DimPlan>& dims, int total, int offset) {
				int avail = total; float fillWeights = 0; int sSize = Scale(m_parentNode, SPLITTER_SIZE);
				for (auto& d : dims) {
					if (d.splitter) avail -= sSize;
					int minPx = (d.min.unit == SizeUnit::Percent) ? (int)(total * d.min.value / 100.0f) : Scale(m_parentNode, (int)d.min.value);
					if (d.size.unit == SizeUnit::Pixels) d.actual = (std::max)(Scale(m_parentNode, (int)d.size.value), minPx);
					else if (d.size.unit == SizeUnit::System) d.actual = GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(m_parentNode));
					else if (d.size.unit == SizeUnit::Percent) d.actual = (std::max)((int)(total * d.size.value / 100.0f), minPx);
					else { d.actual = 0; fillWeights += d.size.value; continue; }
					avail -= d.actual;
				}
				if (fillWeights > 0) {
					for (auto& d : dims) if (d.size.unit == SizeUnit::Fill) {
						int minPx = (d.min.unit == SizeUnit::Percent) ? (int)(total * d.min.value / 100.0f) : Scale(m_parentNode, (int)d.min.value);
						d.actual = (std::max)((avail > 0) ? (int)(avail * d.size.value / fillWeights) : 0, minPx);
					}
				}
				int cur = offset; for (auto& d : dims) { d.pos = cur; cur += d.actual + (d.splitter ? sSize : 0); }
			};

			solve(m_rows, h, y);
			solve(m_cols, w, x);

			if (m_rCount > 0) {
				WCHAR cn[64];
				GetClassNameW(m_parentNode, cn, 64);
				if (wcscmp(cn, L"ChronoMain") == 0) {
					SetPropW(m_parentNode, kPropTitleRowHeight, (HANDLE)(INT_PTR)m_rows[0].actual);
				}
			}

			HDWP hdwp = BeginDeferWindowPos(m_rCount * m_cCount + m_rCount + m_cCount);

			for (int r = 0; r < m_rCount; ++r) {
				for (int c = 0; c < m_cCount; ++c) {
					CellImpl* cell = m_cells[r * m_cCount + c];
					int cx = m_cols[c].pos, cy = m_rows[r].pos, cw = m_cols[c].actual, ch = m_rows[r].actual;
					
					hdwp = DeferWindowPos(hdwp, cell->m_hwnd, NULL, cx, cy, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
				}
			}

			int sSize = Scale(m_parentNode, SPLITTER_SIZE);

			for (int c = 0; c < m_cCount; ++c) {
				if (m_cols[c].splitter && m_cols[c].sd && m_cols[c].sd->hwnd) {
					int sx = m_cols[c].pos + m_cols[c].actual;
					hdwp = DeferWindowPos(hdwp, m_cols[c].sd->hwnd, HWND_TOP,
						sx, y, sSize, h,
						SWP_NOACTIVATE | SWP_NOCOPYBITS);
				}
			}

			for (int r = 0; r < m_rCount; ++r) {
				if (m_rows[r].splitter && m_rows[r].sd && m_rows[r].sd->hwnd) {
					int sy = m_rows[r].pos + m_rows[r].actual;
					hdwp = DeferWindowPos(hdwp, m_rows[r].sd->hwnd, HWND_TOP,
						x, sy, w, sSize,
						SWP_NOACTIVATE | SWP_NOCOPYBITS);
				}
			}

			EndDeferWindowPos(hdwp);
			RedrawWindow(m_parentNode, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_ERASE);
		}

		int GetRowHeight(int index) { return (index >= 0 && index < (int)m_rows.size()) ? m_rows[index].actual : 0; }

		void __stdcall CollapseColumn(int index) override {
			// Validation
			if (index < 0 || index >= m_cCount) return;

			DimPlan& col = m_cols[index];

			// If already collapsed, do nothing to prevent overwriting savedSize with the min size
			if (col.isCollapsed) return;

			// 1. Save the current requested size
			col.savedSize = col.size;

			// 2. Set the current size to the minimum defined size
			// The Arrange function uses 'col.size' to calculate 'col.actual', 
			// effectively forcing the column to be exactly its minimum width.
			col.size = col.min;

			// 3. Update state
			col.isCollapsed = true;

			// 4. Force a re-layout immediately
			RefreshLayout();
		}

		void __stdcall RestoreColumn(int index) override {
			// Validation
			if (index < 0 || index >= m_cCount) return;

			DimPlan& col = m_cols[index];

			// If not collapsed, nothing to restore
			if (!col.isCollapsed) return;

			// 1. Restore the original requested size
			col.size = col.savedSize;

			// 2. Update state
			col.isCollapsed = false;

			// 3. Force a re-layout immediately
			RefreshLayout();
		}

		bool __stdcall IsColumnCollapsed(int index) override {
			if (index < 0 || index >= m_cCount) return false;
			return m_cols[index].isCollapsed;
		}

		private:
			// Helper to trigger Arrange using the last known coordinates
			void RefreshLayout() {
				Arrange(m_lastRect.left, m_lastRect.top,
					m_lastRect.right - m_lastRect.left,
					m_lastRect.bottom - m_lastRect.top);
			}
	};

	// --- Container ---
	struct StyleRule {
		std::map<std::string, std::string> props;
	};

	class ContainerImpl : public IContainer, public ContextNodeImpl {
		HWND m_hwnd;
		HWND m_parent;
		std::map<std::string, StyleRule> m_cssRules;
		LayoutImpl* m_root = nullptr;
		std::vector<IWidget*> m_tabOrder;
		int m_focusIdx = -1;
		bool m_running = true;

		bool m_customTitleBar = false;
		bool m_isPopup = false; // New flag for popup mode

		std::vector<IWidget*> m_registeredWidgets;

		IWidget* m_overlay = nullptr;

	public:
		// Forward IContextNode methods explicitly
		virtual void __stdcall SetParentNode(IContextNode* parent) override { ContextNodeImpl::SetParentNode(parent); }
		virtual IContextNode* __stdcall GetParentNode() override { return ContextNodeImpl::GetParentNode(); }
		virtual IContextNode* __stdcall GetContextNode() override { return ContextNodeImpl::GetContextNode(); }
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override { return ContextNodeImpl::GetProperty(key, def); }
		virtual IContainer* __stdcall SetProperty(const char* key, const char* value) override { ContextNodeImpl::SetProperty(key, value); return this; }
		virtual COLORREF GetColor(const char* key, COLORREF defaultColor = RGB(0, 0, 0)) override { return ContextNodeImpl::GetColor(key, defaultColor); }
		virtual IContextNode* SetColor(const char* key, COLORREF color) override { return SetColor(key, color); };
		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active) override { return ContextNodeImpl::GetStyle(_prop, _def, _classid, _subclass, selected, enabled, hovered, active); }

		~ContainerImpl() {
			// 1. Move the widgets to a local vector to avoid iterator invalidation.
			//    When widgets are destroyed, they might try to call UnregisterWidget(),
			//    which modifies m_registeredWidgets. By swapping, we prevent the loop from breaking.
			std::vector<IWidget*> toDestroy;
			m_registeredWidgets.swap(toDestroy);

			// 2. Now it is safe to iterate and destroy
			for (IWidget* w : toDestroy) {
				// Check if the widget is still tracked by the factory before calling method
				ChronoUI::WidgetFactory::Destroy(w);
			}

			// 3. Cleanup global resources
			ChronoControllerImpl::Instance().Shutdown();
		}

		// Updated Constructor
		ContainerImpl(HWND parent, const wchar_t* t, int w, int h, bool customTitleBar, bool isPopup = false)
			: m_customTitleBar(customTitleBar), m_isPopup(isPopup)
		{
			ChronoControllerImpl::Instance().Initialize();
			this->SetParentNode(&ChronoControllerImpl::Instance());

			SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

			WNDCLASSW wc = { 0 };
			wc.lpfnWndProc = WndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
			// Use a distinct class name for popups if needed, but shared is fine
			wc.lpszClassName = L"ChronoMain";
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);
			if (!GetClassInfoW(wc.hInstance, wc.lpszClassName, &wc)) RegisterClassW(&wc);

			DWORD style = 0;
			DWORD exStyle = 0;
			int x = CW_USEDEFAULT;
			int y = CW_USEDEFAULT;

			// Enable Double-Buffered composition for all children to stop flickering
			exStyle = WS_EX_COMPOSITED;

			if (m_isPopup) {
				// Popup Style: No caption, thin border, popup type
				style = WS_POPUP | WS_CLIPCHILDREN | WS_BORDER;
				exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
				// Popups shouldn't have custom title bar logic
				m_customTitleBar = false;
				// Default position usually set by caller later, but 0,0 is safer than CW_USEDEFAULT for popups
				x = 0; y = 0;
			}
			else {
				// Standard Window
				style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
				exStyle = 0;
			}

			m_parent = parent;
			m_hwnd = CreateWindowExW(
				exStyle, L"ChronoMain", t, style,
				x, y, w, h,
				parent, nullptr, GetModuleHandle(NULL), this);

			if (m_customTitleBar) {
				SetPropW(m_hwnd, kPropCustomTitleBar, (HANDLE)1);
				SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
			}
		}

		virtual IWidget* __stdcall SetOverlay(IWidget* w) override {
			// 1. Remove existing overlay
			if (m_overlay) {
				// If we are swapping overlays, restore the Z-order or cleanup
				UnregisterWidget(m_overlay);
				WidgetFactory::Destroy(m_overlay);
				m_overlay = nullptr;
			}

			if (!w) return w;

			m_overlay = w;

			// 2. Setup Parent
			if (!m_overlay->GetHWND()) {
				m_overlay->Create(m_hwnd);
			}
			else {
				SetParent(m_overlay->GetHWND(), m_hwnd);
			}

			// 3. APPLY STYLES TO FIX FLICKER / INPUT
			HWND hOverlay = m_overlay->GetHWND();
			LONG_PTR exStyle = GetWindowLongPtr(hOverlay, GWL_EXSTYLE);

			// WS_EX_TRANSPARENT: Ensures underlying windows paint first.
			// WS_EX_LAYERED: Often needed for proper alpha blending of D2D on child windows.
			SetWindowLongPtr(hOverlay, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);

			// Set Layered attributes (required if adding WS_EX_LAYERED). 
			// 255 = Opaque, but allow per-pixel alpha from D2D.
			// This setup allows the D2D alpha channel to work against the background.
			// Note: LWA_COLORKEY is not used here, we rely on D2D alpha.
			// Setting LWA_ALPHA with 255 makes the window capable of alpha blending.
			SetLayeredWindowAttributes(hOverlay, 0, 255, LWA_ALPHA);

			m_overlay->SetParentNode((ContextNodeImpl*)this);
			RegisterWidget(m_overlay);

			// 4. Position and Z-Order
			RECT r;
			GetClientRect(m_hwnd, &r);
			m_overlay->SetBounds(0, 0, r.right, r.bottom);

			// HWND_TOP puts it visually on top.
			SetWindowPos(hOverlay, HWND_TOP, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

			InvalidateRect(m_hwnd, NULL, FALSE);

			return w;
		}

		virtual IWidget* __stdcall GetOverlay() override {
			return m_overlay;
		}

		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
			if (msg == WM_NCCREATE) {
				SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)((LPCREATESTRUCT)lp)->lpCreateParams);
				return DefWindowProc(hwnd, msg, wp, lp);
			}

			auto* self = (ContainerImpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			if (!self) return DefWindowProc(hwnd, msg, wp, lp);

			switch (msg) {
				// --- Popup Logic Start ---
			case WM_ACTIVATE:
				if (self->m_isPopup) {
					// If the popup loses activation (clicked another app or window), close it.
					if (LOWORD(wp) == WA_INACTIVE) {
						ShowWindow(hwnd, SW_HIDE);
						self->m_running = false;
						//PostMessage(hwnd, WM_CLOSE, 0, 0);
					}
				}
				break;

			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_NCLBUTTONDOWN:
				if (self->m_isPopup) {
					// Check if click is inside the client area
					POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

					// If it's a non-client message, the coords are screen relative, convert to client
					if (msg == WM_NCLBUTTONDOWN) {
						ScreenToClient(hwnd, &pt);
					}

					RECT rc;
					GetClientRect(hwnd, &rc);

					if (!PtInRect(&rc, pt)) {
						// Clicked outside -> Close
						ShowWindow(hwnd, SW_HIDE);
						self->m_running = false;
						return 0; // Consume the click
					}
				}
				break;
				// --- Popup Logic End ---

			case WM_CHRONO_SPLIT: {
				auto* sd = (SplitterData*)wp;
				if (sd && sd->owner) {
					((LayoutImpl*)sd->owner)->OnSplitter(sd, lp);
				}
				return 0;
			}
			case WM_PAINT: {
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hwnd, &ps);
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_ERASEBKGND: {
				/*
				HDC hdc = (HDC)wp;
				RECT r;
				GetClientRect(hwnd, &r);
				COLORREF bg = self ? self->GetColor("background-color", RGB(64, 64, 64)) : RGB(64, 64, 64);
				HBRUSH hbr = CreateSolidBrush(bg);
				FillRect(hdc, &r, hbr);
				DeleteObject(hbr);
				*/
				return 1;
			}
			case WM_NCCALCSIZE:
				if (self->m_customTitleBar && wp) {
					if (IsZoomed(hwnd)) {
						auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
						UINT dpi = GetDpiForWindow(hwnd);
						int frame = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
						p->rgrc[0].top += frame;
						p->rgrc[0].left += frame;
						p->rgrc[0].right -= frame;
						p->rgrc[0].bottom -= frame;
					}
					else {
						auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
						p->rgrc[0].top += 2;
						p->rgrc[0].left += 2;
						p->rgrc[0].right -= 2;
						p->rgrc[0].bottom -= 2;
					}
					return 0;
				}
				break;

			case WM_NCHITTEST: {
				if (!self->m_customTitleBar)
					break;

				POINT ptMouse = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
				RECT rcWindow;
				GetWindowRect(hwnd, &rcWindow);

				UINT dpi = GetDpiForWindow(hwnd);
				int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
				int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

				if (!IsZoomed(hwnd)) {
					if (ptMouse.y < rcWindow.top + frameY) {
						if (ptMouse.x < rcWindow.left + frameX) return HTTOPLEFT;
						if (ptMouse.x >= rcWindow.right - frameX) return HTTOPRIGHT;
						return HTTOP;
					}
					if (ptMouse.y >= rcWindow.bottom - frameY) {
						if (ptMouse.x < rcWindow.left + frameX) return HTBOTTOMLEFT;
						if (ptMouse.x >= rcWindow.right - frameX) return HTBOTTOMRIGHT;
						return HTBOTTOM;
					}
					if (ptMouse.x < rcWindow.left + frameX) return HTLEFT;
					if (ptMouse.x >= rcWindow.right - frameX) return HTRIGHT;
				}

				POINT ptClient = ptMouse;
				ScreenToClient(hwnd, &ptClient);

				int titleH = (int)(INT_PTR)GetPropW(hwnd, kPropTitleRowHeight);
				if (ptClient.y >= 0 && ptClient.y <= titleH) {
					HWND child = ChildWindowFromPointEx(hwnd, ptClient, CWP_SKIPINVISIBLE);
					if (child == hwnd || child == NULL)
						return HTCAPTION;

					WCHAR cn[64];
					GetClassNameW(child, cn, 64);
					if (wcscmp(cn, L"ChronoMain") == 0 ||
						wcscmp(cn, L"ChronoCell") == 0 ||
						wcscmp(cn, L"StaticText") == 0
						) {
						return HTCAPTION;
					}
				}
				return HTCLIENT;
			}

			case WM_SIZE:
				if (self->m_root) 
					self->m_root->Arrange(0, 0, LOWORD(lp), HIWORD(lp));
				if (self->m_overlay) {
					// Force overlay to fill the entire client area
					self->m_overlay->SetBounds(0, 0, LOWORD(lp), HIWORD(lp));

					// Ensure it stays on top of the Z-order after a resize
					BringWindowToTop(self->m_overlay->GetHWND());
				}
				return 0;

			case WM_DESTROY:
				RemovePropW(hwnd, kPropCustomTitleBar);
				RemovePropW(hwnd, kPropTitleRowHeight);
				PostQuitMessage(0);
				return 0;
			}
			return DefWindowProc(hwnd, msg, wp, lp);
		}

		virtual void RegisterWidget(IWidget* w) override {
			if (std::find(m_registeredWidgets.begin(), m_registeredWidgets.end(), w) == m_registeredWidgets.end()) {
				m_registeredWidgets.push_back(w);
				m_tabOrder.push_back(w);
			}
		}

		virtual void UnregisterWidget(IWidget* widget) override {
#ifdef _DEBUG
			// --- Logging Start ---
			std::string wId = "unknown";
			std::string wType = "unknown";
			try {
				const char* propId = widget->GetProperty("id");
				if (propId) wId = propId;
				const char* propType = widget->GetControlName();
				if (propType) wType = propType;

				char buffer[1024];
				sprintf_s(buffer, "[UnregisterWidget] Destroying Ptr: %p | Type: %s | ID: %s | ",
					widget, wType.c_str(), wId.c_str());
				OutputDebugStringA(buffer);
			}
			catch (...) {}
			// --- Logging End ---
#endif

			auto it = std::find(m_registeredWidgets.begin(), m_registeredWidgets.end(), widget);
			if (it != m_registeredWidgets.end()) 
				m_registeredWidgets.erase(it);
			auto itTab = std::find(m_tabOrder.begin(), m_tabOrder.end(), widget);
			if (itTab != m_tabOrder.end()) 
				m_tabOrder.erase(itTab);
		}

		void CycleFocus(bool forward) {
			if (m_tabOrder.empty()) return;
			m_focusIdx = forward ? m_focusIdx + 1 : m_focusIdx - 1;
			if (m_focusIdx >= (int)m_tabOrder.size()) m_focusIdx = 0;
			if (m_focusIdx < 0) m_focusIdx = (int)m_tabOrder.size() - 1;
			HWND target = m_tabOrder[m_focusIdx]->GetHWND();
			if (target) SetFocus(target);
		}

		ILayout* __stdcall CreateRootLayout(int r, int c) override {
			if (m_root) delete m_root;
			m_root = new LayoutImpl(this, GetHWND(), r, c);
			m_root->SetParentNode((ContextNodeImpl*)this);
			return m_root;
		}

		void __stdcall ShowPopup(int w, int h) override {
			if (!m_isPopup) {
				Show();
				return; // Only for popup mode
			}
			const int POPUP_W = w;
			const int POPUP_H = h;

			POINT pt;
			GetCursorPos(&pt);  // or compute from cell and ClientToScreen

			// Initial desired position (slightly offset from cursor)
			int x = pt.x + 10;
			int y = pt.y + 10;

			// Get work area of the monitor (handles taskbar, multi-monitor)
			RECT rcWork;
			SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

			// Clamp horizontally
			if (x + POPUP_W > rcWork.right)
				x = rcWork.right - POPUP_W;
			if (x < rcWork.left)
				x = rcWork.left;

			// Clamp vertically
			if (y + POPUP_H > rcWork.bottom)
				y = rcWork.bottom - POPUP_H;
			if (y < rcWork.top)
				y = rcWork.top;

			SetWindowPos(
				m_hwnd,
				HWND_TOPMOST,
				x,
				y,
				POPUP_W,
				POPUP_H,
				SWP_SHOWWINDOW
			);

			// Important for popups: Set capture to detect clicks outside
			//SetCapture(m_hwnd);
			SetFocus(m_hwnd);

			m_root->Arrange(0, 0, POPUP_W, POPUP_H);
		}

		void __stdcall Show() override {
			if (m_isPopup) {
				ShowPopup(640, 480);
				return ;
			}

			ShowWindow(m_hwnd, SW_SHOW);
			UpdateWindow(m_hwnd);
		}

		void __stdcall Maximize() override { ShowWindow(m_hwnd, SW_SHOWMAXIMIZED); }

		void __stdcall Redraw() override {
			RedrawWindow(m_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_ERASE);
		}

		HWND __stdcall GetHWND() override { return m_hwnd; };

		void __stdcall Close() { 
			PostMessage(m_hwnd, WM_CLOSE, 0, 0); 
		}

		void __stdcall CenterToParent() override {
			// (Existing logic)
			if (!m_hwnd || !IsWindow(m_hwnd)) return;
			HWND hParent = GetParent(m_hwnd);
			if (hParent == NULL) hParent = GetWindow(m_hwnd, GW_OWNER);
			RECT rcParent;
			if (hParent != NULL) GetWindowRect(hParent, &rcParent);
			else SystemParametersInfo(SPI_GETWORKAREA, 0, &rcParent, 0);
			RECT rcWindow; GetWindowRect(m_hwnd, &rcWindow);
			int w = rcWindow.right - rcWindow.left;
			int h = rcWindow.bottom - rcWindow.top;
			int parentW = rcParent.right - rcParent.left;
			int parentH = rcParent.bottom - rcParent.top;
			int x = rcParent.left + (parentW - w) / 2;
			int y = rcParent.top + (parentH - h) / 2;
			SetWindowPos(m_hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}

		void __stdcall RunMessageLoop() override {
			MSG msg;
			while (m_running && GetMessageW(&msg, nullptr, 0, 0)) {
				/*
				if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
					bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
					CycleFocus(!shiftPressed);
					continue;
				}
				*/

				// For popups using SetCapture, we need to handle special cases where capture is lost
				// or ensure input is processed correctly.

				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}

		void __stdcall DoModal() override {
			if (m_parent)
				EnableWindow(m_parent, FALSE);

			CenterToParent();
			Show();

			RunMessageLoop();

			if (m_parent) {
				BringWindowToTop(m_parent);
				EnableWindow(m_parent, TRUE);
			}
		}
	};

	IWidget* __stdcall CellImpl::CreateOverflowButton() {
		auto* container = GetParentContainer();
		if (!container) return nullptr;
		const char* threeDots = "iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAQAAAD9CzEMAAAAIGNIUk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAACYktHRAAAqo0jMgAAAAlwSFlzAAACWAAAAlgAm8a+swAAAAd0SU1FB+oBAg0EGeS9jegAAACwSURBVFjD7dMxDoIwFIDhX0kHdwe4CF7N+9hLeAUCawnsTpYTOOCCDQUeaXRweR8Tfx7t8AIopZRSKsFhsxrOgOf1YxeUWBwOy0XoZdK8eHzHOD397KiSPvQu6tvzAoMN4yMjN8xmt0L/zIty2uiDlhyAAhd1R7E7HxzT1vK95QUDdfReMwDgaaLe4Hfnd8yXJi0zpU+y1QUPKk4YPHeuVKv+FPpyfvKXH00ppZRSK28PAIB2j3bhOgAAACV0RVh0ZGF0ZTpjcmVhdGUAMjAyNi0wMS0wMlQxMjo1ODo0MSswMDowMJPBJIwAAAAldEVYdGRhdGU6bW9kaWZ5ADIwMjYtMDEtMDJUMTI6NTg6NDErMDA6MDDinJwwAAAAKHRFWHRkYXRlOnRpbWVzdGFtcAAyMDI2LTAxLTAyVDEzOjA0OjI1KzAwOjAwMVQFMgAAAABJRU5ErkJggg==";
		auto* btn = WidgetFactory::Create("cw.Button.dll");

		if (!btn)
			return nullptr;

		btn->Create(m_hwnd);
		btn->SetParentNode((ContextNodeImpl*)this);
		btn->SetProperty("image-base-64", threeDots);
		btn->SetProperty("border-width", "0");
		btn->SetProperty("border-width:hover", "1");

		// Store reference to parent for event handler
		CellImpl* self = this;
		btn->addEventHandler("onClick", [self](auto...) {
			self->ShowOverflowPopup();
		});

		container->RegisterWidget(btn);

		return btn;
	}

	IWidget* __stdcall CellImpl::GetWidget(int idx) {
		if (idx < 0 || idx >= static_cast<int>(widgets.size())) {
			return nullptr;
		}
		return widgets[idx];
	}

	void __stdcall CellImpl::RemoveWidget(IWidget* w) {
		auto it = std::find(widgets.begin(), widgets.end(), w);
		if (it != widgets.end()) {
			ShowWindow(w->GetHWND(), SW_HIDE);
			widgets.erase(it);

			IContainer* container = GetParentContainer();
			if (container) {
				container->UnregisterWidget(w);
			}

			//UpdateWidgets();
		}
	}

	struct PluginInfo {
		HMODULE handle;
		int refCount;
	};

	static std::map<std::string, PluginInfo> g_Plugins;
	static std::map<IWidget*, std::string> g_WidgetToPluginMap;
	static std::recursive_mutex g_FactoryMutex;

	IWidget* WidgetFactory::Create(const char* name) {
		//std::lock_guard<std::recursive_mutex> lock(g_FactoryMutex);
		std::string nameStr = name;

		if (g_Plugins.find(nameStr) == g_Plugins.end()) {
			std::string dllName = nameStr;
			HMODULE h = LoadLibraryA(dllName.c_str());
			if (!h) return nullptr;
			g_Plugins[nameStr] = { h, 0 };
		}

		PluginInfo& info = g_Plugins[nameStr];

		typedef IWidget* (__stdcall* PFN_CREATE)();
		auto pfn = (PFN_CREATE)GetProcAddress(info.handle, "CreateInstance");
		if (!pfn) return nullptr;

		IWidget* widget = pfn();
		if (widget) {
			info.refCount++;
			g_WidgetToPluginMap[widget] = nameStr;
		}
#ifdef _DEBUG
		widget->SetProperty("dll", name);
#endif

		return widget;
	}

	void WidgetFactory::Destroy(IWidget* widget) {
		if (!widget) 
			return;

		std::lock_guard<std::recursive_mutex> lock(g_FactoryMutex);

		// 1. Check if the widget exists in our registry FIRST.
		//    Comparing the pointer in the map is safe even if the object is dead.
		auto it = g_WidgetToPluginMap.find(widget);

		if (it != g_WidgetToPluginMap.end()) {
			// IT IS SAFE TO ACCESS WIDGET NOW (Assuming only Factory destroys widgets)
			std::string pluginName = it->second;
#ifdef _DEBUG
			// --- Logging Start ---
			std::string wId = "unknown";
			std::string wType = "unknown";
			try {
				const char* propId = widget->GetProperty("id");
				if (propId) wId = propId;
				const char* propType = widget->GetControlName();
				if (propType) wType = propType;
			}
			catch (...) {}

			char buffer[1024];
			sprintf_s(buffer, "[WidgetFactory] Destroying Ptr: %p | Type: %s | ID: %s | Plugin: %s ... ",
				widget, wType.c_str(), wId.c_str(), pluginName.c_str());
			OutputDebugStringA(buffer);

			OutputDebugStringA("Done.\n");
#endif
			// --- Logging End ---

			// 2. Destroy the object
			widget->Destroy();

			// 3. Cleanup Registry
			PluginInfo& info = g_Plugins[pluginName];
			info.refCount--;
			g_WidgetToPluginMap.erase(it);

			if (info.refCount <= 0) {
				// DO NOT UNLOAD DLL HERE to prevent shutdown crashes
				// FreeLibrary(info.handle);
				// g_Plugins.erase(pluginName);
			}
		}
		else {
			// 4. Handle Double-Free gracefully
			// If we get here, the widget was likely already destroyed (e.g. by its parent)
			// but was still in the toDestroy list. This is harmless if we just ignore it.
			char buffer[512];
			sprintf_s(buffer, "[WidgetFactory] SKIPPING: Widget %p not found in map (Already destroyed?)\n", widget);
			OutputDebugStringA(buffer);
		}
	}

	void WidgetFactory::DestroyAll() {
		std::lock_guard<std::recursive_mutex> lock(g_FactoryMutex);
		std::vector<IWidget*> active;
		for (auto const& [w, name] : g_WidgetToPluginMap) 
			active.push_back(w);

		for (auto* w : active)
			Destroy(w);
	}

	ILayout* __stdcall CellImpl::GetNestedLayout()
	{
		return this->nested;
	}

	IWidget* __stdcall CellImpl::AddWidget(IWidget* w) {
		if (!w) return nullptr;

		w->Create(m_hwnd);
		w->SetParentNode((ContextNodeImpl*)this);
		widgets.push_back(w);

		// Resolve the correct container dynamically rather than using a global variable
		IContainer* container = GetParentContainer();
		if (container) {
			container->RegisterWidget(w);
		}

		return w;
	}

	void __stdcall CellImpl::DetachWidget(IWidget * w) {
		auto it = std::find(widgets.begin(), widgets.end(), w);
		if (it != widgets.end()) {
			widgets.erase(it);
			IContainer* container = GetParentContainer();
			if (container) {
				container->UnregisterWidget(w);
			}
		}
	}

	// Helper: Takes an existing widget from another cell and moves it here
	void __stdcall CellImpl::AdoptWidget(IWidget* w) {
		if (!w) return;

		HWND hWidget = w->GetHWND();
		HWND hOldParent = GetParent(hWidget);
		if (hOldParent && hOldParent != m_hwnd) {
			// We get the Cell pointer from the window user data
			CellImpl* oldCell = (CellImpl*)GetWindowLongPtr(hOldParent, GWLP_USERDATA);
			if (oldCell) {
				oldCell->DetachWidget(w);
			}
		}

		HWND oldParent = GetParent(w->GetHWND());
		HWND newParent = this->m_hwnd;
		if (oldParent != newParent) {
			SetParent(w->GetHWND(), newParent);

			// Force style refresh after reparent
			SetWindowLong(w->GetHWND(), GWL_STYLE,
				GetWindowLong(w->GetHWND(), GWL_STYLE) | WS_CHILD);

			SetWindowPos(
				w->GetHWND(),
				nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
			);
		}
		widgets.push_back(w);
		w->SetParentNode((ContextNodeImpl*)this);

		// 5. Register with the NEW Container
		IContainer* container = GetParentContainer();
		if (container) {
			container->RegisterWidget(w);
		}
		ShowWindow(hWidget, SW_SHOW);
	}

	void CellImpl::Clean()
	{
		IContainer* container = GetParentContainer();

		if (nested) {
			delete nested;
			nested = nullptr;
		}
		// 3. Destroy Managed Widgets
		if (!widgets.empty()) {
			// We cannot call GetParentContainer() reliably in a destructor 
			// if the window is already being destroyed, but we must try to 
			// prevent leaks if the app is running.

			// Note: If the entire Container is dying, it clears its own lists, 
			// so explicit unregistering here is mostly for runtime layout changes.

			// Create a local copy to iterate safely
			std::vector<IWidget*> toDestroy = widgets;
			widgets.clear();

			for (auto* w : toDestroy) {
				if (container) {
					container->UnregisterWidget(w);
				}
				WidgetFactory::Destroy(w);
			}
		}
		if (overflowButton) {
			WidgetFactory::Destroy(overflowButton);
			overflowButton = nullptr;
		}
	}

	CellImpl::~CellImpl() {
		Clean();
	}

	ILayout* __stdcall CellImpl::CreateLayout(int r, int c)
	{
		Clean();

		auto* subLayout = new LayoutImpl(GetParentContainer(), m_hwnd, r, c);
		subLayout->SetParentNode((ContextNodeImpl*)this);
		this->nested = subLayout;

		for (int x = 0; x < c; x++)
			for (int y = 0; y < r; y++)
				subLayout->GetCell(y, x); // Pre-create cells

		return subLayout;
	}

	// =========================================================
	// --- Public API Implementation for ChronoController ---
	// =========================================================
	IContextNode* __stdcall ChronoController::Instance() {
		return &ChronoControllerImpl::Instance();
	}

	// =========================================================
	// --- PanelImpl (The Child Container) ---
	// =========================================================
	// =========================================================
// --- PanelImpl (Fixed) ---
// =========================================================
	class PanelImpl : public WidgetImpl, public IPanel {
		LayoutImpl* m_root = nullptr;

	public:
		PanelImpl(IContainer* _parentContainer) : parentContainer(_parentContainer) {
			SetProperty("subclass", "panel");
		}

		~PanelImpl() {
			if (m_root) {
				delete m_root;
				m_root = nullptr;
			}
		}

		// -------------------------------------------------------------------------
		// RESOLUTION OF AMBIGUITY
		// We must implement the IPanel -> IWidget -> IContextNode chain
		// by forwarding to the WidgetImpl -> IWidget -> IContextNode chain.
		// -------------------------------------------------------------------------
		virtual bool __stdcall PerformValidation(const char* newValue) override {
			return WidgetImpl::PerformValidation(newValue);
		}
		virtual void __stdcall RegisterValidator(ChronoValidationCallback callback, void* pContext, ChronoValidationCleanup cleanup) override {
			// Forward the call to the implementation in WidgetImpl
			WidgetImpl::RegisterValidator(callback, pContext, cleanup);
		}
		// --- IContextNode Overrides ---
		virtual void __stdcall SetParentNode(IContextNode* parent) override { WidgetImpl::SetParentNode(parent); }
		virtual IContextNode* __stdcall GetParentNode() override { return WidgetImpl::GetParentNode(); }
		virtual IContextNode* __stdcall GetContextNode() override { return ContextNodeImpl::GetContextNode(); }
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override { return WidgetImpl::GetProperty(key, def); }

		// Explicit forwarding for SetProperty to return 'this' (as the interface expected by the caller)
		virtual IWidget* __stdcall SetProperty(const char* key, const char* value) override {
			return WidgetImpl::SetProperty(key, value);			
		}

		virtual IWidget* SetColor(const char* key, COLORREF color) override {
			return WidgetImpl::SetColor(key, color);
		}

		virtual COLORREF GetColor(const char* key, COLORREF defaultColor) override {
			return WidgetImpl::GetColor(key, defaultColor);
		}

		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active) override {
			return WidgetImpl::GetStyle(_prop, _def, _classid, _subclass, selected, enabled, hovered, active);
		}

		// --- IWidget Overrides ---
		virtual const char* __stdcall GetControlName() override { return "Panel"; }
		virtual const char* __stdcall GetControlManifest() override { return "{}"; }

		virtual void __stdcall Create(HWND parent) override { WidgetImpl::Create(parent); }
		virtual void __stdcall SetBounds(int x, int y, int w, int h) override { WidgetImpl::SetBounds(x, y, w, h); }
		virtual void __stdcall OnFocus(bool f) override { WidgetImpl::OnFocus(f); }

		// Fix C2385: Explicitly call WidgetImpl::GetHWND
		virtual HWND __stdcall GetHWND() override { return WidgetImpl::GetHWND(); }

		virtual void __stdcall Destroy() override { WidgetImpl::Destroy(); }
		virtual void __stdcall SetWidgetHost(IWidget* host) override { WidgetImpl::SetWidgetHost(host); }

		virtual IWidget* __stdcall AddTimer(const char* eventName, int milliseconds) override {
			return WidgetImpl::AddTimer(eventName, milliseconds);
		}

		virtual IWidget* __stdcall AddOneShotTimer(const char* eventName, int milliseconds) override {
			return WidgetImpl::AddOneShotTimer(eventName, milliseconds);
		}

		virtual IWidget* __stdcall AddOverlay(IWidget* overlay) override {
			return WidgetImpl::AddOverlay(overlay);
		}

		virtual void __stdcall RemoveOverlay(IWidget* overlay) override {
			return WidgetImpl::RemoveOverlay(overlay);
		}

		virtual void OnChanged(void (*callback)(IWidget* target, void* context), void* context, void (*cleanup)(void*) = nullptr) override {
			// Forwarding to WidgetImpl
			WidgetImpl::OnChanged(callback, context, cleanup);
		}

		virtual IWidget* __stdcall RegisterEventHandler(const char* eventName, ChronoEventCallback callback, void* pContext, ChronoEventCleanup cleanup) override {
			return WidgetImpl::RegisterEventHandler(eventName, callback, pContext, cleanup);
		}

		virtual void __stdcall ClearEventHandlers() override {
			WidgetImpl::ClearEventHandlers();
		}

		virtual void __stdcall FireEvent(const char* eventName, const char* jsonPayload) override {
			WidgetImpl::FireEvent(eventName, jsonPayload);
		}

		// --- Drawing & Layout ---
		// In PanelImpl class
		virtual void OnDrawWidget(ID2D1RenderTarget* pRT) override {
			D2D1_SIZE_F size = pRT->GetSize();
			D2D1_RECT_F rect = D2D1::RectF(0.0f, 0.0f, size.width, size.height);
			WidgetImpl::DrawWidgetBackground(pRT, rect, false);
		}

		virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
			// 1. Resize Layout
			if (msg == WM_SIZE) {
				if (m_root) {
					m_root->Arrange(0, 0, LOWORD(lp), HIWORD(lp));
				}
			}
			// 2. Handle Splitters (bubbled up from LayoutImpl)
			else if (msg == WM_CHRONO_SPLIT) {
				SplitterData* sd = (SplitterData*)wp;
				if (sd && sd->owner) {
					((LayoutImpl*)sd->owner)->OnSplitter(sd, lp);
				}
				return 0;
			}

			return WidgetImpl::HandleMessage(msg, wp, lp);
		}

		IContainer* parentContainer = nullptr;
		IContainer* SetParentContainer(IContainer* _parentContainer) { parentContainer = _parentContainer; };
		IContainer* GetParentContainer() { return parentContainer; };

		// --- IPanel Specific ---
		virtual ILayout* __stdcall CreateLayout(int r, int c) override {
			if (m_root) delete m_root;
			m_root = new LayoutImpl(GetParentContainer(), GetHWND(), r, c); // Attach layout to this widget's HWND
			m_root->SetParentNode((ContextNodeImpl*)this);
			return m_root;
		}

		virtual ILayout* __stdcall GetLayout() override {
			return m_root;
		}
	};

	void __stdcall CellImpl::UpdateWidgets() 
	{
		if (!m_hwnd) return;

		RECT r;
		GetClientRect(m_hwnd, &r);

		// --- NESTED LAYOUT FIX START ---
		if (nested) {
			int contentW = r.right;
			int contentH = r.bottom;
			int offsetX = 0;
			int offsetY = 0;

			if (scrollEnabled) {
				int minW = 0, minH = 0;
				((LayoutImpl*)nested)->CalculateMinimumSize(minW, minH);
				//int minW = 12000, minH = 12000;

				bool isHoriz = (m_mode == StackMode::Horizontal);

				if (isHoriz) contentW = (std::max)((int)r.right, minW);
				else         contentH = (std::max)((int)r.bottom, minH);

				SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL };
				si.nMin = 0;
				si.nMax = (isHoriz ? contentW : contentH) - 1;
				si.nPage = isHoriz ? r.right : r.bottom;
				si.nPos = scrollPos;
				SetScrollInfo(m_hwnd, isHoriz ? SB_HORZ : SB_VERT, &si, TRUE);

				if (isHoriz) offsetX = -scrollPos;
				else         offsetY = -scrollPos;
			}
			nested->Arrange(offsetX, offsetY, contentW, contentH);
			return;
		}
		// --- NESTED LAYOUT FIX END ---

		if (widgets.empty()) {
			InvalidateRect(m_hwnd, NULL, TRUE);
			return;
		}

		// CommandBar mode implementation
		if (m_mode == StackMode::CommandBar) {
			const char* propOverflow = GetProperty("overflow");
			std::string soverflow = propOverflow ? propOverflow : "true";
			bool allowOverflow = (soverflow != "false");

			const char* propAlign = GetProperty("align-items");
			std::string align = propAlign ? propAlign : "normal";

			const char* propJustify = GetProperty("justify-content");
			std::string justify = propJustify ? propJustify : "start";

			int parentHeight = r.bottom;
			int parentWidth = r.right;

			std::vector<IWidget*> visibleWidgets;
			std::vector<IWidget*> overflowWidgets;

			MeasureCommandBarWidgets(parentWidth, visibleWidgets, overflowWidgets, allowOverflow);

			bool needsOverflow = !overflowWidgets.empty();

			if (needsOverflow) {
				if (!overflowButton) overflowButton = CreateOverflowButton();
			}
			else {
				if (overflowButton) {
					overflowButton->Destroy();
					overflowButton = nullptr;
				}
			}

			overflowItems = overflowWidgets;

			int totalUsedWidth = 0;
			int spacing = Scale(m_hwnd, 1);
			int autoWidgetCount = 0;

			std::vector<int> cachedWidths;
			cachedWidths.reserve(visibleWidgets.size());
			std::vector<bool> isAutoWidth;
			isAutoWidth.reserve(visibleWidgets.size());

			for (auto* w : visibleWidgets) {
				const char* wWidthStr = w->GetProperty("width");
				bool isAuto = (wWidthStr && strcmp(wWidthStr, "auto") == 0);
				int finalW = 0;

				if (isAuto) {
					finalW = 0;
					autoWidgetCount++;
					isAutoWidth.push_back(true);
				}
				else {
					int reqW = ParseCssDimension(wWidthStr, parentWidth);
					finalW = (reqW >= 0) ? reqW : Scale(m_hwnd, 80);
					totalUsedWidth += finalW;
					isAutoWidth.push_back(false);
				}
				cachedWidths.push_back(finalW);
			}

			if (!visibleWidgets.empty()) {
				totalUsedWidth += (int)(visibleWidgets.size() - 1) * spacing;
			}

			int btnWidth = 0;
			if (overflowButton) {
				btnWidth = Scale(m_hwnd, 40) / 2;
				totalUsedWidth += spacing + btnWidth;
			}

			int remainingSpace = parentWidth - totalUsedWidth;

			if (autoWidgetCount > 0 && remainingSpace > 0) {
				int widthPerAuto = remainingSpace / autoWidgetCount;
				int remainderPixels = remainingSpace % autoWidgetCount;
				for (size_t i = 0; i < cachedWidths.size(); ++i) {
					if (isAutoWidth[i]) {
						int extra = (remainderPixels > 0) ? 1 : 0;
						cachedWidths[i] = widthPerAuto + extra;
						if (remainderPixels > 0) remainderPixels--;
						totalUsedWidth += cachedWidths[i];
					}
				}
			}

			int x = 0;
			if (justify == "center") x = (parentWidth - totalUsedWidth) / 2;
			else if (justify == "end" || justify == "right") x = parentWidth - totalUsedWidth;
			else x = Scale(m_hwnd, 1);
			if (x < 0) x = 0;

			HDWP hdwp = BeginDeferWindowPos((int)visibleWidgets.size() + (overflowButton ? 1 : 0));

			for (size_t i = 0; i < visibleWidgets.size(); ++i) {
				IWidget* w = visibleWidgets[i];
				int wW = cachedWidths[i];
				int reqH = ParseCssDimension(w->GetProperty("height"), parentHeight);
				int wH = 0;
				if (reqH >= 0) wH = reqH;
				else if (align == "stretch" || align == "normal") wH = parentHeight;
				else wH = Scale(m_hwnd, 32);

				int wY = 0;
				if (align == "center") wY = (parentHeight - wH) / 2;
				else if (align == "end") wY = parentHeight - wH;
				else wY = 0;

				w->SetBounds(x, wY, wW, wH);
				ShowWindow(w->GetHWND(), SW_SHOW);
				hdwp = DeferWindowPos(hdwp, w->GetHWND(), NULL, x, wY, wW, wH - 2, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
				x += wW + spacing;
			}

			if (overflowButton) {
				int btnH = 0;
				if (align == "stretch" || align == "normal") btnH = parentHeight;
				else btnH = Scale(m_hwnd, 32);
				int btnY = 0;
				if (align == "center") btnY = (parentHeight - btnH) / 2;
				else if (align == "end") btnY = parentHeight - btnH;
				else btnY = 0;

				overflowButton->SetBounds(x, btnY, btnWidth, btnH);
				ShowWindow(overflowButton->GetHWND(), SW_SHOW);
				hdwp = DeferWindowPos(hdwp, overflowButton->GetHWND(), NULL, x, btnY, btnWidth, btnH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
			}

			for (auto* w : overflowWidgets) ShowWindow(w->GetHWND(), SW_HIDE);
			EndDeferWindowPos(hdwp);
			return;
		}

		if (m_mode == StackMode::Tabbed) {
			for (int i = 0; i < (int)widgets.size(); ++i) {
				if (i == m_activeTab) {
					widgets[i]->SetBounds(0, 0, r.right, r.bottom);
					ShowWindow(widgets[i]->GetHWND(), SW_SHOW);
				}
				else {
					ShowWindow(widgets[i]->GetHWND(), SW_HIDE);
				}
			}
		}
		else if (widgets.size() == 1 && !scrollEnabled) {
			int parentW = r.right;
			int parentH = r.bottom;
			int childW = parentW;
			int childH = parentH;

			const char* propWidth = widgets[0]->GetProperty("width");
			const char* propHeight = widgets[0]->GetProperty("height");
			const char* propAlign = widgets[0]->GetProperty("align-items");
			std::string align = propAlign ? propAlign : "normal";

			if (propWidth) {
				int reqW = ParseCssDimension(propWidth, parentW);
				if (reqW >= 0) childW = reqW;
			}
			if (propHeight) {
				int reqH = ParseCssDimension(propHeight, parentH);
				if (reqH >= 0) childH = reqH;
			}

			int x = 0, y = 0;
			if (align == "center") x = (parentW - childW) / 2;
			else if (align == "end") x = parentW - childW;
			else if (align == "start") x = 0;
			else if (align == "stretch" || align == "normal") {
				if (!propWidth) { childW = parentW; x = 0; }
				else x = 0;
			}
			ShowWindow(widgets[0]->GetHWND(), SW_SHOW);
			widgets[0]->SetBounds(x, y, childW, childH);
		}
		else {
			bool isHoriz = (m_mode == StackMode::Horizontal);
			const char* propAlign = GetProperty("align-items");
			std::string align = propAlign ? propAlign : "normal";
			if (align == "") align = "normal";

			HDWP hdwp = BeginDeferWindowPos((int)widgets.size());
			int cur = -scrollPos, total = 0;

			for (auto* w : widgets) {
				int reqW = ParseCssDimension(w->GetProperty("width"), r.right);
				int reqH = ParseCssDimension(w->GetProperty("height"), r.bottom);
				int ww = 0, wh = 0;

				if (isHoriz) {
					ww = (reqW >= 0) ? reqW : Scale(m_hwnd, 80);
					if (reqH >= 0) wh = reqH;
					else if (align == "stretch" || align == "normal") wh = r.bottom;
					else wh = Scale(m_hwnd, 45);
				}
				else {
					wh = (reqH >= 0) ? reqH : Scale(m_hwnd, 45);
					if (reqW >= 0) ww = reqW;
					else if (align == "stretch" || align == "normal") ww = r.right;
					else ww = Scale(m_hwnd, 80);
				}

				int wx = isHoriz ? cur : 0;
				int wy = isHoriz ? 0 : cur;

				if (isHoriz) {
					if (align == "center") wy = (r.bottom - wh) / 2;
					else if (align == "end") wy = r.bottom - wh;
					else wy = 0;
				}
				else {
					if (align == "center") wx = (r.right - ww) / 2;
					else if (align == "end") wx = r.right - ww;
					else wx = 0;
				}

				w->SetBounds(wx, wy, ww, wh);
				hdwp = DeferWindowPos(hdwp, w->GetHWND(), NULL, wx, wy, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
				if (!IsWindowVisible(w->GetHWND())) ShowWindow(w->GetHWND(), SW_SHOW);

				int step = isHoriz ? ww : wh;
				cur += step;
				total += step;
			}
			EndDeferWindowPos(hdwp);

			if (scrollEnabled) {
				SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL };
				si.nMin = 0;
				si.nMax = total - 1;
				si.nPage = isHoriz ? r.right : r.bottom;
				si.nPos = scrollPos;
				SetScrollInfo(m_hwnd, isHoriz ? SB_HORZ : SB_VERT, &si, TRUE);
			}
		}
	}

	extern "C" {
		CHRONO_API IContainer* __stdcall CreateChronoContainer(HWND parent, const wchar_t* t, int w, int h, bool customTitleBar) {
			return new ContainerImpl(parent, t, w, h, customTitleBar);
		}
		// New API for creating TrackPopup style windows
		CHRONO_API IContainer* __stdcall CreateChronoPopupContainer(HWND parent, int w, int h) {
			// Create popup window (isPopup = true)
			// Title is usually ignored or empty for popups
			// customTitleBar is false
			return new ContainerImpl(parent, L"", w, h, false, true);
		}
	}
	CHRONO_API IPanel* __stdcall CreateChronoPanel(IContainer* container) {
		return new PanelImpl(container);
	}
}