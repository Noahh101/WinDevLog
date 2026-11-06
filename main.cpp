#include <windows.h>
#include <dbt.h>
#include <devpkey.h>
#include <setupapi.h>
#include <usbiodef.h>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uuid.lib")

struct DeviceEvent
{
    std::string timestamp;
    std::string action;
    std::string deviceName;
};

static std::vector<DeviceEvent> eventHistory;
static std::vector<std::pair<std::wstring, std::string>> knownDevices;
static bool autoScrollRequested = false;

static constexpr size_t maxEventHistory = 2000;
static const DEVPROPKEY deviceParentKey = {
    { 0x4340a6c5, 0x93fa, 0x4706, { 0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7 } },
    8
};
static const DEVPROPKEY busReportedDeviceDescriptionKey = {
    { 0x540b947e, 0x8b40, 0x45bc, { 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2 } },
    4
};

static ID3D11Device* d3dDevice = nullptr;
static ID3D11DeviceContext* d3dContext = nullptr;
static IDXGISwapChain* swapChain = nullptr;
static ID3D11RenderTargetView* renderTargetView = nullptr;
static HDEVNOTIFY deviceNotification = nullptr;
static bool imguiInitialized = false;

static std::string ToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr) == 0)
    {
        return {};
    }

    result.pop_back();
    return result;
}

static std::string CurrentTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &now);

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

static std::wstring ReadDeviceProperty(
    HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData, DWORD property)
{
    DWORD requiredSize = 0;
    DWORD propertyType = 0;
    SetupDiGetDeviceRegistryPropertyW(
        deviceInfoSet, &deviceInfoData, property, &propertyType,
        nullptr, 0, &requiredSize);

    if (requiredSize < sizeof(wchar_t))
    {
        return {};
    }

    std::vector<wchar_t> buffer(
        static_cast<size_t>(requiredSize / sizeof(wchar_t)) + 1, L'\0');
    const DWORD bufferSize = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet, &deviceInfoData, property, &propertyType,
            reinterpret_cast<PBYTE>(buffer.data()), bufferSize, nullptr))
    {
        return {};
    }

    if (propertyType != REG_SZ && propertyType != REG_EXPAND_SZ)
    {
        return {};
    }

    return std::wstring(buffer.data());
}

static std::wstring ReadDeviceProperty(
    HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData,
    const DEVPROPKEY& propertyKey)
{
    DEVPROPTYPE propertyType = 0;
    DWORD requiredSize = 0;
    SetupDiGetDevicePropertyW(
        deviceInfoSet, &deviceInfoData, &propertyKey, &propertyType,
        nullptr, 0, &requiredSize, 0);
    if (requiredSize < sizeof(wchar_t) || propertyType != DEVPROP_TYPE_STRING)
    {
        return {};
    }

    std::vector<wchar_t> buffer(
        static_cast<size_t>(requiredSize / sizeof(wchar_t)) + 1, L'\0');
    if (!SetupDiGetDevicePropertyW(
            deviceInfoSet, &deviceInfoData, &propertyKey, &propertyType,
            reinterpret_cast<PBYTE>(buffer.data()),
            static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), nullptr, 0))
    {
        return {};
    }
    return std::wstring(buffer.data());
}

static std::wstring Lowercase(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return value;
}

static bool IsGenericDeviceName(const std::wstring& name)
{
    const std::wstring value = Lowercase(name);
    constexpr const wchar_t* genericNames[] = {
        L"usb composite device",
        L"usb input device",
        L"usb device",
        L"generic usb hub",
        L"unknown device",
        L"hid keyboard device",
        L"hid-compliant mouse",
        L"hid-compliant system controller",
        L"hid-compliant consumer control device",
        L"hid-compliant device",
        L"hid compliant device",
        L"device descriptor request failed"
    };
    for (const wchar_t* generic : genericNames)
    {
        if (value == generic)
        {
            return true;
        }
    }
    return value.empty();
}

static std::wstring GetPeripheralType(const std::wstring& details)
{
    const std::wstring value = Lowercase(details);
    if (value.find(L"keyboard") != std::wstring::npos)
    {
        return L"Keyboard";
    }
    if (value.find(L"mouse") != std::wstring::npos ||
        value.find(L"touchpad") != std::wstring::npos)
    {
        return L"Mouse";
    }
    if (value.find(L"headphone") != std::wstring::npos ||
        value.find(L"headset") != std::wstring::npos)
    {
        return L"Headset";
    }
    if (value.find(L"speaker") != std::wstring::npos ||
        value.find(L"audio") != std::wstring::npos ||
        value.find(L"microphone") != std::wstring::npos)
    {
        return L"Audio device";
    }
    if (value.find(L"webcam") != std::wstring::npos ||
        value.find(L"camera") != std::wstring::npos)
    {
        return L"Camera";
    }
    if (value.find(L"game controller") != std::wstring::npos ||
        value.find(L"gamepad") != std::wstring::npos ||
        value.find(L"joystick") != std::wstring::npos)
    {
        return L"Game controller";
    }
    if (value.find(L"printer") != std::wstring::npos)
    {
        return L"Printer";
    }
    if (value.find(L"bluetooth") != std::wstring::npos)
    {
        return L"Bluetooth device";
    }
    if (value.find(L"storage") != std::wstring::npos ||
        value.find(L"disk drive") != std::wstring::npos)
    {
        return L"Storage device";
    }
    return {};
}

static std::string GetDeviceName(const std::wstring& devicePath)
{
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr, DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    std::wstring deviceInstanceId;
    std::wstring deviceName;

    if (SetupDiOpenDeviceInterfaceW(
            deviceInfoSet, devicePath.c_str(), 0, &interfaceData))
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(
            deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);

        if (requiredSize != 0)
        {
            std::vector<BYTE> detailBuffer(requiredSize);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
                detailBuffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            SP_DEVINFO_DATA deviceInfoData{};
            deviceInfoData.cbSize = sizeof(deviceInfoData);
            if (SetupDiGetDeviceInterfaceDetailW(
                    deviceInfoSet, &interfaceData, detail, requiredSize,
                    nullptr, &deviceInfoData))
            {
                DWORD idSize = 0;
                SetupDiGetDeviceInstanceIdW(
                    deviceInfoSet, &deviceInfoData, nullptr, 0, &idSize);
                if (idSize != 0)
                {
                    std::vector<wchar_t> idBuffer(idSize);
                    if (SetupDiGetDeviceInstanceIdW(
                            deviceInfoSet, &deviceInfoData, idBuffer.data(),
                            idSize, nullptr))
                    {
                        deviceInstanceId.assign(idBuffer.data());
                    }
                }

                const std::wstring busName = ReadDeviceProperty(
                    deviceInfoSet, deviceInfoData, busReportedDeviceDescriptionKey);
                const std::wstring friendlyName = ReadDeviceProperty(
                    deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME);
                const std::wstring description = ReadDeviceProperty(
                    deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC);

                if (!IsGenericDeviceName(busName))
                {
                    deviceName = busName;
                }
                else if (!IsGenericDeviceName(friendlyName))
                {
                    deviceName = friendlyName;
                }
                else if (!IsGenericDeviceName(description))
                {
                    deviceName = description;
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    std::wstring peripheralType;
    if (deviceName.empty() && !deviceInstanceId.empty())
    {
        HDEVINFO allDevices = SetupDiGetClassDevsW(
            nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (allDevices != INVALID_HANDLE_VALUE)
        {
            for (DWORD index = 0;; ++index)
            {
                SP_DEVINFO_DATA childData{};
                childData.cbSize = sizeof(childData);
                if (!SetupDiEnumDeviceInfo(allDevices, index, &childData))
                {
                    break;
                }

                const std::wstring parentId = ReadDeviceProperty(
                    allDevices, childData, deviceParentKey);
                if (Lowercase(parentId) != Lowercase(deviceInstanceId))
                {
                    continue;
                }

                const std::wstring childBusName = ReadDeviceProperty(
                    allDevices, childData, busReportedDeviceDescriptionKey);
                const std::wstring childFriendlyName = ReadDeviceProperty(
                    allDevices, childData, SPDRP_FRIENDLYNAME);
                const std::wstring childDescription = ReadDeviceProperty(
                    allDevices, childData, SPDRP_DEVICEDESC);
                const std::wstring specificName =
                    !IsGenericDeviceName(childBusName) ? childBusName :
                    !IsGenericDeviceName(childFriendlyName) ? childFriendlyName :
                    !IsGenericDeviceName(childDescription) ? childDescription :
                    std::wstring{};
                if (!specificName.empty())
                {
                    deviceName = specificName;
                    break;
                }

                const std::wstring type = GetPeripheralType(
                    childBusName + L" " + childFriendlyName + L" " + childDescription);
                if (!type.empty())
                {
                    peripheralType = type;
                    if (deviceName.empty())
                    {
                        break;
                    }
                }
            }
            SetupDiDestroyDeviceInfoList(allDevices);
        }
    }

    if (deviceName.empty())
    {
        deviceName = peripheralType.empty() ? L"USB device" : peripheralType;
    }
    return ToUtf8(deviceName);
}

static std::string FindKnownDevice(const std::wstring& devicePath)
{
    for (const auto& device : knownDevices)
    {
        if (device.first == devicePath)
        {
            return device.second;
        }
    }
    return {};
}

static void RememberDevice(const std::wstring& devicePath, const std::string& name)
{
    for (auto& device : knownDevices)
    {
        if (device.first == devicePath)
        {
            device.second = name;
            return;
        }
    }
    knownDevices.emplace_back(devicePath, name);
}

static void ForgetDevice(const std::wstring& devicePath)
{
    for (auto it = knownDevices.begin(); it != knownDevices.end(); ++it)
    {
        if (it->first == devicePath)
        {
            knownDevices.erase(it);
            return;
        }
    }
}

static void AddDeviceEvent(bool connected, const std::wstring& devicePath)
{
    std::string deviceName = GetDeviceName(devicePath);
    if (deviceName.empty())
    {
        deviceName = FindKnownDevice(devicePath);
    }
    if (deviceName.empty())
    {
        deviceName = "USB device";
    }

    eventHistory.push_back({
        CurrentTimestamp(),
        connected ? "Connected" : "Disconnected",
        deviceName
    });
    if (eventHistory.size() > maxEventHistory)
    {
        eventHistory.erase(eventHistory.begin());
    }
    autoScrollRequested = true;

    if (connected)
    {
        RememberDevice(devicePath, deviceName);
    }
    else
    {
        ForgetDevice(devicePath);
    }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (swapChain != nullptr &&
        SUCCEEDED(swapChain->GetBuffer(
            0, IID_PPV_ARGS(&backBuffer))))
    {
        d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        backBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (renderTargetView != nullptr)
    {
        renderTargetView->Release();
        renderTargetView = nullptr;
    }
}

static void CleanupDeviceD3D();

static bool CreateDeviceD3D(HWND window)
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = window;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &swapChainDesc, &swapChain, &d3dDevice, &featureLevel, &d3dContext);
    if (FAILED(result))
    {
        CleanupDeviceD3D();
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            &swapChainDesc, &swapChain, &d3dDevice, &featureLevel, &d3dContext);
        if (FAILED(result))
        {
            CleanupDeviceD3D();
            return false;
        }
    }

    CreateRenderTarget();
    if (renderTargetView == nullptr)
    {
        CleanupDeviceD3D();
        return false;
    }
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (swapChain != nullptr)
    {
        swapChain->Release();
        swapChain = nullptr;
    }
    if (d3dContext != nullptr)
    {
        d3dContext->Release();
        d3dContext = nullptr;
    }
    if (d3dDevice != nullptr)
    {
        d3dDevice->Release();
        d3dDevice = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
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
        if ((wParam == DBT_DEVICEARRIVAL ||
             wParam == DBT_DEVICEREMOVECOMPLETE) && lParam != 0)
        {
            const auto* header =
                reinterpret_cast<const DEV_BROADCAST_HDR*>(lParam);
            if (header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
            {
                const auto* interfaceData =
                    reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(header);
                constexpr size_t pathOffset =
                    offsetof(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name);
                if (header->dbch_size < pathOffset + sizeof(wchar_t))
                {
                    break;
                }

                const size_t pathLength =
                    (header->dbch_size - pathOffset) / sizeof(wchar_t);
                const wchar_t* pathStart = interfaceData->dbcc_name;
                const wchar_t* pathEnd = std::find(
                    pathStart, pathStart + pathLength, L'\0');
                if (pathEnd == pathStart + pathLength)
                {
                    break;
                }

                const std::wstring devicePath(pathStart, pathEnd);
                AddDeviceEvent(
                    wParam == DBT_DEVICEARRIVAL, devicePath);
                return TRUE;
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

static void DrawDeviceMonitor()
{
    size_t connectedCount = 0;
    size_t disconnectedCount = 0;
    for (const DeviceEvent& event : eventHistory)
    {
        if (event.action == "Connected")
        {
            ++connectedCount;
        }
        else
        {
            ++disconnectedCount;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(1000.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "Win Device Log", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.12f, 0.17f, 1.0f));
    ImGui::BeginChild("Header", ImVec2(0.0f, 106.0f), true);
    ImGui::SetCursorPos(ImVec2(22.0f, 19.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.76f, 1.0f, 1.0f));
    ImGui::TextUnformatted("WIN DEVICE LOG");
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(23.0f, 57.0f));
    ImGui::TextDisabled("A live history of USB connections and removals");

    const float statsStart = ImGui::GetWindowWidth() - 430.0f;
    ImGui::SetCursorPos(ImVec2(statsStart, 43.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.39f, 0.85f, 0.65f, 1.0f));
    ImGui::TextUnformatted("CONNECTIONS");
    ImGui::SameLine(statsStart + 122.0f);
    ImGui::Text("%zu", connectedCount);
    ImGui::PopStyleColor();

    ImGui::SameLine(statsStart + 190.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.48f, 0.46f, 1.0f));
    ImGui::TextUnformatted("DISCONNECTIONS");
    ImGui::SameLine(statsStart + 333.0f);
    ImGui::Text("%zu", disconnectedCount);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("ACTIVITY");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu events", eventHistory.size());
    ImGui::SameLine(ImGui::GetWindowWidth() - 125.0f);
    ImGui::BeginDisabled(eventHistory.empty());
    if (ImGui::Button("Clear history"))
    {
        eventHistory.clear();
        connectedCount = 0;
        disconnectedCount = 0;
        autoScrollRequested = false;
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    if (eventHistory.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.088f, 0.115f, 1.0f));
        ImGui::BeginChild("EmptyState", ImVec2(0.0f, 0.0f), true);
        const ImVec2 emptySize = ImGui::GetContentRegionAvail();
        const char* emptyTitle = "All quiet for now";
        const char* emptyHint = "Connect or disconnect a USB device to see it here.";
        const ImVec2 titleSize = ImGui::CalcTextSize(emptyTitle);
        const ImVec2 hintSize = ImGui::CalcTextSize(emptyHint);
        ImGui::SetCursorPos(ImVec2(
            (emptySize.x - titleSize.x) * 0.5f,
            (emptySize.y - titleSize.y - hintSize.y - 20.0f) * 0.5f));
        ImGui::TextColored(
            ImVec4(0.72f, 0.81f, 0.91f, 1.0f), "%s", emptyTitle);
        ImGui::SetCursorPosX((emptySize.x - hintSize.x) * 0.5f);
        ImGui::TextDisabled("%s", emptyHint);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14.0f, 10.0f));
        if (ImGui::BeginTable(
                "DeviceEvents", 3,
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupColumn("TIME", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("STATUS", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("DEVICE", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < eventHistory.size(); ++i)
            {
                const DeviceEvent& event = eventHistory[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(event.timestamp.c_str());

                ImGui::TableSetColumnIndex(1);
                const ImVec4 actionColor = event.action == "Connected"
                    ? ImVec4(0.39f, 0.85f, 0.65f, 1.0f)
                    : ImVec4(1.0f, 0.48f, 0.46f, 1.0f);
                ImGui::TextColored(actionColor, "%s", event.action.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(event.deviceName.c_str());
                if (autoScrollRequested && i + 1 == eventHistory.size())
                {
                    ImGui::SetScrollHereY(1.0f);
                }
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    autoScrollRequested = false;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
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
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.088f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.22f, 0.28f, 0.75f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.15f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.14f, 0.32f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.40f, 0.59f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.29f, 0.43f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.17f, 0.23f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.22f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.16f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.075f, 0.088f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.085f, 0.10f, 0.13f, 1.0f);
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
    deviceNotification = RegisterDeviceNotificationW(
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
