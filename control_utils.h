#pragma once


#include <wil/result.h>

#include <CommCtrl.h>
#include <Windows.h>

#include <cassert>
#include <string>


inline ::std::wstring wnd_class_name(HWND const hwnd)
{
    wchar_t buffer[128] {};
    auto const chars_written { ::RealGetWindowClassW(hwnd, buffer, ARRAYSIZE(buffer)) };
    THROW_LAST_ERROR_IF(chars_written == 0);
    return { buffer, chars_written };
}


inline bool is_list_view(HWND const hwnd) { return wnd_class_name(hwnd) == WC_LISTVIEWW; }


inline void list_view_clear_selection(HWND const list_view)
{
    assert(is_list_view(list_view));

    auto sel_index { ListView_GetNextItem(list_view, -1, LVIS_SELECTED) };
    while (sel_index >= 0)
    {
        ListView_SetItemState(list_view, sel_index, 0, LVIS_SELECTED);
        sel_index = ListView_GetNextItem(list_view, sel_index, LVIS_SELECTED);
    }
}
