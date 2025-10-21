#include "CommandLine.h"
#include <string>
#include <algorithm>

static std::wstring toLower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::towlower);
    return r;
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
            if (i + 1 >= argc) { err = L"Неверный вызов: --capture требует аргумент (секунды)"; return std::nullopt; }
            opt.captureSeconds = _wtoi(argv[++i]);
            if (opt.captureSeconds <= 0) { err = L"Неверный аргумент для --capture: ожидается положительное число"; return std::nullopt; }
        }
        else if (a == L"--output") {
            if (i + 1 >= argc) { err = L"Неверный вызов: --output требует аргумент (путь)"; return std::nullopt; }
            opt.outputPath = argv[++i];
        }
        else if (a == L"--verbose") {
            opt.verbose = true;
        }
        else if (a == L"--quiet") {
            opt.quiet = true;
        }
        else if (a == L"--verbose") {
            // reserved, but parsing here allows future use without error
        }
        else {
            err = L"Неподдерживаемый аргумент: " + std::wstring(argv[i]);
            return std::nullopt;
        }
    }
    int modeCount = (int)opt.info + (int)opt.snap + (int)opt.capture;
    if (modeCount == 0) { err = L"Не указан режим работы: --info, --snap или --capture"; return std::nullopt; }
    if (modeCount > 1) { err = L"Укажите только один режим: --info или --snap или --capture"; return std::nullopt; }
    if (opt.quiet && opt.info) { err = L"--quiet несовместим с --info"; return std::nullopt; }
    return opt;
}
