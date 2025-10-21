#include "Logger.h"                 
#include <mutex>                    // std::mutex, lock_guard
#include <windows.h>                // WinAPI (Console, WriteConsole, etc.)
#include <iostream>                 

// ¬нутренн€€ реализаци€ логгера Ч хранит состо€ние и мьютекс
struct Logger::Impl {
    std::mutex mtx;                 // защищает одновременный вывод из потоков
    bool console = true;            // разрешЄн вывод в консоль
    bool verbose = false;           // флаг подробного вывода
    Impl() = default;               // конструктор по умолчанию
};

// ¬озвращает текущий таймстамп в виде строки "YYYY-MM-DD HH:MM:SS"
static std::wstring TimestampNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);              // получаем локальное врем€
    wchar_t buf[64];
    swprintf_s(buf, sizeof(buf) / sizeof(buf[0]), L"%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); // форматируем строку
    return std::wstring(buf);       
}

// ѕытаетс€ безопасно записать wide-строку в консоль/файл/пайп
static void SafeWriteConsoleWide(HANDLE h, const std::wstring& s) {
    if (!h || h == INVALID_HANDLE_VALUE) return; // ничего не делаем при невалидном дескрипторе
    DWORD written = 0;
    if (WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr)) return; // если WriteConsoleW сработал Ч готово

    // ≈сли WriteConsoleW не поддерживаетс€ (например, дескриптор не консоль) Ч конвертим в UTF-8 и пишем через WriteFile
    int needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr); // размер буфера в байтах
    if (needed <= 0) return;           // не удалось посчитать размер
    std::string buf(needed, '\0');     // выдел€ем строку нужного размера
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &buf[0], needed, nullptr, nullptr); // конвертаци€ в UTF-8
    DWORD out = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &out, nullptr); // пишем байты в дескриптор
}

// Singleton: возвращает единственный экземпл€р Logger
Logger& Logger::Instance() {
    static Logger inst;               // статический единственный объект
    return inst;                      // возвращаем ссылку
}

Logger::Logger() : pImpl(new Impl()) {} // конструктор: создаЄм Impl
Logger::~Logger() { delete pImpl; }     // деструктор: удал€ем Impl

// »нициализаци€ поведени€ логгера 
void Logger::InitConsole(bool enableConsole, bool verbose) {
    if (!pImpl) pImpl = new Impl();    // на вс€кий случай создаЄм Impl если нет
    std::lock_guard<std::mutex> g(pImpl->mtx); // блокируем настройки
    pImpl->console = enableConsole;    // включаем/выключаем вывод в консоль
    pImpl->verbose = verbose;          // включаем/выключаем verbose
    if (pImpl->console) {              // если вывод в консоль разрешЄн
        SetConsoleOutputCP(CP_UTF8);   // ставим кодировку вывода UTF-8
        SetConsoleCP(CP_UTF8);         // ставим кодировку ввода UTF-8
    }
}

// »нформационное сообщение
void Logger::Info(const std::wstring& msg) {
    if (!pImpl) return;                // защита от неинициализированного pImpl
    std::lock_guard<std::mutex> g(pImpl->mtx); // синхронизуем доступ
    if (!pImpl->console) return;       // если консоль отключена Ч не выводим
    std::wstring line;
    if (pImpl->verbose) line = TimestampNow() + L" [INFO] " + msg + L"\n"; // с таймстампом при verbose
    else line = msg + L"\n";           // иначе только сообщение
    SafeWriteConsoleWide(GetStdHandle(STD_OUTPUT_HANDLE), line); // вывод в STDOUT
}

// ѕредупреждение
void Logger::Warn(const std::wstring& msg) {
    if (!pImpl) return;
    std::lock_guard<std::mutex> g(pImpl->mtx); // синхронизаци€
    if (!pImpl->console) return;
    std::wstring line = (pImpl->verbose ? (TimestampNow() + L" [WARN] " + msg + L"\n") : (L"[WARN] " + msg + L"\n")); // формат
    SafeWriteConsoleWide(GetStdHandle(STD_OUTPUT_HANDLE), line); // вывод в STDOUT
}

// ќшибка
void Logger::Error(const std::wstring& msg) {
    if (!pImpl) return;
    std::lock_guard<std::mutex> g(pImpl->mtx);
    if (!pImpl->console) return;
    std::wstring line = (pImpl->verbose ? (TimestampNow() + L" [ERROR] " + msg + L"\n") : (L"[ERROR] " + msg + L"\n")); // формат
    SafeWriteConsoleWide(GetStdHandle(STD_ERROR_HANDLE), line); // вывод в STDERR
}

// ѕодробный лог (выводитс€ только при verbose)
void Logger::Verbose(const std::wstring& msg) {
    if (!pImpl) return;
    std::lock_guard<std::mutex> g(pImpl->mtx);
    if (!pImpl->console) return;
    if (!pImpl->verbose) return;       // пропускаем если verbose отключЄн
    std::wstring line = TimestampNow() + L" [VERBOSE] " + msg + L"\n"; // формат с таймстампом
    SafeWriteConsoleWide(GetStdHandle(STD_OUTPUT_HANDLE), line); // вывод в STDOUT
}
