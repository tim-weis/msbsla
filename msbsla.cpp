#include "msbsla.h"

#include "framework.h"


// Enable Visual Styles by embedding an appropriate manifest
#pragma comment(linker, "\"/manifestdependency:type='win32' \
                         name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
                         processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND: {
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            EndDialog(hwndDlg, wParam);
            return TRUE;
        default:
            return FALSE;
        }
    }

    case WM_CLOSE: {
        EndDialog(hwndDlg, 0);
        return TRUE;
    }

    default:
        return FALSE;
    }
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPWSTR /*lpCmdLine*/,
                      _In_ int nCmdShow)
{
    auto result { ::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, &::MainDlgProc, 0l) };

    return result;
}
