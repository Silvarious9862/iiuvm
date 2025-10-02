#include "console_formatter.h"

void Console_Formatter::PrintHeader() {
    std::cout << "PCI Device Scanner\n";
    std::cout << "------------------\n\n";
}

void Console_Formatter::PrintDevices(const std::vector<PCI_DEVICE_INFO>& devices) {
    if (devices.empty()) {
        std::cout << "No PCI devices found.\n";
        return;
    }

    // Таблица с выравниванием
    constexpr int col_widths[] = { 8, 12, 8, 10, 40 };

    PrintTableRow({ "Addr", "Vendor:Device", "Class", "Rev", "Description" }, col_widths);
    PrintSeparator(80);

    for (const auto& device : devices) {
        PrintTableRow({
            device.GetLocation(),
            device.GetVendorDeviceID(),
            device.GetClassCodes(),
            std::format("{:02X}", device.Revision),
            device.Description
            }, col_widths);
    }
}

void Console_Formatter::PrintStatistics(const std::vector<PCI_DEVICE_INFO>& devices) {
    std::cout << "\nScan Statistics:\n";
    std::cout << "----------------\n";
    std::cout << "Total devices: " << devices.size() << "\n\n";

    // Группировка по классам
    std::map<UCHAR, int> classCount;
    std::map<USHORT, int> vendorCount;

    for (const auto& device : devices) {
        vendorCount[device.VendorID]++;
    }

    std::cout << "Devices by class:\n";
    for (const auto& [classCode, count] : classCount) {
        std::cout << "  Class " << std::hex << static_cast<int>(classCode)
            << std::dec << ": " << count << " devices\n";
    }

    std::cout << "\nDevices by vendor:\n";
    for (const auto& [vendorID, count] : vendorCount) {
        std::cout << "  Vendor " << std::hex << vendorID
            << std::dec << ": " << count << " devices\n";
    }
}

void Console_Formatter::PrintTableRow(const std::vector<std::string>& columns, const int widths[]) {
    for (size_t i = 0; i < columns.size(); ++i) {
        std::cout << std::left << std::setw(widths[i]) << columns[i];
    }
    std::cout << "\n";
}

void Console_Formatter::PrintSeparator(int length) {
    std::cout << std::string(length, '-') << "\n";
}