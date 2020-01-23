#pragma once

#include <Windows.h>


inline auto width(RECT const& r) { return r.right - r.left; }

inline auto height(RECT const& r) { return r.bottom - r.top; }
