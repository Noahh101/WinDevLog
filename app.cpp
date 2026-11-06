#include "app.h"

#include "device_log.h"
#include "device_ui.h"
#include "graphics.h"

#include <dbt.h>
#include <usbiodef.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

#include <windows.h>

static LRESULT WINAPI WndProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (imguiInitialized &&
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
    {
        return TRUE;
    }

    switch (message)
    {
    case WM_SIZE:
        if (d3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            swapChain->ResizeBuffers(
                0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_DEVICECHANGE:
        if (HandleDeviceChange(wParam, lParam))
        {
            return TRUE;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int RunApplication(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"WinDeviceLogWindow";

    if (RegisterClassExW(&windowClass) == 0)
    {
        MessageBoxW(nullptr, L"Could not register the application window.",
                    L"Win Device Log", MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowW(
        windowClass.lpszClassName, L"Win Device Log",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
    {
        UnregisterClassW(windowClass.lpszClassName, instance);
        MessageBoxW(nullptr, L"Could not create the application window.",
                    L"Win Device Log", MB_ICONERROR);
        return 1;
    }

    if (!CreateDeviceD3D(window))
    {
        CleanupDeviceD3D();
        DestroyWindow(window);
        UnregisterClassW(windowClass.lpszClassName, instance);
        MessageBoxW(nullptr, L"Could not initialize Direct3D 11.",
                    L"Win Device Log", MB_ICONERROR);
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
        UnregisterClassW(windowClass.lpszClassName, instance);
        MessageBoxW(nullptr, L"Could not initialize Dear ImGui.",
                    L"Win Device Log", MB_ICONERROR);
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
        UnregisterClassW(windowClass.lpszClassName, instance);
        MessageBoxW(nullptr, L"Could not register USB device notifications.",
                    L"Win Device Log", MB_ICONERROR);
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
    UnregisterClassW(windowClass.lpszClassName, instance);
    return 0;
}
