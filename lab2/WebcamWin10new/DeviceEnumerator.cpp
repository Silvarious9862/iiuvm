#include "DeviceEnumerator.h"
#include "Logger.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

DeviceEnumerator::DeviceEnumerator() {}

DeviceEnumerator::~DeviceEnumerator() {}

std::vector<DeviceInfo> DeviceEnumerator::ListDevices() {
    Logger::Instance().Verbose(L"Enumerating devices...");
    auto devices = EnumerateDevices();
    Logger::Instance().Verbose(std::wstring(L"Devices found: ") + std::to_wstring(devices.size()));
    return devices;
}
