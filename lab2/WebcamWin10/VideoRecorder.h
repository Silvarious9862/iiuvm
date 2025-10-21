#pragma once
#include <string>
#include "MFHelpers.h"

class VideoRecorder {
public:
    VideoRecorder(int deviceIndex);
    ~VideoRecorder();

    // Запись в файл длительностью seconds. Возвращает HRESULT и пишет лог.
    HRESULT RecordToFile(const std::wstring& tmpPath, const std::wstring& finalPath, int seconds, std::wstring* usedDeviceName = nullptr, VideoFormatInfo* usedFmt = nullptr);
};
