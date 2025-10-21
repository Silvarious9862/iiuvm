#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <string>
#include "MFHelpers.h"

class FrameGrabber {
public:
    FrameGrabber(int deviceIndex);
    ~FrameGrabber();
    // захват одного кадра в RGB24, сохранение jpeg (quality 95)
    // usedDeviceName и usedFmt опциональны для получения метаданных захвата
    HRESULT CaptureToJpeg(const std::wstring& outPath, UINT quality = 95, std::wstring* usedDeviceName = nullptr, VideoFormatInfo* usedFmt = nullptr);
private:
    int deviceIndex_;
};
