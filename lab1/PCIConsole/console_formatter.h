#pragma once
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include "pci_device_info.h"

class Console_Formatter {
public:
    static void PrintHeader();
    static void PrintDevices(const std::vector<PCI_DEVICE_INFO>& devices);
    static void PrintStatistics(const std::vector<PCI_DEVICE_INFO>& devices);

private:
    static void PrintTableRow(const std::vector<std::string>& columns, const int widths[]);
    static void PrintSeparator(int length);
};