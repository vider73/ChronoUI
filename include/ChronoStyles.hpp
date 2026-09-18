#pragma once

#include <string>
#include <vector>
#include <map>

// Depend only on the public ABI surface (IContextNode + CHRONO_API), NOT on
// WidgetImpl.hpp. This keeps the dependency direction one-way so WidgetImpl.hpp
// can pull in ChronoStyles.hpp for destructor-time auto-cleanup.
#include "ChronoUI.hpp"

namespace ChronoUI {

	typedef std::map<std::string, std::string> StyleProperties;

	// Apply the API Macro to the class
	class CHRONO_API StyleManager {
	private:
		// Stores ".btn" -> { "background-color": "#333", ... }
		std::map<std::string, StyleProperties> _registry;

		// Nodes that have had AddClass called on them. Used by ReapplyAll() so
		// theme swaps can re-cascade styles onto existing widgets. Non-owning
		// pointers — destructors should call Forget() to keep this clean
		// (WidgetImpl::~WidgetImpl does so automatically).
		std::vector<IContextNode*> _trackedNodes;

		// Private Constructor (Enforce Singleton)
		StyleManager();

		// The Singleton Accessor (Declaration Only)
		static StyleManager& Instance();

		// Private helper to apply styles. Takes an ordered vector so later-listed
		// classes can override earlier ones — same semantics as real CSS where the
		// last applied declaration wins. Callers are responsible for deduplication.
		void ApplyClassesImpl(IContextNode* node, const std::vector<std::string>& classes);

		// Adds the node to _trackedNodes if not already present.
		void TrackNode(IContextNode* node);

	public:
		// Delete copy and move constructors
		StyleManager(const StyleManager&) = delete;
		void operator=(const StyleManager&) = delete;

		// ---------------------------------------------------------------
		// CSS Loading
		// ---------------------------------------------------------------
		static void LoadCSS(const std::string& cssContent);
		static void LoadCSSFile(const std::string& filePath);

		// Clears the parsed CSS registry. Properties already smashed onto widgets
		// are NOT undone — you typically pair this with LoadCSS + ReapplyAll(),
		// or use the LoadCSSReplace() convenience below.
		static void Reset();

		// Convenience: Reset() + LoadCSSFile() + ReapplyAll().
		// Swaps the active stylesheet and re-cascades classes onto every node
		// that AddClass was ever called on (and has not been Forget'd).
		static void LoadCSSReplace(const std::string& filePath);

		// ---------------------------------------------------------------
		// Class manipulation
		// ---------------------------------------------------------------
		static void AddClass(IContextNode* node, const std::string& classNames);
		static void RemoveClass(IContextNode* node, const std::string& classNames);
		static bool HasClass(IContextNode* node, const std::string& className);
		static void ToggleClass(IContextNode* node, const std::string& className);

		// Re-runs ApplyClassesImpl for one node, reading the current value of its
		// "class" property. Useful after Reset()+LoadCSS to refresh a single node
		// without ReapplyAll, or after changes that should re-cascade.
		static void Reapply(IContextNode* node);

		// Calls Reapply on every tracked node. Skips null/forgotten entries.
		static void ReapplyAll();

		// Removes a node from the tracking list. Safe to call multiple times.
		// MUST be called before the node is destroyed if you want to avoid
		// dangling pointers leaking into ReapplyAll. WidgetImpl::~WidgetImpl
		// does this automatically; callers that style ICell or other non-widget
		// nodes must handle their own lifecycle.
		static void Forget(IContextNode* node);
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