// main.cpp - Application entry point
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <shobjidl.h>   // SetCurrentProcessExplicitAppUserModelID
#include "MainWindow.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 设置 AppUserModelID，确保窗口立即出现在任务栏
    SetCurrentProcessExplicitAppUserModelID(L"MonitorTool.MainWindow");

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

    // 强制 Shell 立即刷新任务栏图标
    SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(hWnd, nCmdShow);  // 二次确保任务栏捕获窗口

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
