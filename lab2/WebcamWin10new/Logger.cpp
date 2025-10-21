#include "Logger.h"                 
#include <mutex>                    // std::mutex, lock_guard
#include <windows.h>                // WinAPI (Console, WriteConsole, etc.)
#include <iostream>                 

// ���������� ���������� ������� � ������ ��������� � �������
struct Logger::Impl {
    std::mutex mtx;                 // �������� ������������� ����� �� �������
    bool console = true;            // �������� ����� � �������
    bool verbose = false;           // ���� ���������� ������
    Impl() = default;               // ����������� �� ���������
};

// ���������� ������� ��������� � ���� ������ "YYYY-MM-DD HH:MM:SS"
static std::wstring TimestampNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);              // �������� ��������� �����
    wchar_t buf[64];
    swprintf_s(buf, sizeof(buf) / sizeof(buf[0]), L"%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); // ����������� ������
    return std::wstring(buf);       
}

// �������� ��������� �������� wide-������ � �������/����/����
static void SafeWriteConsoleWide(HANDLE h, const std::wstring& s) {
    if (!h || h == INVALID_HANDLE_VALUE) return; // ������ �� ������ ��� ���������� �����������
    DWORD written = 0;
    if (WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr)) return; // ���� WriteConsoleW �������� � ������

    // ���� WriteConsoleW �� �������������� (��������, ���������� �� �������) � ��������� � UTF-8 � ����� ����� WriteFile
    int needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr); // ������ ������ � ������
    if (needed <= 0) return;           // �� ������� ��������� ������
    std::string buf(needed, '\0');     // �������� ������ ������� �������
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &buf[0], needed, nullptr, nullptr); // ����������� � UTF-8
    DWORD out = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &out, nullptr); // ����� ����� � ����������
}

// Singleton: ���������� ������������ ��������� Logger
Logger& Logger::Instance() {
    static Logger inst;               // ����������� ������������ ������
    return inst;                      // ���������� ������
}

Logger::Logger() : pImpl(new Impl()) {} // �����������: ������ Impl
Logger::~Logger() { delete pImpl; }     // ����������: ������� Impl

// ������������� ��������� ������� 
void Logger::InitConsole(bool enableConsole, bool verbose) {
    if (!pImpl) pImpl = new Impl();    // �� ������ ������ ������ Impl ���� ���
    std::lock_guard<std::mutex> g(pImpl->mtx); // ��������� ���������
    pImpl->console = enableConsole;    // ��������/��������� ����� � �������
    pImpl->verbose = verbose;          // ��������/��������� verbose
    if (pImpl->console) {              // ���� ����� � ������� ��������
        SetConsoleOutputCP(CP_UTF8);   // ������ ��������� ������ UTF-8
        SetConsoleCP(CP_UTF8);         // ������ ��������� ����� UTF-8
    }
}

// �������������� ���������
void Logger::Info(const std::wstring& msg) {
    if (!pImpl) return;                // ������ �� ��������������������� pImpl
    std::lock_guard<std::mutex> g(pImpl->mtx); // ������������ ������
    if (!pImpl->console) return;       // ���� ������� ��������� � �� �������
    std::wstring line;
    if (pImpl->verbose) line = TimestampNow() + L" [INFO] " + msg + L"\n"; // � ����������� ��� verbose
    else line = msg + L"\n";           // ����� ������ ���������
    SafeWriteConsoleWide(GetStdHandle(STD_OUTPUT_HANDLE), line); // ����� � STDOUT
}

// ��������������
void Logger::Warn(const std::wstring& msg) {
    if (!pImpl) return;
    std::lock_guard<std::mutex> g(pImpl->mtx); // �������������
    if (!pImpl->console) return;
    std::wstring line = (pImpl->verbose ? (TimestampNow() + L" [WARN] " + msg + L"\n") : (L"[WARN] " + msg + L"\n")); // ������
    SafeWriteConsoleWide(GetStdHandle(STD_OUTPUT_HANDLE), line); // ����� � STDOUT
}

// ������
void Logger::Error(const std::wstring& msg) {
    if (!pImpl) return;
    std::lock_guard<std::mutex> g(pImpl->mtx);
    if (!pImpl->console) return;
    std::wstring line = (pImpl->verbose ? (TimestampNow() + L" [ERROR] " + msg + L"\n") : (L"[ERROR] " + msg + L"\n")); // ������
    SafeWriteConsoleWide(GetStdHandle(STD_ERROR_HANDLE), line); // ����� � STDERR
}

// ��������� ��� (��������� ������ ��� verbose)
void Logger::Verbose(const std::wstring& msg) {
    if (!pImpl) return;
    std::lock_guard<std::mutex> g(pImpl->mtx);
    if (!pImpl->console) return;
    if (!pImpl->verbose) return;       // ���������� ���� verbose ��������
    std::wstring line = TimestampNow() + L" [VERBOSE] " + msg + L"\n"; // ������ � �����������
    SafeWriteConsoleWide(GetStdHandle(STD_OUTPUT_HANDLE), line); // ����� � STDOUT
}
