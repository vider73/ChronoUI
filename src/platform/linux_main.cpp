// =============================================================================
// linux_main.cpp — the entry point Windows provides and Linux does not.
//
// Every example is a wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int). On
// Linux CMake links this file into each one: main() joins argv into the wide
// command line Windows would hand over and calls the example's wWinMain.
// =============================================================================

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

int main(int argc, char** argv) {
	std::wstring cmd;
	for (int i = 1; i < argc; ++i) {
		if (i > 1) cmd += L' ';
		cmd += chrono_linux::FromUtf8(argv[i]);
	}
	return wWinMain(nullptr, nullptr, cmd.data(), SW_SHOWDEFAULT);
}
