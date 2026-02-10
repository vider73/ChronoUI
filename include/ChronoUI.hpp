#pragma once
#define NOMINMAX
#include <windows.h>
#include <functional>
#include <string>
#include <sstream>
#include <map>
#include <memory>

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

#pragma warning(disable:4100)

#ifdef CHRONOUI_EXPORTS
#define CHRONO_API __declspec(dllexport)
#else
#define CHRONO_API __declspec(dllimport)
#endif

namespace ChronoUI {
	enum class SizeUnit { Pixels, Percent, Fill, System };
	// 1. Expanded Enum with Common Windows UI Definitions
	enum class StandardMetric {
		// Window Chrome
		TitleBar,           // Height of the caption area
		TitleBarThin,       // Height of the caption area
		CaptionButton,      // Width/Height of Minimize/Close buttons
		WindowMenu,			// Width/Height of Minimize/Close buttons

		// Menus & Toolbars
		MenuHeight,         // Height of a standard menu bar
		Toolbar,            // Height of a main application toolbar
		ToolbarButtonHeight,		// Size (width/height) of a standard toolbar button
		ToolbarButtonLabeledWidth,			// Size (width/height) of a standard toolbar button
		StatusBarHeight,    // Height of the bottom status bar

		EditBoxHeight,    // Height of the bottom status bar

		// Icons
		IconSmall,          // 16x16 logical
		IconMedium,         // 24x24 logical
		IconLarge,          // 32x32 logical

		// Controls
		ScrollbarWidth,     // Width of a vertical scrollbar
		CheckBoxSize,       // Size of a checkbox square

		// Layout
		Padding,            // Small padding (between elements)
		Margin,             // Large margin (between groups)
		Border              // Standard border width
	};

	enum class StackMode {
		Vertical, Horizontal, Tabbed, CommandBar
	};

	struct WidgetSize {
		SizeUnit unit;
		float value;
		StandardMetric metric;
		static WidgetSize Fixed(float px) { return { SizeUnit::Pixels, px, {} }; }
		static WidgetSize Fill(float weight = 1.0f) { return { SizeUnit::Fill, weight, {} }; }
		static WidgetSize Percent(float p) { return { SizeUnit::Percent, p, {} }; }
		static WidgetSize System(StandardMetric m) { return { SizeUnit::System, 0, m }; }
	};

	// Helper for DPI scaling if not available globally
	inline int GetDpiForWindowShim(HWND hwnd) {
		HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
		typedef UINT(WINAPI* GetDpiForWindowPtr)(HWND);
		auto getDpi = (GetDpiForWindowPtr)GetProcAddress(hUser32, "GetDpiForWindow");
		if (getDpi) return getDpi(hwnd);
		HDC hdc = GetDC(hwnd);
		int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
		ReleaseDC(hwnd, hdc);
		return dpi;
	}	
	inline float ScaleF(float pixel) {
		UINT dpi = GetDpiForWindowShim(GetDesktopWindow());
		return (pixel * (float)dpi) / 96.0f;
	}

	/*
	inline Gdiplus::Color ToGdiPlusColor(COLORREF c, BYTE alpha = 255) {
		return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
	}
	template <typename T>
	inline T ClampColorVal(T val, T minVal, T maxVal) {
		return (val < minVal) ? minVal : ((val > maxVal) ? maxVal : val);
	}

	inline Gdiplus::Color ToGdiPlusColor(std::string color, BYTE defaultAlpha = 255) {
		if (color.empty()) return Gdiplus::Color(0, 0, 0, 0);

		// 1. Cleanup: Remove whitespace and convert to lower case manually
		//    to avoid <algorithm> dependencies and complex iterators.
		std::string clean;
		clean.reserve(color.length());

		for (char ch : color) {
			unsigned char c = static_cast<unsigned char>(ch);
			if (!isspace(c)) {
				clean += static_cast<char>(tolower(c));
			}
		}

		BYTE r = 0, g = 0, b = 0;
		BYTE a = defaultAlpha;

		try {
			// 2. Handle Hex Formats
			if (clean[0] == '#') {
				std::string hexStr = clean.substr(1);
				unsigned long hex = std::stoul(hexStr, nullptr, 16);
				size_t len = hexStr.length();

				if (len == 6) {
					// #RRGGBB
					r = (hex >> 16) & 0xFF;
					g = (hex >> 8) & 0xFF;
					b = hex & 0xFF;
				}
				else if (len == 3) {
					// #RGB -> #RRGGBB
					r = ((hex >> 8) & 0xF) * 17;
					g = ((hex >> 4) & 0xF) * 17;
					b = (hex & 0xF) * 17;
				}
			}
			// 3. Handle RGB / RGBA
			else if (clean.find("rgb") == 0) {
				size_t openParen = clean.find('(');
				size_t closeParen = clean.find(')');

				if (openParen != std::string::npos && closeParen > openParen) {
					std::string content = clean.substr(openParen + 1, closeParen - openParen - 1);
					std::stringstream ss(content);
					std::string segment;
					std::vector<float> vals;

					while (std::getline(ss, segment, ',')) {
						if (!segment.empty()) {
							vals.push_back(std::stof(segment));
						}
					}

					if (vals.size() >= 3) {
						r = static_cast<BYTE>(ClampColorVal(vals[0], 0.0f, 255.0f));
						g = static_cast<BYTE>(ClampColorVal(vals[1], 0.0f, 255.0f));
						b = static_cast<BYTE>(ClampColorVal(vals[2], 0.0f, 255.0f));

						// Handle Alpha if present
						if (vals.size() > 3) {
							float alphaVal = vals[3];
							// CSS alpha is often 0.0 to 1.0, GDI+ is 0 to 255.
							// If value is small (<=1.0), assume float format.
							if (alphaVal <= 1.0f && alphaVal >= 0.0f) {
								alphaVal *= 255.0f;
							}
							a = static_cast<BYTE>(ClampColorVal(alphaVal, 0.0f, 255.0f));
						}
					}
				}
			}
		}
		catch (...) {
			// Safe fallback for parsing errors
			return Gdiplus::Color(255, 0, 0, 0);
		}

		return Gdiplus::Color(a, r, g, b);
	}*/
	// Helper: Calculates the current scale factor (e.g., 1.0 for 100%, 1.5 for 150%)
	inline float GetWindowScaleFactor() {
		// Requires Windows 10 (v1607) or newer for GetDpiForWindow.
		// Ensure you are linking with User32.lib and have a valid DPI-aware manifest.
		UINT dpi = 96;

		// Check if we have a valid HWND from the class member function
		HWND hwnd = GetDesktopWindow();
		if (hwnd != NULL) {
			dpi = ::GetDpiForWindow(hwnd);
		}
		else {
			// Fallback for cases where HWND isn't ready, or use GetDC(NULL) for system DPI
			HDC hdc = ::GetDC(NULL);
			dpi = ::GetDeviceCaps(hdc, LOGPIXELSX);
			::ReleaseDC(NULL, hdc);
		}

		// Standard Windows DPI is 96
		return static_cast<float>(dpi) / 96.0f;
	}
	inline int ScaleSize(int baseSize) {
		float factor = GetWindowScaleFactor();
		// round() gives better visual results than simple truncation
		return static_cast<int>(std::round(baseSize * factor));
	}

	// Helper: Scales a float size (optional, useful for GDI+ coordinates)
	inline float ScaleSize(float baseSize) {
		return baseSize * GetWindowScaleFactor();
	}

	inline std::string WideToNarrow(const std::wstring& wstr) {
		if (wstr.empty()) return std::string();
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
		std::string strTo(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
		return strTo;
	}

	// Helper for Narrow (UTF-8) -> Wide
	inline std::wstring NarrowToWide(const std::string& str) {
		if (str.empty()) return std::wstring();
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
		std::wstring wstrTo(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
		return wstrTo;
	}

	// 2. The Implementation
	inline float getStandardMetric(StandardMetric metric) {
		float baseSize = 0;

		// These values represent the Standard Windows sizes at 96 DPI (100% Scale).
		// We define the logical size here, and rely on ScaleSize() to adjust for the monitor.
		switch (metric) {
			// --- Window Chrome ---
		case StandardMetric::TitleBar:
			// Modern Windows title bars are taller (~30px) than legacy (~23px)
			baseSize = 30.0f;
			break;
		case StandardMetric::TitleBarThin:
			// Modern Windows title bars are taller (~30px) than legacy (~23px)
			baseSize = 20.0f;
			break;
		case StandardMetric::CaptionButton:
			baseSize = 18.0f;
			break;
		case StandardMetric::WindowMenu:
			baseSize = 256.0f;
			break;
			

			// --- Menus & Toolbars ---
		case StandardMetric::MenuHeight:
			baseSize = 20.0f;
			break;
		case StandardMetric::Toolbar:
			// Standard height to accommodate a 24px icon + padding
			baseSize = 20.0f;
			break;
		case StandardMetric::ToolbarButtonHeight:
			// Standard button for a 24px icon
			baseSize = 14.0f;
			break;
		case StandardMetric::ToolbarButtonLabeledWidth:
			// Standard button for a 24px icon
			baseSize = 96.0f;
			break;
		case StandardMetric::StatusBarHeight:
			baseSize = 22.0f;
			break;
		case StandardMetric::EditBoxHeight:
			baseSize = 16.0f;
			break;
			// --- Icons ---
		case StandardMetric::IconSmall:
			baseSize = 16.0f;
			break;
		case StandardMetric::IconMedium:
			baseSize = 24.0f;
			break;
		case StandardMetric::IconLarge:
			baseSize = 32.0f;
			break;

			// --- Controls ---
		case StandardMetric::ScrollbarWidth:
			baseSize = 17.0f; // Standard Windows scrollbar width
			break;
		case StandardMetric::CheckBoxSize:
			baseSize = 13.0f;
			break;

			// --- Layout ---
		case StandardMetric::Padding:
			baseSize = 4.0f;
			break;
		case StandardMetric::Margin:
			baseSize = 10.0f;
			break;
		case StandardMetric::Border:
			baseSize = 1.0f;
			break;
		}

		// Apply your DPI scaling helper
		return (float)ScaleSize((float)baseSize);
	}

	// Observable Variable Template
	template <typename T>
	class ChronoObservable {
		T _value;
		// Use a map to allow removal of listeners
		std::map<int, std::function<void(const T&)>> _subscribers;
		int _nextId = 0;

	public:
		ChronoObservable(T initial) : _value(initial) {}

		void operator=(const T& newValue) {
			if (_value != newValue) {
				_value = newValue;
				Notify();
			}
		}

		operator const T& () const { return _value; }
		const T& get() const { return _value; }

		// Returns an ID used to unsubscribe
		int OnChange(std::function<void(const T&)> callback) {
			int id = ++_nextId;
			_subscribers[id] = callback;
			return id;
		}

		void Unsubscribe(int id) {
			_subscribers.erase(id);
		}

	private:
		void Notify() {
			for (auto const& [id, callback] : _subscribers) {
				callback(_value);
			}
		}
	};
	template<typename T>
	struct ValueConverter {
		static std::string To(const T& val) { return std::to_string(val); }
		static T From(const char* val) { return static_cast<T>(val ? std::atof(val) : 0); }
	};

	// Specialization for std::string
	template<>
	struct ValueConverter<std::string> {
		static std::string To(const std::string& val) { return val; }
		static std::string From(const char* val) { return val ? std::string(val) : ""; }
	};

	// Specialization for bool (maps to "true"/"false")
	template<>
	struct ValueConverter<bool> {
		static std::string To(bool val) { return val ? "true" : "false"; }
		static bool From(const char* val) {
			if (!val) return false;
			std::string s = val;
			return (s == "true" || s == "1");
		}
	};

	class IContextNode;
	class ChronoController {
	public:
		// Move CHRONO_API to the very front to satisfy MSVC parser
		CHRONO_API static IContextNode* __stdcall Instance();
	};

	// Base interface for anything that allows property inheritance
	class IContextNode {
	public:
		// Mechanism to traverse up
		virtual IContextNode* __stdcall GetParentNode() = 0;
		virtual IContextNode* __stdcall GetContextNode() = 0;

		// Mechanism to set the parent (usually called automatically during creation/add)
		virtual void __stdcall SetParentNode(IContextNode* parent) = 0;

		// The core property retriever
		virtual const char* __stdcall GetProperty(const char* key, const char* def=0) = 0;

		// The core property setter (Moved up from IWidget/IContainer)
		// Returns IContextNode* to allow chaining at the base level.
		// Derived classes can override this with covariant return types (e.g. returning IWidget*).
		virtual IContextNode* __stdcall SetProperty(const char* key, const char* value) = 0;
		virtual const char* GetStyle(const char* _prop, const char* _def, const char* _classid, const char* _subclass, bool selected, bool enabled, bool hovered, bool active) = 0;

	public:
		// Color Setter Helper
		virtual IContextNode* SetColor(const char* key, COLORREF color) = 0;
		// Color Getter Helper
		virtual COLORREF GetColor(const char* key, COLORREF defaultColor = RGB(0, 0, 0)) = 0;
	};

	// Widget event handler
	class IWidget;
	typedef void(__stdcall* ChronoEventCallback)(const char* eventName, const char* jsonPayload, void* pContext);
	typedef void(__stdcall* ChronoEventCleanup)(void* pContext);

	typedef bool(__stdcall* ChronoValidationCallback)(IWidget* sender, const char* value, void* pContext);
	// Clean up the user context (lambda)
	typedef void(__stdcall* ChronoValidationCleanup)(void* pContext);

	class IWidget : public IContextNode {
	public:
		virtual ~IWidget() = default;

		virtual const char* __stdcall GetControlName() = 0;
		virtual const char* __stdcall GetControlManifest() = 0; // JSON string: { "properties": [...], "events": [...], "methods": [...] }

		virtual void __stdcall Create(HWND parent) = 0;
		virtual void __stdcall SetBounds(int x, int y, int w, int h) = 0;
		virtual void __stdcall OnFocus(bool focused) = 0;
		virtual HWND __stdcall GetHWND() = 0;
		virtual void __stdcall Destroy() = 0;
		virtual void __stdcall SetWidgetHost(IWidget* host) = 0;

		// Features
		virtual IWidget* AddTimer(const char* eventName, int milliseconds) = 0;
		virtual IWidget* AddOneShotTimer(const char* eventName, int milliseconds) = 0;

		// Overlay support
		virtual IWidget*__stdcall AddOverlay(IWidget* overlay) = 0;
		virtual void __stdcall RemoveOverlay(IWidget* overlay) = 0;


		// ---------------------------------------------------------
		// NEW: Validation Logic
		// ---------------------------------------------------------

		virtual bool __stdcall PerformValidation(const char* newValue) = 0;

		// 1. The ABI-safe virtual method (Implemented in DLL)
		virtual void __stdcall RegisterValidator(ChronoValidationCallback callback, void* pContext, ChronoValidationCleanup cleanup) = 0;

		// 2. The Helper for C++ Lambdas (Client side)
		template<typename F>
		IWidget* OnValidate(F&& lambda) {
			// Define context to hold the lambda
			// Signature: bool(IWidget* sender, const char* value)
			using FuncType = std::function<bool(IWidget*, const char*)>;
			struct ValidatorContext {
				FuncType func;
			};

			// Create context on the heap
			auto* ctx = new ValidatorContext{
				FuncType(std::forward<F>(lambda))
			};

			// Bridge: Calls the C++ lambda from the C function pointer
			ChronoValidationCallback execBridge = [](IWidget* sender, const char* value, void* pCtx) -> bool {
				auto* vc = static_cast<ValidatorContext*>(pCtx);
				if (vc && vc->func) {
					return vc->func(sender, value);
				}
				return true; // Default to valid if no func
			};

			// Cleanup: Deletes the context when the widget is destroyed or validator replaced
			ChronoValidationCleanup cleanupBridge = [](void* pCtx) {
				delete static_cast<ValidatorContext*>(pCtx);
			};

			// Register via the virtual method
			this->RegisterValidator(execBridge, ctx, cleanupBridge);

			const char* currentVal = this->GetProperty("value");
			if (currentVal) {
				PerformValidation(currentVal ? currentVal : "");
			} else {
				const char* _currentVal = this->GetProperty("text");
				if (_currentVal) {
					PerformValidation(_currentVal ? _currentVal : "");
				}
			}

			return this; // Allow chaining
		}

		////////////////////////////////////////////////////////////////////////////
		// Inline Binding
		
		// Virtual methods to be implemented on the DLL side
		// In class IWidget...
		virtual void OnChanged(void (*callback)(IWidget* target, void* context), void* context, void (*cleanup)(void*) = nullptr) = 0;

		// Bind a UI property (key) to an Observable string variable.
		// Supports chaining: widget->Bind("text", a)->Bind("value", b);
		template <typename T>
		IWidget* Bind(const char* propertyName, std::shared_ptr<ChronoObservable<T>> obs) {
			if (!obs) return this;

			// Store property name in a string for lambda capture
			std::string key = propertyName;

			// --- 1. INITIAL STATE (Observable -> UI) ---
			// Convert T to string and set the property
			this->SetProperty(key.c_str(), ValueConverter<T>::To(obs->get()).c_str());

			// --- 2. OBSERVABLE -> UI (External Code updates Variable) ---
			int listenerId = obs->OnChange([this, key](const T& newValue) {
				// Convert new value to string
				std::string newStr = ValueConverter<T>::To(newValue);

				// Get current UI value to avoid infinite loops/unnecessary redraws
				const char* currentPtr = this->GetProperty(key.c_str());
				std::string currentStr = currentPtr ? currentPtr : "";

				if (currentStr != newStr) {
					this->SetProperty(key.c_str(), newStr.c_str());
				}
			});

			// Cleanup: Unsubscribe when widget is destroyed
			// We capture 'obs' (shared_ptr) to ensure it stays alive until the widget dies
			struct DestroyCtx {
				std::shared_ptr<ChronoObservable<T>> obs;
				int id;
			};
			// Note: We use a lightweight lambda here for the destroy event
			this->addEventHandler("onDestroy", [listenerId, obs](IWidget* w, const char* json) {
				obs->Unsubscribe(listenerId);
			});

			// --- 3. UI -> OBSERVABLE (User updates Control) ---

			// Context to hold data for the C-style callback
			struct BindCtx {
				std::shared_ptr<ChronoObservable<T>> obs;
				std::string key;
			};
			BindCtx* context = new BindCtx{ obs, key };

			// The logic causing the Observable to update
			auto uiToObsLogic = [](IWidget* target, void* ctx) {
				BindCtx* bCtx = static_cast<BindCtx*>(ctx);

				// Get raw string from UI
				const char* rawVal = target->GetProperty(bCtx->key.c_str());

				// Convert string -> T
				T val = ValueConverter<T>::From(rawVal);

				// Update Observable if different
				if (bCtx->obs->get() != val) {
					*(bCtx->obs) = val;
				}
			};

			// Cleanup function to delete BindCtx
			auto uiCleanup = [](void* ctx) {
				delete static_cast<BindCtx*>(ctx);
			};

			// Register the change listener
			this->OnChanged(uiToObsLogic, context, uiCleanup);

			return this;
		}

		virtual IWidget* __stdcall RegisterEventHandler(const char* eventName, ChronoEventCallback callback, void* pContext, ChronoEventCleanup cleanup) = 0;
		virtual void __stdcall ClearEventHandlers() = 0;
		virtual void __stdcall FireEvent(const char* eventName, const char* jsonPayload) = 0;

		// Helper template for C++11 lambdas (Header-only, lives on EXE side)
		template<typename F>
		IWidget* addEventHandler(const char* eventName, F&& lambda) {
			// Define context to hold the lambda and the widget pointer
			using FuncType = std::function<void(IWidget*, const char*)>;
			struct HandlerContext {
				FuncType func;
				IWidget* widget;
			};

			// Create context on the caller's heap
			auto* ctx = new HandlerContext{
				FuncType(std::forward<F>(lambda)),
				this
			};

			// Bridge: Unpacks the context and executes the lambda
			ChronoEventCallback execBridge = [](const char* name, const char* json, void* pCtx) {
				(void)name; // <--- This silences the warning
				auto* hc = static_cast<HandlerContext*>(pCtx);
				if (hc && hc->func) {
					hc->func(hc->widget, json);
				}
			};

			// Cleanup: Deletes the context (prevents memory leaks)
			ChronoEventCleanup cleanupBridge = [](void* pCtx) {
				delete static_cast<HandlerContext*>(pCtx);
			};

			// Register via the virtual ABI-safe method
			this->RegisterEventHandler(eventName, execBridge, ctx, cleanupBridge);

			return this; // Fluent API
		}

		// Covariant Override: Returns IWidget* instead of IContextNode* for method chaining specific to Widgets
		virtual IWidget* __stdcall SetProperty(const char* key, const char* value) override = 0;
		// Returns a pointer owned by the widget. Valid until the next call or destruction.
		// (GetProperty is already defined in IContextNode, but we can override if needed, though not strictly required if signature matches)
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override = 0;
		virtual IWidget* SetColor(const char* key, COLORREF color) override = 0;

		virtual void OnDrawWidget(ID2D1RenderTarget* pRT) = 0;
		virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) = 0;
	};

	class ILayout;
	class ICell : public IContextNode {
	public:
		virtual IWidget* __stdcall AddWidget(IWidget* widget) = 0;
		virtual void __stdcall RemoveWidget(IWidget* widget) = 0;
		virtual IWidget* __stdcall GetWidget(int idx) = 0;
		virtual void __stdcall DetachWidget(IWidget* w) = 0;
		virtual void __stdcall AdoptWidget(IWidget* w) = 0;
		virtual void __stdcall RemoveAllWidgets() = 0;

		virtual ILayout* __stdcall CreateLayout(int rows, int cols) = 0;
		virtual ILayout* __stdcall GetNestedLayout() = 0;

		virtual void __stdcall UpdateWidgets() = 0;

		virtual void __stdcall SetStackMode(StackMode mode) = 0;

		virtual void __stdcall SetActiveTab(int index) = 0;
		virtual void __stdcall EnableScroll(bool enable) = 0;

		virtual ICell* __stdcall SetProperty(const char* key, const char* value) override = 0;
	};

	class ILayout : public IContextNode {
	public:
		virtual ~ILayout() = default;

		virtual void __stdcall SetRow(int index, WidgetSize size, bool splitter = false, WidgetSize min = { SizeUnit::Pixels, 0 }) = 0;
		virtual void __stdcall SetCol(int index, WidgetSize size, bool splitter = false, WidgetSize min = { SizeUnit::Pixels, 0 }) = 0;
		virtual ICell* __stdcall GetCell(int row, int col) = 0;
		virtual ICell* __stdcall GetCell(const char* name) = 0;
		virtual void __stdcall SetCellName(int row, int col, const char* name) = 0;
		

		virtual void __stdcall Arrange(int x, int y, int w, int h) = 0;

		virtual ILayout* __stdcall SetProperty(const char* key, const char* value) override = 0;

		virtual void __stdcall CollapseColumn(int index) = 0;
		virtual void __stdcall RestoreColumn(int index) = 0;
		virtual bool __stdcall IsColumnCollapsed(int index) = 0;
	};

	class IContainer : public IContextNode {
	public:
		virtual ~IContainer() = default;

		virtual ILayout* __stdcall CreateRootLayout(int rows, int cols) = 0;
		
		virtual void __stdcall Redraw() = 0;
		virtual void __stdcall Show() = 0;
		virtual void __stdcall ShowPopup(int w, int h) = 0;
		virtual void __stdcall Maximize() = 0;
		virtual void __stdcall CenterToParent() = 0;
		virtual HWND __stdcall GetHWND() = 0;

		virtual void __stdcall RunMessageLoop() = 0;
		virtual void __stdcall DoModal() = 0;

		virtual void __stdcall RegisterWidget(IWidget* w) = 0;
		virtual void __stdcall UnregisterWidget(IWidget* w) = 0;

		virtual IContainer* __stdcall SetProperty(const char* key, const char* value) override = 0;

		virtual IWidget* __stdcall SetOverlay(IWidget* w) = 0;
		virtual IWidget* __stdcall GetOverlay() = 0;
	};

	class WidgetFactory {
	public:
		// Creates the widget and tracks it
		static CHRONO_API IWidget* Create(const char* name);

		// Properly removes from tracking and calls widget->Destroy()
		static CHRONO_API void Destroy(IWidget* widget);

		// Optional: Destroys all widgets managed by the factory (Cleanup)
		static CHRONO_API void DestroyAll();
	};
	// Client-side RAII helper: Automatically calls Factory::Destroy
		// This allows the client to do: WidgetPtr<IWidget> btn = Factory::Create("button");
	template<typename T>
	class WidgetPtr {
		T* ptr;
	public:
		WidgetPtr(T* p = nullptr) : ptr(p) {}
		~WidgetPtr() { if (ptr) WidgetFactory::Destroy(ptr); }
		T* operator->() { return ptr; }
		T* get() { return ptr; }
		void reset(T* p = nullptr) {
			if (ptr) WidgetFactory::Destroy(ptr);
			ptr = p;
		}
		operator bool() const { return ptr != nullptr; }
		T* release() {
			T* temp = ptr;
			ptr = nullptr; // Stop tracking so destructor does nothing
			return temp;
		}

		// Also add a way to get the raw pointer without releasing
		T* get() const { return ptr; }
	};

	// An IPanel is a Widget that contains a Layout.
	// It bridges the gap between a leaf Widget and a Root Container.
	class IPanel : public IWidget {
	public:
		// Initialize the internal layout of this panel
		virtual ILayout* __stdcall CreateLayout(int rows, int cols) = 0;

		// Access the layout
		virtual ILayout* __stdcall GetLayout() = 0;
	};

	extern "C" {
		CHRONO_API IContainer* __stdcall CreateChronoContainer(HWND parent, const wchar_t* title, int w, int h, bool customTitleBar = false);
		CHRONO_API IContainer* __stdcall CreateChronoPopupContainer(HWND parent, int w, int h);
		CHRONO_API IPanel* __stdcall CreateChronoPanel(IContainer* parent);
	}
}