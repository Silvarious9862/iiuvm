#pragma once
#include <string>
#include <mutex>
#include <fstream>

class Logger {
public:
    enum class Level { Info, Warn, Error, Debug };
    static Logger& Instance();

    void Init(const std::wstring& exePath, bool consolePresent);
    void Log(Level lvl, const std::wstring& msg);
    void LogInfo(const std::wstring& msg) { Log(Level::Info, msg); }
    void LogWarn(const std::wstring& msg) { Log(Level::Warn, msg); }
    void LogError(const std::wstring& msg) { Log(Level::Error, msg); }

private:
    Logger() = default;
    std::mutex mtx_;
    std::wofstream logfile_;
    bool console_ = false;
    std::wstring logPath_;
};
