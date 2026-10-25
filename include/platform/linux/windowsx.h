// Linux port: <windowsx.h> message crackers.
#pragma once
#include "chrono_win32.hpp"
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
