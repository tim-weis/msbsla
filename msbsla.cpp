#include "msbsla.h"

#include "framework.h"

#include "display_utils.h"
#include "log_utils.h"
#include "model.h"
#include "utils.h"

#include <wil/resource.h>

#include <CommCtrl.h>

#include <cassert>
#include <filesystem>
#include <memory>
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
    payload
};

// Local data
static HWND g_hDlg { nullptr };
static HWND g_hLBLogList { nullptr };

std::unique_ptr<raw_data> g_spModel { nullptr };


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
        ListView_InsertColumn(lv_packets, 0, &col);

        col.cx = 50;
        col.pszText = const_cast<wchar_t*>(L"Type");
        ListView_InsertColumn(lv_packets, packet_col::type, &col);

        col.cx = 50;
        col.pszText = const_cast<wchar_t*>(L"Size");
        ListView_InsertColumn(lv_packets, packet_col::size, &col);

        col.cx = 400;
        col.pszText = const_cast<wchar_t*>(L"Payload");
        ListView_InsertColumn(lv_packets, packet_col::payload, &col);

        return TRUE;
    }

    case WM_SIZE: {
        uint32_t const width_client { LOWORD(lParam) };
        uint32_t const height_client { HIWORD(lParam) };
        arrange_controls(hwndDlg, width_client, height_client);
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

                    g_spModel.reset(new raw_data(dir_entry.path().c_str()));
                    auto const packet_count { g_spModel->directory().size() };

                    // Set virtual list view size
                    auto const lv_packets { ::GetDlgItem(hwndDlg, IDC_LISTVIEW_PACKET_LIST) };
                    ::SendMessageW(lv_packets, LVM_SETITEMCOUNT, static_cast<WPARAM>(packet_count),
                                   0x0);
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
                    auto const index_str { ::std::to_wstring(item_index + 1) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, index_str.c_str(), index_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::type: {
                    auto const type_str { ::to_hex_string(g_spModel->directory()[item_index].type()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, type_str.c_str(), type_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::size: {
                    auto const size_str { ::std::to_wstring(g_spModel->directory()[item_index].size()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, size_str.c_str(), size_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                case packet_col::payload: {
                    auto const payload_str { ::to_hex_string(g_spModel->directory()[item_index].data() + 2,
                                                             g_spModel->directory()[item_index].size()) };
                    ::wcsncpy_s(nmlvdi.item.pszText, nmlvdi.item.cchTextMax, payload_str.c_str(), payload_str.size());
                    nmlvdi.item.mask |= LVIF_DI_SETITEM;
                }
                break;

                default:
                    break;
                }
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
    INITCOMMONCONTROLSEX icc { static_cast<DWORD>(sizeof(icc)), ICC_STANDARD_CLASSES };
    THROW_IF_WIN32_BOOL_FALSE(::InitCommonControlsEx(&icc));

    auto const result { ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, &::MainDlgProc, 0l) };

    return static_cast<int>(result);
}
