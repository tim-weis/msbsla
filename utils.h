#pragma once

#include <Windows.h>

#include <wil/result.h>


//! \brief Calculates the width of a rectangle.
//!
//! \param r The rectangle to calculate the width for.
//!
//! \return The width of the rectangle. This value can be negative if the
//!         rectangle isn't normalized.
//!
[[nodiscard]] constexpr inline auto width(RECT const& r) noexcept { return r.right - r.left; }


//! \brief Calculates the height of a rectangle.
//!
//! \param r The rectangle to calculate the height for.
//!
//! \return The height of the rectangle. This value can be negative if the
//!         rectangle isn't normalized.
//!
[[nodiscard]] constexpr inline auto height(RECT const& r) noexcept { return r.bottom - r.top; }


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
