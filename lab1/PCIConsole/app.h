#pragma once
#include <windows.h>
#include <iostream>

class Application {
public:
    int Run();

private:
    void SetupConsole();
    void ShowError(DWORD errorCode);
    void WaitForExit();
}; 
