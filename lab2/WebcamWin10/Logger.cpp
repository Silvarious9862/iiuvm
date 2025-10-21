#include "Logger.h"
#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#pragma comment(lib, "shlwapi.lib")

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

void Logger::Init(const std::wstring& exePath, bool consolePresent) {
    std::lock_guard<std::mutex> lk(mtx_);
    console_ = consolePresent;

    wchar_t exeDir[MAX_PATH];
    wcscpy_s(exeDir, exePath.c_str());
    PathRemoveFileSpecW(exeDir);
    std::wstring logsDir = std::wstring(exeDir) + L"\\logs";
    CreateDirectoryW(logsDir.c_str(), nullptr);
    logPath_ = logsDir + L"\\app.log";
    logfile_.open(logPath_, std::ios::app);
    if (!logfile_) {
        // fallback to stdout only
        console_ = true;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[200];
    swprintf_s(buf, L"--- Start: %04d-%02d-%02d %02d:%02d:%02d ---\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (logfile_) logfile_ << buf;
    if (console_) {
        std::wcout << buf;
    }
}

void Logger::Log(Level lvl, const std::wstring& msg) {
    std::lock_guard<std::mutex> lk(mtx_);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timebuf[100];
    swprintf_s(timebuf, L"%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const wchar_t* lvlstr = L"[INFO]";
    if (lvl == Level::Warn) lvlstr = L"[WARN]";
    if (lvl == Level::Error) lvlstr = L"[ERR ]";
    if (lvl == Level::Debug) lvlstr = L"[DBG ]";

    std::wstring line = std::wstring(timebuf) + L" " + lvlstr + L" " + msg + L"\n";
    if (logfile_) logfile_ << line;
    if (console_) {
        std::wcout << line;
    }
}
