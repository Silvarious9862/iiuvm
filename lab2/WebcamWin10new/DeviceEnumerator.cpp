#include "DeviceEnumerator.h"           
#include "Logger.h"                     
#include <mfapi.h>                      // Media Foundation
#include <mfidl.h>                      // MF ����������
#include <mfreadwrite.h>                // SourceReader/SinkWriter

DeviceEnumerator::DeviceEnumerator() {}

DeviceEnumerator::~DeviceEnumerator() {}

// ���������� ������ ��������� ��������������
std::vector<DeviceInfo> DeviceEnumerator::ListDevices() {
    Logger::Instance().Verbose(L"Enumerating devices..."); 
    auto devices = EnumerateDevices();                      // �������� ������������ MF ��������� � MFHelpers
    Logger::Instance().Verbose(std::wstring(L"Devices found: ") + std::to_wstring(devices.size())); // ������� ������� (verbose)
    return devices;                                         // ���������� ������ DeviceInfo
}
