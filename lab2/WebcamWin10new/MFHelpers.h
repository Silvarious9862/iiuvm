#pragma once

// Windows / COM / MF base includes Ч об€зательно в этом пор€дке
#include <windows.h>
#include <objbase.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>

#include <shlwapi.h>
#include <wincodec.h>

#include <wrl/client.h>
#include <string>
#include <vector>

#include "ScopeGuard.h"

// “ипы, используемые в проекте
struct VideoFormatInfo {
    UINT32 width{};
    UINT32 height{};
    UINT32 fpsNumerator{};
    UINT32 fpsDenominator{};
    GUID subtype{};
    UINT32 bitDepth{};
};

struct DeviceInfo {
    std::wstring id;
    std::wstring name;
    std::wstring vendor;
    std::vector<VideoFormatInfo> formats;
};

// Ёкспортируемые функции/объекты
std::vector<DeviceInfo> EnumerateDevices();
std::wstring GuidToString(const GUID& g);
void ParseMediaType(IMFMediaType* pType, VideoFormatInfo& out);
