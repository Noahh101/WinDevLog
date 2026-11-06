#include "device_log.h"

#include <windows.h>
#include <dbt.h>
#include <devpkey.h>
#include <setupapi.h>
#include <usbiodef.h>

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <cwctype>
#include <utility>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "uuid.lib")

std::vector<DeviceEvent> eventHistory;
bool autoScrollRequested = false;

static std::vector<std::pair<std::wstring, std::string>> knownDevices;
static constexpr size_t maxEventHistory = 2000;

static const DEVPROPKEY deviceParentKey = {
    { 0x4340a6c5, 0x93fa, 0x4706, { 0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7 } },
    8
};
static const DEVPROPKEY busReportedDeviceDescriptionKey = {
    { 0x540b947e, 0x8b40, 0x45bc, { 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2 } },
    4
};

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

bool HandleDeviceChange(WPARAM eventType, LPARAM eventData)
{
    if ((eventType != DBT_DEVICEARRIVAL &&
         eventType != DBT_DEVICEREMOVECOMPLETE) || eventData == 0)
    {
        return false;
    }

    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(eventData);
    if (header->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
    {
        return false;
    }

    const auto* interfaceData =
        reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(header);
    constexpr size_t pathOffset =
        offsetof(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name);
    if (header->dbch_size < pathOffset + sizeof(wchar_t))
    {
        return false;
    }

    const size_t pathLength =
        (header->dbch_size - pathOffset) / sizeof(wchar_t);
    const wchar_t* pathStart = interfaceData->dbcc_name;
    const wchar_t* pathEnd = std::find(
        pathStart, pathStart + pathLength, L'\0');
    if (pathEnd == pathStart + pathLength)
    {
        return false;
    }

    AddDeviceEvent(
        eventType == DBT_DEVICEARRIVAL,
        std::wstring(pathStart, pathEnd));
    return true;
}
