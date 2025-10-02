#include "pci_scanner.h"

PCI_Scanner_App::~PCI_Scanner_App() {
    Close();
}

bool PCI_Scanner_App::Initialize() {
    return Open();
}

void PCI_Scanner_App::Shutdown() {
    Close();
}

bool PCI_Scanner_App::Open() {
    m_hDevice = CreateFileW(
        L"\\\\.\\PCIScanner",
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    return (m_hDevice != INVALID_HANDLE_VALUE);
}

void PCI_Scanner_App::Close() {
    if (m_hDevice && m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = nullptr;
    }
}

std::vector<PCI_DEVICE_INFO> PCI_Scanner_App::Scan() {
    std::vector<PCI_DEVICE_INFO> devices;

    if (!IsOpen()) {
        throw std::runtime_error("Device not opened");
    }

    PCI_DEVICE_LIST deviceList{};
    DWORD bytesReturned = 0;

    BOOL result = DeviceIoControl(
        m_hDevice,
        IOCTL_PCI_GET_DEVICES,
        nullptr, 0,
        &deviceList, sizeof(deviceList),
        &bytesReturned,
        nullptr
    );

    if (!result) {
        DWORD error = GetLastError();
        throw std::runtime_error(std::format("DeviceIoControl failed with error: {}", error));
    }

    devices.assign(deviceList.Devices, deviceList.Devices + deviceList.NumberOfDevices);
    return devices;
}

bool PCI_Scanner_App::IsOpen() const {
    return m_hDevice && m_hDevice != INVALID_HANDLE_VALUE;
}