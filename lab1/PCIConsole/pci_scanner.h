#pragma once
#include <windows.h>
#include <vector>
#include <stdexcept>
#include "pci_device_info.h"

class PCI_Scanner_App {
private:
    HANDLE m_hDevice{ nullptr };

public:
    PCI_Scanner_App() = default;
    ~PCI_Scanner_App();

    PCI_Scanner_App(const PCI_Scanner_App&) = delete;
    PCI_Scanner_App& operator=(const PCI_Scanner_App&) = delete;

    bool Initialize();
    void Shutdown();
    std::vector<PCI_DEVICE_INFO> Scan();
    bool IsOpen() const;

private:
    bool Open();
    void Close();
};