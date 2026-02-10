#include <map>
#include <string>

namespace ChronoUI {
	class ContextNodeImpl : public virtual IContextNode {
	protected:
		IContextNode* m_parent = nullptr;
		std::unordered_map<std::string, std::string> m_properties;
		std::string m_lastQuery; // Buffer for returned C-strings

	public:
		virtual void __stdcall SetParentNode(IContextNode* parent) override {
			m_parent = parent;
		}

		virtual IContextNode* __stdcall GetParentNode() override {
			return m_parent;
		}
		virtual IContextNode* __stdcall GetContextNode() override {
			return this;
		}
		

		// THE MAGIC: Recursive Property Lookup
		virtual const char* __stdcall GetProperty(const char* key, const char* def = "") override {
			// 1. Check Local Properties
			auto it = m_properties.find(key);
			if (it != m_properties.end()) {
				return it->second.c_str();
			}

			// 2. Check Parent (The "Carry" mechanism)
			if (m_parent) {
				return m_parent->GetProperty(key, def);
			}

			// 3. Not found anywhere in the chain
			return def;
		}

		// Helper to set local properties
		virtual IContextNode* __stdcall SetProperty(const char* key, const char* value) {
			m_properties[key] = (value) ? value : "";
			return this;
		}

		IContextNode* SetColor(const char* key, COLORREF color) override {
			char buf[16];
			sprintf_s(buf, "#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
			return SetProperty(key, buf);
		}

		// Color Getter Helper
		COLORREF GetColor(const char* key, COLORREF defaultColor = RGB(0, 0, 0)) override {
			const char* val = GetProperty(key);

			// Check if the property exists and looks like a hex color (#RRGGBB)
			if (!val || val[0] != '#' || strlen(val) < 7) {
				return defaultColor;
			}

			unsigned int r, g, b;
			// sscanf_s reads the hex values. val + 1 skips the '#' character.
			if (sscanf_s(val + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
				return RGB(r, g, b);
			}

			return defaultColor;
		}

		bool HasProperty(const std::string& key)
		{
			std::string k = GetProperty(key.c_str());
			return k != "";
		}

		std::string TryResolveState(const std::string& baseKey, bool selected, bool enabled, bool hovered, bool active)
		{
			// 1. Check Disabled
			if (!enabled) {
				std::string k = baseKey + ":disabled";
				if (HasProperty(k.c_str())) 
					return GetProperty(k.c_str());
			}
			// 3. Check Hovered
			if (hovered) {
				std::string k = baseKey + ":hover";
				if (HasProperty(k.c_str()))
					return GetProperty(k.c_str());
			}

			// 2. Check Selected
			if (selected) {
				std::string k = baseKey + ":selected";
				if (HasProperty(k.c_str()))
					return GetProperty(k.c_str());
			}
			if (active) {
				std::string k = baseKey + ":active";
				if (HasProperty(k.c_str()))
					return GetProperty(k.c_str());
			}

			// 4. Return Base value (if it exists)
			if (HasProperty(baseKey)) {
				std::string k = baseKey;
				return GetProperty(k.c_str());
			}
			// Return empty string to signal "Not Found"
			return "";
		}
		std::string result;
		const char* GetStyle(const char* _prop, const char* _def, const char* _classid,const char* _variant, bool selected, bool enabled, bool hovered, bool active)
		{
			std::string prop = _prop;
			std::string classid = _classid;
			std::string variant = _variant;

			// --- LEVEL 1: VARIANT SPECIFIC (e.g., button:danger:color) ---
			if (!variant.empty()) {
				std::string variantKey = classid + ":" + variant + ":" + prop;
				result = TryResolveState(variantKey, selected, enabled, hovered, active);
				if (!result.empty()) 
					return result.c_str();
			}

			// --- LEVEL 2: CLASS SPECIFIC (e.g., button:color) ---
			if (!classid.empty()) {
				std::string classKey = classid + ":" + prop;
				result = TryResolveState(classKey, selected, enabled, hovered, active);
				if (!result.empty()) 
					return result.c_str();
			}

			// --- LEVEL 3: GLOBAL FALLBACK (e.g., color) ---
			result = TryResolveState(prop, selected, enabled, hovered, active);

			if (result.empty()) {
				result = _def;
			}

			return result.c_str(); // Returns value or "" if nothing found
		}
	};
}