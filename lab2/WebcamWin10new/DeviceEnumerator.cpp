#include "DeviceEnumerator.h"           
#include "Logger.h"                     
#include <mfapi.h>                      // Media Foundation
#include <mfidl.h>                      // MF интерфейсы
#include <mfreadwrite.h>                // SourceReader/SinkWriter

DeviceEnumerator::DeviceEnumerator() {}

DeviceEnumerator::~DeviceEnumerator() {}

// Возвращает список доступных видеоустройств
std::vector<DeviceInfo> DeviceEnumerator::ListDevices() {
    Logger::Instance().Verbose(L"Enumerating devices..."); 
    auto devices = EnumerateDevices();                      // вызываем перечисление MF устройств в MFHelpers
    Logger::Instance().Verbose(std::wstring(L"Devices found: ") + std::to_wstring(devices.size())); // сколько найдено (verbose)
    return devices;                                         // возвращаем вектор DeviceInfo
}
