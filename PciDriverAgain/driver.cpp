#include <wdm.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

DRIVER_UNLOAD UnloadDriver;

void UnloadDriver(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrint("PCISCAN: Driver Unloaded\n");
}

// Функция для определения типа устройства по Class/Subclass
const char* GetDeviceType(UCHAR base_class, UCHAR sub_class) {
    switch (base_class) {
    case 0x00: return "Pre-2.0 Device";
    case 0x01:
        switch (sub_class) {
        case 0x00: return "SCSI Controller";
        case 0x01: return "IDE Controller";
        case 0x02: return "Floppy Controller";
        case 0x03: return "IPI Controller";
        case 0x04: return "RAID Controller";
        case 0x05: return "ATA Controller";
        case 0x06: return "SATA Controller";
        case 0x80: return "Other Mass Storage";
        default: return "Mass Storage Controller";
        }
    case 0x02:
        switch (sub_class) {
        case 0x00: return "Ethernet Controller";
        case 0x01: return "Token Ring Controller";
        case 0x02: return "FDDI Controller";
        case 0x03: return "ATM Controller";
        case 0x04: return "ISDN Controller";
        case 0x80: return "Other Network Controller";
        default: return "Network Controller";
        }
    case 0x03:
        switch (sub_class) {
        case 0x00: return "VGA Compatible Controller";
        case 0x01: return "XGA Controller";
        case 0x02: return "3D Controller";
        case 0x80: return "Other Display Controller";
        default: return "Display Controller";
        }
    case 0x06:
        switch (sub_class) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x02: return "EISA Bridge";
        case 0x03: return "MCA Bridge";
        case 0x04: return "PCI-to-PCI Bridge";
        case 0x05: return "PCMCIA Bridge";
        case 0x06: return "NuBus Bridge";
        case 0x07: return "CardBus Bridge";
        case 0x08: return "RACEway Bridge";
        case 0x80: return "Other Bridge";
        default: return "Bridge Device";
        }
    case 0x0C:
        switch (sub_class) {
        case 0x00: return "Serial Controller";
        case 0x01: return "Parallel Controller";
        case 0x02: return "Multiport Serial Controller";
        case 0x03: return "Modem";
        case 0x80: return "Other Communications";
        default: return "Communications Controller";
        }
    default: return "Unknown Device";
    }
}

// Функция для определения вендора по VendorID
const char* GetVendorName(USHORT vendor_id) {
    switch (vendor_id) {
    case 0x8086: return "Intel";
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD";
    case 0x1414: return "Microsoft";
    case 0x5333: return "S3";
    case 0x1011: return "Digital Equipment";
    case 0x10EC: return "Realtek";
    case 0x1969: return "Atheros";
    default: return "Unknown Vendor";
    }
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverUnload = UnloadDriver;

    DbgPrint("PCISCAN: ===== ENHANCED PCI SCANNER FOR HYPER-V GEN1 =====\n");
    DbgPrint("PCISCAN: Complete PCI Configuration Space Analysis\n\n");

    UCHAR bus, device, function;
    ULONG devices_found = 0;

    DbgPrint("PCISCAN: Location  Vendor   Device   Class     SubClass Type\n");
    DbgPrint("PCISCAN: ---------  ------   ------   -----     -------- ----\n");

    for (bus = 0; bus < 2; bus++) {
        for (device = 0; device < 32; device++) {
            for (function = 0; function < 8; function++) {
                // Читаем VendorID/DeviceID
                ULONG address = (1 << 31) | (bus << 16) | (device << 11) | (function << 8);
                WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS, address);
                ULONG vendor_device = READ_PORT_ULONG((PULONG)PCI_CONFIG_DATA);

                USHORT vendor_id = (USHORT)(vendor_device & 0xFFFF);
                USHORT device_id = (USHORT)((vendor_device >> 16) & 0xFFFF);

                if (vendor_id != 0xFFFF) {
                    // Читаем Class Code и Revision
                    WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS, address | 0x08);
                    ULONG class_rev = READ_PORT_ULONG((PULONG)PCI_CONFIG_DATA);
                    UCHAR revision = (UCHAR)(class_rev & 0xFF);
                    UCHAR sub_class = (UCHAR)((class_rev >> 16) & 0xFF);
                    UCHAR base_class = (UCHAR)((class_rev >> 24) & 0xFF);

                    // Читаем Header Type
                    WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS, address | 0x0C);
                    ULONG bist_header = READ_PORT_ULONG((PULONG)PCI_CONFIG_DATA);
                    UCHAR header_type = (UCHAR)((bist_header >> 16) & 0xFF);

                    devices_found++;

                    const char* vendor_name = GetVendorName(vendor_id);
                    const char* device_type = GetDeviceType(base_class, sub_class);

                    DbgPrint("PCISCAN: %02X:%02X.%X %04X:%04X %02X(%s) %02X      %s\n",
                        bus, device, function,
                        vendor_id, device_id,
                        base_class, device_type, sub_class, vendor_name);

                    // Дополнительная информация для некоторых устройств
                    if (vendor_id == 0x8086) {
                        switch (device_id) {
                        case 0x7192:
                            DbgPrint("PCISCAN:   -> Intel 440BX Host Bridge (Revision %02X)\n", revision);
                            break;
                        case 0x7110:
                            DbgPrint("PCISCAN:   -> Intel PIIX4 ISA Bridge (Revision %02X)\n", revision);
                            break;
                        case 0x7111:
                            DbgPrint("PCISCAN:   -> Intel PIIX4 IDE Controller (Revision %02X)\n", revision);
                            break;
                        case 0x7113:
                            DbgPrint("PCISCAN:   -> Intel PIIX4 ACPI Controller (Revision %02X)\n", revision);
                            break;
                        }
                    }
                    else if (vendor_id == 0x1414 && device_id == 0x5353) {
                        DbgPrint("PCISCAN:   -> Microsoft S3 Trio64 Emulation (Header Type: %02X)\n", header_type);
                    }
                }
            }
        }
    }

    DbgPrint("\nPCISCAN: ===== SCAN SUMMARY =====\n");
    DbgPrint("PCISCAN: Total PCI devices found: %lu\n", devices_found);
    DbgPrint("PCISCAN: PCI bus scanning completed successfully!\n");
    DbgPrint("PCISCAN: =========================\n");

    return STATUS_SUCCESS;
}