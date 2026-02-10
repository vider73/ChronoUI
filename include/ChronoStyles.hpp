#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>


#include "WidgetImpl.hpp"

namespace ChronoUI {

	typedef std::map<std::string, std::string> StyleProperties;

	// Apply the API Macro to the class
	class CHRONO_API StyleManager {
	private:
		// Stores ".btn" -> { "background-color": "#333", ... }
		std::map<std::string, StyleProperties> _registry;

		// Private Constructor (Enforce Singleton)
		StyleManager();

		// The Singleton Accessor (Declaration Only)
		static StyleManager& Instance();

		// Private helper to apply styles
		void ApplyClassesImpl(IContextNode* node, const std::set<std::string>& classes);

	public:
		// Delete copy and move constructors
		StyleManager(const StyleManager&) = delete;
		void operator=(const StyleManager&) = delete;

		// --- Public Static API ---

		static void LoadCSS(const std::string& cssContent);
		static void LoadCSSFile(const std::string& filePath);

		static void AddClass(IContextNode* node, const std::string& classNames);
		static void RemoveClass(IContextNode* node, const std::string& classNames);
		static bool HasClass(IContextNode* node, const std::string& className);
		static void ToggleClass(IContextNode* node, const std::string& className);
	};

	// Internal helpers (kept in header as inline utilities, or move to .cpp if preferred)
	namespace Internal {
		inline std::string Trim(const std::string& str) {
			size_t first = str.find_first_not_of(" \t\n\r");
			if (std::string::npos == first) return str;
			size_t last = str.find_last_not_of(" \t\n\r");
			return str.substr(first, (last - first + 1));
		}
	}
}