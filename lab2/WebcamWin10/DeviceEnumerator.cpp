#include "DeviceEnumerator.h"
#include "Logger.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

DeviceEnumerator::DeviceEnumerator() {
    // MFInit handled at app level
}

DeviceEnumerator::~DeviceEnumerator() {}

std::vector<DeviceInfo> DeviceEnumerator::ListDevices() {
    Logger::Instance().LogInfo(L"������������ ���������...");
    auto devices = EnumerateDevices();
    Logger::Instance().LogInfo(std::wstring(L"������� ���������: ") + std::to_wstring(devices.size()));
    return devices;
}
