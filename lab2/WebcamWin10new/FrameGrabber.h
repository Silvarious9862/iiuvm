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
    HRESULT CaptureToJpeg(const std::wstring& outPath, UINT quality = 95, std::wstring* usedDeviceName = nullptr, VideoFormatInfo* usedFmt = nullptr);
private:
    int deviceIndex_;
};
