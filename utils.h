#pragma once

#include <Windows.h>

#include <wil/result.h>


inline auto width(RECT const& r) { return r.right - r.left; }


inline auto height(RECT const& r) { return r.bottom - r.top; }


inline void modify_style(HWND const wnd, DWORD style_add, DWORD style_remove = 0x0)
{
    auto style { ::GetWindowLongPtrW(wnd, GWL_STYLE) };
    THROW_LAST_ERROR_IF(style == 0);

    style |= style_add;
    style &= ~static_cast<LONG_PTR>(style_remove);

    auto result { ::SetWindowLongPtrW(wnd, GWL_STYLE, style) };
    THROW_LAST_ERROR_IF(result == 0);
}


inline void modify_style_ex(HWND const wnd, DWORD style_add, DWORD style_remove = 0x0)
{
    auto style { ::GetWindowLongPtrW(wnd, GWL_EXSTYLE) };
    THROW_LAST_ERROR_IF(style == 0);

    style |= style_add;
    style &= ~static_cast<LONG_PTR>(style_remove);

    auto result { ::SetWindowLongPtrW(wnd, GWL_EXSTYLE, style) };
    THROW_LAST_ERROR_IF(result == 0);
}
