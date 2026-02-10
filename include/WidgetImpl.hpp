#pragma once

#ifndef CHRONOUI_EXPORTS
#define CHRONOUI_EXPORTS
#endif

#include <vector>
#include <string>
#include <windowsx.h>
#include <algorithm>
#include <dwmapi.h>
#include <map>
#include <atomic>
#include <mutex>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h> // IWICImagingFactory for Base64 images

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib") // For image loading

#include "ChronoUI.hpp"
#include "ContextNodeImpl.hpp"

#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Crypt32.lib") 

#define CHRONOUI_ANIM_TIMER				WM_USER+256

namespace ChronoUI {
	template <class T> void SafeRelease(T** ppT) {
		if (*ppT) {
			(*ppT)->Release();
			*ppT = NULL;
		}
	}

	inline D2D1_COLOR_F CSSColorToD2D(const std::string& cssColor, float alpha = 1.0f) {
		// Basic parser implementation (You'll need to expand this based on your GDI+ parser)
		if (cssColor.empty()) return D2D1::ColorF(D2D1::ColorF::Black, 0.0f); // Transparent

		if (cssColor[0] == '#') {
			int r, g, b;
			sscanf_s(cssColor.c_str(), "#%02x%02x%02x", &r, &g, &b);
			return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
		}
		return D2D1::ColorF(D2D1::ColorF::Black);
	}

	struct EventHandlerEntry {
		std::string eventName;
		ChronoEventCallback callback;
		void* pContext;
		ChronoEventCleanup cleanup;
	};

	struct CallbackEntry {
		void (*func)(IWidget*, void*);
		void* ctx;
		void (*cleanup)(void*);
	};

	// Forward Declaration
	class ContainerImpl;

	// =========================================================
	// --- ChronoControllerImpl (Internal Singleton) ---
	//     RENAMED: This is the internal workhorse.
	// =========================================================
	class ChronoControllerImpl : public ContextNodeImpl {
	private:
		int m_refCount = 0;
		std::recursive_mutex m_mutex;

		ChronoControllerImpl() {
			// Initialize Default Global Styles here
			SetDefaultStyles();

			Initialize();
		}

		~ChronoControllerImpl() {
			Shutdown();
		}

	public:
		// D2D Resources
		ComPtr<ID2D1Factory> m_pD2DFactory;
		ComPtr<IDWriteFactory> m_pDWriteFactory;
		ComPtr<IWICImagingFactory> m_pWICFactory;

		// Returns Reference for internal usage
		static ChronoControllerImpl& Instance() {
			static ChronoControllerImpl instance;
			return instance;
		}

		void Initialize() {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			if (m_refCount == 0) {
				// Create D2D Factory
				D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_pD2DFactory.GetAddressOf());

				// Create DirectWrite Factory
				DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
					reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf()));

				// Create WIC Factory (for images)
				CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(m_pWICFactory.GetAddressOf()));
			}
			m_refCount++;
		}

		void Shutdown() {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_refCount--;
			if (m_refCount <= 0) {
				m_pD2DFactory.Reset();
				m_pDWriteFactory.Reset();
				m_pWICFactory.Reset();
				m_refCount = 0;
			}
		}

		void SetDefaultStyles() {
			// --- Main Window (Light Base) ---
			SetProperty("background-color", "#ffffff");
			SetProperty("background-color:hover", "#f9f9f9");
			SetProperty("foreground-color", "#333333");
			SetProperty("foreground-color:hover", "#333333");
			SetProperty("border-color", "#e0e0e0");
			SetProperty("border-color:hover", "#b3e5fc");

			SetProperty("color", "#ffffff"); // For buttons
			SetProperty("color:hover", "#fefefe"); // For buttons
			SetProperty("image-align", "right");
			SetProperty("border-radius", "2");

			//description-color
			//dimmed-color

			// --- Color Widget (Pastel Blue Theme) ---
			SetProperty("StaticText:background-color", "#ffffff");
			SetProperty("StaticText:background-color:hover", "#e3f2fd");
			SetProperty("StaticText:foreground-color:hover", "#333333");
			SetProperty("StaticText:border-color:hover", "#90caf9");
			SetProperty("StaticText:border-width:hover", "1");
			SetProperty("StaticText:danger:background-color", "#ffcdd2");

			// --- DateTime Widget (Pastel Mint Theme) ---
			SetProperty("ViewDateTimeWidget:background-color", "#fcfcfc");
			SetProperty("ViewDateTimeWidget:background-color:hover", "#e8f5e9");
			SetProperty("ViewDateTimeWidget:foreground-color:hover", "#333333");
			SetProperty("ViewDateTimeWidget:border-color:hover", "#a5d6a7");
			SetProperty("ViewDateTimeWidget:border-width:hover", "1");
			SetProperty("ViewDateTimeWidget:danger:background-color", "#ffcdd2");

			// --- Global Typography ---
			SetProperty("font-family", "Segoe UI");
			SetProperty("font-size", "10");
			SetProperty("font-size:hover", "12");
			SetProperty("font-style", "normal");
			SetProperty("text-align", "center");
		}

		// Implement IContextNode overrides to expose this class as the root
		virtual void __stdcall SetParentNode(IContextNode* parent) override { /* Root has no parent */ }
		virtual IContextNode* __stdcall GetParentNode() override { return nullptr; }
		virtual IContextNode* __stdcall GetContextNode() override { return nullptr; }
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override { return ContextNodeImpl::GetProperty(key, def); }
		virtual IContextNode* SetColor(const char* key, COLORREF color) override { return ContextNodeImpl::SetColor(key, color); };
		virtual COLORREF GetColor(const char* key, COLORREF defaultColor) override { return ContextNodeImpl::GetColor(key, defaultColor); }
		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active) override {
			return ContextNodeImpl::GetStyle(_prop, _def, _classid, _subclass, selected, enabled, hovered, active);
		}
	};

	// --- Factory Helper ---
	// Creates a static shared DWrite Factory for this compilation unit.
	inline ComPtr<IDWriteFactory> GetDWriteFactory()
	{
		return ChronoControllerImpl::Instance().m_pDWriteFactory;
	}
	inline ComPtr<ID2D1Factory> GetD2DFactory()
	{
		return ChronoControllerImpl::Instance().m_pD2DFactory;
	}


	inline void ImageFromBase64(ID2D1RenderTarget* pRT, const char* b64, ID2D1Bitmap** ppBitmap) {
		// 0. Input Validation
		if (!pRT || !b64 || !ppBitmap) return;

		// 1. Clean up existing image (In-parameter handling)
		if (*ppBitmap) {
			(*ppBitmap)->Release();
			*ppBitmap = nullptr;
		}

		// 2. Decode Base64 string to memory
		DWORD len = 0;
		if (!CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, NULL, &len, NULL, NULL)) {
			return;
		}

		std::vector<BYTE> buf(len);
		if (!CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, buf.data(), &len, NULL, NULL)) {
			return;
		}

		// 3. Create Stream
		// SHCreateMemStream creates a stream and copies the data into it. 
		// We wrap it in a ComPtr to ensure Release() is called automatically when the function exits.
		ComPtr<IStream> pStream;
		pStream.Attach(SHCreateMemStream(buf.data(), len));

		if (!pStream) return;

		// 4. Access WIC Factory
		// Assuming ChronoControllerImpl is your singleton manager.
		// Ensure m_pWICFactory is a valid IWICImagingFactory* or ComPtr<IWICImagingFactory>
		ComPtr<IWICImagingFactory> pWicFactory = ChronoControllerImpl::Instance().m_pWICFactory;

		// Fallback/Safety check if factory is not initialized
		if (!pWicFactory) return;

		// 5. Create Decoder
		ComPtr<IWICBitmapDecoder> decoder;
		HRESULT hr = pWicFactory->CreateDecoderFromStream(
			pStream.Get(),
			NULL,
			WICDecodeMetadataCacheOnLoad,
			&decoder
		);

		if (FAILED(hr)) return;

		// 6. Get the first frame
		ComPtr<IWICBitmapFrameDecode> source;
		hr = decoder->GetFrame(0, &source);
		if (FAILED(hr)) return;

		// 7. Convert format to D2D compatible (32bppPBGRA)
		ComPtr<IWICFormatConverter> converter;
		hr = pWicFactory->CreateFormatConverter(&converter);
		if (FAILED(hr)) return;

		hr = converter->Initialize(
			source.Get(),                          // Source frame
			GUID_WICPixelFormat32bppPBGRA,         // Destination format (Pre-multiplied Alpha is critical for D2D)
			WICBitmapDitherTypeNone,               // Dithering
			NULL,                                  // Palette
			0.f,                                   // Alpha threshold
			WICBitmapPaletteTypeMedianCut          // Palette translation
		);

		if (FAILED(hr)) return;

		// 8. Create D2D Bitmap
		// The WIC converter is now a valid IWICBitmapSource that D2D can consume
		hr = pRT->CreateBitmapFromWicBitmap(
			converter.Get(),
			NULL,
			ppBitmap
		);

		// If successful, *ppBitmap now holds the valid image.
		// pStream, decoder, source, and converter will auto-release here via ComPtr.
	}

	class WidgetImpl : public IWidget, public ContextNodeImpl {
	protected:
		HWND m_hwnd = nullptr;
		std::vector<EventHandlerEntry> m_handlers;
		
		std::vector<IWidget*> m_overlays;		// List of overlays
		IWidget* m_overlayHost = nullptr;		// In case is a overlay, points to host
		
		// D2D Resources
		ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
		// Brushes cache (Optional: usually recreated in Draw, 
		// but for performance, keep commonly used brushes here)
		ComPtr<ID2D1SolidColorBrush> m_pSolidBrush;


		bool m_focused = false;
		bool m_validated = true;
		bool m_checked = false;

		bool m_isEnabled = false; // Updated on draw
		bool m_isHovered = false; // Updated on mouse move

		bool m_hoverActive = false;

		struct TimerInfo {
			std::string eventName;
			bool isOneShot;
		};
		std::map<UINT_PTR, TimerInfo> m_timers;
		std::vector<CallbackEntry> m_onChangedCallbacks;

		int m_width = 0;
		int m_height = 0;

		// Store the validator
		struct ValidatorData {
			ChronoValidationCallback callback = nullptr;
			void* context = nullptr;
			ChronoValidationCleanup cleanup = nullptr;
		} m_validator;

	public:
		WidgetImpl() {
			static std::atomic<size_t> g_globalWidgetCounter{ 1000 };

			// 2. Increment and get unique ID
			size_t uniqueId = g_globalWidgetCounter.fetch_add(1);

			// 3. Format string (e.g., "widget_1001")
			std::string autoId = "widget_" + std::to_string(uniqueId);

			// 4. Set the property
			SetProperty("id", autoId.c_str());
		}
		virtual ~WidgetImpl() {
			DiscardDeviceResources();
			// Cleanup Overlays
			for (auto* ov : m_overlays) {
				ov->Destroy(); // Overlays are owned by creation, but usually linked here
			}
			m_overlays.clear();

			if (m_hwnd) {
				for (const auto& t : m_timers) ::KillTimer(m_hwnd, t.first);
			}
			for (const auto& entry : m_onChangedCallbacks) {
				if (entry.cleanup && entry.ctx) entry.cleanup(entry.ctx);
			}
			m_onChangedCallbacks.clear();
			WidgetImpl::ClearEventHandlers();
		}

		HRESULT CreateDeviceResources() {
			if (m_pRenderTarget) return S_OK;

			RECT rc;
			GetClientRect(m_hwnd, &rc);
			D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

			ChronoControllerImpl& controller = ChronoControllerImpl::Instance();

			// 1. Create Render Target
			HRESULT hr = controller.m_pD2DFactory->CreateHwndRenderTarget(
				D2D1::RenderTargetProperties(),
				D2D1::HwndRenderTargetProperties(m_hwnd, size),
				&m_pRenderTarget
			);

			// 2. CRITICAL FIX: Synchronize D2D DPI with Window DPI
			// Without this, mouse coordinates (Pixels) and Drawing coordinates (DIPs) 
			// will drift apart on high-DPI screens, breaking hit-tests.
			if (SUCCEEDED(hr)) {
				float dpi = (float)GetDpiForWindow(m_hwnd);
				m_pRenderTarget->SetDpi(dpi, dpi);
			}

			return hr;
		}
		void DiscardDeviceResources() {
			m_pRenderTarget.Reset();
			m_pSolidBrush.Reset();
		}

		// Implement the virtual method
		void __stdcall RegisterValidator(ChronoValidationCallback callback, void* pContext, ChronoValidationCleanup cleanup) override {
			// 1. Clean up existing validator if one exists
			if (m_validator.cleanup && m_validator.context) {
				m_validator.cleanup(m_validator.context);
			}

			// 2. Store new one
			m_validator.callback = callback;
			m_validator.context = pContext;
			m_validator.cleanup = cleanup;
		}

		// --- Scaling Helpers (Essential for drawing and margins) ---
		int ScaleI(int val) {
			return val;
			if (!m_hwnd) return val;
			UINT dpi = GetDpiForWindow(m_hwnd);
			return (int)((val * dpi) / 96.0f);
		}
		float ScaleF(float val) {
			return val;
			if (!m_hwnd) return val;
			UINT dpi = GetDpiForWindow(m_hwnd);
			return (val * dpi) / 96.0f;
		}
		float ScaleSizeF(float val) {
			return ScaleF(val);
		}
		// -----------------------------------------------------------
		// --- Overlay Support Implementation ---
		virtual void __stdcall SetWidgetHost(IWidget* host) override {
			// Propagate to overlays
			m_overlayHost = host;
		}

		void __stdcall OnFocus(bool f) override {
			FireEvent(f ? "onFocus" : "onBlur", "{}");
			InvalidateRect(m_hwnd, NULL, FALSE);
		}

		// Primary implementation for strings
		void UpdateBind(const char* key, const std::string& value) {
			// 1. Update the generic property store.
			// This ensures that when the binding callback executes and calls 
			// GetProperty(key), it retrieves the new value we just set.
			m_properties[key] = value;

			// 2. Trigger the bindings associated with this widget
			TriggerOnChanged();
		}

		// Overload for C-strings
		void UpdateBind(const char* key, const char* value) {
			UpdateBind(key, std::string(value ? value : ""));
		}

		// Overload for boolean (mapped to "true"/"false" for XML/JSON consistency)
		void UpdateBind(const char* key, bool value) {
			UpdateBind(key, std::string(value ? "true" : "false"));
		}

		// Template for arithmetic types (int, float, double)
		template <typename T, typename std::enable_if<std::is_arithmetic<T>::value>::type* = nullptr>
		void UpdateBind(const char* key, T value) {
			UpdateBind(key, std::to_string(value));
		}

		IWidget* __stdcall AddOverlay(IWidget* overlay) override {
			if (overlay) {
				overlay->SetWidgetHost(this);
				m_overlays.push_back(overlay);

				// Create the overlay's hidden helper window if it hasn't been created
				if (m_hwnd && !overlay->GetHWND()) {
					overlay->Create(m_hwnd);
					HWND hOverlay = overlay->GetHWND();
					if (hOverlay) {
						// Add layered + transparent styles
						LONG_PTR ex = GetWindowLongPtr(hOverlay, GWL_EXSTYLE);
						ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
						SetWindowLongPtr(hOverlay, GWL_EXSTYLE, ex);

						// Optional: full opacity but still click-through
						SetLayeredWindowAttributes(hOverlay, 0, 255, LWA_ALPHA);

						// Ensure it stays on top of parent client area
						SetWindowPos(hOverlay, HWND_TOP, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
					}
				}
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
			return this;
		}

		void __stdcall RemoveOverlay(IWidget* overlay) override {
			auto it = std::remove(m_overlays.begin(), m_overlays.end(), overlay);
			if (it != m_overlays.end()) {
				m_overlays.erase(it);
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
		}
		// -------------------------------------
		virtual void OnDrawWidget(ID2D1RenderTarget* pRT) = 0;
		virtual bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) { return false; }
		virtual bool OnUpdateAnimation(float deltaTime) { return false;  }

		void __stdcall Create(HWND parent) override {
			WNDCLASSW wc = { 0 };
			wc.lpfnWndProc = BaseWndProc;
			wc.hInstance = GetModuleHandle(NULL);

			std::string classname = "ChronoUI." + std::string(GetControlName());
			std::wstring wname(NarrowToWide(classname));
			wc.lpszClassName = wname.c_str();
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);
			wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; // Redraw on resize

			if (!GetClassInfoW(wc.hInstance, wc.lpszClassName, &wc)) {
				RegisterClassW(&wc);
			}

			// Initial size 0 is fine, CellImpl will call SetBounds
			m_hwnd = CreateWindowExW(0, wname.c_str(), nullptr,
				WS_CHILD | WS_VISIBLE | WS_TABSTOP,
				0, 0, 0, 0, parent, nullptr, wc.hInstance, this);
		}

		void OnChanged(void (*cb)(IWidget*, void*), void* ctx, void (*cleanup)(void*) = nullptr) override {
			if (cb) m_onChangedCallbacks.push_back({ cb, ctx, cleanup });
		}

		void RemoveOnChanged(void (*cb)(IWidget*, void*), void* ctx) {
			m_onChangedCallbacks.erase(std::remove_if(m_onChangedCallbacks.begin(), m_onChangedCallbacks.end(),
				[cb, ctx](const CallbackEntry& e) { return e.func == cb && e.ctx == ctx; }), m_onChangedCallbacks.end());
		}

		void TriggerOnChanged() {
			for (const auto& entry : m_onChangedCallbacks) if (entry.func) entry.func(this, entry.ctx);
		}

		static void CALLBACK StaticTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
			WidgetImpl* pThis = reinterpret_cast<WidgetImpl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
			if (pThis) pThis->ProcessTimer(idEvent);
		}

		void ProcessTimer(UINT_PTR id) {
			auto it = m_timers.find(id);
			if (it != m_timers.end()) {
				FireEvent(it->second.eventName.c_str(), "{}");
				if (it->second.isOneShot) {
					::KillTimer(m_hwnd, id);
					m_timers.erase(it);
				}
			}
			else { ::KillTimer(m_hwnd, id); }
		}

		virtual void __stdcall SetBounds(int x, int y, int w, int h) override {
			if (m_hwnd) {
				// This triggers WM_SIZE, which calls ResizeBackBuffer
				SetWindowPos(m_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
				for (auto * ov : m_overlays) {
					ov->SetBounds(x, y, w, h); // Overlays match size
				}
			}
		}

		HWND __stdcall GetHWND() override { return m_hwnd; }

		void __stdcall Destroy() override {
			// Cleanup on destruction
			if (m_validator.cleanup && m_validator.context) {
				m_validator.cleanup(m_validator.context);
				m_validator.context = nullptr;
			}

			FireEvent("onDestroy", "{}");
			if (m_hwnd) {
				for (const auto& t : m_timers) ::KillTimer(m_hwnd, t.first);
				DestroyWindow(m_hwnd);
			}
			m_timers.clear();
			delete this;
		}

		IWidget* AddTimer(const char* eventName, int milliseconds) override {
			if (m_hwnd) {
				UINT_PTR id = 1000;
				while (m_timers.find(id) != m_timers.end()) id++;
				m_timers[id] = { eventName, false };
				::SetTimer(m_hwnd, id, milliseconds, StaticTimerProc);
			}
			return this;
		}

		IWidget* AddOneShotTimer(const char* eventName, int milliseconds) override {
			if (m_hwnd) {
				UINT_PTR id = 1000;
				while (m_timers.find(id) != m_timers.end()) id++;
				m_timers[id] = { eventName, true };
				::SetTimer(m_hwnd, id, milliseconds, StaticTimerProc);
			}
			return this;
		}

		IWidget* __stdcall RegisterEventHandler(const char* eventName, ChronoEventCallback callback, void* pContext, ChronoEventCleanup cleanup) override {
			m_handlers.push_back({ eventName, callback, pContext, cleanup });
			return this;
		}

		void __stdcall ClearEventHandlers() override {
			for (auto& h : m_handlers) if (h.cleanup) h.cleanup(h.pContext);
			m_handlers.clear();
		}

		// Explicit IContextNode Forwarding
		virtual void __stdcall SetParentNode(IContextNode* parent) override { ContextNodeImpl::SetParentNode(parent); }
		virtual IContextNode* __stdcall GetParentNode() override { return ContextNodeImpl::GetParentNode(); }
		virtual IContextNode* __stdcall GetContextNode() override { return ContextNodeImpl::GetContextNode(); }
		
		
		virtual COLORREF GetColor(const char* key, COLORREF defaultColor = RGB(0, 0, 0)) override { return ContextNodeImpl::GetColor(key, defaultColor); }
		virtual IWidget* SetColor(const char* key, COLORREF color) override { 
			ContextNodeImpl::SetColor(key, color); return this; 
		};
		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active=false) override { return ContextNodeImpl::GetStyle(_prop, _def, _classid, _subclass, selected, enabled, hovered, active); }

		 
		D2D1_COLOR_F GetCSSColorStyle(const char* key) {
			const char* controlName = GetControlName();
			std::string subclass = GetProperty("subclass");
			const char* subCStr = subclass.c_str();
			return CSSToD2DColor(
				GetStyle(key, "#00ff00", controlName, subCStr, m_focused, m_isEnabled, m_hoverActive&m_isHovered, m_checked)
				);
		}
		COLORREF GetCSSColorRefStyle(const char* key) {
			// 1. Get the D2D color
			D2D1_COLOR_F color = GetCSSColorStyle(key);

			// 2. Convert floats (0.0 - 1.0) to bytes (0 - 255)
			//    We explicitly cast to prevent warnings.
			BYTE r = static_cast<BYTE>(color.r * 255.0f);
			BYTE g = static_cast<BYTE>(color.g * 255.0f);
			BYTE b = static_cast<BYTE>(color.b * 255.0f);

			// 3. Construct the COLORREF (0x00BBGGRR)
			//    Note: Alpha is lost in standard COLORREF
			return RGB(r, g, b);
		}

		int GetCSSIntStyle(const char* key, int def = 0) {
			const char* controlName = GetControlName();
			std::string subclass = GetProperty("subclass");
			const char* subCStr = subclass.c_str();

			const char* r = GetStyle(key, std::to_string(def).c_str(), controlName, subCStr, m_focused, m_isEnabled, m_hoverActive&m_isHovered, m_checked);

			if (!r) return def;
			try {
				return (std::stoi(std::string(r)));
			}
			catch (...) {
				return def;
			}
		}
		

		virtual IWidget* __stdcall SetProperty(const char* key, const char* value) override { 
			m_properties[key] = (value) ? value : ""; 
			OnPropertyChanged(key, value); 
			return this; 
		}
		IWidget* SetBoolProperty(const char* key, bool value) {
			return SetProperty(key, value ? "true" : "false");
		}
		D2D1_COLOR_F CSSToD2DColor(const std::string& hex) {
			if (hex.empty() || hex[0] != '#') return D2D1::ColorF(D2D1::ColorF::Black);

			int r, g, b, a = 255;
			if (hex.length() > 7) {
				// Format #AARRGGBB
				sscanf_s(hex.c_str(), "#%02x%02x%02x%02x", &a, &r, &g, &b);
			}
			else {
				// Format #RRGGBB
				sscanf_s(hex.c_str(), "#%02x%02x%02x", &r, &g, &b);
			}
			return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
		}
		D2D1_COLOR_F GetColorProperty(const char* key, D2D1_COLOR_F def = D2D1::ColorF(0.0f, 1.0f, 0.0f)) {
			const char* r = GetProperty(key);
			if (!r) return def;

			return CSSToD2DColor(std::string(r));
		}

		bool GetBoolProperty(const char* key, bool def = true) {
			const char* r = GetProperty(key, def ? "true" : "false");
			if (!r) return def;
			return (std::string(r) == "true");
		}
		float GetFloatProperty(const char* key, float def = 0) {
			const char* r = GetProperty(key, def ? "true" : "false");
			if (!r) return def;
			try {
				return (std::stof(std::string(r)));
			}
			catch (...) {
				return def;
			}
		}
		int GetIntProperty(const char* key, int def = 0) {
			const char* r = GetProperty(key, def ? "true" : "false");
			if (!r) return def;
			try {
				return (std::stoi(std::string(r)));
			} catch(...){
				return def;
			}
		}
		std::string GetStringProperty(std::string key, std::string def = "") {
			const char* r = GetProperty(key.c_str(), def.c_str());
			if (!r) return def;
			return std::string(r);
		}
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override { 
			std::string k = key;
			std::string d = def ? def : "";

			if (k == "checked") {
				return m_checked?"true":"false";
			}
			else if (k == "validated") {
				return m_validated ? "true" : "false";
			}
			if (m_hwnd) {
				if (k == "enabled") {
					return (::IsWindow(m_hwnd) && ::IsWindowEnabled(m_hwnd)) ? "true" : "false";
				}
				else if (k == "disabled") {
					return (::IsWindow(m_hwnd) && ::IsWindowEnabled(m_hwnd)) ? "false" : "true";
				}
			}

			return ContextNodeImpl::GetProperty(key, def); 
		}

		virtual void OnPropertyChanged(const char* key, const char* value) {
			std::string k = key;
			std::string v = value ? value : "";

			if (k == "checked") {
				m_checked = (v == "true");
			} else if (k == "validated") {
				m_validated = (v == "true");
			}
			if ((k == "value") || (k == "text")) {
				PerformValidation(v.c_str());
			}
			if (IsWindow(m_hwnd)) {
				if (k == "enabled") {
					if (v == "true") {
						EnableWindow(m_hwnd, TRUE);
					}
					else {
						EnableWindow(m_hwnd, FALSE);
					}
				}
				else if (k == "disabled") {
					if (v == "true") {
						EnableWindow(m_hwnd, FALSE);
					}
					else {
						EnableWindow(m_hwnd, TRUE);
					}
				}
				InvalidateRect(m_hwnd, NULL, FALSE);
			}
		}

	protected:
		// -----------------------------------------------------------------------------
		// --- Drawing Helpers ---

		void DrawFlatBackground(ID2D1RenderTarget* pRT) {
			// 1. Get Context
			const char* controlName = GetControlName();
			std::string subclass = GetProperty("subclass");

			// 2. Fetch Style (using false/0 for state args as per original code)
			std::string bgStr = GetStyle("background-color", "", controlName, subclass.c_str(), false, false, false);

			// 3. Clear the Render Target
			// Note: pRT->Clear ignores the current transform but respects the clip. 
			// It fills the entire render target with the specified color.
			pRT->Clear(CSSColorToD2D(bgStr));
		}

		void DrawWidgetBackground(ID2D1RenderTarget* pRT, const D2D1_RECT_F& r, bool hovereffect = true) {
			const char* controlName = GetControlName();
			std::string subclass = GetProperty("subclass");
			bool isEnabled = ::IsWindowEnabled(m_hwnd);
			bool hover = m_isHovered && hovereffect;

			// Fetch Styles
			std::string bgStr = GetStyle("background-color", "", controlName, subclass.c_str(), m_focused, isEnabled, hover);
			std::string borderStr = GetStyle("border-color", "", controlName, subclass.c_str(), m_focused, isEnabled, hover);
			float borderWidth = std::stof(GetStyle("border-width", "0", controlName, subclass.c_str(), m_focused, isEnabled, hover));
			float radius = std::stof(GetStyle("border-radius", "2", controlName, subclass.c_str(), m_focused, isEnabled, hover));

			// Scale margins (Keep existing ScaleF logic)
			float mt = ScaleF(std::stof(GetStyle("margin-top", "1", controlName, subclass.c_str(), m_focused, isEnabled, hover)));
			float ml = ScaleF(std::stof(GetStyle("margin-left", "1", controlName, subclass.c_str(), m_focused, isEnabled, hover)));
			float w = r.right - r.left - ml - ScaleF(std::stof(GetStyle("margin-right", "1", controlName, subclass.c_str(), m_focused, isEnabled, hover)));
			float h = r.bottom - r.top - mt - ScaleF(std::stof(GetStyle("margin-bottom", "1", controlName, subclass.c_str(), m_focused, isEnabled, hover)));

			D2D1_RECT_F drawRect = D2D1::RectF(ml, mt, ml + w, mt + h);

			// Create Brush (Reusable)
			ComPtr<ID2D1SolidColorBrush> pBrush;

			// Fill
			D2D1_COLOR_F bgColor = CSSColorToD2D(bgStr);
			if (bgColor.a > 0) {
				//pRT->CreateSolidColorBrush(bgColor, &pBrush);
				//pRT->FillRectangle(drawRect, pBrush.Get());
				pRT->Clear(bgColor);
			}

			// Border
			if (borderWidth > 0) {
				D2D1_COLOR_F borderColor = CSSColorToD2D(borderStr);
				pRT->CreateSolidColorBrush(borderColor, &pBrush);
				if (radius > 0) {
					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(drawRect, radius, radius);
					pRT->DrawRoundedRectangle(roundedRect, pBrush.Get(), borderWidth);
				} else {
					pRT->DrawRectangle(drawRect, pBrush.Get(), borderWidth);
				}
			}
		}

		void DrawTextStyled(ID2D1RenderTarget* pRT, const std::string& text, const D2D1_RECT_F& r, bool allowHover = true) {
			if (text.empty()) return;

			// 1. Retrieve State and Context
			auto& controller = ChronoControllerImpl::Instance();
			std::string subclass = GetProperty("subclass");
			bool isEnabled = ::IsWindowEnabled(m_hwnd);
			const char* cname = GetControlName();
			bool hover = allowHover && m_isHovered;

			// 2. Fetch Styles
			// Foreground Color
			std::string fgStr = GetStyle("color", "#000000", cname, subclass.c_str(), m_focused, isEnabled, hover);

			// Font Family
			std::string fontName = GetStyle("font-family", "Segoe UI", cname, subclass.c_str(), m_focused, isEnabled, hover);
			std::wstring wFontName(fontName.begin(), fontName.end());

			// Font Style (Bold/Italic/Normal)
			std::string style = GetStyle("font-style", "normal", cname, subclass.c_str(), m_focused, isEnabled, hover);
			DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL;
			DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL;

			if (style == "bold") {
				fontWeight = DWRITE_FONT_WEIGHT_BOLD;
			}
			else if (style == "italic") {
				fontStyle = DWRITE_FONT_STYLE_ITALIC;
			}

			// Font Size
			float fontSize = std::stof(GetStyle("font-size", "12", cname, subclass.c_str(), m_focused, isEnabled, hover));

			// 3. Create Text Format
			ComPtr<IDWriteTextFormat> pTextFormat;
			HRESULT hr = controller.m_pDWriteFactory->CreateTextFormat(
				wFontName.c_str(),
				NULL,
				fontWeight,
				fontStyle,
				DWRITE_FONT_STRETCH_NORMAL,
				fontSize,
				L"en-us", // Locale
				&pTextFormat
			);

			if (SUCCEEDED(hr)) {
				// 4. Alignment
				// Horizontal Alignment
				std::string align = GetStyle("text-align", "center", cname, subclass.c_str(), m_focused, isEnabled, hover);
				if (align == "left") {
					pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
				}
				else if (align == "right") {
					pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
				}
				else {
					pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				}

				// Vertical Alignment (Matches GDI+ SetLineAlignment(StringAlignmentCenter))
				pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

				// 5. Draw
				std::wstring wText(text.begin(), text.end());
				ComPtr<ID2D1SolidColorBrush> pBrush;

				// CSSColorToD2D is assumed to be available based on context
				pRT->CreateSolidColorBrush(CSSColorToD2D(fgStr), &pBrush);

				if (pBrush) {
					pRT->DrawText(
						wText.c_str(),
						(UINT32)wText.length(),
						pTextFormat.Get(),
						r,
						pBrush.Get()
					);
				}
			}
		}

		// --- Master Window Proc ---

		static LRESULT CALLBACK BaseWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
			WidgetImpl* self = nullptr;
			if (msg == WM_NCCREATE) {
				LPCREATESTRUCT lpcs = (LPCREATESTRUCT)lp;
				self = (WidgetImpl*)lpcs->lpCreateParams;
				SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
				self->m_hwnd = hwnd;
			}
			else {
				self = (WidgetImpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			}
			if (self) {
				return self->HandleMessage(msg, wp, lp);
			}

			return DefWindowProc(hwnd, msg, wp, lp);
		}

		virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
			switch (msg) {
			case WM_ERASEBKGND:
				return 1; // Double buffered, no flicker
			case WM_PAINT:
				DoPaint(); // Your Direct2D drawing
				return 1;
			}

			// 2. Widget Custom Interception
			if (OnMessage(msg, wp, lp)) {
				return 0;
			}

			// 3. Standard Handling
			switch (msg) {
			case WM_SIZE: {
				UINT width = LOWORD(lp);
				UINT height = HIWORD(lp);

				// Update internal state
				m_width = (int)width;
				m_height = (int)height;

				if (m_pRenderTarget) {
					m_pRenderTarget->Resize(D2D1::SizeU(width, height));
				}
				return 0; // Return 0 to indicate we handled it
			}
			case WM_TIMER:
				if (wp == CHRONOUI_ANIM_TIMER) {
					// Default animation timer
					if (OnUpdateAnimation(0.016f)) { // ~60fps tick assumption
						if (m_overlayHost) {
							InvalidateRect(m_overlayHost->GetHWND(), NULL, FALSE);
						} else {
							InvalidateRect(GetHWND(), NULL, FALSE);
						}
					}
					return 0;
				}
				break;
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

			case WM_SETFOCUS:
				m_focused = true;
				InvalidateRect(m_hwnd, NULL, FALSE);
				break;

			case WM_KILLFOCUS:
				m_focused = false;
				InvalidateRect(m_hwnd, NULL, FALSE);
				break;
			case WM_DPICHANGED_AFTERPARENT: {
				if (m_pRenderTarget) {
					float dpi = (float)GetDpiForWindow(m_hwnd);
					m_pRenderTarget->SetDpi(dpi, dpi);
					InvalidateRect(m_hwnd, NULL, FALSE);
				}
				return 0;
			}
			}

			return DefWindowProc(m_hwnd, msg, wp, lp);
		}

		// Inside WidgetImpl class
		void DoPaint() {
			// BeginPaint is essential to validate the window region.
			// Without it, Windows keeps sending WM_PAINT messages forever (freezing the app).
			PAINTSTRUCT ps;
			BeginPaint(m_hwnd, &ps);

			if (m_overlayHost) { // Overlays are painted by their host, so skip self-paint to avoid redundant drawing and flicker
				EndPaint(m_hwnd, &ps);
				return;
			}

			HRESULT hr = CreateDeviceResources();

			if (SUCCEEDED(hr) && !(m_pRenderTarget->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED)) {
				m_pRenderTarget->BeginDraw();
				m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
				if (m_overlayHost == nullptr) {
					m_isEnabled = ::IsWindowEnabled(m_hwnd);
					m_focused = (GetFocus() == m_hwnd);

					// Draw Self
					OnDrawWidget(m_pRenderTarget.Get());

					// Draw Overlays
					for (auto* ov : m_overlays) {
						((WidgetImpl*)ov)->OnDrawWidget(m_pRenderTarget.Get());
					}
				}
				hr = m_pRenderTarget->EndDraw();

				// Handle Device Loss (e.g. resolution change, RDP connect)
				if (hr == D2DERR_RECREATE_TARGET) {
					DiscardDeviceResources();
				}
			}
			EndPaint(m_hwnd, &ps);
		}

		void __stdcall FireEvent(const char* eventName, const char* jsonPayload) override {
			for (const auto& h : m_handlers) {
				if (h.eventName == eventName)
					h.callback(eventName, jsonPayload, h.pContext);
			}
		}

		// Inside class WidgetImpl : public IWidget ...

protected: // Make sure this is protected or public so derived classes can call it
	// Returns true if valid, false if invalid
	virtual bool __stdcall PerformValidation(const char* newValue) override {
		bool isValid = true; // Default to true if no validator is attached

		if (m_validator.callback) {
			// Call the user's lambda/callback
			isValid = m_validator.callback(this, newValue, m_validator.context);
		}

		// Update internal state only if it changed (optimization)
		if (m_validated != isValid) {
			m_validated = isValid;

			SetProperty("validated", m_validated?"true":"false");

			// Force a redraw because validation failure usually changes borders/colors
			if (m_hwnd) 
				InvalidateRect(m_hwnd, NULL, FALSE);
		}

		return isValid;
	}
	};
}
