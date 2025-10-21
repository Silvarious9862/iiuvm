#pragma once
#include <string>
#include <optional>

struct CmdOptions {
    bool info = false;
    bool snap = false;
    bool capture = false;
    bool quiet = false;
    std::optional<std::wstring> outputPath;
    std::optional<int> deviceId;
    int captureSeconds = 0;
};

class CommandLineParser {
public:
    static std::optional<CmdOptions> Parse(int argc, wchar_t** argv, std::wstring& err);
};
