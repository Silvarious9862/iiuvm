#include "CommandLine.h"
#include <string>
#include <algorithm>

static std::wstring toLower(const std::wstring& s) {
    std::wstring r = s; std::transform(r.begin(), r.end(), r.begin(), ::towlower); return r;
}

std::optional<CmdOptions> CommandLineParser::Parse(int argc, wchar_t** argv, std::wstring& err) {
    CmdOptions opt;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = toLower(argv[i]);
        if (a == L"--info") {
            opt.info = true;
        }
        else if (a == L"--snap") {
            opt.snap = true;
        }
        else if (a == L"--capture") {
            opt.capture = true;
            if (i + 1 >= argc) { err = L"Аргумент --capture требует значение длительности"; return std::nullopt; }
            opt.captureSeconds = _wtoi(argv[++i]);
            if (opt.captureSeconds <= 0) { err = L"Неверная длительность для --capture"; return std::nullopt; }
        }
        else if (a == L"--output") {
            if (i + 1 >= argc) { err = L"Аргумент --output требует путь"; return std::nullopt; }
            opt.outputPath = argv[++i];
        }
        else if (a == L"--device") {
            if (i + 1 >= argc) { err = L"Аргумент --device требует id"; return std::nullopt; }
            opt.deviceId = std::stoi(argv[++i]);
        }
        else if (a == L"--quiet") {
            opt.quiet = true;
        }
        else {
            err = L"Неизвестный аргумент: " + std::wstring(argv[i]);
            return std::nullopt;
        }
    }
    int modeCount = (int)opt.info + (int)opt.snap + (int)opt.capture;
    if (modeCount == 0) { err = L"Укажите одну из команд: --info, --snap, --capture"; return std::nullopt; }
    if (modeCount > 1) { err = L"Одновременный запуск более чем одной из команд --info, --snap, --capture недопустим"; return std::nullopt; }
    if (opt.quiet && opt.info) { err = L"--quiet нельзя использовать с --info"; return std::nullopt; }
    return opt;
}
