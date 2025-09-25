#include <ntddk.h>

extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverUnload = [](PDRIVER_OBJECT) {
        KdPrint(("PCI Driver unloaded\n"));
        };

    KdPrint(("PCI Driver loaded\n"));
    return STATUS_SUCCESS;
}
