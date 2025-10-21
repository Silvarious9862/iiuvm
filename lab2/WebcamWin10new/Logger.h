#pragma once
#include <string>

class Logger {
public:
    static Logger& Instance();

    void InitConsole(bool enableConsole, bool verbose);

    void Info(const std::wstring& msg);
    void Warn(const std::wstring& msg);
    void Error(const std::wstring& msg);
    void Verbose(const std::wstring& msg); 
private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Impl;
    Impl* pImpl;
};
