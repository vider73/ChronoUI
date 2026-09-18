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
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h> // IWICImagingFactory for Base64 images

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib") // For image loading

#include "ChronoUI.hpp"
#include "ContextNodeImpl.hpp"
#include "ChronoStyles.hpp"   // for StyleManager::Forget in ~WidgetImpl

#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Crypt32.lib") 

#define CHRONOUI_ANIM_TIMER				WM_USER+256

namespace ChronoUI {

	// ------------------------------------------------------------------
	// Layout dimension — pre-parsed form of "auto" / "200" / "200px" /
	// "200lu" / "25%" so the layout pass doesn't have to re-do string ops
	// on every frame. Widgets cache one of these per dimension property.
	// ------------------------------------------------------------------
	struct LayoutDim {
		enum Kind : uint8_t { Invalid = 0, Auto, Pixels, Percent };
		Kind  kind  = Invalid;
		float value = 0.0f;
	};

	// Parse a single dimension string. Returns Invalid kind on parse failure
	// or empty input. Recognized:
	//   ""          -> Invalid (caller should treat as "use default")
	//   "auto"      -> Auto    (caller should use refTotalSize)
	//   "200" / "200px" / "200lu" -> Pixels (scaled by DPI at resolve time)
	//   "25%"       -> Percent (resolved against refTotalSize)
	inline LayoutDim ParseLayoutDim(const std::string& input) {
		LayoutDim out;
		if (input.empty()) return out;

		if (input == "auto") {
			out.kind = LayoutDim::Auto;
			return out;
		}

		std::string val = input;
		bool isPercent = false;

		if (val.back() == '%') {
			isPercent = true;
			val.pop_back();
		} else if (val.length() > 2) {
			std::string suffix = val.substr(val.length() - 2);
			if (suffix == "px" || suffix == "lu") val = val.substr(0, val.length() - 2);
		}

		try {
			float fVal = std::stof(val);
			out.value = fVal;
			out.kind  = isPercent ? LayoutDim::Percent : LayoutDim::Pixels;
		} catch (...) {
			out.kind = LayoutDim::Invalid;
		}
		return out;
	}

	// Resolve a parsed dimension to an integer pixel value relative to a
	// container's available size. 'hwndForScale' is used for DPI scaling
	// of pixel/lu values (matches the legacy Scale() helper in ChronoUI.cpp).
	// Returns -1 for Invalid so callers can apply their own default fallback.
	inline int ResolveLayoutDim(const LayoutDim& d, int refTotal, HWND hwndForScale) {
		switch (d.kind) {
		case LayoutDim::Auto:    return refTotal;
		case LayoutDim::Pixels:  return MulDiv((int)d.value, GetDpiForWindow(hwndForScale), 96);
		case LayoutDim::Percent: return (int)(refTotal * (d.value / 100.0f));
		default:                 return -1;
		}
	}

	template <class T> void SafeRelease(T** ppT) {
		if (*ppT) {
			(*ppT)->Release();
			*ppT = NULL;
		}
	}

	// Parse a CSS color string into a Direct2D color.
	// Supports:
	//   - "#RRGGBB"     (hex)
	//   - "#AARRGGBB"   (hex with alpha)
	//   - "#RGB"        (short hex, expanded; e.g. "#f0c" -> "#ff00cc")
	//   - "rgb(R,G,B)"  (0-255 ints)
	//   - "rgba(R,G,B,A)" (A as 0..1 float or 0..255 int — auto-detected)
	//   - "transparent" -> fully transparent black
	//   - a small set of common named colors (red, green, blue, white, black,
	//     gray/grey, yellow, orange, purple, cyan, magenta, pink, brown,
	//     navy, teal, lime, maroon, silver, olive)
	// Unknown input falls back to opaque black so missing styles are visible.
	// The optional 'alpha' multiplier is applied on top of the parsed alpha.
	inline D2D1_COLOR_F CSSColorToD2D(const std::string& cssColor, float alpha = 1.0f) {
		auto clampf = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

		if (cssColor.empty()) return D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f); // Transparent

		// Lowercase + strip whitespace, locally.
		std::string s;
		s.reserve(cssColor.size());
		for (char c : cssColor) {
			unsigned char uc = (unsigned char)c;
			if (!isspace(uc)) s.push_back((char)tolower(uc));
		}

		if (s == "transparent" || s == "none") {
			return D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
		}

		// --- Hex forms ---
		if (!s.empty() && s[0] == '#') {
			const std::string h = s.substr(1);
			auto hexVal = [](char ch) -> int {
				if (ch >= '0' && ch <= '9') return ch - '0';
				if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
				return -1;
			};

			auto parseByte = [&](size_t i, int& outByte) -> bool {
				int hi = hexVal(h[i]), lo = hexVal(h[i + 1]);
				if (hi < 0 || lo < 0) return false;
				outByte = (hi << 4) | lo;
				return true;
			};

			int r = 0, g = 0, b = 0, a = 255;
			bool ok = false;

			if (h.size() == 3) {
				// #RGB -> each digit doubled
				int hr = hexVal(h[0]), hg = hexVal(h[1]), hb = hexVal(h[2]);
				if (hr >= 0 && hg >= 0 && hb >= 0) {
					r = (hr << 4) | hr;
					g = (hg << 4) | hg;
					b = (hb << 4) | hb;
					ok = true;
				}
			} else if (h.size() == 6) {
				ok = parseByte(0, r) && parseByte(2, g) && parseByte(4, b);
			} else if (h.size() == 8) {
				// #AARRGGBB
				ok = parseByte(0, a) && parseByte(2, r) && parseByte(4, g) && parseByte(6, b);
			}

			if (ok) {
				return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, clampf((a / 255.0f) * alpha));
			}
			// Malformed hex — fall through to black.
		}

		// --- rgb() / rgba() ---
		if (s.rfind("rgb", 0) == 0) {
			size_t open  = s.find('(');
			size_t close = s.find(')');
			if (open != std::string::npos && close != std::string::npos && close > open + 1) {
				std::string body = s.substr(open + 1, close - open - 1);
				// Split on comma
				std::vector<float> parts;
				size_t start = 0;
				while (start <= body.size()) {
					size_t comma = body.find(',', start);
					std::string seg = body.substr(start, (comma == std::string::npos) ? std::string::npos : comma - start);
					if (!seg.empty()) {
						try { parts.push_back(std::stof(seg)); } catch (...) { parts.clear(); break; }
					}
					if (comma == std::string::npos) break;
					start = comma + 1;
				}
				if (parts.size() >= 3) {
					float r = clampf(parts[0] / 255.0f);
					float g = clampf(parts[1] / 255.0f);
					float b = clampf(parts[2] / 255.0f);
					float a = 1.0f;
					if (parts.size() >= 4) {
						// CSS rgba alpha is 0..1 (float); some authors write 0..255 ints.
						a = parts[3];
						if (a > 1.0f) a = clampf(a / 255.0f);
						else          a = clampf(a);
					}
					return D2D1::ColorF(r, g, b, clampf(a * alpha));
				}
			}
		}

		// --- Named colors (small commonly-used table) ---
		struct NamedColor { const char* name; uint8_t r, g, b; };
		static const NamedColor kNamed[] = {
			{"black",   0,   0,   0},   {"white",   255, 255, 255},
			{"red",     255, 0,   0},   {"green",   0,   128, 0},
			{"blue",    0,   0,   255}, {"yellow",  255, 255, 0},
			{"cyan",    0,   255, 255}, {"magenta", 255, 0,   255},
			{"gray",    128, 128, 128}, {"grey",    128, 128, 128},
			{"silver",  192, 192, 192}, {"maroon",  128, 0,   0},
			{"olive",   128, 128, 0},   {"lime",    0,   255, 0},
			{"teal",    0,   128, 128}, {"navy",    0,   0,   128},
			{"purple",  128, 0,   128}, {"orange",  255, 165, 0},
			{"pink",    255, 192, 203}, {"brown",   165, 42,  42},
		};
		for (const auto& nc : kNamed) {
			if (s == nc.name) {
				return D2D1::ColorF(nc.r / 255.0f, nc.g / 255.0f, nc.b / 255.0f, clampf(alpha));
			}
		}

		// Unknown — visible-but-wrong (opaque black) so the user notices.
		return D2D1::ColorF(0.0f, 0.0f, 0.0f, clampf(alpha));
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

	// Forward Declarations
	class ContainerImpl;
	class WidgetImpl;

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

		// The singleton dies from the CRT's atexit table, long after the app
		// left its message loop and possibly after COM tore its apartment down
		// (CoUninitialize unloads in-proc servers such as windowscodecs.dll even
		// while references are outstanding). Calling Release() there is a jump
		// into unmapped memory, so the destructor deliberately LEAKS whatever
		// is still held: the OS reclaims it at process exit anyway. Apps that
		// want a clean release call Shutdown() themselves while COM is alive
		// (see ClaudeMM's wWinMain).
		~ChronoControllerImpl() {
			(void)m_pD2DFactory.Detach();
			(void)m_pDWriteFactory.Detach();
			(void)m_pWICFactory.Detach();
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

		// =============================================================
		// Animation Heartbeat
		// =============================================================
		// A single timer drives synchronized animation ticks for every widget
		// that called StartAnimation(). Replaces ad-hoc per-widget SetTimer(16ms)
		// calls and produces a real deltaTime via QueryPerformanceCounter.
		// Widgets opt in by calling WidgetImpl::StartAnimation()/StopAnimation().
		// The old SetTimer(CHRONOUI_ANIM_TIMER) path remains supported for
		// backward compatibility — both can coexist (don't enable both in one widget).
	private:
		std::vector<WidgetImpl*> m_animSubscribers;
		std::mutex               m_animMutex;
		HWND                     m_animHost = nullptr;
		LARGE_INTEGER            m_qpcFreq{};
		LARGE_INTEGER            m_qpcLast{};
		static const UINT_PTR    kAnimTimerId    = 1;
		static const UINT        kAnimIntervalMs = 16;  // ~60Hz target

	public:
		void SubscribeAnimation(WidgetImpl* w);
		void UnsubscribeAnimation(WidgetImpl* w);

	private:
		void EnsureAnimHost();
		void TickAllAnimations();
		static LRESULT CALLBACK AnimHostWndProc(HWND, UINT, WPARAM, LPARAM);
	public:

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

		// --- Style Cache (perf) ---
		// Avoids re-parsing CSS strings via stof/stoi/sscanf on every paint.
		// Cache is invalidated by:
		//   - SetProperty (any property change on this widget)
		//   - hover / focus state transitions in HandleMessage
		//   - manual InvalidateStyleCache() (call after StyleManager class changes,
		//     parent property changes, or global StyleManager::LoadCSS at runtime)
		// NOTE: Changes to global StyleManager state after construction are NOT detected
		// automatically. If you mutate global styles at runtime, call InvalidateStyleCache().
		uint64_t m_styleCacheGen = 1;        // bumped on each invalidate
		uint64_t m_styleCacheLastGen = 0;    // gen captured when cache was last refreshed
		uint8_t  m_styleCacheLastBits = 0xFF;// state bits captured at last refresh
		std::unordered_map<std::string, D2D1_COLOR_F> m_colorCache;
		std::unordered_map<std::string, float>        m_floatCache;
		std::unordered_map<std::string, int>          m_intCache;

		// Layout-dimension cache. Stores the PARSED form of width / height /
		// min / max etc., keyed by property name. The expensive string-to-typed
		// conversion (suffix detect + stof) happens once per change to the
		// property, not once per frame. Entries are dropped in SetProperty()
		// when the underlying string changes.
		std::unordered_map<std::string, LayoutDim> m_dimCache;

		// Animation heartbeat subscription state
		bool m_animSubscribed = false;

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
			StopAnimation();                                       // unsubscribe from heartbeat first
			// Use GetContextNode() — the implicit WidgetImpl* -> IContextNode* conversion
			// is ambiguous because IWidget and ContextNodeImpl both derive from
			// IContextNode along distinct paths. AddClass tracks the same pointer.
			StyleManager::Forget(this->GetContextNode());          // drop from StyleManager tracking
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

		// --- Scaling Helpers ---
		// The D2D render target is configured with the window DPI in CreateDeviceResources()
		// (via SetDpi). That means drawing coordinates are already in DIPs and do not need
		// per-call scaling. These helpers remain as identity passthroughs so widget code that
		// still calls ScaleF(...) for clarity keeps compiling. If you need pixel-space scaling
		// for HWND-level work (e.g. SetWindowPos), use the Scale() helpers in ChronoUI.cpp.
		int   ScaleI(int val)       { return val; }
		float ScaleF(float val)     { return val; }
		float ScaleSizeF(float val) { return val; }
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

		// --- Animation Heartbeat (preferred over per-widget SetTimer) ---
		// Subscribe to the centralized ChronoController heartbeat. While subscribed,
		// the controller will call OnUpdateAnimation(deltaTime) with real elapsed
		// seconds (measured via QueryPerformanceCounter) at ~60Hz, and InvalidateRect
		// will be issued automatically when OnUpdateAnimation returns true.
		//
		// Do NOT also use SetTimer(CHRONOUI_ANIM_TIMER, ...) in the same widget —
		// pick one path. The legacy SetTimer route still works for older code.
		void StartAnimation();
		void StopAnimation();

		// Called by ChronoControllerImpl on each heartbeat tick. Public so the
		// controller can invoke it through a WidgetImpl* without friendship.
		void TickAnimation(float dt) {
			if (OnUpdateAnimation(dt)) {
				HWND target = m_overlayHost ? m_overlayHost->GetHWND() : m_hwnd;
				if (target) InvalidateRect(target, NULL, FALSE);
			}
		}

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

		void __stdcall OnChanged(void (*cb)(IWidget*, void*), void* ctx, void (*cleanup)(void*)) override {
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

		// ---------------------------------------------------------------------
		// Style cache helpers (perf path for hot OnDrawWidget code)
		// ---------------------------------------------------------------------
		// Bumps the cache generation. Subsequent Cached* calls will re-resolve.
		void InvalidateStyleCache() { m_styleCacheGen++; }

		// State bits that affect style resolution. If any change, the cache is stale.
		uint8_t CurrentStyleStateBits() const {
			uint8_t b = 0;
			if (m_focused)                             b |= 0x01;
			if (m_hwnd && ::IsWindowEnabled(m_hwnd))   b |= 0x02;
			if (m_isHovered && m_hoverActive)          b |= 0x04;
			if (m_checked)                             b |= 0x08;
			if (m_validated)                           b |= 0x10;
			return b;
		}

		// Ensures the cache reflects the current generation + state. Clears all
		// cached entries if either changed. O(1) when nothing changed.
		void EnsureStyleCacheFresh() {
			uint8_t curBits = CurrentStyleStateBits();
			if (m_styleCacheLastGen != m_styleCacheGen || m_styleCacheLastBits != curBits) {
				m_colorCache.clear();
				m_floatCache.clear();
				m_intCache.clear();
				m_styleCacheLastGen  = m_styleCacheGen;
				m_styleCacheLastBits = curBits;
			}
		}

		// Cached lookup for a CSS color resolved against the current widget state.
		D2D1_COLOR_F CachedCSSColor(const char* key) {
			EnsureStyleCacheFresh();
			auto it = m_colorCache.find(key);
			if (it != m_colorCache.end()) return it->second;
			D2D1_COLOR_F val = GetCSSColorStyle(key);
			m_colorCache.emplace(key, val);
			return val;
		}

		// Cached lookup for a CSS float (e.g. border-width, margin-*). Returns 'def' on parse failure.
		float CachedCSSFloat(const char* key, float def = 0.0f) {
			EnsureStyleCacheFresh();
			auto it = m_floatCache.find(key);
			if (it != m_floatCache.end()) return it->second;

			const char* ctrl = GetControlName();
			std::string sub = GetProperty("subclass");
			const char* r = GetStyle(key, std::to_string(def).c_str(), ctrl, sub.c_str(),
				m_focused, m_isEnabled, m_hoverActive&m_isHovered, m_checked);
			float v = def;
			if (r) { try { v = std::stof(std::string(r)); } catch (...) { v = def; } }
			m_floatCache.emplace(key, v);
			return v;
		}

		// Cached lookup for a CSS int. Mirrors GetCSSIntStyle but memoized.
		int CachedCSSInt(const char* key, int def = 0) {
			EnsureStyleCacheFresh();
			auto it = m_intCache.find(key);
			if (it != m_intCache.end()) return it->second;
			int v = GetCSSIntStyle(key, def);
			m_intCache.emplace(key, v);
			return v;
		}

		virtual IWidget* __stdcall SetProperty(const char* key, const char* value) override {
			m_properties[key] = (value) ? value : "";
			InvalidateStyleCache();
			// Drop any cached parse of this same key — it will lazily re-parse on
			// next GetCachedLayoutDim() read. Cheap O(1) erase, no-op if not cached.
			if (key) m_dimCache.erase(key);
			OnPropertyChanged(key, value);
			return this;
		}

		// Lazy-parsed layout dimension. Returns Invalid kind if the property
		// isn't set on this widget. Caches the parse so subsequent reads
		// (every paint frame) skip the string ops entirely.
		LayoutDim GetCachedLayoutDim(const char* key) {
			if (!key) return LayoutDim{};
			auto it = m_dimCache.find(key);
			if (it != m_dimCache.end()) return it->second;

			// Note: we look only at this widget's own m_properties — not the
			// inherited GetProperty chain. Layout dimensions (width / height /
			// min / max) are intentionally per-widget and shouldn't bleed in
			// from ancestors.
			auto pit = m_properties.find(key);
			LayoutDim d;
			if (pit != m_properties.end()) d = ParseLayoutDim(pit->second);
			m_dimCache.emplace(key, d);
			return d;
		}
		IWidget* SetBoolProperty(const char* key, bool value) {
			return SetProperty(key, value ? "true" : "false");
		}
		// Delegates to the namespace-level CSSColorToD2D so that widget code and
		// the framework's core paths share one comprehensive parser
		// (hex / short hex / rgb / rgba / named colors / transparent).
		D2D1_COLOR_F CSSToD2DColor(const std::string& cssColor) {
			return CSSColorToD2D(cssColor);
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

		// NOTE: DrawWidgetBackground intentionally uses direct GetStyle calls (not the
		// CachedCSS* helpers) because its hover gating is per-call (`hovereffect`),
		// not per-widget (`m_hoverActive`). Migrating it to the cache would change
		// hover semantics for the ~5 widgets that pass hovereffect=true without
		// having m_hoverActive set. Widget authors who want the cache for *their*
		// custom drawing should call CachedCSSColor/CachedCSSFloat/CachedCSSInt
		// directly — those keys against (m_isHovered && m_hoverActive), matching
		// GetCSSColorStyle/GetCSSIntStyle.
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

			// Font Style / Weight resolution.
			// Two CSS conventions are supported:
			//   * font-style: bold | italic | normal   (engine-specific shortcut)
			//   * font-weight: bold | 100..900         (real CSS)
			// Either can produce DWRITE_FONT_WEIGHT_BOLD. font-style can also produce italic.
			std::string style  = GetStyle("font-style",  "normal", cname, subclass.c_str(), m_focused, isEnabled, hover);
			std::string weight = GetStyle("font-weight", "",       cname, subclass.c_str(), m_focused, isEnabled, hover);

			DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL;
			DWRITE_FONT_STYLE  fontStyle  = DWRITE_FONT_STYLE_NORMAL;

			// font-style first (engine shortcut path)
			if (style == "bold") {
				fontWeight = DWRITE_FONT_WEIGHT_BOLD;
			} else if (style == "italic") {
				fontStyle = DWRITE_FONT_STYLE_ITALIC;
			}

			// font-weight overrides/augments (real CSS)
			if (!weight.empty()) {
				if (weight == "bold" || weight == "bolder") {
					fontWeight = DWRITE_FONT_WEIGHT_BOLD;
				} else if (weight == "normal" || weight == "lighter") {
					// leave at normal
				} else {
					try {
						int w = std::stoi(weight);
						if      (w >= 900) fontWeight = DWRITE_FONT_WEIGHT_BLACK;
						else if (w >= 800) fontWeight = DWRITE_FONT_WEIGHT_EXTRA_BOLD;
						else if (w >= 700) fontWeight = DWRITE_FONT_WEIGHT_BOLD;
						else if (w >= 600) fontWeight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
						else if (w >= 500) fontWeight = DWRITE_FONT_WEIGHT_MEDIUM;
						else if (w >= 400) fontWeight = DWRITE_FONT_WEIGHT_NORMAL;
						else if (w >= 300) fontWeight = DWRITE_FONT_WEIGHT_LIGHT;
						else if (w >= 200) fontWeight = DWRITE_FONT_WEIGHT_EXTRA_LIGHT;
						else if (w >= 100) fontWeight = DWRITE_FONT_WEIGHT_THIN;
					} catch (...) { /* leave at current value */ }
				}
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
			case WM_NCHITTEST: {
				// Widgets that opt in as "drag-through" hand mouse events up to
				// their parent. The main use case: passive labels in a custom
				// title bar should let the user grab and drag the window, not
				// consume the click themselves. Set via SetProperty("drag-through",
				// "true"). StaticText sets this by default.
				auto it = m_properties.find("drag-through");
				if (it != m_properties.end() && it->second == "true") {
					return HTTRANSPARENT;
				}
				break; // fall through to DefWindowProc → HTCLIENT
			}
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

	// =================================================================
	// Animation heartbeat: inline definitions
	// =================================================================
	// Defined here because they bridge WidgetImpl (above) and
	// ChronoControllerImpl (declared above WidgetImpl). Both classes are
	// fully defined by this point in the translation unit.

	inline void WidgetImpl::StartAnimation() {
		if (!m_animSubscribed) {
			ChronoControllerImpl::Instance().SubscribeAnimation(this);
			m_animSubscribed = true;
		}
	}

	inline void WidgetImpl::StopAnimation() {
		if (m_animSubscribed) {
			ChronoControllerImpl::Instance().UnsubscribeAnimation(this);
			m_animSubscribed = false;
		}
	}

	inline void ChronoControllerImpl::SubscribeAnimation(WidgetImpl* w) {
		if (!w) return;
		{
			std::lock_guard<std::mutex> lock(m_animMutex);
			if (std::find(m_animSubscribers.begin(), m_animSubscribers.end(), w) == m_animSubscribers.end()) {
				m_animSubscribers.push_back(w);
			}
		}
		EnsureAnimHost();
	}

	inline void ChronoControllerImpl::UnsubscribeAnimation(WidgetImpl* w) {
		std::lock_guard<std::mutex> lock(m_animMutex);
		m_animSubscribers.erase(
			std::remove(m_animSubscribers.begin(), m_animSubscribers.end(), w),
			m_animSubscribers.end()
		);
		// We intentionally keep m_animHost alive even when idle — recreating it on each
		// subscribe/unsubscribe burst would churn the timer. The OS will reclaim it at
		// process exit. If you want explicit teardown, call DestroyWindow(m_animHost)
		// from your app shutdown path.
	}

	// __ImageBase is the linker-emitted symbol that resolves to each module's
	// load address. By using it as our HINSTANCE and as a discriminator in the
	// window-class name, every widget DLL ends up with its OWN class registered
	// against its OWN AnimHostWndProc — so DLL_B's heartbeat ticks don't get
	// routed to DLL_A's WndProc (which would tick DLL_A's subscribers, leaving
	// DLL_B's widgets frozen). This is the per-DLL singleton workaround.
	extern "C" IMAGE_DOS_HEADER __ImageBase;
	inline HINSTANCE ChronoUI_ThisModule() { return (HINSTANCE)&__ImageBase; }

	inline void ChronoControllerImpl::EnsureAnimHost() {
		if (m_animHost) return;

		HINSTANCE hInst = ChronoUI_ThisModule();

		// Per-DLL class name. Use the module handle as a hex discriminator.
		wchar_t kClass[64];
		swprintf_s(kClass, 64, L"ChronoUI.AnimHost.%p", (void*)hInst);

		WNDCLASSW wc = { 0 };
		if (!GetClassInfoW(hInst, kClass, &wc)) {
			wc = {};
			wc.lpfnWndProc = AnimHostWndProc;
			wc.hInstance = hInst;
			wc.lpszClassName = kClass;
			RegisterClassW(&wc);
		}

		// Message-only window — never displayed, just hosts the timer.
		m_animHost = CreateWindowExW(0, kClass, L"", 0,
			0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);

		if (m_animHost) {
			QueryPerformanceFrequency(&m_qpcFreq);
			QueryPerformanceCounter(&m_qpcLast);
			SetTimer(m_animHost, kAnimTimerId, kAnimIntervalMs, NULL);
		}
	}

	inline void ChronoControllerImpl::TickAllAnimations() {
		// Compute real elapsed seconds since last tick.
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		float dt = 0.016f;
		if (m_qpcFreq.QuadPart > 0) {
			double seconds = (double)(now.QuadPart - m_qpcLast.QuadPart) / (double)m_qpcFreq.QuadPart;
			// Clamp to guard against huge jumps after debugger breaks or system sleep.
			if (seconds > 0.0 && seconds < 0.25) dt = (float)seconds;
		}
		m_qpcLast = now;

		// Snapshot under lock so subscribers can safely Stop/Start during a tick.
		std::vector<WidgetImpl*> snapshot;
		{
			std::lock_guard<std::mutex> lock(m_animMutex);
			snapshot = m_animSubscribers;
		}
		for (WidgetImpl* w : snapshot) {
			if (w) w->TickAnimation(dt);
		}
	}

	inline LRESULT CALLBACK ChronoControllerImpl::AnimHostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
		if (msg == WM_TIMER && wp == kAnimTimerId) {
			ChronoControllerImpl::Instance().TickAllAnimations();
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}
