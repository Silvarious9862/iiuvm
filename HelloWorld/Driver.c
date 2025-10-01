#include <wdm.h>
#include <ntstrsafe.h>

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
void UnloadDriver(PDRIVER_OBJECT DriverObject);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, UnloadDriver)
#endif

void UnloadDriver(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    // ���������� KdPrint ������ DbgPrint � UNLOAD
    KdPrint(("HelloWorld: Driver UNLOADED successfully!\n"));
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    // ������������� ����� ���������
    DriverObject->DriverUnload = UnloadDriver;

    // ���������� KdPrint ��� ������ �������������
    KdPrint(("HelloWorld: DriverEntry called\n"));
    KdPrint(("HelloWorld: Driver object at 0x%p\n", DriverObject));

    // ��������� ��������� �������� ��� �������
    LARGE_INTEGER interval;
    interval.QuadPart = -1000000; // 100ms
    KeDelayExecutionThread(KernelMode, FALSE, &interval);

    KdPrint(("HelloWorld: Driver LOADED successfully!\n"));

    return STATUS_SUCCESS;
}