#pragma once
#include <string>
#include "MFHelpers.h"

class FrameGrabber {
public:
    FrameGrabber(int deviceIndex);
    ~FrameGrabber();
    // ������ ������ ����� � RGB24, ���������� jpeg (quality 95)
    HRESULT CaptureToJpeg(const std::wstring& outPath, UINT quality = 95, std::wstring* usedDeviceName = nullptr, VideoFormatInfo* usedFmt = nullptr);
private:
    int deviceIndex_;
};
