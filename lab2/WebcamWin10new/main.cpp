// main.cpp
#include <windows.h>                // WinAPI ������� ������� � ����
#include <objbase.h>                // COM �������������/����
#include <iostream>                 
#include <string>                   
#include <vector>                   
#include <optional>                 
#include <locale>                   // ������ 
#include <codecvt>                  // �������������� ���������

#include <shlwapi.h>                // PathIsRelative, PathRemoveFileSpec
#pragma comment(lib, "shlwapi.lib") // �������� shlwapi

#include <mfapi.h>                  // Media Foundation API
#include <mfidl.h>                  // MF ����������
#include <mfobjects.h>              // MF �������
#include <mfreadwrite.h>            // source/sink reader/writer
#include <mftransform.h>            // MFT ����
#include <mferror.h>                // HRESULT MF ����
#pragma comment(lib, "mfplat.lib")  // �������� MF
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

#include <wincodec.h>               // WIC (������ JPEG)
#pragma comment(lib, "windowscodecs.lib")

#include <wrl/client.h>             // ComPtr
using Microsoft::WRL::ComPtr;       // ������� ��� ��� COM ����������

#include "CommandLine.h"            // ������ ����������
#include "Logger.h"                 // ���������������� ������
#include "DeviceEnumerator.h"       // ������������ �����
#include "FrameGrabber.h"           // ������ � JPEG
#include "VideoRecorder.h"          // ������ ����� � MP4
#include "MFHelpers.h"              // ��������������� MF �������
#include "ScopeGuard.h"             // RAII ��� �������

using namespace std;                

// ���������� ������ ���� � ������������ �����
static wstring GetExePath() {
    wchar_t buf[MAX_PATH]{};                        // ����� ����
    GetModuleFileNameW(nullptr, buf, MAX_PATH);     // ��������� ���� exe
    return wstring(buf);                            // ���������� std::wstring
}

// ����������� � ������ �������� ����������
static wstring NormalizeOutputPath(const optional<wstring>& outArg, const wstring& exePath) {
    wchar_t cur[MAX_PATH]{};                        
    GetCurrentDirectoryW(MAX_PATH, cur);            // ������� ����������
    wstring out;
    if (!outArg.has_value()) out = wstring(cur);    // ���� ��� --output -> CWD
    else {
        wstring v = outArg.value();                 // �������� ���������
        if (v == L".") {                            // "." -> ���������� exe
            wchar_t exedir[MAX_PATH];
            wcscpy_s(exedir, exePath.c_str());     // �������� exe path
            PathRemoveFileSpecW(exedir);           // ������� ���� exe
            out = exedir;                          // ���������� ����� exe
        }
        else if (PathIsRelativeW(v.c_str())) {     // ������������� ����
            out = wstring(cur) + L"\\" + v;        // ������ ����������
        }
        else out = v;                              // ��� ���������� ����
    }
    CreateDirectoryW(out.c_str(), nullptr);        // ������ ���������� ��� �������������
    return out;                                    // ���������� �������� ����
}

// ��������� ��� ����� � ����������� � �����������
static wstring MakeFilename(const wstring& dir, const wstring& ext) {
    SYSTEMTIME st;
    GetLocalTime(&st);                              // ������� ��������� �����
    wchar_t buf[128];
    swprintf_s(buf, L"%04d-%02d-%02d_%02d-%02d-%02d%s",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext.c_str()); // ������ YYYY-MM-DD_hh-mm-ss.ext
    wstring path = dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L"\\"; // ��������� ���� ���� �����
    path += buf;                                    // ��������� ��� �����
    return path;                                    // ���������� ������ ����
}

// � �������� ��� ������� � ���������� � �������������� stdio
static bool EnsureConsole() {
    if (GetConsoleWindow()) return false;           // ���� ������� ��� ���� � ������ �� ������
    if (!AllocConsole()) return false;              // �������� �������� ����� �������
    FILE* fOut = nullptr;
    freopen_s(&fOut, "CONOUT$", "w", stdout);       // �������������� stdout � �������
    freopen_s(&fOut, "CONOUT$", "w", stderr);       // �������������� stderr
    FILE* fIn = nullptr;
    freopen_s(&fIn, "CONIN$", "r", stdin);          // �������������� stdin
    std::ios::sync_with_stdio(false);               // ��������� ������������� i/o
    SetConsoleOutputCP(CP_UTF8);                    // ������ ������� �������� UTF-8 ��� ������
    SetConsoleCP(CP_UTF8);                          // ������ ������� �������� UTF-8 ��� �����
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);  // ���������� ������������ ������
    if (hOut && hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {          // ������ ������� ����� �������
            mode |= ENABLE_PROCESSED_OUTPUT;       // �������� ����������� ��������� ������
            SetConsoleMode(hOut, mode);            // ���������� ����� �����
        }
    }
    return true;                                    // ������� ���� ��������
}

// ���� ������� ���� �������� ���� ���������� � ��� ������� Enter
static void PauseIfConsoleAllocated(bool allocated) {
    if (!allocated) return;                         // ���� ���c��� �� ���� � �� ���
    Logger::Instance().Info(L"\n������� Enter ��� ������...");      // ����������� ��� ������������
    std::wcout.flush();                             // ���������� �����
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE); // ���������� stdin
    if (hStdin && hStdin != INVALID_HANDLE_VALUE) {
        wchar_t buf[16] = { 0 };
        DWORD read = 0;
        ReadConsoleW(hStdin, buf, (DWORD)(std::size(buf) - 1), &read, nullptr); // ������ wide ����
    }
    else {
        std::wstring dummy;
        std::getline(std::wcin, dummy);            // ����� �� ����������� ����
    }
}

int wmain(int argc, wchar_t** argv) {
    std::wstring parseErr;
    auto opt = CommandLineParser::Parse(argc, argv, parseErr); // ������ ���������
    if (!opt) {
        MessageBoxW(nullptr, parseErr.c_str(), L"������ �������", MB_ICONERROR); // ����� ������ ��� ������ ��������
        return -1;                                   // ��������� � �������
    }

    bool wantConsole = !opt->quiet;                  // ����� ������� ���� �� ����� --quiet
    bool consoleAllocated = false;
    if (wantConsole) consoleAllocated = EnsureConsole(); // ���������� �������

    auto exePath = GetExePath();                     // ���� � ������������ �����

    // �������������� ���������������� ������
    Logger::Instance().InitConsole(wantConsole, opt->verbose); // ��������/��������� ������� � verbose
    Logger::Instance().Info(L"��������� ��������");         // �������� ��������� ���������

    Logger::Instance().Verbose(L"Parse success");            // ������� �������

    HRESULT hr = MFStartup(MF_VERSION);               // ������������� Media Foundation
    if (FAILED(hr)) {
        Logger::Instance().Error(L"MFStartup failed: HRESULT=" + to_wstring((long)hr)); // �������� ������
        PauseIfConsoleAllocated(consoleAllocated);  // ��� Enter 
        return -1;                                  // ������� � �������
    }
    ScopeGuard mfGuard([&] { MFShutdown(); });      // ����������� MFShutdown ��� ������

    DeviceEnumerator de;                            // ������ ��� ������������ ���������

    if (opt->info) {                                // ����� --info: ������ ������ ���������
        auto devices = de.ListDevices();            // �������� ������
        if (devices.empty()) {
            Logger::Instance().Info(L"�� ������� �� ����� ���-������"); 
        }
        else {
            for (size_t i = 0; i < devices.size(); ++i) {
                const auto& d = devices[i];
                if (wantConsole) {                   
                    std::wcout << L"[" << i << L"] " << d.name << L"\n";  // ��� ����������
                    std::wcout << L"  id: " << d.id << L"\n";             // id / ���������� ������
                    std::wcout << L"  vendor: " << (d.vendor.empty() ? L"(unknown)" : d.vendor) << L"\n"; // vendor
                    std::wcout << L"  formats:\n";                        // ��������� ��������
                    for (const auto& f : d.formats) {
                        wchar_t buf[256];
                        swprintf_s(buf, L"    %ux%u @ %u/%u fps subtype: %ls bitDepth:%u\n",
                            f.width, f.height, f.fpsNumerator, f.fpsDenominator,
                            GuidToString(f.subtype).c_str(), f.bitDepth);  // ������ �������
                        std::wcout << buf;                                 // �������� ������
                    }
                    std::wcout << L"\n";
                }
                Logger::Instance().Verbose(L"Device[" + std::to_wstring(i) + L"] " + d.name); 
            }
        }
        PauseIfConsoleAllocated(consoleAllocated);    // �����
        return 0;                                     // ������� ����� ������������
    }

    wstring outDir = NormalizeOutputPath(opt->outputPath, exePath); // ����������� output ����
    int devIdx = opt->deviceId.value_or(0);          // ������ ���������� (�� ��������� 0)

    if (opt->snap) {                                 // ����� --snap: ������� ������
        wstring filePath = MakeFilename(outDir, L".jpg"); // ��� ��������� �����
        FrameGrabber fg(devIdx);                      // ������ ������ FrameGrabber
        std::wstring usedDevName;
        VideoFormatInfo usedFmt{};
        Logger::Instance().Verbose(L"Starting CaptureToJpeg: device=" + to_wstring(devIdx) + L" out=" + filePath);

        HRESULT r = fg.CaptureToJpeg(filePath, 95, &usedDevName, &usedFmt); // �������� ����� �������
        Logger::Instance().Verbose(L"CaptureToJpeg returned HRESULT=" + to_wstring((long)r)); // verbose ���������

        if (FAILED(r)) {
            Logger::Instance().Error(L"CaptureToJpeg failed. HRESULT=" + to_wstring((long)r)); // �������� ������
            PauseIfConsoleAllocated(consoleAllocated);  // �����
            return (int)r;                              // ���������� HRESULT ��� ��� ������
        }

        Logger::Instance().Info(L"���� ���������: " + filePath); // ��������� �� ������
        PauseIfConsoleAllocated(consoleAllocated);      // ����� � �����
        return 0;
    }

    if (opt->capture) {                                     // ����� --capture: ������ �����
        wstring tmpPath = MakeFilename(outDir, L".tmp");    // ��������� ���� ��� SinkWriter
        wstring finalPath = MakeFilename(outDir, L".mp4");  // �������� ����
        VideoRecorder vr(devIdx);                           // ������ VideoRecorder
        std::wstring usedDevName;
        VideoFormatInfo usedFmt{};
        Logger::Instance().Verbose(L"Starting RecordToFile: device=" + to_wstring(devIdx) + L" final=" + finalPath);

        HRESULT r = vr.RecordToFile(tmpPath, finalPath, opt->captureSeconds, &usedDevName, &usedFmt); // ������
        Logger::Instance().Verbose(L"RecordToFile returned HRESULT=" + to_wstring((long)r)); // verbose ���������

        if (FAILED(r)) {
            Logger::Instance().Error(L"RecordToFile failed. HRESULT=" + to_wstring((long)r)); // ��� ������
            DeleteFileW(tmpPath.c_str());                   // ������ ��������� ���� ��� ������
            PauseIfConsoleAllocated(consoleAllocated);      // ����� ���� �����
            return (int)r;                                  // ���������� ��� ������
        }

        Logger::Instance().Info(L"����� ���������: " + finalPath); // ��������� �� ������
        PauseIfConsoleAllocated(consoleAllocated);  // ����� � �����
        return 0;
    }

    Logger::Instance().Info(L"�� ������� ��������. ������� --info, --snap ��� --capture"); // ��������� �� �������������
    PauseIfConsoleAllocated(consoleAllocated);      // ����� 
    return 0;                                       // ����� ��� ������
}