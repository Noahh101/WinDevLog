#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct DeviceEvent
{
    std::string timestamp;
    std::string action;
    std::string deviceName;
};

extern std::vector<DeviceEvent> eventHistory;
extern bool autoScrollRequested;

bool HandleDeviceChange(WPARAM eventType, LPARAM eventData);
