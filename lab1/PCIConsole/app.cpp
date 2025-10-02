#include "app.h"
#include "pci_scanner.h"
#include "console_formatter.h"

int Application::Run() {
    SetupConsole();

    Console_Formatter::PrintHeader();

    try {
        PCI_Scanner_App scanner;

        std::cout << "Initializing PCI scanner... ";
        if (!scanner.Initialize()) {
            DWORD error = GetLastError();
            std::cout << "FAILED\n\n";
            ShowError(error);
            WaitForExit();
            return 1;
        }
        std::cout << "SUCCESS\n\n";

        // Сканирование
        std::cout << "Scanning PCI bus... ";
        auto devices = scanner.Scan();
        std::cout << "COMPLETED\n\n";

        // Вывод результатов
        Console_Formatter::PrintDevices(devices);
        Console_Formatter::PrintStatistics(devices);

        std::cout << "\nOperation completed successfully!\n";

    }
    catch (const std::exception& ex) {
        std::cerr << "\nError: " << ex.what() << "\n";
        WaitForExit();
        return 1;
    }

    WaitForExit();
    return 0;
}

void Application::SetupConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"PCI Device Scanner - C++");
}

void Application::ShowError(DWORD errorCode) {
    std::cerr << "Cannot access PCI scanner driver.\n";
    std::cerr << "Error code: " << errorCode << "\n\n";
    std::cerr << "Some help:\n";
    std::cerr << "1. sc create PCIScanner binPath= \"C:\\path\\pci_scanner.sys\" type= kernel\n";
    std::cerr << "2. sc start PCIScanner\n";
    std::cerr << "3. You have administrator privileges\n";
}

void Application::WaitForExit() {
    std::cout << "\nPress Enter to exit...";

    char buffer[100];
    if (std::cin.getline(buffer, sizeof(buffer))) {
    }

    std::cin.clear(); 
    std::cin.ignore(10000, '\n'); 
    std::cin.get(); 
}