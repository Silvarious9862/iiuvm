#include "CommandLine.h"                // ���������� CmdOptions � �������
#include <string>                       // std::wstring
#include <algorithm>                    // std::transform

// ���������� ������ ������� ������
static std::wstring toLower(const std::wstring& s) {
    std::wstring r = s;                  // �������� ����
    std::transform(r.begin(), r.end(), r.begin(), ::towlower); // ����������� ������ wchar � lower
    return r;                            // ���������� ���������
}

// ������ ���������� ��������� ������
std::optional<CmdOptions> CommandLineParser::Parse(int argc, wchar_t** argv, std::wstring& err) {
    CmdOptions opt;                         // ��������� � ������������ ��������
    for (int i = 1; i < argc; ++i) {        // ���������� argv[0], ��������� ���������
        std::wstring a = toLower(argv[i]);  // �������� �������� � ������� �������� ��� ������������������
        if (a == L"--info") {
            opt.info = true;                // ����� ������������ ���������
        }
        else if (a == L"--snap") {
            opt.snap = true;                // ����� ������ ������ �����
        }
        else if (a == L"--capture") {
            opt.capture = true;             // ����� ������ �����
            if (i + 1 >= argc) {            // ������� �������� (�������) ����� --capture
                err = L"�������� �����: --capture ������� �������� (�������)";
                return std::nullopt;        // ��������� ����� � ���������� nullopt
            }
            opt.captureSeconds = _wtoi(argv[++i]); // ������ ����� ����� �� ���������� ���������
            if (opt.captureSeconds <= 0) {  // ��������� ���������������
                err = L"�������� �������� ��� --capture: ��������� ������������� �����";
                return std::nullopt;        // �������� �������� � ������
            }
        }
        else if (a == L"--output") {
            if (i + 1 >= argc) {            // ������� ���� ����� --output
                err = L"�������� �����: --output ������� �������� (����)";
                return std::nullopt;
            }
            opt.outputPath = argv[++i];     // ��������� ������ ���� (�� ����������� �����)
        }
        else if (a == L"--verbose") {
            opt.verbose = true;             // �������� ��������� �����
        }
        else if (a == L"--quiet") {
            opt.quiet = true;               // ������������ ����� / �� ���������� �������
        }
        else {
            err = L"���������������� ��������: " + std::wstring(argv[i]); // ����������� ����
            return std::nullopt;            // ������ ��������
        }
    }

    int modeCount = (int)opt.info + (int)opt.snap + (int)opt.capture; // ������� ������� �������
    if (modeCount == 0) {                   // �� ���� ����� �� ������
        err = L"�� ������ ����� ������: --info, --snap ��� --capture";
        return std::nullopt;
    }
    if (modeCount > 1) {                    // ������������� ������ ������������
        err = L"������� ������ ���� �����: --info ��� --snap ��� --capture";
        return std::nullopt;
    }
    if (opt.quiet && opt.info) {            // --quiet ����������� � ������������� --info
        err = L"--quiet ����������� � --info";
        return std::nullopt;
    }
    return opt;                             // ���������� �����
}
