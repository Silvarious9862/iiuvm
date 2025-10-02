#pragma once
#include <windows.h>
#include <string>
#include <format>

#define IOCTL_PCI_GET_DEVICES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct PCI_DEVICE_INFO {
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    USHORT VendorID;
    USHORT DeviceID;
    UCHAR BaseClass;
    UCHAR SubClass;
    UCHAR Revision;
    char Description[64];

    std::string GetLocation() const {
        return std::format("{:02X}:{:02X}.{:X}", Bus, Device, Function);
    }

    std::string GetVendorDeviceID() const {
        return std::format("{:04X}:{:04X}", VendorID, DeviceID);
    }

    std::string GetClassCodes() const {
        return std::format("{:02X}:{:02X}", BaseClass, SubClass);
    }
};

struct PCI_DEVICE_LIST {
    ULONG NumberOfDevices;
    PCI_DEVICE_INFO Devices[32];
};