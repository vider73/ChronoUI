// =============================================================================
// chrono_win32.hpp — the slice of Win32 ChronoUI uses, on SDL2.
//
// The Linux port does not touch the framework headers: they keep including
// <windows.h>, and on Linux that name resolves to this directory. Here a HWND
// is an SDL window plus a WNDPROC, and GetMessageW turns SDL events into the
// same WM_* messages Windows would send, so VirtualWindow's WndProc runs as is.
//
// What is emulated (and only that): window classes and creation, the message
// queue (posted messages, WM_PAINT and WM_TIMER generated when the queue is
// empty, like Windows), mouse capture and leave tracking, keyboard with VK_*
// codes and WM_CHAR, custom chrome through WM_NCHITTEST (SDL hit-test
// callback), file drop, the Unicode clipboard, popup menus, timing and a few
// CRT spellings MSVC has and glibc doesn't.
//
// wchar_t is 32 bits here, so a std::wstring holds code points (UTF-32), not
// UTF-16. Text positions stay consistent because every layer indexes the same
// std::wstring; conversion to UTF-8 happens only at the SDL / Pango boundary.
// =============================================================================

#pragma once

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

// --- basic types -------------------------------------------------------------
typedef int                BOOL;
typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef int16_t            SHORT;
typedef uint16_t           USHORT;
typedef int32_t            LONG;
typedef uint32_t           ULONG;
typedef uint32_t           DWORD;
typedef int32_t            INT32;
typedef uint32_t           UINT32;
typedef int64_t            INT64;
typedef uint64_t           UINT64;
typedef uint64_t           ULONGLONG;
typedef unsigned int       UINT;
typedef int                INT;
typedef float              FLOAT;
typedef char               CHAR;
typedef wchar_t            WCHAR;
typedef intptr_t           LONG_PTR;
typedef uintptr_t          ULONG_PTR;
typedef uintptr_t          UINT_PTR;
typedef intptr_t           INT_PTR;
typedef uintptr_t          DWORD_PTR;
typedef size_t             SIZE_T;
typedef uintptr_t          WPARAM;
typedef intptr_t           LPARAM;
typedef intptr_t           LRESULT;
typedef int32_t            HRESULT;
typedef void*              LPVOID;
typedef const void*        LPCVOID;
typedef wchar_t*           PWSTR;
typedef wchar_t*           LPWSTR;
typedef const wchar_t*     PCWSTR;
typedef const wchar_t*     LPCWSTR;
typedef char*              LPSTR;
typedef const char*        LPCSTR;
typedef DWORD*             LPDWORD;
typedef void*              HANDLE;
typedef void*              HINSTANCE;
typedef void*              HMODULE;
typedef void*              HDC;
typedef void*              HICON;
typedef void*              HCURSOR;
typedef void*              HBRUSH;
typedef void*              HFONT;
typedef void*              HGLOBAL;
typedef void*              HDROP;
typedef int                errno_t;

struct HWND__;  typedef HWND__*  HWND;
struct HMENU__; typedef HMENU__* HMENU;

#define WINAPI
#define CALLBACK
#define APIENTRY
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu

#define S_OK           ((HRESULT)0)
#define S_FALSE        ((HRESULT)1)
#define E_NOTIMPL      ((HRESULT)0x80004001)
#define E_NOINTERFACE  ((HRESULT)0x80004002)
#define E_POINTER      ((HRESULT)0x80004003)
#define E_FAIL         ((HRESULT)0x80004005)
#define E_OUTOFMEMORY  ((HRESULT)0x8007000E)
#define E_INVALIDARG   ((HRESULT)0x80070057)
#define SUCCEEDED(hr)  (((HRESULT)(hr)) >= 0)
#define FAILED(hr)     (((HRESULT)(hr)) < 0)

#define LOWORD(l)  ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l)  ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w)  ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define MAKELONG(a, b)   ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define UNREFERENCED_PARAMETER(p) (void)(p)

struct POINT { LONG x, y; };
struct SIZE  { LONG cx, cy; };
struct RECT  { LONG left, top, right, bottom; };
typedef POINT* LPPOINT;
typedef RECT*  LPRECT;
typedef const RECT* LPCRECT;
union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; int64_t QuadPart; };

struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

struct WNDCLASSW {
	UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
	HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName;
};
struct WNDCLASSEXW {
	UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
	HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName; HICON hIconSm;
};
struct CREATESTRUCTW {
	LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent;
	int cy, cx, y, x; LONG style; LPCWSTR lpszName, lpszClass; DWORD dwExStyle;
};
typedef CREATESTRUCTW  CREATESTRUCT;
typedef CREATESTRUCTW* LPCREATESTRUCT;
typedef CREATESTRUCTW* LPCREATESTRUCTW;
struct PAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; };
struct TRACKMOUSEEVENT { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
struct MINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; };
struct SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct MEMORYSTATUSEX {
	DWORD dwLength, dwMemoryLoad; ULONGLONG ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile,
	ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual;
};

// --- messages, styles, codes -------------------------------------------------
#define WM_NULL           0x0000
#define WM_CREATE         0x0001
#define WM_DESTROY        0x0002
#define WM_MOVE           0x0003
#define WM_SIZE           0x0005
#define WM_ACTIVATE       0x0006
#define WM_SETFOCUS       0x0007
#define WM_KILLFOCUS      0x0008
#define WM_PAINT          0x000F
#define WM_CLOSE          0x0010
#define WM_QUIT           0x0012
#define WM_ERASEBKGND     0x0014
#define WM_SHOWWINDOW     0x0018
#define WM_SETCURSOR      0x0020
#define WM_GETMINMAXINFO  0x0024
#define WM_NCCREATE       0x0081
#define WM_NCDESTROY      0x0082
#define WM_NCCALCSIZE     0x0083
#define WM_NCHITTEST      0x0084
#define WM_NCLBUTTONDOWN  0x00A1
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#define WM_SYSCHAR        0x0106
#define WM_COMMAND        0x0111
#define WM_SYSCOMMAND     0x0112
#define WM_TIMER          0x0113
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_LBUTTONDBLCLK  0x0203
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_RBUTTONDBLCLK  0x0206
#define WM_MBUTTONDOWN    0x0207
#define WM_MBUTTONUP      0x0208
#define WM_MOUSEWHEEL     0x020A
#define WM_MOUSEHWHEEL    0x020E
#define WM_DROPFILES      0x0233
#define WM_MOUSELEAVE     0x02A3
#define WM_DPICHANGED     0x02E0
#define WM_USER           0x0400
#define WM_APP            0x8000

#define HTERROR       (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE     0
#define HTCLIENT      1
#define HTCAPTION     2
#define HTSYSMENU     3
#define HTMINBUTTON   8
#define HTMAXBUTTON   9
#define HTLEFT        10
#define HTRIGHT       11
#define HTTOP         12
#define HTTOPLEFT     13
#define HTTOPRIGHT    14
#define HTBOTTOM      15
#define HTBOTTOMLEFT  16
#define HTBOTTOMRIGHT 17
#define HTCLOSE       20

#define WS_OVERLAPPED     0x00000000L
#define WS_POPUP          0x80000000L
#define WS_CHILD          0x40000000L
#define WS_MINIMIZE       0x20000000L
#define WS_VISIBLE        0x10000000L
#define WS_DISABLED       0x08000000L
#define WS_CLIPSIBLINGS   0x04000000L
#define WS_CLIPCHILDREN   0x02000000L
#define WS_MAXIMIZE       0x01000000L
#define WS_CAPTION        0x00C00000L
#define WS_BORDER         0x00800000L
#define WS_SYSMENU        0x00080000L
#define WS_THICKFRAME     0x00040000L
#define WS_MINIMIZEBOX    0x00020000L
#define WS_MAXIMIZEBOX    0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_TOPMOST     0x00000008L
#define WS_EX_TOOLWINDOW  0x00000080L
#define WS_EX_APPWINDOW   0x00040000L
#define WS_EX_LAYERED     0x00080000L
#define WS_EX_NOACTIVATE  0x08000000L
#define CS_VREDRAW        0x0001
#define CS_HREDRAW        0x0002
#define CS_DBLCLKS        0x0008
#define CW_USEDEFAULT     ((int)0x80000000)

#define SW_HIDE            0
#define SW_SHOWNORMAL      1
#define SW_NORMAL          1
#define SW_SHOWMINIMIZED   2
#define SW_SHOWMAXIMIZED   3
#define SW_MAXIMIZE        3
#define SW_SHOWNOACTIVATE  4
#define SW_SHOW            5
#define SW_MINIMIZE        6
#define SW_SHOWNA          8
#define SW_RESTORE         9
#define SW_SHOWDEFAULT     10

#define SWP_NOSIZE         0x0001
#define SWP_NOMOVE         0x0002
#define SWP_NOZORDER       0x0004
#define SWP_NOACTIVATE     0x0010
#define SWP_FRAMECHANGED   0x0020
#define SWP_SHOWWINDOW     0x0040
#define SWP_HIDEWINDOW     0x0080
#define HWND_TOP           ((HWND)0)
#define HWND_TOPMOST       ((HWND)-1)
#define HWND_NOTOPMOST     ((HWND)-2)

#define GWLP_WNDPROC   (-4)
#define GWLP_USERDATA  (-21)
#define GWL_STYLE      (-16)
#define GWL_EXSTYLE    (-20)

#define TME_LEAVE      0x00000002
#define SIZE_RESTORED  0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SC_MINIMIZE    0xF020
#define SC_MAXIMIZE    0xF030
#define SC_CLOSE       0xF060
#define SC_RESTORE     0xF120
#define SM_CXSCREEN    0
#define SM_CYSCREEN    1
#define USER_DEFAULT_SCREEN_DPI 96

#define MK_LBUTTON  0x0001
#define MK_RBUTTON  0x0002
#define MK_SHIFT    0x0004
#define MK_CONTROL  0x0008

#define VK_LBUTTON   0x01
#define VK_RBUTTON   0x02
#define VK_MBUTTON   0x04
#define VK_BACK      0x08
#define VK_TAB       0x09
#define VK_CLEAR     0x0C
#define VK_RETURN    0x0D
#define VK_SHIFT     0x10
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_PAUSE     0x13
#define VK_CAPITAL   0x14
#define VK_ESCAPE    0x1B
#define VK_SPACE     0x20
#define VK_PRIOR     0x21
#define VK_NEXT      0x22
#define VK_END       0x23
#define VK_HOME      0x24
#define VK_LEFT      0x25
#define VK_UP        0x26
#define VK_RIGHT     0x27
#define VK_DOWN      0x28
#define VK_INSERT    0x2D
#define VK_DELETE    0x2E
#define VK_LWIN      0x5B
#define VK_RWIN      0x5C
#define VK_APPS      0x5D
#define VK_NUMPAD0   0x60
#define VK_MULTIPLY  0x6A
#define VK_ADD       0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT  0x6D
#define VK_DECIMAL   0x6E
#define VK_DIVIDE    0x6F
#define VK_F1        0x70
#define VK_F2        0x71
#define VK_F3        0x72
#define VK_F4        0x73
#define VK_F5        0x74
#define VK_F6        0x75
#define VK_F7        0x76
#define VK_F8        0x77
#define VK_F9        0x78
#define VK_F10       0x79
#define VK_F11       0x7A
#define VK_F12       0x7B
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5
#define VK_OEM_1      0xBA
#define VK_OEM_PLUS   0xBB
#define VK_OEM_COMMA  0xBC
#define VK_OEM_MINUS  0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2      0xBF
#define VK_OEM_3      0xC0
#define VK_OEM_4      0xDB
#define VK_OEM_5      0xDC
#define VK_OEM_6      0xDD
#define VK_OEM_7      0xDE

#define MF_STRING     0x0000
#define MF_ENABLED    0x0000
#define MF_UNCHECKED  0x0000
#define MF_GRAYED     0x0001
#define MF_DISABLED   0x0002
#define MF_CHECKED    0x0008
#define MF_POPUP      0x0010
#define MF_SEPARATOR  0x0800
#define TPM_LEFTALIGN    0x0000
#define TPM_LEFTBUTTON   0x0000
#define TPM_TOPALIGN     0x0000
#define TPM_RIGHTBUTTON  0x0002
#define TPM_CENTERALIGN  0x0004
#define TPM_RIGHTALIGN   0x0008
#define TPM_VCENTERALIGN 0x0010
#define TPM_BOTTOMALIGN  0x0020
#define TPM_NONOTIFY     0x0080
#define TPM_RETURNCMD    0x0100

#define CF_TEXT        1
#define CF_UNICODETEXT 13
#define GMEM_FIXED     0x0000
#define GMEM_MOVEABLE  0x0002
#define GMEM_ZEROINIT  0x0040
#define GHND           (GMEM_MOVEABLE | GMEM_ZEROINIT)

#define CP_ACP  0
#define CP_UTF8 65001

#define MB_OK              0x0000
#define MB_OKCANCEL        0x0001
#define MB_YESNO           0x0004
#define MB_ICONERROR       0x0010
#define MB_ICONWARNING     0x0030
#define MB_ICONINFORMATION 0x0040
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

#define COINIT_APARTMENTTHREADED 0x2
#define COINIT_MULTITHREADED     0x0
#define COINIT_DISABLE_OLE1DDE   0x4
#define CLSCTX_INPROC_SERVER     0x1

#define MAKEINTRESOURCEW(i) ((LPWSTR)(ULONG_PTR)((WORD)(i)))
#define IDC_ARROW    MAKEINTRESOURCEW(32512)
#define IDC_IBEAM    MAKEINTRESOURCEW(32513)
#define IDC_WAIT     MAKEINTRESOURCEW(32514)
#define IDC_CROSS    MAKEINTRESOURCEW(32515)
#define IDC_SIZENWSE MAKEINTRESOURCEW(32642)
#define IDC_SIZENESW MAKEINTRESOURCEW(32643)
#define IDC_SIZEWE   MAKEINTRESOURCEW(32644)
#define IDC_SIZENS   MAKEINTRESOURCEW(32645)
#define IDC_SIZEALL  MAKEINTRESOURCEW(32646)
#define IDC_HAND     MAKEINTRESOURCEW(32649)
#define IDI_APPLICATION MAKEINTRESOURCEW(32512)
#define LoadCursor LoadCursorW
#define LoadIcon   LoadIconW

#define DefWindowProc     DefWindowProcW
#define GetWindowLongPtr  GetWindowLongPtrW
#define SetWindowLongPtr  SetWindowLongPtrW
#define PostMessage       PostMessageW
#define SendMessage       SendMessageW
#define MessageBox        MessageBoxW

// --- COM essentials ------------------------------------------------------------
// No registry and no apartments: an IID is a tag nobody compares, and every
// object counts its own references. QueryInterface is a dynamic_cast done by
// ComPtr::As(); the virtual here exists for the code that calls it directly.
struct GUID { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; };
typedef GUID IID;
typedef GUID CLSID;
typedef const IID& REFIID;
typedef const CLSID& REFCLSID;
typedef const GUID& REFGUID;
#define __uuidof(x) (::GUID{})
#define IID_PPV_ARGS(pp) ::GUID{}, reinterpret_cast<void**>(pp)

struct IUnknown {
	std::atomic<ULONG> m_comRef{1};
	virtual ~IUnknown() = default;
	virtual HRESULT QueryInterface(REFIID, void** ppv) { if (ppv) *ppv = nullptr; return E_NOINTERFACE; }
	virtual ULONG   AddRef()  { return ++m_comRef; }
	virtual ULONG   Release() { ULONG n = --m_comRef; if (n == 0) delete this; return n; }
};

inline HRESULT CoInitializeEx(void*, DWORD) { return S_OK; }
inline HRESULT CoInitialize(void*)          { return S_OK; }
inline void    CoUninitialize()             {}

// =============================================================================
namespace chrono_linux {

	// --- text ----------------------------------------------------------------
	inline std::string ToUtf8(const wchar_t* s, size_t n) {
		std::string out; out.reserve(n);
		for (size_t i = 0; i < n; ++i) {
			uint32_t c = (uint32_t)s[i];
			if (c < 0x80) out += (char)c;
			else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
			else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
			else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		}
		return out;
	}
	inline std::string ToUtf8(const std::wstring& s) { return ToUtf8(s.data(), s.size()); }
	inline std::string ToUtf8(const wchar_t* s) { return s ? ToUtf8(s, wcslen(s)) : std::string(); }

	inline std::wstring FromUtf8(const char* s, size_t n) {
		std::wstring out; out.reserve(n);
		for (size_t i = 0; i < n; ) {
			unsigned char c = (unsigned char)s[i];
			uint32_t cp; int len;
			if (c < 0x80) { cp = c; len = 1; }
			else if ((c >> 5) == 6) { cp = c & 0x1F; len = 2; }
			else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
			else if ((c >> 3) == 30) { cp = c & 0x07; len = 4; }
			else { out += (wchar_t)0xFFFD; ++i; continue; }
			if (i + (size_t)len > n) { out += (wchar_t)0xFFFD; break; }
			for (int k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3F);
			out += (wchar_t)cp; i += (size_t)len;
		}
		return out;
	}
	inline std::wstring FromUtf8(const char* s) { return s ? FromUtf8(s, strlen(s)) : std::wstring(); }

	// A Windows path ("C:\\x\\assets\\a.png" style separators) as a POSIX one.
	inline std::string PosixPath(const wchar_t* p) {
		std::string s = ToUtf8(p);
		for (char& c : s) if (c == '\\') c = '/';
		return s;
	}

	// MSVC's wide printf reads %s as a wide string; glibc's reads it as a
	// narrow one. Rewrite the conversions so one format means the same thing.
	inline std::wstring FixWideFormat(const wchar_t* f) {
		std::wstring out;
		for (const wchar_t* p = f; *p; ++p) {
			out += *p;
			if (*p != L'%') continue;
			if (p[1] == L'%') { out += *++p; continue; }
			const wchar_t* q = p + 1;
			while (*q && wcschr(L"-+ #0123456789.*", *q)) out += *q++;
			bool sized = false;
			while (*q && wcschr(L"hlLqjzt", *q)) { sized = true; out += *q++; }
			if (!*q) { p = q - 1; continue; }
			if (*q == L's' || *q == L'c') { if (!sized) out += L'l'; out += *q; }
			else if (*q == L'S' || *q == L'C') { out += (wchar_t)towlower(*q); }   // MSVC: the other width
			else out += *q;
			p = q;
		}
		return out;
	}

	// --- time ----------------------------------------------------------------
	inline uint64_t NowMs() {
		using namespace std::chrono;
		return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}
}

// --- CRT spellings ---------------------------------------------------------------
inline int vswprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap) {
	std::wstring f = chrono_linux::FixWideFormat(fmt);
	int r = vswprintf(buf, n, f.c_str(), ap);
	if (r < 0 && n) buf[n - 1] = 0;     // truncated: keep what fit, NUL-terminated
	return r;
}
inline int swprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vswprintf_s(buf, n, fmt, ap); va_end(ap); return r;
}
template <size_t N> inline int swprintf_s(wchar_t (&buf)[N], const wchar_t* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vswprintf_s(buf, N, fmt, ap); va_end(ap); return r;
}
template <size_t N> inline int _snwprintf_s(wchar_t (&buf)[N], size_t, const wchar_t* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vswprintf_s(buf, N, fmt, ap); va_end(ap); return r;
}
template <size_t N> inline int sprintf_s(char (&buf)[N], const char* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, N, fmt, ap); va_end(ap); return r;
}
inline int sprintf_s(char* buf, size_t n, const char* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, n, fmt, ap); va_end(ap); return r;
}
inline errno_t localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : 1; }
inline errno_t gmtime_s(struct tm* out, const time_t* t)    { return gmtime_r(t, out) ? 0 : 1; }
inline int     _wtoi(const wchar_t* s)        { return (int)wcstol(s, nullptr, 10); }
inline double  _wtof(const wchar_t* s)        { return wcstod(s, nullptr); }
inline int     _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int     _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
inline int     _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
template <size_t N> inline errno_t wcscpy_s(wchar_t (&d)[N], const wchar_t* s) { wcsncpy(d, s, N - 1); d[N - 1] = 0; return 0; }
inline errno_t wcscpy_s(wchar_t* d, size_t n, const wchar_t* s) { wcsncpy(d, s, n - 1); d[n - 1] = 0; return 0; }
template <size_t N> inline errno_t wcscat_s(wchar_t (&d)[N], const wchar_t* s) { wcsncat(d, s, N - wcslen(d) - 1); return 0; }
inline errno_t _wfopen_s(FILE** f, const wchar_t* path, const wchar_t* mode) {
	*f = fopen(chrono_linux::PosixPath(path).c_str(), chrono_linux::ToUtf8(mode).c_str());
	return *f ? 0 : 1;
}
inline FILE* _wfopen(const wchar_t* path, const wchar_t* mode) { FILE* f = nullptr; _wfopen_s(&f, path, mode); return f; }

// =============================================================================
// Windows on SDL
// =============================================================================
struct HWND__ {
	SDL_Window* sdl = nullptr;
	WNDPROC     proc = nullptr;
	LONG_PTR    userData = 0;
	DWORD       style = 0, exStyle = 0;
	bool        visible = false, destroyed = false, dirty = true, trackLeave = false;
	struct Timer { UINT ms; uint64_t due; };
	std::map<UINT_PTR, Timer> timers;
	std::vector<std::wstring> pendingDrops;
};

struct HMENU__ {
	struct Item { UINT flags; UINT_PTR id; std::wstring text; };
	std::vector<Item> items;
};

namespace chrono_linux {

	struct State {
		std::map<std::wstring, WNDCLASSW> classes;
		std::vector<HWND>                 windows;     // live ones
		std::deque<MSG>                   queue;       // posted messages
		std::recursive_mutex              queueLock;   // PostMessage may come from a worker thread
		HWND                              capture = nullptr;
		bool                              quit = false;
		int                               quitCode = 0;
		Uint32                            wakeEvent = 0;
		std::wstring                      clipboard;   // set by SetClipboardData, flushed on CloseClipboard
		bool                              clipboardDirty = false;
		uint64_t                          shotAt = 0;  // CHRONOUI_SHOT: when to arm the screenshot
		bool                              shotArmed = false, shotDone = false;
	};
	inline State& S() { static State s; return s; }

	inline void EnsureSDL() {
		static bool done = false;
		if (done) return;
		done = true;
		SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
		SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
		SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");      // capture is the app's call (SetCapture)
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
			fprintf(stderr, "ChronoUI: SDL_Init failed: %s\n", SDL_GetError());
		S().wakeEvent = SDL_RegisterEvents(1);
		SDL_StartTextInput();
	}

	inline HWND FromSDL(Uint32 windowID) {
		SDL_Window* w = SDL_GetWindowFromID(windowID);
		return w ? (HWND)SDL_GetWindowData(w, "hwnd") : nullptr;
	}

	inline void Post(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		{
			std::lock_guard<std::recursive_mutex> g(S().queueLock);
			S().queue.push_back(MSG{ h, msg, wp, lp, (DWORD)NowMs(), {0, 0} });
		}
		if (S().wakeEvent) { SDL_Event e{}; e.type = S().wakeEvent; SDL_PushEvent(&e); }
	}

	// SDL keycode -> Windows virtual-key code.
	inline UINT ToVK(SDL_Keycode k) {
		if (k >= SDLK_a && k <= SDLK_z) return (UINT)('A' + (k - SDLK_a));
		if (k >= SDLK_0 && k <= SDLK_9) return (UINT)('0' + (k - SDLK_0));
		if (k >= SDLK_F1 && k <= SDLK_F12) return (UINT)(VK_F1 + (k - SDLK_F1));
		if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return (UINT)(VK_NUMPAD0 + 1 + (k - SDLK_KP_1));
		switch (k) {
			case SDLK_BACKSPACE: return VK_BACK;     case SDLK_TAB: return VK_TAB;
			case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
			case SDLK_ESCAPE: return VK_ESCAPE;      case SDLK_SPACE: return VK_SPACE;
			case SDLK_PAGEUP: return VK_PRIOR;       case SDLK_PAGEDOWN: return VK_NEXT;
			case SDLK_END: return VK_END;            case SDLK_HOME: return VK_HOME;
			case SDLK_LEFT: return VK_LEFT;          case SDLK_UP: return VK_UP;
			case SDLK_RIGHT: return VK_RIGHT;        case SDLK_DOWN: return VK_DOWN;
			case SDLK_INSERT: return VK_INSERT;      case SDLK_DELETE: return VK_DELETE;
			case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
			case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
			case SDLK_LALT: case SDLK_RALT: return VK_MENU;
			case SDLK_CAPSLOCK: return VK_CAPITAL;   case SDLK_PAUSE: return VK_PAUSE;
			case SDLK_LGUI: return VK_LWIN;          case SDLK_RGUI: return VK_RWIN;
			case SDLK_APPLICATION: return VK_APPS;
			case SDLK_KP_0: return VK_NUMPAD0;       case SDLK_KP_PLUS: return VK_ADD;
			case SDLK_KP_MINUS: return VK_SUBTRACT;  case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
			case SDLK_KP_DIVIDE: return VK_DIVIDE;   case SDLK_KP_PERIOD: return VK_DECIMAL;
			case SDLK_PLUS: case SDLK_EQUALS: return VK_OEM_PLUS;
			case SDLK_MINUS: return VK_OEM_MINUS;    case SDLK_COMMA: return VK_OEM_COMMA;
			case SDLK_PERIOD: return VK_OEM_PERIOD;  case SDLK_SLASH: return VK_OEM_2;
			case SDLK_SEMICOLON: return VK_OEM_1;    case SDLK_BACKQUOTE: return VK_OEM_3;
			case SDLK_LEFTBRACKET: return VK_OEM_4;  case SDLK_BACKSLASH: return VK_OEM_5;
			case SDLK_RIGHTBRACKET: return VK_OEM_6; case SDLK_QUOTE: return VK_OEM_7;
			default: return 0;
		}
	}
	inline SDL_Scancode FromVK(int vk) {
		if (vk >= 'A' && vk <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
		if (vk >= '1' && vk <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
		if (vk == '0') return SDL_SCANCODE_0;
		if (vk >= VK_F1 && vk <= VK_F12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - VK_F1));
		switch (vk) {
			case VK_BACK: return SDL_SCANCODE_BACKSPACE; case VK_TAB: return SDL_SCANCODE_TAB;
			case VK_RETURN: return SDL_SCANCODE_RETURN;  case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
			case VK_SPACE: return SDL_SCANCODE_SPACE;    case VK_LEFT: return SDL_SCANCODE_LEFT;
			case VK_RIGHT: return SDL_SCANCODE_RIGHT;    case VK_UP: return SDL_SCANCODE_UP;
			case VK_DOWN: return SDL_SCANCODE_DOWN;      case VK_HOME: return SDL_SCANCODE_HOME;
			case VK_END: return SDL_SCANCODE_END;        case VK_DELETE: return SDL_SCANCODE_DELETE;
			case VK_PRIOR: return SDL_SCANCODE_PAGEUP;   case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
			case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;  case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
			case VK_LCONTROL: return SDL_SCANCODE_LCTRL; case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
			default: return SDL_SCANCODE_UNKNOWN;
		}
	}

	inline SDL_HitTestResult HitTestThunk(SDL_Window* w, const SDL_Point* pt, void* data);
	inline void TranslateEvent(const SDL_Event& e);
}

// --- windows -------------------------------------------------------------------
inline LRESULT SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (!h || !h->proc) return 0;
	return h->proc(h, msg, wp, lp);
}
inline BOOL PostMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { chrono_linux::Post(h, msg, wp, lp); return TRUE; }
inline void PostQuitMessage(int code) { chrono_linux::Post(nullptr, WM_QUIT, (WPARAM)code, 0); }

inline BOOL DestroyWindow(HWND h) {
	if (!h || h->destroyed) return FALSE;
	auto& s = chrono_linux::S();
	if (s.capture == h) { s.capture = nullptr; SDL_CaptureMouse(SDL_FALSE); }
	SendMessageW(h, WM_DESTROY, 0, 0);
	SendMessageW(h, WM_NCDESTROY, 0, 0);
	h->destroyed = true;
	h->timers.clear();
	if (h->sdl) { SDL_DestroyWindow(h->sdl); h->sdl = nullptr; }
	auto& v = s.windows;
	v.erase(std::remove(v.begin(), v.end(), h), v.end());
	return TRUE;   // the struct itself is never freed: stale HWNDs stay safe to test
}

inline LRESULT DefWindowProcW(HWND h, UINT msg, WPARAM, LPARAM) {
	switch (msg) {
		case WM_NCCREATE: return TRUE;
		case WM_CLOSE:    DestroyWindow(h); return 0;
		case WM_NCHITTEST: return HTCLIENT;
		default:          return 0;
	}
}

inline BOOL GetClassInfoW(HINSTANCE, LPCWSTR name, WNDCLASSW* wc) {
	auto& c = chrono_linux::S().classes;
	auto it = c.find(name ? name : L"");
	if (it == c.end()) return FALSE;
	if (wc) *wc = it->second;
	return TRUE;
}
inline WORD RegisterClassW(const WNDCLASSW* wc) {
	if (!wc || !wc->lpszClassName) return 0;
	chrono_linux::S().classes[wc->lpszClassName] = *wc;
	return 1;
}
inline WORD RegisterClassExW(const WNDCLASSEXW* wc) {
	WNDCLASSW w{ wc->style, wc->lpfnWndProc, 0, 0, wc->hInstance, wc->hIcon, wc->hCursor,
	             wc->hbrBackground, wc->lpszMenuName, wc->lpszClassName };
	return RegisterClassW(&w);
}

inline HWND CreateWindowExW(DWORD exStyle, LPCWSTR cls, LPCWSTR title, DWORD style,
                            int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
	using namespace chrono_linux;
	EnsureSDL();
	WNDCLASSW wc{};
	if (!GetClassInfoW(inst, cls, &wc)) return nullptr;

	HWND hwnd = new HWND__();
	hwnd->proc = wc.lpfnWndProc;
	hwnd->style = style;
	hwnd->exStyle = exStyle;
	if (w == CW_USEDEFAULT || w <= 0) w = 800;
	if (h == CW_USEDEFAULT || h <= 0) h = 600;
	int px = (x == CW_USEDEFAULT) ? (int)SDL_WINDOWPOS_CENTERED : x;
	int py = (x == CW_USEDEFAULT) ? (int)SDL_WINDOWPOS_CENTERED : y;

	// No OS caption = the app draws its own chrome; WM_NCHITTEST then drives
	// moving and resizing through SDL's hit-test callback.
	bool custom = (style & WS_POPUP) || !(style & WS_CAPTION);
	Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;
	if (style & WS_THICKFRAME) flags |= SDL_WINDOW_RESIZABLE;
	if (custom) flags |= SDL_WINDOW_BORDERLESS;
	if (exStyle & WS_EX_TOPMOST) flags |= SDL_WINDOW_ALWAYS_ON_TOP;
	if (exStyle & WS_EX_TOOLWINDOW) flags |= SDL_WINDOW_SKIP_TASKBAR;
	hwnd->sdl = SDL_CreateWindow(ToUtf8(title).c_str(), px, py, w, h, flags);
	if (!hwnd->sdl) { fprintf(stderr, "ChronoUI: SDL_CreateWindow failed: %s\n", SDL_GetError()); delete hwnd; return nullptr; }
	SDL_SetWindowData(hwnd->sdl, "hwnd", hwnd);
	if (custom && (style & WS_THICKFRAME)) SDL_SetWindowHitTest(hwnd->sdl, HitTestThunk, hwnd);
	S().windows.push_back(hwnd);

	CREATESTRUCTW cs{ param, inst, menu, parent, h, w, y, x, (LONG)style, title, cls, exStyle };
	if (!SendMessageW(hwnd, WM_NCCREATE, 0, (LPARAM)&cs)) { DestroyWindow(hwnd); return nullptr; }
	if (SendMessageW(hwnd, WM_CREATE, 0, (LPARAM)&cs) == -1) { DestroyWindow(hwnd); return nullptr; }
	SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, h));
	if (style & WS_VISIBLE) { SDL_ShowWindow(hwnd->sdl); hwnd->visible = true; }
	return hwnd;
}
inline HWND CreateWindowW(LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h,
                          HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
	return CreateWindowExW(0, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

inline LONG_PTR GetWindowLongPtrW(HWND h, int idx) {
	if (!h) return 0;
	switch (idx) {
		case GWLP_USERDATA: return h->userData;
		case GWL_STYLE:     return (LONG_PTR)h->style;
		case GWL_EXSTYLE:   return (LONG_PTR)h->exStyle;
		case GWLP_WNDPROC:  return (LONG_PTR)h->proc;
		default:            return 0;
	}
}
inline LONG_PTR SetWindowLongPtrW(HWND h, int idx, LONG_PTR v) {
	if (!h) return 0;
	LONG_PTR old = GetWindowLongPtrW(h, idx);
	switch (idx) {
		case GWLP_USERDATA: h->userData = v; break;
		case GWL_STYLE:     h->style = (DWORD)v; break;
		case GWL_EXSTYLE:   h->exStyle = (DWORD)v; break;
		case GWLP_WNDPROC:  h->proc = (WNDPROC)v; break;
	}
	return old;
}

inline BOOL IsWindow(HWND h) { return h && !h->destroyed; }
inline BOOL IsWindowVisible(HWND h) { return IsWindow(h) && h->visible; }
inline BOOL IsZoomed(HWND h) { return IsWindow(h) && (SDL_GetWindowFlags(h->sdl) & SDL_WINDOW_MAXIMIZED); }
inline BOOL IsIconic(HWND h) { return IsWindow(h) && (SDL_GetWindowFlags(h->sdl) & SDL_WINDOW_MINIMIZED); }

inline BOOL ShowWindow(HWND h, int cmd) {
	if (!IsWindow(h)) return FALSE;
	BOOL was = h->visible;
	switch (cmd) {
		case SW_HIDE:          SDL_HideWindow(h->sdl); h->visible = false; break;
		case SW_SHOWMAXIMIZED: SDL_ShowWindow(h->sdl); SDL_MaximizeWindow(h->sdl); h->visible = true; break;
		case SW_MINIMIZE:
		case SW_SHOWMINIMIZED: SDL_MinimizeWindow(h->sdl); break;
		case SW_RESTORE:       SDL_RestoreWindow(h->sdl); SDL_ShowWindow(h->sdl); h->visible = true; break;
		default:               SDL_ShowWindow(h->sdl); h->visible = true; break;
	}
	h->dirty = true;
	return was;
}
inline BOOL UpdateWindow(HWND h) {
	if (IsWindow(h) && h->dirty) SendMessageW(h, WM_PAINT, 0, 0);
	return TRUE;
}
inline BOOL InvalidateRect(HWND h, const RECT*, BOOL) {
	if (!h) { for (HWND w : chrono_linux::S().windows) w->dirty = true; return TRUE; }
	if (IsWindow(h)) h->dirty = true;
	return TRUE;
}
inline BOOL ValidateRect(HWND h, const RECT*) { if (IsWindow(h)) h->dirty = false; return TRUE; }
inline HDC  BeginPaint(HWND h, PAINTSTRUCT* ps) { if (ps) *ps = PAINTSTRUCT{}; if (IsWindow(h)) h->dirty = false; return nullptr; }
inline BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }

inline BOOL GetClientRect(HWND h, RECT* r) {
	if (!r) return FALSE;
	*r = RECT{ 0, 0, 0, 0 };
	if (!IsWindow(h)) return FALSE;
	int w = 0, hh = 0; SDL_GetWindowSize(h->sdl, &w, &hh);
	*r = RECT{ 0, 0, w, hh };
	return TRUE;
}
inline BOOL GetWindowRect(HWND h, RECT* r) {
	if (!r || !IsWindow(h)) return FALSE;
	int x = 0, y = 0, w = 0, hh = 0;
	SDL_GetWindowPosition(h->sdl, &x, &y); SDL_GetWindowSize(h->sdl, &w, &hh);
	*r = RECT{ x, y, x + w, y + hh };
	return TRUE;
}
inline BOOL ClientToScreen(HWND h, POINT* p) {
	if (!p || !IsWindow(h)) return FALSE;
	int x = 0, y = 0; SDL_GetWindowPosition(h->sdl, &x, &y);
	p->x += x; p->y += y; return TRUE;
}
inline BOOL ScreenToClient(HWND h, POINT* p) {
	if (!p || !IsWindow(h)) return FALSE;
	int x = 0, y = 0; SDL_GetWindowPosition(h->sdl, &x, &y);
	p->x -= x; p->y -= y; return TRUE;
}
inline BOOL GetCursorPos(POINT* p) {
	if (!p) return FALSE;
	int x = 0, y = 0; SDL_GetGlobalMouseState(&x, &y);
	*p = POINT{ x, y }; return TRUE;
}
inline BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
	if (!IsWindow(h)) return FALSE;
	if (!(flags & SWP_NOMOVE)) SDL_SetWindowPosition(h->sdl, x, y);
	if (!(flags & SWP_NOSIZE)) SDL_SetWindowSize(h->sdl, cx, cy);
	if (after == HWND_TOPMOST)   SDL_SetWindowAlwaysOnTop(h->sdl, SDL_TRUE);
	if (after == HWND_NOTOPMOST) SDL_SetWindowAlwaysOnTop(h->sdl, SDL_FALSE);
	if (flags & SWP_SHOWWINDOW) ShowWindow(h, SW_SHOW);
	if (flags & SWP_HIDEWINDOW) ShowWindow(h, SW_HIDE);
	h->dirty = true;
	return TRUE;
}
inline BOOL MoveWindow(HWND h, int x, int y, int w, int hh, BOOL) { return SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER); }
inline BOOL SetWindowTextW(HWND h, LPCWSTR t) { if (!IsWindow(h)) return FALSE; SDL_SetWindowTitle(h->sdl, chrono_linux::ToUtf8(t).c_str()); return TRUE; }
inline HWND GetActiveWindow() {
	SDL_Window* w = SDL_GetKeyboardFocus();
	if (w) return (HWND)SDL_GetWindowData(w, "hwnd");
	auto& v = chrono_linux::S().windows;
	return v.empty() ? nullptr : v.back();
}
inline HWND GetForegroundWindow() { return GetActiveWindow(); }
inline HWND GetFocus()            { return GetActiveWindow(); }
inline HWND SetFocus(HWND h)      { if (IsWindow(h)) SDL_RaiseWindow(h->sdl); return h; }
inline BOOL SetForegroundWindow(HWND h) { if (IsWindow(h)) SDL_RaiseWindow(h->sdl); return TRUE; }
inline HWND GetParent(HWND)       { return nullptr; }
inline UINT GetDpiForWindow(HWND) { return USER_DEFAULT_SCREEN_DPI; }
inline UINT GetDpiForSystem()     { return USER_DEFAULT_SCREEN_DPI; }
inline int  GetSystemMetrics(int i) {
	chrono_linux::EnsureSDL();
	SDL_Rect r{ 0, 0, 1920, 1080 }; SDL_GetDisplayUsableBounds(0, &r);
	return i == SM_CXSCREEN ? r.w : i == SM_CYSCREEN ? r.h : 0;
}
inline BOOL EnableWindow(HWND, BOOL) { return TRUE; }

// --- mouse -----------------------------------------------------------------------
inline HWND SetCapture(HWND h) {
	auto& s = chrono_linux::S();
	HWND old = s.capture; s.capture = h;
	SDL_CaptureMouse(h ? SDL_TRUE : SDL_FALSE);
	return old;
}
inline BOOL ReleaseCapture() { chrono_linux::S().capture = nullptr; SDL_CaptureMouse(SDL_FALSE); return TRUE; }
inline HWND GetCapture()     { return chrono_linux::S().capture; }
inline BOOL TrackMouseEvent(TRACKMOUSEEVENT* t) { if (t && IsWindow(t->hwndTrack)) t->hwndTrack->trackLeave = (t->dwFlags & TME_LEAVE) != 0; return TRUE; }

inline HCURSOR LoadCursorW(HINSTANCE, LPCWSTR id) { return (HCURSOR)id; }
inline HICON   LoadIconW(HINSTANCE, LPCWSTR id)   { return (HICON)id; }
inline HCURSOR SetCursor(HCURSOR c) {
	static std::map<uintptr_t, SDL_Cursor*> cache;
	uintptr_t id = (uintptr_t)c;
	if (!id) { SDL_ShowCursor(SDL_DISABLE); return nullptr; }
	SDL_ShowCursor(SDL_ENABLE);
	SDL_SystemCursor sc = SDL_SYSTEM_CURSOR_ARROW;
	switch (id) {
		case 32513: sc = SDL_SYSTEM_CURSOR_IBEAM; break;    case 32514: sc = SDL_SYSTEM_CURSOR_WAIT; break;
		case 32515: sc = SDL_SYSTEM_CURSOR_CROSSHAIR; break; case 32642: sc = SDL_SYSTEM_CURSOR_SIZENWSE; break;
		case 32643: sc = SDL_SYSTEM_CURSOR_SIZENESW; break;  case 32644: sc = SDL_SYSTEM_CURSOR_SIZEWE; break;
		case 32645: sc = SDL_SYSTEM_CURSOR_SIZENS; break;    case 32646: sc = SDL_SYSTEM_CURSOR_SIZEALL; break;
		case 32649: sc = SDL_SYSTEM_CURSOR_HAND; break;
	}
	SDL_Cursor*& cur = cache[id];
	if (!cur) cur = SDL_CreateSystemCursor(sc);
	if (cur) SDL_SetCursor(cur);
	return c;
}

// --- keyboard ----------------------------------------------------------------------
inline SHORT GetKeyState(int vk) {
	using namespace chrono_linux;
	SDL_Keymod m = SDL_GetModState();
	auto down = [](bool b) -> SHORT { return b ? (SHORT)0x8000 : (SHORT)0; };
	switch (vk) {
		case VK_SHIFT:   return down(m & KMOD_SHIFT);
		case VK_CONTROL: return down(m & KMOD_CTRL);
		case VK_MENU:    return down(m & KMOD_ALT);
		case VK_CAPITAL: return (SHORT)((m & KMOD_CAPS) ? 1 : 0);
		case VK_LBUTTON: return down(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK);
		case VK_RBUTTON: return down(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK);
		case VK_MBUTTON: return down(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MMASK);
		case VK_LWIN: case VK_RWIN: return down(m & KMOD_GUI);
	}
	SDL_Scancode sc = FromVK(vk);
	if (sc == SDL_SCANCODE_UNKNOWN) return 0;
	return down(SDL_GetKeyboardState(nullptr)[sc] != 0);
}
inline SHORT GetAsyncKeyState(int vk) { return GetKeyState(vk); }

// --- timers and time -----------------------------------------------------------------
inline UINT_PTR SetTimer(HWND h, UINT_PTR id, UINT ms, void*) {
	if (!IsWindow(h)) return 0;
	h->timers[id] = HWND__::Timer{ ms ? ms : 1, chrono_linux::NowMs() + (ms ? ms : 1) };
	return id;
}
inline BOOL KillTimer(HWND h, UINT_PTR id) { if (IsWindow(h)) h->timers.erase(id); return TRUE; }

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = (int64_t)SDL_GetPerformanceFrequency(); return TRUE; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c)   { c->QuadPart = (int64_t)SDL_GetPerformanceCounter(); return TRUE; }
inline DWORD     GetTickCount()   { return (DWORD)chrono_linux::NowMs(); }
inline ULONGLONG GetTickCount64() { return (ULONGLONG)chrono_linux::NowMs(); }
inline void      Sleep(DWORD ms)  { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline UINT      timeBeginPeriod(UINT) { return 0; }
inline UINT      timeEndPeriod(UINT)   { return 0; }
inline void GetLocalTime(SYSTEMTIME* st) {
	using namespace std::chrono;
	auto now = system_clock::now();
	time_t t = system_clock::to_time_t(now);
	struct tm tmv; localtime_r(&t, &tmv);
	*st = SYSTEMTIME{ (WORD)(tmv.tm_year + 1900), (WORD)(tmv.tm_mon + 1), (WORD)tmv.tm_wday, (WORD)tmv.tm_mday,
	                  (WORD)tmv.tm_hour, (WORD)tmv.tm_min, (WORD)tmv.tm_sec,
	                  (WORD)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000) };
}
inline void GetSystemTime(SYSTEMTIME* st) {
	time_t t = time(nullptr); struct tm tmv; gmtime_r(&t, &tmv);
	*st = SYSTEMTIME{ (WORD)(tmv.tm_year + 1900), (WORD)(tmv.tm_mon + 1), (WORD)tmv.tm_wday, (WORD)tmv.tm_mday,
	                  (WORD)tmv.tm_hour, (WORD)tmv.tm_min, (WORD)tmv.tm_sec, 0 };
}

// --- process -------------------------------------------------------------------------
inline DWORD GetModuleFileNameW(HMODULE, LPWSTR buf, DWORD size) {
	char path[4096]; ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (n <= 0 || !buf || !size) return 0;
	std::wstring w = chrono_linux::FromUtf8(path, (size_t)n);
	DWORD k = (DWORD)(w.size() < size ? w.size() : size - 1);
	wmemcpy(buf, w.data(), k); buf[k] = 0;
	return k;
}
// CPU times from /proc/stat, in Windows' 100 ns units. Windows counts idle
// time inside kernel time, so kernel = system + irq + softirq + idle.
inline BOOL GetSystemTimes(FILETIME* idle, FILETIME* kernel, FILETIME* user) {
	FILE* f = fopen("/proc/stat", "r");
	if (!f) return FALSE;
	unsigned long long u = 0, n = 0, s = 0, i = 0, io = 0, irq = 0, sirq = 0;
	int got = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &i, &io, &irq, &sirq);
	fclose(f);
	if (got < 4) return FALSE;
	double k = 1e7 / (double)sysconf(_SC_CLK_TCK);
	auto ft = [k](unsigned long long ticks) { ULONGLONG v = (ULONGLONG)((double)ticks * k); return FILETIME{ (DWORD)v, (DWORD)(v >> 32) }; };
	if (idle)   *idle = ft(i + io);
	if (kernel) *kernel = ft(s + irq + sirq + i + io);
	if (user)   *user = ft(u + n);
	return TRUE;
}
inline BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* m) {
	if (!m) return FALSE;
	FILE* f = fopen("/proc/meminfo", "r");
	if (!f) return FALSE;
	char key[64]; unsigned long long v, total = 0, avail = 0;
	while (fscanf(f, "%63s %llu kB\n", key, &v) == 2) {
		if (!strcmp(key, "MemTotal:")) total = v * 1024;
		if (!strcmp(key, "MemAvailable:")) avail = v * 1024;
	}
	fclose(f);
	if (!total) return FALSE;
	m->ullTotalPhys = total; m->ullAvailPhys = avail;
	m->dwMemoryLoad = (DWORD)(100.0 * (1.0 - (double)avail / (double)total) + 0.5);
	return TRUE;
}

inline HMODULE GetModuleHandleW(LPCWSTR) { return nullptr; }
inline DWORD   GetCurrentThreadId() { return (DWORD)std::hash<std::thread::id>()(std::this_thread::get_id()); }
inline DWORD   GetLastError() { return 0; }
inline void    OutputDebugStringW(LPCWSTR s) { fprintf(stderr, "%s", chrono_linux::ToUtf8(s).c_str()); }
inline void    OutputDebugStringA(LPCSTR s)  { fprintf(stderr, "%s", s); }

inline int WideCharToMultiByte(UINT, DWORD, LPCWSTR src, int n, LPSTR dst, int size, LPCSTR, BOOL*) {
	if (!src) return 0;
	bool withNul = n < 0;
	std::string s = chrono_linux::ToUtf8(src, withNul ? wcslen(src) : (size_t)n);
	int need = (int)s.size() + (withNul ? 1 : 0);
	if (!dst || size == 0) return need;
	int k = need < size ? need : size;
	memcpy(dst, s.c_str(), (size_t)k);
	return k;
}
inline int MultiByteToWideChar(UINT, DWORD, LPCSTR src, int n, LPWSTR dst, int size) {
	if (!src) return 0;
	bool withNul = n < 0;
	std::wstring w = chrono_linux::FromUtf8(src, withNul ? strlen(src) : (size_t)n);
	int need = (int)w.size() + (withNul ? 1 : 0);
	if (!dst || size == 0) return need;
	int k = need < size ? need : size;
	wmemcpy(dst, w.c_str(), (size_t)k);
	return k;
}

inline int MessageBoxW(HWND h, LPCWSTR text, LPCWSTR caption, UINT type) {
	using namespace chrono_linux;
	std::string t = ToUtf8(text), c = ToUtf8(caption);
	SDL_MessageBoxButtonData yesno[2] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDYES, "Yes" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDNO, "No" } };
	SDL_MessageBoxButtonData okc[2]   = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDOK, "OK" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDCANCEL, "Cancel" } };
	bool yn = (type & 0xF) == MB_YESNO, oc = (type & 0xF) == MB_OKCANCEL;
	SDL_MessageBoxData d{};
	d.flags = (type & MB_ICONERROR) ? SDL_MESSAGEBOX_ERROR : (type & MB_ICONWARNING) == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
	d.window = IsWindow(h) ? h->sdl : nullptr;
	d.title = c.c_str(); d.message = t.c_str();
	d.numbuttons = (yn || oc) ? 2 : 1;
	d.buttons = yn ? yesno : okc;
	int id = IDOK;
	SDL_ShowMessageBox(&d, &id);
	return id;
}

// --- file drop -------------------------------------------------------------------------
inline void DragAcceptFiles(HWND, BOOL) {}   // SDL delivers drops to every window
inline UINT DragQueryFileW(HDROP d, UINT i, LPWSTR buf, UINT cch) {
	auto* v = (std::vector<std::wstring>*)d;
	if (!v) return 0;
	if (i == 0xFFFFFFFF) return (UINT)v->size();
	if (i >= v->size()) return 0;
	const std::wstring& p = (*v)[i];
	if (!buf) return (UINT)p.size();
	UINT k = (UINT)(p.size() < cch ? p.size() : (cch ? cch - 1 : 0));
	wmemcpy(buf, p.data(), k); if (cch) buf[k] = 0;
	return k;
}
inline void DragFinish(HDROP d) { delete (std::vector<std::wstring>*)d; }

// --- clipboard ---------------------------------------------------------------------------
// HGLOBAL is a malloc'd block with its size in front, enough for the
// Alloc / Lock / copy / Unlock / SetClipboardData dance the widgets do.
inline HGLOBAL GlobalAlloc(UINT, SIZE_T n) {
	size_t* p = (size_t*)calloc(1, n + sizeof(size_t) * 2);
	if (!p) return nullptr;
	*p = n; return (HGLOBAL)(p + 2);
}
inline LPVOID  GlobalLock(HGLOBAL h)   { return h; }
inline BOOL    GlobalUnlock(HGLOBAL)   { return TRUE; }
inline SIZE_T  GlobalSize(HGLOBAL h)   { return h ? *((size_t*)h - 2) : 0; }
inline HGLOBAL GlobalFree(HGLOBAL h)   { if (h) free((size_t*)h - 2); return nullptr; }

inline BOOL OpenClipboard(HWND)  { chrono_linux::EnsureSDL(); return TRUE; }
inline BOOL EmptyClipboard()     { auto& s = chrono_linux::S(); s.clipboard.clear(); s.clipboardDirty = true; return TRUE; }
inline HANDLE SetClipboardData(UINT fmt, HANDLE h) {
	auto& s = chrono_linux::S();
	if (fmt == CF_UNICODETEXT && h) s.clipboard.assign((const wchar_t*)h, wcsnlen((const wchar_t*)h, GlobalSize(h) / sizeof(wchar_t)));
	else if (fmt == CF_TEXT && h) s.clipboard = chrono_linux::FromUtf8((const char*)h);
	s.clipboardDirty = true;
	GlobalFree(h);   // the clipboard owns the block once set
	return h;
}
inline HANDLE GetClipboardData(UINT fmt) {
	if (fmt != CF_UNICODETEXT) return nullptr;
	static HGLOBAL last = nullptr;          // valid until the next call, like the real one until CloseClipboard
	if (last) { GlobalFree(last); last = nullptr; }
	char* t = SDL_GetClipboardText();
	std::wstring w = chrono_linux::FromUtf8(t ? t : "");
	SDL_free(t);
	last = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
	wmemcpy((wchar_t*)last, w.c_str(), w.size() + 1);
	return last;
}
inline BOOL IsClipboardFormatAvailable(UINT fmt) { return (fmt == CF_UNICODETEXT || fmt == CF_TEXT) && SDL_HasClipboardText(); }
inline BOOL CloseClipboard() {
	auto& s = chrono_linux::S();
	if (s.clipboardDirty) { SDL_SetClipboardText(chrono_linux::ToUtf8(s.clipboard).c_str()); s.clipboardDirty = false; }
	return TRUE;
}

// --- popup menus -------------------------------------------------------------------------
inline HMENU CreatePopupMenu() { return new HMENU__(); }
inline HMENU CreateMenu()      { return new HMENU__(); }
inline BOOL  DestroyMenu(HMENU m) { delete m; return TRUE; }
inline BOOL  AppendMenuW(HMENU m, UINT flags, UINT_PTR id, LPCWSTR text) {
	if (!m) return FALSE;
	m->items.push_back(HMENU__::Item{ flags, id, (flags & MF_SEPARATOR) || !text ? L"" : text });
	return TRUE;
}
inline BOOL CheckMenuItem(HMENU, UINT, UINT) { return TRUE; }
namespace chrono_linux { inline int RunPopupMenu(HMENU m, int x, int y, UINT flags); }
inline BOOL TrackPopupMenu(HMENU m, UINT flags, int x, int y, int, HWND owner, const RECT*) {
	int cmd = chrono_linux::RunPopupMenu(m, x, y, flags);
	if (flags & TPM_RETURNCMD) return cmd;
	if (cmd > 0 && !(flags & TPM_NONOTIFY)) PostMessageW(owner, WM_COMMAND, (WPARAM)cmd, 0);
	return cmd > 0;
}
inline BOOL TrackPopupMenuEx(HMENU m, UINT flags, int x, int y, HWND owner, void*) { return TrackPopupMenu(m, flags, x, y, 0, owner, nullptr); }

// --- the message loop ----------------------------------------------------------------------
namespace chrono_linux {

	inline SDL_HitTestResult HitTestThunk(SDL_Window* w, const SDL_Point* pt, void* data) {
		HWND h = (HWND)data;
		int wx = 0, wy = 0; SDL_GetWindowPosition(w, &wx, &wy);
		LRESULT ht = SendMessageW(h, WM_NCHITTEST, 0, MAKELPARAM(pt->x + wx, pt->y + wy));
		switch (ht) {
			case HTCAPTION:     return SDL_HITTEST_DRAGGABLE;
			case HTLEFT:        return SDL_HITTEST_RESIZE_LEFT;
			case HTRIGHT:       return SDL_HITTEST_RESIZE_RIGHT;
			case HTTOP:         return SDL_HITTEST_RESIZE_TOP;
			case HTBOTTOM:      return SDL_HITTEST_RESIZE_BOTTOM;
			case HTTOPLEFT:     return SDL_HITTEST_RESIZE_TOPLEFT;
			case HTTOPRIGHT:    return SDL_HITTEST_RESIZE_TOPRIGHT;
			case HTBOTTOMLEFT:  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
			case HTBOTTOMRIGHT: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
			default:            return SDL_HITTEST_NORMAL;
		}
	}

	inline WPARAM MouseKeys() {
		Uint32 b = SDL_GetMouseState(nullptr, nullptr);
		SDL_Keymod m = SDL_GetModState();
		WPARAM k = 0;
		if (b & SDL_BUTTON_LMASK) k |= MK_LBUTTON;
		if (b & SDL_BUTTON_RMASK) k |= MK_RBUTTON;
		if (m & KMOD_SHIFT) k |= MK_SHIFT;
		if (m & KMOD_CTRL)  k |= MK_CONTROL;
		return k;
	}

	inline void TranslateEvent(const SDL_Event& e) {
		auto& s = S();
		switch (e.type) {
		case SDL_WINDOWEVENT: {
			HWND h = FromSDL(e.window.windowID);
			if (!h) break;
			switch (e.window.event) {
				case SDL_WINDOWEVENT_SIZE_CHANGED: {
					WPARAM kind = (SDL_GetWindowFlags(h->sdl) & SDL_WINDOW_MAXIMIZED) ? SIZE_MAXIMIZED : SIZE_RESTORED;
					Post(h, WM_SIZE, kind, MAKELPARAM(e.window.data1, e.window.data2));
					h->dirty = true; break;
				}
				case SDL_WINDOWEVENT_MINIMIZED: Post(h, WM_SIZE, SIZE_MINIMIZED, 0); break;
				case SDL_WINDOWEVENT_EXPOSED:
				case SDL_WINDOWEVENT_SHOWN:
				case SDL_WINDOWEVENT_RESTORED:  h->dirty = true; break;
				case SDL_WINDOWEVENT_LEAVE:
					if (h->trackLeave && s.capture != h) { h->trackLeave = false; Post(h, WM_MOUSELEAVE, 0, 0); }
					break;
				case SDL_WINDOWEVENT_FOCUS_GAINED: Post(h, WM_SETFOCUS, 0, 0); break;
				case SDL_WINDOWEVENT_FOCUS_LOST:   Post(h, WM_KILLFOCUS, 0, 0); break;
				case SDL_WINDOWEVENT_CLOSE:        Post(h, WM_CLOSE, 0, 0); break;
			}
			break;
		}
		case SDL_MOUSEMOTION: {
			HWND h = s.capture ? s.capture : FromSDL(e.motion.windowID);
			if (!h) break;
			int x = e.motion.x, y = e.motion.y;
			if (s.capture && h->sdl != SDL_GetWindowFromID(e.motion.windowID)) {   // outside: from global coords
				int gx, gy, wx, wy; SDL_GetGlobalMouseState(&gx, &gy); SDL_GetWindowPosition(h->sdl, &wx, &wy);
				x = gx - wx; y = gy - wy;
			}
			Post(h, WM_MOUSEMOVE, MouseKeys(), MAKELPARAM(x, y));
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			HWND h = s.capture ? s.capture : FromSDL(e.button.windowID);
			if (!h) break;
			bool down = e.type == SDL_MOUSEBUTTONDOWN;
			UINT msg = 0;
			if (e.button.button == SDL_BUTTON_LEFT)   msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
			if (e.button.button == SDL_BUTTON_RIGHT)  msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
			if (e.button.button == SDL_BUTTON_MIDDLE) msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
			if (!msg) break;
			int x = e.button.x, y = e.button.y;
			if (s.capture && h->sdl != SDL_GetWindowFromID(e.button.windowID)) {
				int gx, gy, wx, wy; SDL_GetGlobalMouseState(&gx, &gy); SDL_GetWindowPosition(h->sdl, &wx, &wy);
				x = gx - wx; y = gy - wy;
			}
			Post(h, msg, MouseKeys(), MAKELPARAM(x, y));
			break;
		}
		case SDL_MOUSEWHEEL: {
			HWND h = FromSDL(e.wheel.windowID);
			if (!h) break;
			int gx, gy; SDL_GetGlobalMouseState(&gx, &gy);
			float dy = e.wheel.preciseY, dx = e.wheel.preciseX;
			if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { dy = -dy; dx = -dx; }
			if (dy != 0.0f) Post(h, WM_MOUSEWHEEL, MAKEWPARAM(MouseKeys(), (short)(dy * 120.0f)), MAKELPARAM(gx, gy));
			if (dx != 0.0f) Post(h, WM_MOUSEHWHEEL, MAKEWPARAM(MouseKeys(), (short)(dx * 120.0f)), MAKELPARAM(gx, gy));
			break;
		}
		case SDL_KEYDOWN:
		case SDL_KEYUP: {
			HWND h = FromSDL(e.key.windowID);
			if (!h) break;
			UINT vk = ToVK(e.key.keysym.sym);
			if (!vk) break;
			bool alt = (e.key.keysym.mod & KMOD_ALT) != 0;
			if (e.type == SDL_KEYUP) { Post(h, alt ? WM_SYSKEYUP : WM_KEYUP, vk, 0); break; }
			Post(h, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, e.key.repeat ? (1 << 30) : 0);
			// What TranslateMessage would add: control characters as WM_CHAR.
			// Printable text arrives through SDL_TEXTINPUT instead.
			wchar_t ch = 0;
			if (vk == VK_BACK) ch = 8; else if (vk == VK_TAB) ch = 9;
			else if (vk == VK_RETURN) ch = 13; else if (vk == VK_ESCAPE) ch = 27;
			else if ((e.key.keysym.mod & KMOD_CTRL) && !alt && vk >= 'A' && vk <= 'Z') ch = (wchar_t)(vk - 'A' + 1);
			if (ch) Post(h, WM_CHAR, (WPARAM)ch, 0);
			break;
		}
		case SDL_TEXTINPUT: {
			HWND h = FromSDL(e.text.windowID);
			if (!h) break;
			SDL_Keymod m = SDL_GetModState();
			if ((m & KMOD_CTRL) && !(m & KMOD_ALT)) break;   // Ctrl+letter already went out as a control char
			for (wchar_t c : FromUtf8(e.text.text)) Post(h, WM_CHAR, (WPARAM)c, 0);
			break;
		}
		case SDL_DROPFILE: {
			HWND h = FromSDL(e.drop.windowID);
			if (!h && !s.windows.empty()) h = s.windows.back();
			if (h && e.drop.file) h->pendingDrops.push_back(FromUtf8(e.drop.file));
			SDL_free(e.drop.file);
			break;
		}
		case SDL_DROPCOMPLETE: {
			HWND h = FromSDL(e.drop.windowID);
			if (!h && !s.windows.empty()) h = s.windows.back();
			if (h && !h->pendingDrops.empty()) {
				auto* list = new std::vector<std::wstring>(std::move(h->pendingDrops));
				h->pendingDrops.clear();
				Post(h, WM_DROPFILES, (WPARAM)list, 0);
			}
			break;
		}
		}
	}

	// One message, the way Windows orders them: posted and input messages
	// first, then WM_TIMER, then WM_PAINT for windows that were invalidated.
	// Blocks in SDL until something happens or the next timer is due.
	inline bool NextMessage(MSG* out, bool wait) {
		auto& s = S();
		for (;;) {
			SDL_Event e;
			while (SDL_PollEvent(&e)) TranslateEvent(e);
			{
				std::lock_guard<std::recursive_mutex> g(s.queueLock);
				if (!s.queue.empty()) { *out = s.queue.front(); s.queue.pop_front(); return true; }
			}
			uint64_t now = NowMs(), next = UINT64_MAX;
			if (getenv("CHRONOUI_SHOT") && !s.shotDone) {
				if (!s.shotAt) s.shotAt = now + (getenv("CHRONOUI_SHOT_MS") ? (uint64_t)atoi(getenv("CHRONOUI_SHOT_MS")) : 1000);
				if (now >= s.shotAt) { s.shotDone = s.shotArmed = true; InvalidateRect(nullptr, nullptr, FALSE); }
				else next = s.shotAt;
			}
			for (HWND h : s.windows) for (auto& t : h->timers) {
				if (t.second.due <= now) {
					t.second.due = now + t.second.ms;
					*out = MSG{ h, WM_TIMER, t.first, 0, (DWORD)now, {0, 0} };
					return true;
				}
				if (t.second.due < next) next = t.second.due;
			}
			for (HWND h : s.windows) if (h->dirty && h->visible) { *out = MSG{ h, WM_PAINT, 0, 0, (DWORD)now, {0, 0} }; return true; }
			if (!wait) return false;
			int timeout = next == UINT64_MAX ? 100 : (int)(next - now);
			if (SDL_WaitEventTimeout(&e, timeout)) TranslateEvent(e);
		}
	}
}

inline BOOL GetMessageW(MSG* msg, HWND, UINT, UINT) {
	chrono_linux::NextMessage(msg, true);
	return msg->message == WM_QUIT ? FALSE : TRUE;
}
#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001
inline BOOL PeekMessageW(MSG* msg, HWND, UINT, UINT, UINT) { return chrono_linux::NextMessage(msg, false) ? TRUE : FALSE; }
inline BOOL TranslateMessage(const MSG*) { return TRUE; }   // WM_CHAR is produced while translating SDL events
inline LRESULT DispatchMessageW(const MSG* m) {
	if (m->hwnd) {
		if (!IsWindow(m->hwnd)) return 0;
		if (m->message == WM_PAINT) {
			LRESULT r = SendMessageW(m->hwnd, WM_PAINT, 0, 0);
			m->hwnd->dirty = false;   // a WndProc that painted without BeginPaint must not spin
			return r;
		}
		return SendMessageW(m->hwnd, m->message, m->wParam, m->lParam);
	}
	return 0;
}
#define GetMessage      GetMessageW
#define PeekMessage     PeekMessageW
#define DispatchMessage DispatchMessageW

// --- the popup menu itself ------------------------------------------------------------------
// A borderless always-on-top SDL window drawn with Cairo and Pango, run in a
// nested loop until an item is picked, Escape is pressed or focus leaves.
namespace chrono_linux {

	inline int RunPopupMenu(HMENU m, int x, int y, UINT flags) {
		if (!m || m->items.empty()) return 0;
		EnsureSDL();
		const int rowH = 30, sepH = 9, padY = 4, padX = 32, minW = 140;

		PangoFontMap* fm = pango_cairo_font_map_get_default();
		PangoContext* pc = pango_font_map_create_context(fm);
		PangoLayout* lay = pango_layout_new(pc);
		PangoFontDescription* fd = pango_font_description_from_string("Noto Sans");
		pango_font_description_set_absolute_size(fd, 13.0 * PANGO_SCALE);
		pango_layout_set_font_description(lay, fd);

		int w = minW, h = padY * 2;
		for (auto& it : m->items) {
			if (it.flags & MF_SEPARATOR) { h += sepH; continue; }
			pango_layout_set_text(lay, ToUtf8(it.text).c_str(), -1);
			int tw, th; pango_layout_get_pixel_size(lay, &tw, &th);
			if (tw + padX * 2 > w) w = tw + padX * 2;
			h += rowH;
		}
		if (flags & TPM_RIGHTALIGN) x -= w; else if (flags & TPM_CENTERALIGN) x -= w / 2;
		if (flags & TPM_BOTTOMALIGN) y -= h; else if (flags & TPM_VCENTERALIGN) y -= h / 2;
		SDL_Rect screen{ 0, 0, 1920, 1080 }; SDL_GetDisplayUsableBounds(0, &screen);
		if (x + w > screen.x + screen.w) x = screen.x + screen.w - w;
		if (y + h > screen.y + screen.h) y = screen.y + screen.h - h;
		if (x < screen.x) x = screen.x;
		if (y < screen.y) y = screen.y;

		SDL_Window* win = SDL_CreateWindow("", x, y, w, h,
			SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_POPUP_MENU);
		if (!win) { g_object_unref(lay); g_object_unref(pc); pango_font_description_free(fd); return 0; }
		SDL_RaiseWindow(win);

		auto rowAt = [&](int my) -> int {
			int top = padY;
			for (size_t i = 0; i < m->items.size(); ++i) {
				int rh = (m->items[i].flags & MF_SEPARATOR) ? sepH : rowH;
				if (my >= top && my < top + rh) return (m->items[i].flags & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED)) ? -1 : (int)i;
				top += rh;
			}
			return -1;
		};
		auto step = [&](int from, int dir) -> int {
			int n = (int)m->items.size();
			for (int k = 1; k <= n; ++k) {
				int i = ((from < 0 ? (dir > 0 ? -1 : n) : from) + dir * k % n + n * 2) % n;
				if (!(m->items[(size_t)i].flags & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED))) return i;
			}
			return -1;
		};

		int hot = -1, result = 0;
		bool done = false, everFocused = false, dirty = true;
		cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		while (!done) {
			if (dirty) {
				dirty = false;
				cairo_t* cr = cairo_create(surf);
				cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
				cairo_set_source_rgba(cr, 0, 0, 0, 0.18); cairo_set_line_width(cr, 1);
				cairo_rectangle(cr, 0.5, 0.5, w - 1, h - 1); cairo_stroke(cr);
				int top = padY;
				for (size_t i = 0; i < m->items.size(); ++i) {
					auto& it = m->items[i];
					if (it.flags & MF_SEPARATOR) {
						cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
						cairo_rectangle(cr, 8, top + sepH / 2, w - 16, 1); cairo_fill(cr);
						top += sepH; continue;
					}
					if ((int)i == hot) {
						cairo_set_source_rgba(cr, 0, 0, 0, 0.06);
						cairo_rectangle(cr, 4, top, w - 8, rowH); cairo_fill(cr);
					}
					bool grey = (it.flags & (MF_GRAYED | MF_DISABLED)) != 0;
					cairo_set_source_rgb(cr, grey ? 0.62 : 0.07, grey ? 0.64 : 0.09, grey ? 0.68 : 0.15);
					if (it.flags & MF_CHECKED) {
						cairo_set_line_width(cr, 1.6);
						cairo_move_to(cr, 11, top + rowH / 2); cairo_line_to(cr, 15, top + rowH / 2 + 4);
						cairo_line_to(cr, 22, top + rowH / 2 - 4); cairo_stroke(cr);
					}
					pango_layout_set_text(lay, ToUtf8(it.text).c_str(), -1);
					int tw, th; pango_layout_get_pixel_size(lay, &tw, &th);
					cairo_move_to(cr, padX, top + (rowH - th) / 2);
					pango_cairo_show_layout(cr, lay);
					top += rowH;
				}
				cairo_destroy(cr);
				cairo_surface_flush(surf);
				SDL_Surface* ws = SDL_GetWindowSurface(win);
				if (ws) {
					SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_ARGB8888, cairo_image_surface_get_data(surf),
						cairo_image_surface_get_stride(surf), ws->format->format, ws->pixels, ws->pitch);
					SDL_UpdateWindowSurface(win);
				}
			}
			SDL_Event e;
			if (!SDL_WaitEventTimeout(&e, 50)) continue;
			Uint32 myId = SDL_GetWindowID(win);
			switch (e.type) {
			case SDL_MOUSEMOTION:
				if (e.motion.windowID == myId) { int r = rowAt(e.motion.y); if (r != hot) { hot = r; dirty = true; } }
				break;
			case SDL_MOUSEBUTTONUP:
				if (e.button.windowID == myId) {
					int r = rowAt(e.button.y);
					if (r >= 0) { result = (int)m->items[(size_t)r].id; done = true; }
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (e.button.windowID != myId) done = true;     // a click elsewhere dismisses
				break;
			case SDL_KEYDOWN:
				switch (e.key.keysym.sym) {
					case SDLK_ESCAPE: done = true; break;
					case SDLK_DOWN: hot = step(hot, +1); dirty = true; break;
					case SDLK_UP:   hot = step(hot, -1); dirty = true; break;
					case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
						if (hot >= 0) { result = (int)m->items[(size_t)hot].id; done = true; } break;
				}
				break;
			case SDL_WINDOWEVENT:
				if (e.window.windowID == myId) {
					if (e.window.event == SDL_WINDOWEVENT_EXPOSED) dirty = true;
					if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) everFocused = true;
					if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && everFocused) done = true;
					if (e.window.event == SDL_WINDOWEVENT_LEAVE && hot >= 0) { hot = -1; dirty = true; }
				} else {
					TranslateEvent(e);   // keep the owner's resizes / exposes for later
				}
				break;
			case SDL_QUIT: done = true; break;
			}
		}
		cairo_surface_destroy(surf);
		SDL_DestroyWindow(win);
		g_object_unref(lay); g_object_unref(pc); pango_font_description_free(fd);
		for (HWND hw : S().windows) hw->dirty = true;
		return result;
	}
}
