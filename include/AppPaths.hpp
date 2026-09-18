// =============================================================================
// AppPaths.hpp — resolve bundled assets relative to the EXECUTABLE, not to the
// current working directory.
//
// Why this exists
// ---------------
// CMake stages the repo's `assets/` folder next to every demo executable
// (see the StageAssets post-build step), so a file ships at
// `<exe_dir>/assets/<name>`. A relative path like `..\assets\foo.png` resolves
// against the *working directory*, which differs depending on how the app was
// started (Explorer, a debugger, a shell in another folder, a shortcut), so it
// silently fails: the image or stylesheet just never loads.
//
// Usage
// -----
//     StyleManager::LoadCSSFile(AssetPathA("bootstrap_lite.css").c_str());
//     myVirtualDrive.Open(AssetPathW(L"resources.pak").c_str(), nullptr);
//
// Both helpers take a path RELATIVE TO the assets folder and return an absolute
// one. Header-only, no dependencies beyond <windows.h> and the STL.
// =============================================================================

#pragma once

#include <windows.h>
#include <string>

namespace ChronoUI {

	// Directory holding the running .exe, with a trailing backslash.
	inline std::wstring ExeDir() {
		wchar_t buf[MAX_PATH] = {};
		DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
		std::wstring p(buf, n);
		size_t slash = p.find_last_of(L"\\/");
		return (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash + 1);
	}

	// <exe_dir>\assets\<relative>
	inline std::wstring AssetPathW(const wchar_t* relative) {
		return ExeDir() + L"assets\\" + (relative ? relative : L"");
	}

	// Same, as a narrow (ANSI) string for the APIs that take const char*.
	inline std::string AssetPathA(const char* relative) {
		std::wstring w = AssetPathW(L"");
		if (relative) while (*relative) w.push_back((wchar_t)(unsigned char)*relative++);
		int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string out((size_t)n, '\0');
		WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
		return out;
	}

} // namespace ChronoUI
