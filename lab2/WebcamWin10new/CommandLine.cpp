#include "CommandLine.h"                // объявление CmdOptions и парсера
#include <string>                       // std::wstring
#include <algorithm>                    // std::transform

// Возвращает нижний регистр строки
static std::wstring toLower(const std::wstring& s) {
    std::wstring r = s;                  // копируем вход
    std::transform(r.begin(), r.end(), r.begin(), ::towlower); // преобразуем каждый wchar в lower
    return r;                            // возвращаем результат
}

// Разбор аргументов командной строки
std::optional<CmdOptions> CommandLineParser::Parse(int argc, wchar_t** argv, std::wstring& err) {
    CmdOptions opt;                         // структура с результатами парсинга
    for (int i = 1; i < argc; ++i) {        // пропускаем argv[0], итерируем аргументы
        std::wstring a = toLower(argv[i]);  // приводим аргумент к нижнему регистру для нечувствительности
        if (a == L"--info") {
            opt.info = true;                // режим перечисления устройств
        }
        else if (a == L"--snap") {
            opt.snap = true;                // режим снятия одного кадра
        }
        else if (a == L"--capture") {
            opt.capture = true;             // режим записи видео
            if (i + 1 >= argc) {            // ожидаем параметр (секунды) после --capture
                err = L"Неверный вызов: --capture требует аргумент (секунды)";
                return std::nullopt;        // ошибочный вызов — возвращаем nullopt
            }
            opt.captureSeconds = _wtoi(argv[++i]); // парсим целое число из следующего аргумента
            if (opt.captureSeconds <= 0) {  // проверяем положительность
                err = L"Неверный аргумент для --capture: ожидается положительное число";
                return std::nullopt;        // неверный аргумент — ошибка
            }
        }
        else if (a == L"--output") {
            if (i + 1 >= argc) {            // ожидаем путь после --output
                err = L"Неверный вызов: --output требует аргумент (путь)";
                return std::nullopt;
            }
            opt.outputPath = argv[++i];     // сохраняем строку пути (не нормализуем здесь)
        }
        else if (a == L"--verbose") {
            opt.verbose = true;             // включаем подробный вывод
        }
        else if (a == L"--quiet") {
            opt.quiet = true;               // минимизируем вывод / не аллоцируем консоль
        }
        else {
            err = L"Неподдерживаемый аргумент: " + std::wstring(argv[i]); // неизвестный флаг
            return std::nullopt;            // ошибка парсинга
        }
    }

    int modeCount = (int)opt.info + (int)opt.snap + (int)opt.capture; // сколько режимов указано
    if (modeCount == 0) {                   // ни один режим не выбран
        err = L"Не указан режим работы: --info, --snap или --capture";
        return std::nullopt;
    }
    if (modeCount > 1) {                    // конфликтующие режимы одновременно
        err = L"Укажите только один режим: --info или --snap или --capture";
        return std::nullopt;
    }
    if (opt.quiet && opt.info) {            // --quiet конфликтует с интерактивным --info
        err = L"--quiet несовместим с --info";
        return std::nullopt;
    }
    return opt;                             // возвращаем опции
}
