#include "msbsla.h"

#include "framework.h"

#include "display_utils.h"
#include "log_utils.h"
#include "model.h"

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
                                   LVSICF_NOINVALIDATEALL);
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
