// main.cpp - Application entry point
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include "MainWindow.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 使用 STA 线程模型，Shell API（如 SHBrowseForFolderW）需要 STA
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Initialize Common Controls
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // Register window class
    if (!RegisterMainWindowClass(hInstance)) {
        MessageBoxW(nullptr, L"无法注册窗口类", L"错误", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    // Create main window
    HWND hWnd = CreateMainWindow(hInstance);
    if (!hWnd) {
        MessageBoxW(nullptr, L"无法创建主窗口", L"错误", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    // Show window
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    if (SUCCEEDED(hrCom)) CoUninitialize();

    return (int)msg.wParam;
}
