#include <wdm.h>
#include <ntstrsafe.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define DEVICE_NAME L"\\Device\\PCIScanner"
#define SYMBOLIC_NAME L"\\DosDevices\\PCIScanner"

#define IOCTL_PCI_GET_DEVICES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _PCI_DEVICE_INFO {
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    USHORT VendorID;
    USHORT DeviceID;
    UCHAR BaseClass;
    UCHAR SubClass;
    UCHAR Revision;
    char Description[64];
} PCI_DEVICE_INFO, * PPCI_DEVICE_INFO;

typedef struct _PCI_DEVICE_LIST {
    ULONG NumberOfDevices;
    PCI_DEVICE_INFO Devices[32];
} PCI_DEVICE_LIST, * PPCI_DEVICE_LIST;

DRIVER_UNLOAD UnloadDriver;
DRIVER_DISPATCH DispatchCreateClose;
DRIVER_DISPATCH DispatchDeviceControl;

// Определение типа устройства по Class/Subclass
const char* GetDeviceType(UCHAR base_class, UCHAR sub_class) {
    UNREFERENCED_PARAMETER(base_class);
    UNREFERENCED_PARAMETER(sub_class);

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

// Определение вендора по VendorID
const char* GetVendorName(USHORT vendor_id) {
    UNREFERENCED_PARAMETER(vendor_id);

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

NTSTATUS ScanPciDevices(PPCI_DEVICE_LIST deviceList) {
    UCHAR bus, device, function;
    ULONG devices_found = 0;

    for (bus = 0; bus < 2; bus++) {
        for (device = 0; device < 32; device++) {
            for (function = 0; function < 8; function++) {
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

                    // Сохраняем информацию об устройстве
                    PPCI_DEVICE_INFO devInfo = &deviceList->Devices[devices_found];
                    devInfo->Bus = bus;
                    devInfo->Device = device;
                    devInfo->Function = function;
                    devInfo->VendorID = vendor_id;
                    devInfo->DeviceID = device_id;
                    devInfo->BaseClass = base_class;
                    devInfo->SubClass = sub_class;
                    devInfo->Revision = revision;

                    // Формируем описание
                    const char* vendor_name = GetVendorName(vendor_id);
                    const char* device_type = GetDeviceType(base_class, sub_class);

                    // Копируем информацию
                    NTSTATUS status;
                    status = RtlStringCbPrintfA(devInfo->Description, sizeof(devInfo->Description),
                        "%s %s", vendor_name, device_type);

                    // Запасной вариант копирования
                    if (!NT_SUCCESS(status)) {
                        RtlStringCbCopyA(devInfo->Description, sizeof(devInfo->Description), "Unknown Device");
                    }

                    devices_found++;

                    if (devices_found >= 32) {
                        deviceList->NumberOfDevices = devices_found;
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
    }

    deviceList->NumberOfDevices = devices_found;
    return STATUS_SUCCESS;
}

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG infoLength = 0;

    switch (irpStack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_PCI_GET_DEVICES: {
        if (irpStack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(PCI_DEVICE_LIST)) {
            PPCI_DEVICE_LIST deviceList = (PPCI_DEVICE_LIST)Irp->AssociatedIrp.SystemBuffer;
            status = ScanPciDevices(deviceList);
            infoLength = sizeof(PCI_DEVICE_LIST);
        }
        else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = infoLength;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

void UnloadDriver(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symbolicName;
    RtlInitUnicodeString(&symbolicName, SYMBOLIC_NAME);
    IoDeleteSymbolicLink(&symbolicName);

    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    DbgPrint("PCISCAN: Driver Unloaded\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING deviceName, symbolicName;

    // Инициализируем строки
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&symbolicName, SYMBOLIC_NAME);

    // Создаем устройство
    status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Создаем символическую ссылку
    status = IoCreateSymbolicLink(&symbolicName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    // Настраиваем функции драйвера
    DriverObject->DriverUnload = UnloadDriver;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

    DbgPrint("PCISCAN: Driver loaded successfully\n");

    return STATUS_SUCCESS;
}