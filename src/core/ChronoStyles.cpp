#include <sstream>
#include <fstream>
#include <iostream>

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
				auto kv = Split(rawProp, ':');
				if (kv.size() == 2) {
					props[kv[0]] = kv[1];
				}
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

	void StyleManager::LoadCSSFile(const std::string& filePath) {
		std::ifstream file(filePath);
		if (file.is_open()) {
			std::stringstream buffer;
			buffer << file.rdbuf();
			LoadCSS(buffer.str());
		}
		else {
			std::cerr << "[ChronoUI] Could not open CSS file: " << filePath << std::endl;
		}
	}

	// --- Class Manipulation Logic ---

	void StyleManager::AddClass(IContextNode* node, const std::string& classNames) {
		if (!node) return;

		std::string currentClassStr = node->GetProperty("class", "");
		std::set<std::string> classes;

		auto existingTokens = Split(currentClassStr, ' ');
		for (const auto& t : existingTokens) classes.insert(t);

		auto newTokens = Split(classNames, ' ');
		bool changed = false;
		for (const auto& t : newTokens) {
			if (classes.find(t) == classes.end()) {
				classes.insert(t);
				changed = true;
			}
		}

		if (changed) {
			std::string finalClassStr;
			for (const auto& c : classes) {
				if (!finalClassStr.empty()) finalClassStr += " ";
				finalClassStr += c;
			}
			node->SetProperty("class", finalClassStr.c_str());

			// Delegate to instance
			Instance().ApplyClassesImpl(node, classes);
		}
	}

	void StyleManager::RemoveClass(IContextNode* node, const std::string& classNames) {
		if (!node) return;

		std::string currentClassStr = node->GetProperty("class", "");
		std::set<std::string> classes;

		auto existingTokens = Split(currentClassStr, ' ');
		for (const auto& t : existingTokens) classes.insert(t);

		auto removeTokens = Split(classNames, ' ');
		bool changed = false;
		for (const auto& t : removeTokens) {
			if (classes.erase(t) > 0) changed = true;
		}

		if (changed) {
			std::string finalClassStr;
			for (const auto& c : classes) {
				if (!finalClassStr.empty()) finalClassStr += " ";
				finalClassStr += c;
			}
			node->SetProperty("class", finalClassStr.c_str());

			// Re-apply remaining classes
			Instance().ApplyClassesImpl(node, classes);
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

	// Instance method implementation
	void StyleManager::ApplyClassesImpl(IContextNode* node, const std::set<std::string>& classes) {
		// 1. Define the pseudo-states you want to support
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