#pragma once
#include <mfapi.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

struct VideoFormatInfo {
    UINT32 width;
    UINT32 height;
    UINT32 fpsNumerator;
    UINT32 fpsDenominator;
    GUID subtype;
    UINT32 bitDepth;
};

struct DeviceInfo {
    std::wstring id;
    std::wstring name;
    std::wstring vendor;
    std::vector<VideoFormatInfo> formats;
};
