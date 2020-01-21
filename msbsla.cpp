#include "msbsla.h"

#include "framework.h"

#include "log_utils.h"
#include "display_utils.h"

#include <wil/resource.h>

#include <CommCtrl.h>

#include <filesystem>


namespace fs = std::filesystem;


// Enable Visual Styles by embedding an appropriate manifest
#pragma comment(linker, "\"/manifestdependency:type='win32' \
                         name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
                         processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


// Local data
static HWND g_hDlg { nullptr };
static HWND g_hLBLogList { nullptr };


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
            // wil::unique_hfile f { ::CreateFileW(file_path.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            //                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
            // THROW_LAST_ERROR_IF(!f.is_valid());
            //// Skip first 2 bytes
            //::SetFilePointer(f.get(), 2, nullptr, FILE_BEGIN);

            // uint64_t timestamp {};
            // DWORD bytes_read {};
            //::ReadFile(f.get(), reinterpret_cast<char*>(&timestamp), sizeof(timestamp), &bytes_read, nullptr);
            // FILETIME const ft { .dwLowDateTime = static_cast<DWORD>(timestamp),
            //                    .dwHighDateTime = static_cast<DWORD>(timestamp >> 32) };
            // SYSTEMTIME st {};
            //::FileTimeToSystemTime(&ft, &st);

            // std::wcout << file_path.path().filename() << L":\n";
            // std::wcout << L"  Start: " << std::setfill(L'0') << std::setw(4) << st.wYear << L"-" <<
            // std::setfill(L'0')
            //           << std::setw(2) << st.wMonth << L"-" << std::setfill(L'0') << std::setw(2) << st.wDay << L" "
            //           << std::setfill(L'0') << std::setw(2) << st.wHour << L":" << std::setfill(L'0') << std::setw(2)
            //           << st.wMinute << L":" << std::setfill(L'0') << std::setw(2) << st.wSecond << std::endl;
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
                break;
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
