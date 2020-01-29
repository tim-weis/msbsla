#include "msbsla.h"

#include "framework.h"

#include "display_utils.h"
#include "log_utils.h"
#include "model.h"
#include "utils.h"

#include <wil/resource.h>

#include <CommCtrl.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <wchar.h>


namespace fs = std::filesystem;


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

// Local data
static HWND g_hDlg { nullptr };
static HWND g_hLBLogList { nullptr };

::std::unique_ptr<model> g_spModel { nullptr };


// Local functions
static void clear_log_list(HWND list_box)
{
    intptr_t const count { ::SendMessageW(list_box, LB_GETCOUNT, 0, 0) };
    // Delete associated data
    for (intptr_t index { 0 }; index < count; ++index)
    {
        auto p { reinterpret_cast<fs::directory_entry*>(::SendMessageW(list_box, LB_GETITEMDATA, index, 0)) };
        delete p;
    }
    // Remove all content
    ::SendMessageW(list_box, LB_RESETCONTENT, 0, 0);
}

static void populate_log_list(wchar_t const* log_dir, HWND list_box)
{
    auto it { fs::directory_iterator(log_dir) };
    for (auto file_path : it)
    {
        if (file_path.path().extension() == L".bin")
        {
            intptr_t index { ::SendMessageW(list_box, LB_ADDSTRING, 0,
                                            reinterpret_cast<LPARAM>(file_path.path().filename().c_str())) };
            if (index >= 0)
            {
                auto p { new fs::directory_entry { file_path } };
                ::SendMessageW(list_box, LB_SETITEMDATA, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(p));
            }
        }
    }
}


static void update_sel_log_timestamp(HWND hDlg)
{
    auto h_timestamp { ::GetDlgItem(hDlg, IDC_STATIC_TIMESTAMP) };
    intptr_t sel_index { ::SendMessageW(::GetDlgItem(hDlg, IDC_LB_LOGS), LB_GETCURSEL, 0, 0) };
    if (sel_index >= 0)
    {
        auto entry { reinterpret_cast<fs::directory_entry const*>(
            ::SendMessageW(::GetDlgItem(hDlg, IDC_LB_LOGS), LB_GETITEMDATA, sel_index, 0)) };
        auto const timestamp { get_start_timestamp(entry->path().c_str()) };
        auto const formatted_timestamp { ::format_timestamp(timestamp) };
        ::SetWindowTextW(h_timestamp, formatted_timestamp.c_str());
    }
    else
    {
        // No item selected
        ::SetWindowTextW(h_timestamp, L"");
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
        g_spModel->data().sort();
    }
    else
    {
        // Map UI sorting criteria onto model's sorting criteria; this is a bit clunky, may have to revisit this at some
        // point.
        auto const pred { static_cast<::sort_predicate>(col_index) };
        auto const order { next_sorting_order == sort_order::asc ? ::sort_direction::asc : ::sort_direction::desc };
        g_spModel->data().sort(pred, order);
    }

    return true;
}


static RECT window_rect_in_client_coords(HWND dlg_handle, int control_id)
{
    auto const control_handle { ::GetDlgItem(dlg_handle, control_id) };
    assert(control_handle != nullptr);
    RECT window_rect {};
    THROW_IF_WIN32_BOOL_FALSE(::GetWindowRect(control_handle, &window_rect));
    THROW_IF_WIN32_BOOL_FALSE(::ScreenToClient(dlg_handle, reinterpret_cast<POINT*>(&window_rect)));
    THROW_IF_WIN32_BOOL_FALSE(::ScreenToClient(dlg_handle, reinterpret_cast<POINT*>(&window_rect) + 1));
    return window_rect;
}


static void arrange_controls(HWND dlg_handle, int32_t width_client, int32_t height_client)
{
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
    RECT rc_logs_label { ::window_rect_in_client_coords(dlg_handle, IDC_STATIC_SENSOR_LOG_LIST_LABEL) };
    ::OffsetRect(&rc_logs_label, margin_x - rc_logs_label.left, margin_y - rc_logs_label.top);
    auto const logs_label_handle { ::GetDlgItem(dlg_handle, IDC_STATIC_SENSOR_LOG_LIST_LABEL) };
    ::SetWindowPos(logs_label_handle, nullptr, rc_logs_label.left, rc_logs_label.top, 0, 0,
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
    auto const logs_handle { ::GetDlgItem(dlg_handle, IDC_LB_LOGS) };
    ::SetWindowPos(logs_handle, nullptr, rc_logs.left, rc_logs.top, ::width(rc_logs), ::height(rc_logs),
                   SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position sensor log information label
    auto rc_log_ts_label { ::window_rect_in_client_coords(dlg_handle, IDC_STATIC_SENSOR_LOG_TIMESTAMP_LABEL) };
    ::OffsetRect(&rc_log_ts_label, rc_logs.right + margin_y - rc_log_ts_label.left, rc_logs.top - rc_log_ts_label.top);
    auto const log_ts_label_handle { ::GetDlgItem(dlg_handle, IDC_STATIC_SENSOR_LOG_TIMESTAMP_LABEL) };
    ::SetWindowPos(log_ts_label_handle, nullptr, rc_log_ts_label.left, rc_log_ts_label.top, ::width(rc_log_ts_label),
                   ::height(rc_log_ts_label), SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position sensor log timestamp
    auto rc_log_ts { ::window_rect_in_client_coords(dlg_handle, IDC_STATIC_TIMESTAMP) };
    ::OffsetRect(&rc_log_ts, rc_log_ts_label.left - rc_log_ts.left,
                 rc_log_ts_label.bottom + space_y_related - rc_log_ts.top);
    auto const log_ts_handle { ::GetDlgItem(dlg_handle, IDC_STATIC_TIMESTAMP) };
    ::SetWindowPos(log_ts_handle, nullptr, rc_log_ts.left, rc_log_ts.top, ::width(rc_log_ts), ::height(rc_log_ts),
                   SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position "Load" button
    auto rc_load { ::window_rect_in_client_coords(dlg_handle, IDC_BUTTON_LOAD_LOG) };
    ::OffsetRect(&rc_load, rc_log_ts_label.left - rc_load.left, rc_logs.bottom - rc_load.bottom);
    auto const load_handle { ::GetDlgItem(dlg_handle, IDC_BUTTON_LOAD_LOG) };
    ::SetWindowPos(load_handle, nullptr, rc_load.left, rc_load.top, ::width(rc_load), ::height(rc_load),
                   SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    // Position packet list
    // Top: log_list's bottom + space_y_unrelated
    // Left: margin_x
    // Width: full client width (minus 2 * margin_x)
    // Height: all the way down to the bottom (minus margin_y)
    RECT rc_packets { .left = margin_x,
                      .top = rc_logs.bottom + space_y_unrelated,
                      .right = width_client - margin_x,
                      .bottom = height_client - margin_y };
    ::SetWindowPos(::GetDlgItem(dlg_handle, IDC_LISTVIEW_PACKET_LIST), nullptr, rc_packets.left, rc_packets.top,
                   ::width(rc_packets), ::height(rc_packets), SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
}


// TEMP --- VVV
// My local sensor log capture store; needs to be replaced with a folder selection dialog
auto const& log_location {
    L"C:\\Users\\Tim\\SourceCode\\VSTS\\Git\\WindowsUniversal\\recommissioned\\doc\\SensorLogCaptures\\"
};
// TEMP --- AAA


INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG: {
        // Store window handles in globals for convenience
        g_hDlg = hwndDlg;
        g_hLBLogList = ::GetDlgItem(hwndDlg, IDC_LB_LOGS);
        // TODO: Query user for log folder
        populate_log_list(log_location, ::GetDlgItem(hwndDlg, IDC_LB_LOGS));

        // Set column(s) for the packet list view
        auto lv_packets { ::GetDlgItem(hwndDlg, IDC_LISTVIEW_PACKET_LIST) };
        LV_COLUMNW col {};
        col.mask = LVCF_WIDTH | LVCF_TEXT;
        col.cx = 70;
        col.pszText = const_cast<wchar_t*>(L"Index");
        ListView_InsertColumn(lv_packets, packet_col::index, &col);

        col.mask &= ~LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(L"Type");
        ListView_InsertColumn(lv_packets, packet_col::type, &col);

        col.mask |= LVCF_FMT;
        col.fmt = LVCFMT_RIGHT;
        col.pszText = const_cast<wchar_t*>(L"Size");
        ListView_InsertColumn(lv_packets, packet_col::size, &col);
        col.mask &= ~LVCF_FMT;

        col.pszText = const_cast<wchar_t*>(L"Payload");
        ListView_InsertColumn(lv_packets, packet_col::payload, &col);
        ListView_SetColumnWidth(lv_packets, packet_col::payload, LVSCW_AUTOSIZE_USEHEADER);

        col.pszText = const_cast<wchar_t*>(L"Details");
        ListView_InsertColumn(lv_packets, packet_col::details, &col);
        ListView_SetColumnWidth(lv_packets, packet_col::details, LVSCW_AUTOSIZE_USEHEADER);

        // Make listview "full-row select"
        ListView_SetExtendedListViewStyle(lv_packets, LVS_EX_FULLROWSELECT);

        // TEMP --- VVV --- Add a filter bar to the list view header
        // TODO: Find out how to apply visual styles to filter bar (it does render, but looks funky).
        // auto lv_header { ListView_GetHeader(lv_packets) };
        // auto header_style { ::GetWindowLongPtrW(lv_header, GWL_STYLE) };
        //::SetWindowLongPtrW(lv_header, GWL_STYLE, header_style | HDS_FILTERBAR);
        // TEMP --- AAA

        return TRUE;
    }

    case WM_SIZE: {
        uint32_t const width_client { LOWORD(lParam) };
        uint32_t const height_client { HIWORD(lParam) };
        arrange_controls(hwndDlg, width_client, height_client);
        // Resize list view columns. Needs to be done in a WM_SIZE handler, otherwise the widths are based on the
        // outdated control width.
        auto const lv_packets { ::GetDlgItem(hwndDlg, IDC_LISTVIEW_PACKET_LIST) };
        auto const col_widths { ::std::array { ::std::make_pair(packet_col::type, LVSCW_AUTOSIZE_USEHEADER),
                                               ::std::make_pair(packet_col::size, LVSCW_AUTOSIZE_USEHEADER),
                                               ::std::make_pair(packet_col::payload, LVSCW_AUTOSIZE_USEHEADER),
                                               ::std::make_pair(packet_col::details, LVSCW_AUTOSIZE_USEHEADER) } };
        for (auto const [index, width] : col_widths)
        {
            ListView_SetColumnWidth(lv_packets, index, width);
        }

        return TRUE;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam))
        {
        case IDC_LB_LOGS: {
            switch (HIWORD(wParam))
            {
            case LBN_SELCHANGE: {
                ::update_sel_log_timestamp(g_hDlg);
                return TRUE;
            }

            default:
                break;
            }
        }

        case IDC_BUTTON_LOAD_LOG: {
            switch (HIWORD(wParam))
            {
            case BN_CLICKED: {
                auto const lb_logs { ::GetDlgItem(hwndDlg, IDC_LB_LOGS) };
                auto const sel_index { static_cast<intptr_t>(::SendMessageW(lb_logs, LB_GETCURSEL, 0, 0)) };
                if (sel_index >= 0)
                {
                    auto const& dir_entry { *reinterpret_cast<fs::directory_entry const*>(
                        ::SendMessageW(lb_logs, LB_GETITEMDATA, sel_index, 0)) };

                    g_spModel.reset(new model(dir_entry.path().c_str()));
                    auto const packet_count { g_spModel->data().directory().size() };

                    auto const lv_packets { ::GetDlgItem(hwndDlg, IDC_LISTVIEW_PACKET_LIST) };
                    // Reset sorting indicators
                    ::set_header_sorting(ListView_GetHeader(lv_packets));
                    // Set virtual list view size
                    ::SendMessageW(lv_packets, LVM_SETITEMCOUNT, static_cast<WPARAM>(packet_count), 0x0);
                }
                return TRUE;
            }

            default:
                break;
            }
        }

        default:
            break;
        }
        break;
    }

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
                    auto const mapped_index { g_spModel->data().sort_map()[item_index] };
                    auto const index_str { ::std::to_wstring(mapped_index + 1) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, index_str.c_str(), index_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::type: {
                    auto const type_str { ::to_hex_string(g_spModel->data().packet(item_index).type()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, type_str.c_str(), type_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::size: {
                    auto const size_str { ::std::to_wstring(g_spModel->data().packet(item_index).size()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, size_str.c_str(), size_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::payload: {
                    auto payload_str { ::to_hex_string(g_spModel->data().packet(item_index).data() + 2,
                                                       g_spModel->data().packet(item_index).size()) };
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
                    auto const details { ::details_from_packet(g_spModel->data().packet(item_index),
                                                               g_spModel->known_types()) };
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

        return FALSE;
    }

    case WM_GETMINMAXINFO: {
        auto& info { *reinterpret_cast<MINMAXINFO*>(lParam) };
        info.ptMinTrackSize = POINT { 1200, 800 };
        return TRUE;
    }

    case WM_CLOSE: {
        EndDialog(hwndDlg, 0);
        return TRUE;
    }

    default:
        break;
    }

    return FALSE;
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPWSTR /*lpCmdLine*/,
                      _In_ int /*nCmdShow*/)
{
    INITCOMMONCONTROLSEX icc { static_cast<DWORD>(sizeof(icc)), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
    THROW_IF_WIN32_BOOL_FALSE(::InitCommonControlsEx(&icc));

    auto const result { ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, &::MainDlgProc, 0l) };

    return static_cast<int>(result);
}
