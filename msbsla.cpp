#include "msbsla.h"

#include "framework.h"

#include "control_utils.h"
#include "display_utils.h"
#include "log_utils.h"
#include "model.h"
#include "utils.h"

#include <wil/resource.h>

#include <CommCtrl.h>
#include <Windows.h>
#include <windowsx.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <wchar.h>


namespace fs = ::std::filesystem;


// Enable Visual Styles by embedding an appropriate manifest
#pragma comment(linker, "\"/manifestdependency:type='win32' \
                         name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
                         processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Types
enum class packet_col
{
    index,
    type,
    size,
    payload,
    details,

    last_value = details
};


// Constants
constexpr auto k_diagram_height { 120 };


// Local data
static HWND g_main_dlg_handle { nullptr };
static HWND g_label_logs_handle { nullptr };
static HWND g_lv_logs_handle { nullptr };
static HWND g_button_load_handle { nullptr };
static HWND g_lv_packets_handle { nullptr };

static RECT g_rc_diagram {};

::std::unique_ptr<model> g_spModel { nullptr };


struct log_info
{
    fs::directory_entry file_path;
    ::FILETIME timestamp;
};


// Local functions
static void clear_log_list(HWND list_view)
{
    assert(is_list_view(list_view));

    auto const count { ListView_GetItemCount(list_view) };
    // Delete associated data
    for (auto index { 0 }; index < count; ++index)
    {
        LVITEMW lvi {};
        lvi.mask = LVIF_PARAM;
        lvi.iItem = 0;
        ListView_GetItem(list_view, &lvi);
        auto p { reinterpret_cast<log_info*>(lvi.lParam) };
        delete p;
    }

    ListView_DeleteAllItems(list_view);
}

static void populate_log_list(wchar_t const* log_dir, HWND list_view)
{
    assert(is_list_view(list_view));

    auto item_index { 0 };

    auto it { fs::directory_iterator(log_dir) };
    for (auto file_path : it)
    {
        if (file_path.path().extension() == L".bin")
        {
            auto filename { file_path.path().filename() };

            ::LVITEMW lvi {};
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = item_index;
            lvi.pszText = LPSTR_TEXTCALLBACK;
            lvi.lParam
                = reinterpret_cast<LPARAM>(new log_info { file_path, get_start_timestamp(file_path.path().c_str()) });

            ListView_InsertItem(list_view, &lvi);

            ++item_index;
        }
    }
}


enum struct sort_order
{
    none,
    asc,
    desc,

    last_value = desc
};

int get_header_col_count(HWND const header)
{
    auto const col_count { Header_GetItemCount(header) };
    if (col_count < 0)
    {
        assert(!"Unexpected condition. Cannot call GetLastError() to find out what went wrong");
        THROW_IF_FAILED(E_FAIL);
    }
    return col_count;
}


// Find previously selected sort column (if any)
::std::optional<::std::pair<::packet_col, ::sort_order>> get_current_sorting(HWND const header)
{
    auto const col_count { ::get_header_col_count(header) };

    for (auto index { 0 }; index < col_count; ++index)
    {
        HDITEMW col_item {};
        col_item.mask = HDI_FORMAT;
        Header_GetItem(header, index, &col_item);
        if (col_item.fmt & (HDF_SORTDOWN | HDF_SORTUP))
        {
            return ::std::pair { static_cast<packet_col>(index),
                                 col_item.fmt & HDF_SORTDOWN ? sort_order::desc : sort_order::asc };
        }
    }

    // No column sorting currently active
    return {};
}


// Set sorting indicator on header control. Defaults to removing all visual cues.
static void set_header_sorting(HWND const header, size_t const col_index = 0,
                               ::sort_order const order = ::sort_order::none)
{
    // Parameter validation
    ::SetLastError(ERROR_INVALID_PARAMETER);
    THROW_LAST_ERROR_IF_NULL(header);

    auto const col_count { ::get_header_col_count(header) };
    ::SetLastError(ERROR_INVALID_PARAMETER);
    THROW_LAST_ERROR_IF(col_count <= col_index);

    for (auto current_index { 0 }; current_index < col_count; ++current_index)
    {
        // Unconditionally remove sorting
        HDITEMW col_item {};
        col_item.mask = HDI_FORMAT;
        Header_GetItem(header, current_index, &col_item);
        col_item.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);

        // Adjust sorting on requested column
        if (current_index == col_index)
        {
            switch (order)
            {
            case ::sort_order::asc:
                col_item.fmt |= HDF_SORTUP;
                break;

            case ::sort_order::desc:
                col_item.fmt |= HDF_SORTDOWN;
                break;

            case ::sort_order::none:
            default:
                break;
            }
        }

        // Finally set the column format back
        Header_SetItem(header, current_index, &col_item);
    }
}


// This function returns `true`, if it handled the sort, `false` otherwise.
// This is for convenience, so the LVN_* notification handler can simply return this function's return value.
static bool handle_sorting(HWND const header, size_t const col_index)
{
    if (static_cast<packet_col const>(col_index) >= packet_col::payload)
    {
        // Do not sort by payload column
        return false;
    }

    // Calculate new sorting criteria.
    // * If no sorting is active, choose 'asc' on the requested col_index.
    // * If sorting is active, but col_index != current col_index, choose 'asc' on the requested col_index.
    // * Otherwise advance sorting on col_index (none -> asc -> desc -> none).
    auto const current_sorting { ::get_current_sorting(header) };
    auto next_sorting_order { ::sort_order::none };
    if (!current_sorting || current_sorting->first != static_cast<::packet_col>(col_index))
    {
        next_sorting_order = ::sort_order::asc;
    }
    else
    {
        auto next_order_as_int { static_cast<int>(current_sorting->second) + 1 };
        next_sorting_order = static_cast<::sort_order>(next_order_as_int);
        if (next_sorting_order > ::sort_order::last_value)
        {
            next_sorting_order = ::sort_order::none;
        }
    }

    // Sanity checks
    assert(next_sorting_order == ::sort_order::asc || next_sorting_order == ::sort_order::desc
           || next_sorting_order == ::sort_order::none);
    assert(col_index != static_cast<int>(::packet_col::payload));

    // Update visual cues on the header control
    ::set_header_sorting(header, col_index, next_sorting_order);

    // Sort the collection
    if (next_sorting_order == ::sort_order::none)
    {
        g_spModel->sort();
    }
    else
    {
        // Map UI sorting criteria onto model's sorting criteria; this is a bit clunky, may have to revisit this at some
        // point.
        auto const pred { static_cast<::sort_predicate>(col_index) };
        auto const order { next_sorting_order == sort_order::asc ? ::sort_direction::asc : ::sort_direction::desc };
        g_spModel->sort(pred, order);
    }

    return true;
}


static RECT window_rect_in_client_coords(HWND const dlg_handle, HWND const control_handle)
{
    assert(control_handle != nullptr);
    RECT window_rect {};
    THROW_IF_WIN32_BOOL_FALSE(::GetWindowRect(control_handle, &window_rect));
    THROW_IF_WIN32_BOOL_FALSE(::ScreenToClient(dlg_handle, reinterpret_cast<POINT*>(&window_rect)));
    THROW_IF_WIN32_BOOL_FALSE(::ScreenToClient(dlg_handle, reinterpret_cast<POINT*>(&window_rect) + 1));
    return window_rect;
}


static RECT window_rect_in_client_coords(HWND const dlg_handle, int const control_id)
{
    return window_rect_in_client_coords(dlg_handle, ::GetDlgItem(dlg_handle, control_id));
}


static void arrange_controls(HWND dlg_handle, int32_t width_client, int32_t height_client)
{
    // The spacing/margin information is taken from
    // [Layout](https://docs.microsoft.com/en-us/windows/win32/uxguide/vis-layout).

    // Calculate outer dialog margins (7 DLU's on either side)
    RECT rect_tmp { .left = 7, .top = 7, .right = 7, .bottom = 7 };
    THROW_IF_WIN32_BOOL_FALSE(::MapDialogRect(dlg_handle, &rect_tmp));
    auto const margin_x { rect_tmp.right };
    auto const margin_y { rect_tmp.bottom };
    // Calculate vertical spacing between related controls (e.g. label and listbox)
    rect_tmp = RECT { .top = 3 };
    THROW_IF_WIN32_BOOL_FALSE(::MapDialogRect(dlg_handle, &rect_tmp));
    auto const space_y_related { rect_tmp.top };
    // Calculate vertical spacing between unrelated controls
    rect_tmp = RECT { .top = 7 };
    THROW_IF_WIN32_BOOL_FALSE(::MapDialogRect(dlg_handle, &rect_tmp));
    auto const space_y_unrelated { rect_tmp.top };

    // Position sensor log list label
    // Top: margin_y
    // Left: margin_x
    // Width: from resource script
    // Height: from resource script
    RECT rc_logs_label { ::window_rect_in_client_coords(dlg_handle, g_label_logs_handle) };
    ::OffsetRect(&rc_logs_label, margin_x - rc_logs_label.left, margin_y - rc_logs_label.top);
    ::SetWindowPos(g_label_logs_handle, nullptr, rc_logs_label.left, rc_logs_label.top, 0, 0,
                   SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position sensor log list
    // Top: Label's bottom + space_y_related
    // Left: margin_x
    // Width: constant
    // Height: constant
    static constexpr auto const log_list_width { 580 };
    static constexpr auto const log_list_height { 320 };
    RECT rc_logs { .left = 0, .top = 0, .right = log_list_width, .bottom = log_list_height };
    ::OffsetRect(&rc_logs, rc_logs_label.left, rc_logs_label.bottom + space_y_related);
    ::SetWindowPos(g_lv_logs_handle, nullptr, rc_logs.left, rc_logs.top, ::width(rc_logs), ::height(rc_logs),
                   SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position "Load" button
    auto rc_load { ::window_rect_in_client_coords(dlg_handle, g_button_load_handle) };
    ::OffsetRect(&rc_load, rc_logs.right + margin_y - rc_load.left, rc_logs.bottom - rc_load.bottom);
    ::SetWindowPos(g_button_load_handle, nullptr, rc_load.left, rc_load.top, ::width(rc_load), ::height(rc_load),
                   SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position packet list
    // Top: log_list's bottom + space_y_unrelated
    // Left: margin_x
    // Width: full client width (minus 2 * margin_x)
    // Height: all the way down to the bottom (minus margin_y)
    RECT rc_packets { .left = margin_x,
                      .top = rc_logs.bottom + space_y_unrelated,
                      .right = width_client - margin_x,
                      .bottom = height_client - margin_y - k_diagram_height - space_y_related };
    ::SetWindowPos(g_lv_packets_handle, nullptr, rc_packets.left, rc_packets.top, ::width(rc_packets),
                   ::height(rc_packets), SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Update rectangle for diagram area
    g_rc_diagram = RECT { .left = rc_packets.left,
                          .top = rc_packets.bottom + space_y_related,
                          .right = rc_packets.right,
                          .bottom = height_client - margin_y };
}


static void set_log_list_column_widths(HWND const log_list)
{
    auto const col_widths { ::std::array { ::std::make_pair(0, LVSCW_AUTOSIZE),
                                           ::std::make_pair(1, LVSCW_AUTOSIZE_USEHEADER) } };
    for (auto const [index, width] : col_widths)
    {
        ListView_SetColumnWidth(log_list, index, width);
    }
}


static void set_packets_list_column_widths(HWND const packets_list)
{
    auto const col_widths { ::std::array { ::std::make_pair(packet_col::type, LVSCW_AUTOSIZE_USEHEADER),
                                           ::std::make_pair(packet_col::size, LVSCW_AUTOSIZE_USEHEADER),
                                           ::std::make_pair(packet_col::payload, LVSCW_AUTOSIZE_USEHEADER),
                                           ::std::make_pair(packet_col::details, LVSCW_AUTOSIZE_USEHEADER) } };
    for (auto const [index, width] : col_widths)
    {
        ListView_SetColumnWidth(packets_list, index, width);
    }
}


template <typename T>
void render_graph(HDC const hdc, RECT const& rect, COLORREF const color, model const& m,
                  unsigned char const packet_type, size_t const offset) noexcept
{
    ::std::vector<size_t> indexes {};
    for (size_t i { 0 }; i < m.packet_count(); ++i)
    {
        if (m.packet(i).type() == packet_type)
        {
            indexes.push_back(i);
        }
    }

    if (indexes.size() > 0)
    {
        // Find min/max values
        auto [min_it, max_it] { ::std::minmax_element(begin(indexes), end(indexes),
                                                      [& m = m, offset](auto const lhs, auto const rhs) {
                                                          auto const val_lhs { m.packet(lhs).value<T>(offset) };
                                                          auto const val_rhs { m.packet(rhs).value<T>(offset) };

                                                          return val_lhs < val_rhs;
                                                      }) };
        auto const min_val { m.packet(*min_it).value<T>(offset) };
        auto const max_val { m.packet(*max_it).value<T>(offset) };

        // Render graph
        auto const w { ::width(rect) - 2 };
        auto const h { ::height(rect) - 2 };
        auto const value_range { max_val - min_val };

        if (value_range != 0)
        {
            auto x { rect.left + 1 + ::MulDiv(static_cast<int>(indexes[0]), w, static_cast<int>(m.packet_count())) };
            auto val { m.packet(indexes[0]).value<T>(offset) };
            auto y { rect.bottom - 1 - ::MulDiv(val - min_val, h, value_range) };
            ::MoveToEx(hdc, x, y, nullptr);

            auto const pen { ::CreatePen(PS_SOLID, 0, color) };
            auto const prev_pen { SelectPen(hdc, pen) };

            for (size_t i { 1 }; i < indexes.size(); ++i)
            {
                x = rect.left + 1 + ::MulDiv(static_cast<int>(indexes[i]), w, static_cast<int>(m.packet_count()));
                val = m.packet(indexes[i]).value<T>(offset);
                y = rect.bottom - 1 - ::MulDiv(val - min_val, h, value_range);
                ::LineTo(hdc, x, y);
            }

            SelectPen(hdc, prev_pen);
            ::DeleteObject(pen);
        }
    }
}


// TEMP --- VVV
// My local sensor log capture store; needs to be replaced with a folder selection dialog
auto const& log_location {
    L"C:\\Users\\Tim\\SourceCode\\VSTS\\Git\\WindowsUniversal\\recommissioned\\doc\\SensorLogCaptures\\"
};
// TEMP --- AAA


#pragma region Message handlers

BOOL OnInitDialog(HWND hwnd, HWND /*hwndFocus*/, LPARAM /*lParam*/)
{
    // Store window handles in globals for convenience
    g_main_dlg_handle = hwnd;
    assert(g_main_dlg_handle != nullptr);
    g_label_logs_handle = ::GetDlgItem(hwnd, IDC_STATIC_SENSOR_LOG_LIST_LABEL);
    assert(g_label_logs_handle != nullptr);
    g_lv_logs_handle = ::GetDlgItem(hwnd, IDC_LISTVIEW_LOGS);
    assert(g_lv_logs_handle != nullptr);
    g_button_load_handle = ::GetDlgItem(hwnd, IDC_BUTTON_LOAD_LOG);
    assert(g_button_load_handle != nullptr);
    g_lv_packets_handle = ::GetDlgItem(hwnd, IDC_LISTVIEW_PACKET_LIST);
    assert(g_lv_packets_handle != nullptr);

    // TODO: Query user for log folder
    populate_log_list(log_location, g_lv_logs_handle);

    // Set column(s) for logs list view
    LV_COLUMNW lvc {};
    lvc.mask = LVCF_TEXT;
    lvc.pszText = const_cast<wchar_t*>(L"Filename");
    ListView_InsertColumn(g_lv_logs_handle, 0, &lvc);
    ListView_SetColumnWidth(g_lv_logs_handle, 0, LVSCW_AUTOSIZE);

    lvc.pszText = const_cast<wchar_t*>(L"Timestamp");
    ListView_InsertColumn(g_lv_logs_handle, 1, &lvc);
    ListView_SetColumnWidth(g_lv_logs_handle, 1, LVSCW_AUTOSIZE_USEHEADER);

    ListView_SetExtendedListViewStyle(g_lv_logs_handle, LVS_EX_FULLROWSELECT);


    // Set column(s) for the packet list view
    LV_COLUMNW col {};
    col.mask = LVCF_WIDTH | LVCF_TEXT;
    col.cx = 70;
    col.pszText = const_cast<wchar_t*>(L"Index");
    ListView_InsertColumn(g_lv_packets_handle, packet_col::index, &col);

    col.mask &= ~LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Type");
    ListView_InsertColumn(g_lv_packets_handle, packet_col::type, &col);

    col.mask |= LVCF_FMT;
    col.fmt = LVCFMT_RIGHT;
    col.pszText = const_cast<wchar_t*>(L"Size");
    ListView_InsertColumn(g_lv_packets_handle, packet_col::size, &col);
    col.mask &= ~LVCF_FMT;

    col.pszText = const_cast<wchar_t*>(L"Payload");
    ListView_InsertColumn(g_lv_packets_handle, packet_col::payload, &col);
    ListView_SetColumnWidth(g_lv_packets_handle, packet_col::payload, LVSCW_AUTOSIZE_USEHEADER);

    col.pszText = const_cast<wchar_t*>(L"Details");
    ListView_InsertColumn(g_lv_packets_handle, packet_col::details, &col);
    ListView_SetColumnWidth(g_lv_packets_handle, packet_col::details, LVSCW_AUTOSIZE_USEHEADER);

    // Make listview "full-row select"
    ListView_SetExtendedListViewStyle(g_lv_packets_handle, LVS_EX_FULLROWSELECT);

    // TEMP --- VVV --- Add a filter bar to the list view header
    // TODO: Find out how to apply visual styles to filter bar (it does render, but looks funky).
    // auto lv_header { ListView_GetHeader(lv_packets) };
    // auto header_style { ::GetWindowLongPtrW(lv_header, GWL_STYLE) };
    //::SetWindowLongPtrW(lv_header, GWL_STYLE, header_style | HDS_FILTERBAR);
    // TEMP --- AAA

    return TRUE;
}


static void OnSize(HWND const hwnd, UINT const /*state*/, int const cx, int const cy)
{
    arrange_controls(hwnd, cx, cy);
    // Resize list view columns. Needs to be done in a WM_SIZE handler, otherwise the widths are based on
    // the outdated control width.
    set_packets_list_column_widths(g_lv_packets_handle);
    set_log_list_column_widths(g_lv_logs_handle);
}


static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps {};
    auto const hdc { ::BeginPaint(hwnd, &ps) };

    auto prev_pen { SelectPen(hdc, static_cast<HPEN const>(::GetStockObject(BLACK_PEN))) };
    auto prev_brush { SelectBrush(hdc, static_cast<HBRUSH const>(::GetStockObject(WHITE_BRUSH))) };

    ::Rectangle(hdc, g_rc_diagram.left, g_rc_diagram.top, g_rc_diagram.right, g_rc_diagram.bottom);

    SelectBrush(hdc, prev_brush);
    SelectPen(hdc, prev_pen);

    if (g_spModel)
    {
        // Unknown data
        render_graph<uint16_t>(hdc, g_rc_diagram, RGB(0, 162, 232), *g_spModel, 0x42, 0);
        // Heart rate
        render_graph<uint8_t>(hdc, g_rc_diagram, RGB(210, 0, 0), *g_spModel, 0x80, 0);
        render_graph<uint8_t>(hdc, g_rc_diagram, RGB(252, 209, 211), *g_spModel, 0x80, 1);
        // Unknown data (apparently some cumulative sum)
        render_graph<uint32_t>(hdc, g_rc_diagram, RGB(0, 220, 0), *g_spModel, 0x81, 0);
    }

    ::EndPaint(hwnd, &ps);
}


static void OnCommand(HWND /*hwnd*/, int id, HWND /*hwndCtl*/, UINT codeNotify)
{
    switch (id)
    {
    case IDC_BUTTON_LOAD_LOG:
        switch (codeNotify)
        {
        case BN_CLICKED: {
            auto const sel_index { ListView_GetNextItem(g_lv_logs_handle, -1, LVNI_SELECTED) };
            if (sel_index >= 0)
            {
                LVITEMW lvi {};
                lvi.mask = LVIF_PARAM;
                lvi.iItem = sel_index;
                ListView_GetItem(g_lv_logs_handle, &lvi);
                auto const& info { *reinterpret_cast<log_info const*>(lvi.lParam) };

                g_spModel.reset(new model(info.file_path.path().c_str()));
                auto const packet_count { g_spModel->packet_count() };

                // Reset sorting indicators
                ::set_header_sorting(ListView_GetHeader(g_lv_packets_handle));
                // Set virtual list view size
                ::SendMessageW(g_lv_packets_handle, LVM_SETITEMCOUNT, static_cast<WPARAM>(packet_count), 0x0);
                // Adjust packets list column widths. If we don't do this after setting the items count, a
                // potentially appearing vertical scrollbar will not be accounted for.
                set_packets_list_column_widths(g_lv_packets_handle);

                // Redraw diagram
                ::InvalidateRect(g_main_dlg_handle, &g_rc_diagram, FALSE);
            }
            break;
        }

        default:
            break;
        }
    }
}


static void OnLButtonDown(HWND /*hwnd*/, BOOL /*fDoubleClick*/, int x, int y, UINT /*keyFlags*/)
{
    POINT const pt { x, y };
    if (::PtInRect(&g_rc_diagram, pt) && g_spModel != nullptr)
    {
        list_view_clear_selection(g_lv_packets_handle);

        // Select item
        auto const x_offset { x - g_rc_diagram.left };
        auto const index { ::MulDiv(x_offset, static_cast<int>(g_spModel->packet_count()), ::width(g_rc_diagram)) };
        ListView_SetItemState(g_lv_packets_handle, index, LVIS_SELECTED, LVIS_SELECTED);

        // Make sure it's in view
        ListView_EnsureVisible(g_lv_packets_handle, index, FALSE);
    }
}


static void OnGetMinMaxInfo(HWND /*hwnd*/, LPMINMAXINFO lpMinMaxInfo)
{
    lpMinMaxInfo->ptMinTrackSize = POINT { 1200, 800 };
}


static void OnClose(HWND hwnd) { EndDialog(hwnd, 0); }


#pragma endregion


INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return HANDLE_WM_INITDIALOG(hwndDlg, wParam, lParam, &::OnInitDialog);

    case WM_SIZE:
        HANDLE_WM_SIZE(hwndDlg, wParam, lParam, &::OnSize);
        return TRUE;

    case WM_PAINT:
        HANDLE_WM_PAINT(hwndDlg, wParam, lParam, &::OnPaint);
        return TRUE;

    case WM_COMMAND:
        HANDLE_WM_COMMAND(hwndDlg, wParam, lParam, &::OnCommand);
        return TRUE;

    case WM_LBUTTONDOWN:
        HANDLE_WM_LBUTTONDOWN(hwndDlg, wParam, lParam, &::OnLButtonDown);
        return TRUE;

    case WM_GETMINMAXINFO:
        HANDLE_WM_GETMINMAXINFO(hwndDlg, wParam, lParam, &::OnGetMinMaxInfo);
        return TRUE;

    case WM_CLOSE:
        HANDLE_WM_CLOSE(hwndDlg, wParam, lParam, &::OnClose);
        return TRUE;

    case WM_NOTIFY: {
        auto const& nmhdr { *reinterpret_cast<NMHDR const*>(lParam) };
#pragma warning(suppress : 26454) // Disable C26454 warning for LVN_GETDISPINFOW
        if (nmhdr.idFrom == IDC_LISTVIEW_PACKET_LIST && nmhdr.code == LVN_GETDISPINFOW)
        {
            auto& nmlvdi { *reinterpret_cast<NMLVDISPINFOW*>(lParam) };
            if (nmlvdi.item.mask & LVIF_TEXT)
            {
                auto const item_index { nmlvdi.item.iItem };

                auto col_index { static_cast<packet_col>(nmlvdi.item.iSubItem) };
                switch (col_index)
                {
                case packet_col::index: {
                    auto const mapped_index { g_spModel->packet_index(item_index) };
                    auto const index_str { ::std::to_wstring(mapped_index + 1) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, index_str.c_str(), index_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::type: {
                    auto const type_str { ::to_hex_string(g_spModel->packet(item_index).type()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, type_str.c_str(), type_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::size: {
                    auto const size_str { ::std::to_wstring(g_spModel->packet(item_index).payload_size()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, size_str.c_str(), size_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::payload: {
                    auto payload_str { ::to_hex_string(g_spModel->packet(item_index).data() + 2,
                                                       g_spModel->packet(item_index).payload_size()) };
                    // Truncate payload if it exceeds available space.
                    if (payload_str.size() >= nmlvdi.item.cchTextMax)
                    {
                        payload_str
                            = payload_str.substr(0, static_cast<size_t>(nmlvdi.item.cchTextMax) - 1 - 4) + L" ...";
                    }
                    assert(payload_str.size() < nmlvdi.item.cchTextMax);
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, payload_str.c_str(), payload_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::details: {
                    auto const details { ::details_from_packet(g_spModel->packet(item_index),
                                                               g_spModel->packet_descriptions()) };
                    auto details_str { details.value_or(L"n/a") };
                    // Truncate payload if it exceeds available space.
                    if (details_str.size() >= nmlvdi.item.cchTextMax)
                    {
                        details_str
                            = details_str.substr(0, static_cast<size_t>(nmlvdi.item.cchTextMax) - 1 - 4) + L" ...";
                    }
                    assert(details_str.size() < nmlvdi.item.cchTextMax);
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, details_str.c_str(), details_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                default:
                    break;
                }
                return TRUE;
            }
        }

        // Handle column click to toggle sorting
#pragma warning(suppress : 26454)
        if (nmhdr.idFrom == IDC_LISTVIEW_PACKET_LIST && nmhdr.code == LVN_COLUMNCLICK)
        {
            auto const& msg_info { *reinterpret_cast<NMLISTVIEW const*>(lParam) };
            auto header_handle { ListView_GetHeader(nmhdr.hwndFrom) };

            if (handle_sorting(header_handle, msg_info.iSubItem))
            {
                auto const lv_packets { nmhdr.hwndFrom };
                // Refresh list view
                ListView_RedrawItems(lv_packets, 0, static_cast<size_t>(ListView_GetItemCount(lv_packets)) - 1);
                return TRUE;
            }
        }

#pragma warning(suppress : 26454)
        if (nmhdr.idFrom == IDC_LISTVIEW_LOGS && nmhdr.code == LVN_GETDISPINFOW)
        {
            auto& nmlvdi { *reinterpret_cast<NMLVDISPINFOW*>(lParam) };
            if (nmlvdi.item.mask & LVIF_TEXT)
            {
                LVITEMW lvi {};
                lvi.mask = LVIF_PARAM;
                lvi.iItem = nmlvdi.item.iItem;
                ListView_GetItem(nmhdr.hwndFrom, &lvi);
                auto const& info { *reinterpret_cast<log_info const*>(lvi.lParam) };

                switch (nmlvdi.item.iSubItem)
                {
                case 0: {
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, info.file_path.path().filename().c_str(),
                                ::wcslen(info.file_path.path().filename().c_str()));
                    // Don't ask again
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                    break;
                }

                case 1: {
                    auto const text { format_timestamp(info.timestamp) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, text.c_str(), text.length());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                    break;
                }

                default:
                    assert(!"Unexpected column; update code to account for additional column.");
                    break;
                }


                return TRUE;
            }
        }


        return FALSE;
    }

    default:
        break;
    }

    return FALSE;
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPWSTR /*lpCmdLine*/,
                      _In_ int /*nCmdShow*/)
{
    INITCOMMONCONTROLSEX icc { static_cast<DWORD>(sizeof(icc)),
                               ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_DATE_CLASSES };
    THROW_IF_WIN32_BOOL_FALSE(::InitCommonControlsEx(&icc));

    auto const result { ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, &::MainDlgProc, 0l) };

    return static_cast<int>(result);
}
