#pragma once
#include <string>
#include "MFHelpers.h"

class VideoRecorder {
public:
    VideoRecorder(int deviceIndex);
    ~VideoRecorder();

    HRESULT RecordToFile(const std::wstring& tmpPath, const std::wstring& finalPath, int seconds, std::wstring* usedDeviceName = nullptr, VideoFormatInfo* usedFmt = nullptr);
};
