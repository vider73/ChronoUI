#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>

#include "ChronoStyles.hpp"

namespace ChronoUI {

	// --- Helper for splitting strings (Local to this file) ---
	static std::vector<std::string> Split(const std::string& str, char delimiter) {
		std::vector<std::string> tokens;
		std::string token;
		std::istringstream tokenStream(str);
		while (std::getline(tokenStream, token, delimiter)) {
			std::string trimmed = Internal::Trim(token);
			if (!trimmed.empty()) tokens.push_back(trimmed);
		}
		return tokens;
	}

	// Split on any whitespace (CSS values like "5px 10px" or "1px solid red").
	static std::vector<std::string> SplitWhitespace(const std::string& s) {
		std::vector<std::string> out;
		std::istringstream iss(s);
		std::string t;
		while (iss >> t) out.push_back(t);
		return out;
	}

	// Expands CSS shorthand into the four side-specific sub-properties.
	// Currently handles `margin` and `padding`. Supports the 1/2/3/4-value spec:
	//   one value   -> all four sides
	//   two values  -> top/bottom, left/right
	//   three       -> top, left/right, bottom
	//   four        -> top, right, bottom, left
	// The original shorthand key is also stored, so future widget code that
	// reads e.g. "padding" directly still works.
	static void ExpandShorthand(const std::string& key, const std::string& value, StyleProperties& outProps) {
		const bool isMargin  = (key == "margin");
		const bool isPadding = (key == "padding");
		if (!isMargin && !isPadding) return;

		auto toks = SplitWhitespace(value);
		if (toks.empty()) return;

		std::string t, r, b, l;
		switch (toks.size()) {
		case 1: t = r = b = l = toks[0]; break;
		case 2: t = b = toks[0]; r = l = toks[1]; break;
		case 3: t = toks[0]; r = l = toks[1]; b = toks[2]; break;
		default: // 4 or more — extra tokens ignored
			t = toks[0]; r = toks[1]; b = toks[2]; l = toks[3]; break;
		}

		outProps[key + "-top"]    = t;
		outProps[key + "-right"]  = r;
		outProps[key + "-bottom"] = b;
		outProps[key + "-left"]   = l;
	}

	// --- Singleton Implementation ---

	// Constructor
	StyleManager::StyleManager() {}

	// THE GLOBAL INSTANCE
	// Because this is in the .cpp, the memory is allocated here ONCE.
	StyleManager& StyleManager::Instance() {
		static StyleManager instance;
		return instance;
	}

	// --- CSS Loading Logic ---

	void StyleManager::LoadCSS(const std::string& cssContent) {
		// 1. Sanitize (Remove Comments)
		std::string cleanContent;
		cleanContent.reserve(cssContent.length());

		for (size_t i = 0; i < cssContent.length(); ++i) {
			if (i + 1 < cssContent.length() && cssContent[i] == '/' && cssContent[i + 1] == '*') {
				size_t closeComment = cssContent.find("*/", i + 2);
				if (closeComment != std::string::npos) {
					i = closeComment + 1;
					cleanContent += ' ';
				}
				else {
					break;
				}
			}
			else {
				cleanContent += cssContent[i];
			}
		}

		// 2. Parse
		size_t pos = 0;
		while (pos < cleanContent.length()) {
			size_t openBrace = cleanContent.find('{', pos);
			if (openBrace == std::string::npos) break;

			size_t closeBrace = cleanContent.find('}', openBrace);
			if (closeBrace == std::string::npos) break;

			std::string selectorStr = cleanContent.substr(pos, openBrace - pos);
			std::string bodyStr = cleanContent.substr(openBrace + 1, closeBrace - openBrace - 1);

			StyleProperties props;
			auto rawProps = Split(bodyStr, ';');
			for (const auto& rawProp : rawProps) {
				// Split on first colon only — values themselves never contain a colon
				// in any property this engine understands today.
				size_t colon = rawProp.find(':');
				if (colon == std::string::npos) continue;
				std::string key   = Internal::Trim(rawProp.substr(0, colon));
				std::string value = Internal::Trim(rawProp.substr(colon + 1));
				if (key.empty() || value.empty()) continue;

				props[key] = value;
				ExpandShorthand(key, value, props);
			}

			auto selectors = Split(selectorStr, ',');
			for (auto& sel : selectors) {
				if (sel.size() > 0 && sel[0] == '.') sel = sel.substr(1); // Remove dot
				if (sel.empty()) continue;

				// ACCESS SINGLETON HERE
				for (const auto& kv : props) {
					Instance()._registry[sel][kv.first] = kv.second;
				}
			}
			pos = closeBrace + 1;
		}
	}

	// Returns the directory containing the running executable, with no trailing
	// slash. Empty string on failure. Win32-only.
	// Path is converted to UTF-8 via the framework's WideToNarrow helper so
	// non-ASCII directory components (e.g. user names with accents) survive.
	static std::string GetExeDir() {
		wchar_t buf[MAX_PATH];
		DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) return {};
		std::wstring w(buf, len);
		size_t slash = w.find_last_of(L"\\/");
		if (slash == std::wstring::npos) return {};
		w.resize(slash);
		return WideToNarrow(w);
	}

	void StyleManager::LoadCSSFile(const std::string& filePath) {
		// Try a small list of candidate paths so the demo works no matter where
		// the user runs the EXE from. Order:
		//   1. Path exactly as given (current-working-directory relative)
		//   2. <exe_dir>/<filePath>            — straight join
		//   3. <exe_dir>/<filePath_no_dotdot>  — strip leading "..\" / "../" sequences
		// Stops at the first one that opens successfully.
		// No capture — both LoadCSSFile (this method) and LoadCSS (called below)
		// are static members, so the lambda needs nothing from an instance.
		auto tryLoad = [](const std::string& p) -> bool {
			std::ifstream file(p);
			if (!file.is_open()) return false;
			std::stringstream buffer;
			buffer << file.rdbuf();
			LoadCSS(buffer.str());
			return true;
		};

		if (tryLoad(filePath)) return;

		std::string exeDir = GetExeDir();
		if (!exeDir.empty()) {
			if (tryLoad(exeDir + "\\" + filePath)) return;

			// Strip leading "..\" or "../" pairs so a path like
			//   "..\\assets\\bootstrap_lite.css"
			// resolves to <exe_dir>/assets/bootstrap_lite.css when the EXE lives
			// alongside an assets/ folder.
			std::string stripped = filePath;
			while (stripped.size() >= 3 &&
				(stripped.compare(0, 3, "..\\") == 0 ||
				 stripped.compare(0, 3, "../") == 0)) {
				stripped = stripped.substr(3);
			}
			if (stripped != filePath && tryLoad(exeDir + "\\" + stripped)) return;
		}

		std::cerr << "[ChronoUI] Could not open CSS file: " << filePath << std::endl;
	}

	// --- Class Manipulation Logic ---

	// Helpers: keep insertion order, deduplicate
	static bool VecContains(const std::vector<std::string>& v, const std::string& s) {
		for (const auto& x : v) if (x == s) return true;
		return false;
	}
	static bool VecEraseAll(std::vector<std::string>& v, const std::string& s) {
		auto before = v.size();
		v.erase(std::remove(v.begin(), v.end(), s), v.end());
		return v.size() != before;
	}
	static std::string JoinSpace(const std::vector<std::string>& v) {
		std::string out;
		for (const auto& s : v) {
			if (!out.empty()) out += ' ';
			out += s;
		}
		return out;
	}

	void StyleManager::AddClass(IContextNode* node, const std::string& classNames) {
		if (!node) return;

		// Keep order so later tokens override earlier ones (real-CSS semantics).
		std::vector<std::string> classes;
		for (const auto& t : Split(node->GetProperty("class", ""), ' ')) {
			if (!VecContains(classes, t)) classes.push_back(t);
		}

		bool changed = false;
		for (const auto& t : Split(classNames, ' ')) {
			if (!VecContains(classes, t)) {
				classes.push_back(t);
				changed = true;
			}
		}

		if (changed) {
			node->SetProperty("class", JoinSpace(classes).c_str());
			Instance().ApplyClassesImpl(node, classes);
		}
		// Track unconditionally so a no-op AddClass call (re-adding existing
		// tokens) still registers the node for theme-swap reapply. TrackNode
		// dedupes internally.
		if (!classes.empty()) Instance().TrackNode(node);
	}

	void StyleManager::RemoveClass(IContextNode* node, const std::string& classNames) {
		if (!node) return;

		std::vector<std::string> classes;
		for (const auto& t : Split(node->GetProperty("class", ""), ' ')) {
			if (!VecContains(classes, t)) classes.push_back(t);
		}

		bool changed = false;
		for (const auto& t : Split(classNames, ' ')) {
			if (VecEraseAll(classes, t)) changed = true;
		}

		if (changed) {
			node->SetProperty("class", JoinSpace(classes).c_str());
			Instance().ApplyClassesImpl(node, classes);
			// If the node no longer has any classes, drop it from tracking.
			if (classes.empty()) Forget(node);
		}
	}

	bool StyleManager::HasClass(IContextNode* node, const std::string& className) {
		if (!node) return false;
		std::string currentClassStr = node->GetProperty("class", "");
		auto tokens = Split(currentClassStr, ' ');
		for (const auto& t : tokens) {
			if (t == className) return true;
		}
		return false;
	}

	void StyleManager::ToggleClass(IContextNode* node, const std::string& className) {
		if (HasClass(node, className)) RemoveClass(node, className);
		else AddClass(node, className);
	}

	// ---------------------------------------------------------------
	// Theme-swap support: Reset / Reapply / ReapplyAll / Forget
	// ---------------------------------------------------------------

	void StyleManager::Reset() {
		Instance()._registry.clear();
		// Note: _trackedNodes is intentionally preserved so a subsequent
		// LoadCSS + ReapplyAll() can rebuild the cascade on existing widgets.
	}

	void StyleManager::LoadCSSReplace(const std::string& filePath) {
		Reset();
		LoadCSSFile(filePath);
		ReapplyAll();
	}

	void StyleManager::Reapply(IContextNode* node) {
		if (!node) return;

		// Read current class list from the node and re-cascade it.
		std::vector<std::string> classes;
		for (const auto& t : Split(node->GetProperty("class", ""), ' ')) {
			if (!VecContains(classes, t)) classes.push_back(t);
		}
		if (classes.empty()) return;

		Instance().ApplyClassesImpl(node, classes);
	}

	void StyleManager::ReapplyAll() {
		// Snapshot the tracked-nodes list — Reapply may mutate properties which
		// could feed back into AddClass/RemoveClass via user code in theory.
		auto& mgr = Instance();
		std::vector<IContextNode*> snapshot = mgr._trackedNodes;
		for (IContextNode* node : snapshot) {
			if (node) Reapply(node);
		}
	}

	void StyleManager::Forget(IContextNode* node) {
		if (!node) return;
		auto& nodes = Instance()._trackedNodes;
		nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
	}

	void StyleManager::TrackNode(IContextNode* node) {
		if (!node) return;
		for (auto* n : _trackedNodes) {
			if (n == node) return; // already tracked
		}
		_trackedNodes.push_back(node);
	}

	// Instance method implementation.
	// IMPORTANT: 'classes' is ordered. We iterate in caller order so a later class
	// can override an earlier one — same intent as CSS "last declaration wins."
	void StyleManager::ApplyClassesImpl(IContextNode* node, const std::vector<std::string>& classes) {
		// Pseudo-states supported by this engine. Kept here as the source of truth;
		// the runtime ContextNodeImpl::TryResolveState mirrors this list.
		static const std::vector<std::string> pseudoStates = {
			"hover", "active", "checked", "selected", "focus", "disabled"
		};

		for (const auto& cls : classes) {
			// --- 1. Apply the Main Class (Base State) ---
			auto it = _registry.find(cls);
			if (it != _registry.end()) {
				const auto& props = it->second;
				for (const auto& kv : props) {
					node->SetProperty(kv.first.c_str(), kv.second.c_str());
				}
			}

			// --- 2. Apply Pseudo States (e.g., btn-primary:hover) ---
			for (const auto& state : pseudoStates) {
				// Construct the class name to look for (e.g., "btn-primary:hover")
				std::string pseudoClassName = cls + ":" + state;

				auto pseudoIt = _registry.find(pseudoClassName);
				if (pseudoIt != _registry.end()) {
					const auto& props = pseudoIt->second;
					for (const auto& kv : props) {
						// Construct the property key (e.g., "background-color:hover")
						// Note: We use std::string to ensure correct concatenation, 
						// as adding char* + char* in C++ is pointer arithmetic.
						std::string propKey = kv.first + ":" + state;

						node->SetProperty(propKey.c_str(), kv.second.c_str());
					}
				}
			}
		}
	}
}