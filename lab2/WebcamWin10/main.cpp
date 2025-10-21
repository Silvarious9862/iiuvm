#include <windows.h>
#include <string>
#include <vector>
#include <locale>
#include <codecvt>
#include <shlwapi.h>
#include "CommandLine.h"
#include "Logger.h"
#include "DeviceEnumerator.h"
#include "FrameGrabber.h"
#include "VideoRecorder.h"
#include "MFHelpers.h"
#pragma comment(lib, "shlwapi.lib")

using namespace std;

static bool HasConsole() {
    return !!GetConsoleWindow();
}

static wstring GetExePath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return wstring(buf);
}

static wstring NormalizeOutputPath(const optional<wstring>& outArg, const wstring& exePath) {
    wchar_t cur[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cur);
    wstring out;
    if (!outArg.has_value()) out = wstring(cur);
    else {
        wstring v = outArg.value();
        if (v == L".") { wchar_t exedir[MAX_PATH]; wcscpy_s(exedir, exePath.c_str()); PathRemoveFileSpecW(exedir); out = exedir; }
        else if (PathIsRelativeW(v.c_str())) {
            out = wstring(cur) + L"\\" + v;
        }
        else out = v;
    }
    // ensure directory exists
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}

static wstring MakeFilename(const wstring& dir, const wstring& ext) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[128];
    swprintf_s(buf, L"%04d-%02d-%02d_%02d-%02d-%02d%s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext.c_str());
    wstring path = dir;
    if (path.back() != L'\\' && path.back() != L'/') path += L"\\";
    path += buf;
    return path;
}

int wmainCRTStartup() {
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // parse args
    wstring parseErr;
    auto opt = CommandLineParser::Parse(argc, argv, parseErr);
    if (!opt) {
        MessageBoxW(nullptr, parseErr.c_str(), L"Ошибка аргументов", MB_ICONERROR);
        return -1;
    }

    // subsystem=windows: attach console if launched from console or if not quiet
    bool startedWithConsole = HasConsole();
    bool needConsole = startedWithConsole || (!opt->quiet && (!opt->snap && !opt->capture && opt->info));
    if (startedWithConsole) {
        // do nothing, console exists
    }
    else {
        // if not quiet and launched from double-click, we should still show minimal UI
        if (!opt->quiet) {
            // no console, but will show MessageBoxes when needed
        }
    }

    // Init logger
    auto exePath = GetExePath();
    Logger::Instance().Init(exePath, startedWithConsole);

    // Initialize Media Foundation
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        Logger::Instance().LogError(L"MFStartup failed");
        return -1;
    }
    ScopeGuard mfGuard([&] { MFShutdown(); });

    DeviceEnumerator de;
    if (opt->info) {
        auto devices = de.ListDevices();
        // Prepare human-readable text
        wstring text;
        if (devices.empty()) {
            text = L"Устройства не найдены.\n";
        }
        else {
            for (size_t i = 0; i < devices.size(); ++i) {
                const auto& d = devices[i];
                text += L"[" + to_wstring(i) + L"] " + d.name + L"\n";
                text += L"  id: " + d.id + L"\n";
                text += L"  vendor: " + (d.vendor.empty() ? L"(неизвестно)" : d.vendor) + L"\n";
                text += L"  formats:\n";
                for (const auto& f : d.formats) {
                    wchar_t buf[256];
                    swprintf_s(buf, L"    %ux%u @ %u/%u fps subtype: %ls bitDepth:%u\n",
                        f.width, f.height, f.fpsNumerator, f.fpsDenominator, GuidToString(f.subtype).c_str(), f.bitDepth);
                    text += buf;
                }
                text += L"\n";
            }
        }

        if (startedWithConsole) {
            wcout << text;
            Logger::Instance().LogInfo(L"Выполнена команда --info");
        }
        else {
            // show window with text and "Save" button
            HWND hwnd = CreateWindowExW(0, L"STATIC", text.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, nullptr, nullptr);
            // simple message box with save prompt
            int r = MessageBoxW(nullptr, L"Сохранить информацию в файл?", L"Информация об устройствах", MB_YESNO | MB_ICONQUESTION);
            if (r == IDYES) {
                // save to file next to exe
                wchar_t exedir[MAX_PATH];
                wcscpy_s(exedir, exePath.c_str());
                PathRemoveFileSpecW(exedir);
                wstring out = std::wstring(exedir) + L"\\device_info.txt";
                FILE* f = nullptr;
                _wfopen_s(&f, out.c_str(), L"w, ccs=UTF-8");
                if (f) {
                    fwprintf(f, L"%s", text.c_str());
                    fclose(f);
                    Logger::Instance().LogInfo(L"Информация сохранена: " + out);
                }
                else {
                    Logger::Instance().LogError(L"Не удалось сохранить файл: " + out);
                }
            }
        }
        return 0;
    }

    // snap or capture
    // Determine output directory
    wstring outDir = NormalizeOutputPath(opt->outputPath, exePath);

    // If launching from GUI and not quiet -> show modal confirmation before capture
    if (!startedWithConsole && !opt->quiet) {
        int rr = MessageBoxW(nullptr, L"Программа собирается записать данные с камеры. Продолжить?", L"Разрешение на запись", MB_OKCANCEL | MB_ICONWARNING);
        if (rr != IDOK) {
            Logger::Instance().LogInfo(L"Пользователь отменил запись");
            return 0;
        }
    }

    // default device index
    int devIdx = opt->deviceId.value_or(0);

    if (opt->snap) {
        // create filename
        wstring filePath = MakeFilename(outDir, L".jpg");
        FrameGrabber fg(devIdx);
        std::wstring usedDevName;
        VideoFormatInfo usedFmt;
        HRESULT r = fg.CaptureToJpeg(filePath, 95, &usedDevName, &usedFmt);
        if (FAILED(r)) {
            Logger::Instance().LogError(L"Ошибка при сохранении фото. HRESULT=" + to_wstring(r));
            if (startedWithConsole) wcout << L"Ошибка при сохранении фото. Код: " << r << L"\n";
            return (int)r;
        }
        // log details
        wchar_t meta[512];
        swprintf_s(meta, L"Фото записано: %s; Устройство: %s; Разрешение: %u x %u; Время: ",
            filePath.c_str(), usedDevName.c_str(), usedFmt.width, usedFmt.height);
        Logger::Instance().LogInfo(meta);
        if (startedWithConsole) wcout << L"Фото сохранено: " << filePath << L"\n";
        return 0;
    }

    if (opt->capture) {
        // create temporary file then move
        wstring tmpPath = MakeFilename(outDir, L".tmp");
        wstring finalPath = MakeFilename(outDir, L".mp4");
        VideoRecorder vr(devIdx);
        std::wstring usedDevName;
        VideoFormatInfo usedFmt;
        // record
        HRESULT r = vr.RecordToFile(tmpPath, finalPath, opt->captureSeconds, &usedDevName, &usedFmt);
        if (FAILED(r)) {
            Logger::Instance().LogError(L"Ошибка при записи видео. HRESULT=" + to_wstring(r));
            // delete tmp
            DeleteFileW(tmpPath.c_str());
            return (int)r;
        }
        // log details
        wchar_t meta[512];
        swprintf_s(meta, L"Видео записано: %s; Устройство: %s; Длительность: %d с; Разрешение: %u x %u",
            finalPath.c_str(), usedDevName.c_str(), opt->captureSeconds, usedFmt.width, usedFmt.height);
        Logger::Instance().LogInfo(meta);
        if (startedWithConsole) wcout << L"Видео сохранено: " << finalPath << L"\n";
        return 0;
    }

    return 0;
}
