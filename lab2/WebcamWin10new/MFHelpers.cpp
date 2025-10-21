// MFHelpers.cpp
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "MFHelpers.h"

#include <windows.h>
#include <objbase.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>

#include <wrl/client.h>
#include <shlwapi.h>
#include <propvarutil.h>
#include <comdef.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

// Human-readable GUID to string helper
std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};
    if (0 == StringFromGUID2(g, buf, (int)std::size(buf))) {
        return std::wstring(L"{?}");
    }
    return std::wstring(buf);
}

// Helper to extract UINT64 attr representing frame size or rate
static bool GetAttributeUINT64(IMFAttributes* attr, const GUID& key, UINT64& out) {
    if (!attr) return false;
    PROPVARIANT var;
    PropVariantInit(&var);
    HRESULT hr = attr->GetItem(key, &var);
    if (FAILED(hr)) return false;
    if (var.vt == VT_UI8) out = var.uhVal.QuadPart;
    else if (var.vt == VT_UI4) out = var.ulVal;
    else { PropVariantClear(&var); return false; }
    PropVariantClear(&var);
    return true;
}

// Convert MF media type -> VideoFormatInfo
void ParseMediaType(IMFMediaType* pType, VideoFormatInfo& out) {
    if (!pType) return;
    UINT32 width = 0, height = 0;
    if (SUCCEEDED(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height))) {
        out.width = width; out.height = height;
    }
    else {
        out.width = out.height = 0;
    }

    UINT32 num = 0, den = 0;
    if (SUCCEEDED(MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &den))) {
        out.fpsNumerator = num; out.fpsDenominator = den;
    }
    else {
        out.fpsNumerator = out.fpsDenominator = 0;
    }

    GUID subtype = { 0 };
    if (SUCCEEDED(pType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
        out.subtype = subtype;
    }
    else {
        out.subtype = GUID_NULL;
    }

    // битовая глубина: если MF_MT_BITS_PER_SAMPLE определён в текущем SDK, используем его,
    // иначе пробуем получить MF_MT_ALL_SAMPLES_INDEPENDENT или пропускаем.
#ifdef MF_MT_BITS_PER_SAMPLE
    UINT32 bitDepth = 0;
    if (SUCCEEDED(pType->GetUINT32(MF_MT_BITS_PER_SAMPLE, &bitDepth))) out.bitDepth = bitDepth;
    else out.bitDepth = 0;
#else
    // SDK не содержит MF_MT_BITS_PER_SAMPLE — пробуем MF_MT_ALL_SAMPLES_INDEPENDENT как индикатор,
    // но точное значение битовой глубины может быть недоступно — устанавливаем 0
    out.bitDepth = 0;
#endif
}

// Enumerate video capture devices and their formats
static std::vector<DeviceInfo> EnumerateDevicesInternal() {
    std::vector<DeviceInfo> list;

    ComPtr<IMFAttributes> spAttr;
    if (FAILED(MFCreateAttributes(&spAttr, 2))) return list;

    // запрос устройств видеозахвата
    if (FAILED(spAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) return list;

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(spAttr.Get(), &ppDevices, &count);
    if (FAILED(hr) || count == 0) {
        if (ppDevices) CoTaskMemFree(ppDevices);
        return list;
    }

    for (UINT32 i = 0; i < count; ++i) {
        IMFActivate* act = ppDevices[i];
        DeviceInfo di;

        // Friendly name
        WCHAR* friendlyName = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, nullptr))) {
            di.name = friendlyName;
            CoTaskMemFree(friendlyName);
        }

        // Symbolic link (id)
        WCHAR* symId = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symId, nullptr))) {
            di.id = symId;
            CoTaskMemFree(symId);
        }

        // vendor/manufacturer — этот атрибут может отсутствовать в некоторых SDK
#ifdef MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID
        WCHAR* vendor = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID, &vendor, nullptr))) {
            di.vendor = vendor;
            CoTaskMemFree(vendor);
        }
#else
        // если атрибута нет в SDK, оставляем vendor пустым
        (void)0;
#endif

        // activate source to enumerate native formats
        ComPtr<IMFMediaSource> spSource;
        if (SUCCEEDED(act->ActivateObject(IID_PPV_ARGS(&spSource)))) {
            ComPtr<IMFSourceReader> spReader;
            if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(spSource.Get(), nullptr, &spReader))) {
                DWORD idx = 0;
                while (true) {
                    ComPtr<IMFMediaType> spType;
                    HRESULT hr2 = spReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, &spType);
                    if (FAILED(hr2)) break;
                    VideoFormatInfo vfi{};
                    ParseMediaType(spType.Get(), vfi);
                    di.formats.push_back(vfi);
                    ++idx;
                }
            }
        }

        list.push_back(std::move(di));
        act->Release();
    }

    CoTaskMemFree(ppDevices);
    return list;
}

std::vector<DeviceInfo> EnumerateDevices() {
    return EnumerateDevicesInternal();
}
