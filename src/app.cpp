#include "app.h"
#include "resource.h"

#include "device_log.h"
#include "device_ui.h"
#include "graphics.h"

#include <dbt.h>
#include <shellapi.h>
#include <usbiodef.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

#include <windows.h>

namespace
{
constexpr UINT trayIconMessage = WM_APP + 1;
constexpr UINT trayOpenCommand = 1;
constexpr UINT trayExitCommand = 2;
constexpr wchar_t windowClassName[] = L"WinDeviceLogWindow";
constexpr wchar_t windowTitle[] = L"Win Device Log";

HICON appIcon = nullptr;
HICON appSmallIcon = nullptr;
bool trayIconVisible = false;
UINT taskbarCreatedMessage = 0;

void DestroyAppIcons()
{
    if (appSmallIcon != nullptr)
    {
        DestroyIcon(appSmallIcon);
        appSmallIcon = nullptr;
    }
    if (appIcon != nullptr)
    {
        DestroyIcon(appIcon);
        appIcon = nullptr;
    }
}

bool AddTrayIcon(HWND window)
{
    NOTIFYICONDATAW notification{};
    notification.cbSize = sizeof(notification);
    notification.hWnd = window;
    notification.uID = 1;
    notification.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notification.uCallbackMessage = trayIconMessage;
    notification.hIcon = appSmallIcon;
    lstrcpynW(notification.szTip, windowTitle, ARRAYSIZE(notification.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &notification))
    {
        return false;
    }

    trayIconVisible = true;
    return true;
}

void RemoveTrayIcon(HWND window)
{
    if (!trayIconVisible)
    {
        return;
    }

    NOTIFYICONDATAW notification{};
    notification.cbSize = sizeof(notification);
    notification.hWnd = window;
    notification.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &notification);
    trayIconVisible = false;
}

void RestoreWindow(HWND window)
{
    RemoveTrayIcon(window);
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}

void ShowTrayMenu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr ||
        !AppendMenuW(menu, MF_STRING, trayOpenCommand, L"Open Win Device Log") ||
        !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu, MF_STRING, trayExitCommand, L"Exit"))
    {
        if (menu != nullptr)
        {
            DestroyMenu(menu);
        }
        MessageBoxW(window, L"Could not open the system tray menu.",
                    windowTitle, MB_ICONERROR);
        return;
    }

    POINT cursorPosition{};
    GetCursorPos(&cursorPosition);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
        cursorPosition.x, cursorPosition.y, 0, window, nullptr);
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);

    if (command == trayOpenCommand)
    {
        RestoreWindow(window);
    }
    else if (command == trayExitCommand)
    {
        DestroyWindow(window);
    }
}
}

static LRESULT WINAPI WndProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == taskbarCreatedMessage && trayIconVisible)
    {
        trayIconVisible = false;
        if (!AddTrayIcon(window))
        {
            MessageBoxW(window, L"Could not restore the system tray icon.",
                        windowTitle, MB_ICONERROR);
        }
        return 0;
    }

    if (imguiInitialized &&
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
    {
        return TRUE;
    }

    switch (message)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            if (!AddTrayIcon(window))
            {
                ShowWindow(window, SW_RESTORE);
                MessageBoxW(window, L"Could not add the system tray icon.",
                            windowTitle, MB_ICONERROR);
                return 0;
            }
            ShowWindow(window, SW_HIDE);
        }
        else if (d3dDevice != nullptr)
        {
            CleanupRenderTarget();
            swapChain->ResizeBuffers(
                0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case trayIconMessage:
        if (lParam == WM_LBUTTONDBLCLK)
        {
            RestoreWindow(window);
        }
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
        {
            ShowTrayMenu(window);
        }
        return 0;

    case WM_DEVICECHANGE:
        if (HandleDeviceChange(wParam, lParam))
        {
            return TRUE;
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon(window);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int RunApplication(HINSTANCE instance, int showCommand)
{
    appIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    appSmallIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (appIcon == nullptr || appSmallIcon == nullptr)
    {
        MessageBoxW(nullptr, L"Could not create the application icon.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMessage == 0)
    {
        MessageBoxW(nullptr, L"Could not register the taskbar restart event.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = appIcon;
    windowClass.hIconSm = appSmallIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = windowClassName;

    if (RegisterClassExW(&windowClass) == 0)
    {
        MessageBoxW(nullptr, L"Could not register the application window.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    HWND window = CreateWindowW(
        windowClassName, windowTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
    {
        UnregisterClassW(windowClassName, instance);
        MessageBoxW(nullptr, L"Could not create the application window.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    if (!CreateDeviceD3D(window))
    {
        CleanupDeviceD3D();
        DestroyWindow(window);
        UnregisterClassW(windowClassName, instance);
        MessageBoxW(nullptr, L"Could not initialize Direct3D 11.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    ImGui::CreateContext();
    ApplyDeviceMonitorStyle();
    imguiInitialized = ImGui_ImplWin32_Init(window) &&
                       ImGui_ImplDX11_Init(d3dDevice, d3dContext);
    if (!imguiInitialized)
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        DestroyWindow(window);
        UnregisterClassW(windowClassName, instance);
        MessageBoxW(nullptr, L"Could not initialize Dear ImGui.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    DEV_BROADCAST_DEVICEINTERFACE_W notificationFilter{};
    notificationFilter.dbcc_size = sizeof(notificationFilter);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
    HDEVNOTIFY deviceNotification = RegisterDeviceNotificationW(
        window, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (deviceNotification == nullptr)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        imguiInitialized = false;
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        DestroyWindow(window);
        UnregisterClassW(windowClassName, instance);
        MessageBoxW(nullptr, L"Could not register USB device notifications.",
                    windowTitle, MB_ICONERROR);
        DestroyAppIcons();
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (message.message != WM_QUIT)
    {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }

        if (trayIconVisible)
        {
            WaitMessage();
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawDeviceMonitor();
        ImGui::Render();

        constexpr float clearColor[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
        d3dContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
        d3dContext->ClearRenderTargetView(renderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(1, 0);
    }

    UnregisterDeviceNotification(deviceNotification);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    imguiInitialized = false;
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(windowClassName, instance);
    DestroyAppIcons();
    return 0;
}
