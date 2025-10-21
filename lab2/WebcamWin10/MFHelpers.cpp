#include "MFHelpers.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <shlwapi.h>
#include <sstream>
#include <iomanip>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

// Human-readable GUID to string helper
static std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64];
    StringFromGUID2(g, buf, (int)std::size(buf));
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

// Convert MF frame size and rate values
static void ParseMediaType(IMFMediaType* pType, VideoFormatInfo& out) {
    UINT32 width = 0, height = 0;
    MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
    out.width = width; out.height = height;
    UINT32 num = 0, den = 0;
    MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &den);
    out.fpsNumerator = num; out.fpsDenominator = den;
    GUID subtype;
    MFGetAttributeGUID(pType, MF_MT_SUBTYPE, &subtype);
    out.subtype = subtype;
    // try bit depth
    UINT32 bitDepth = 0;
    if (SUCCEEDED(pType->GetUINT32(MF_MT_BITS_PER_SAMPLE, &bitDepth))) out.bitDepth = bitDepth;
    else out.bitDepth = 0;
}

// Enumerate video capture devices and their formats
static std::vector<DeviceInfo> EnumerateDevicesInternal() {
    std::vector<DeviceInfo> list;
    IMFAttributes* pAttr = nullptr;
    if (FAILED(MFCreateAttributes(&pAttr, 1))) return list;
    ScopeGuard g([&] { if (pAttr) pAttr->Release(); });
    pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    if (FAILED(hr) || count == 0) return list;
    for (UINT32 i = 0; i < count; ++i) {
        IMFActivate* act = ppDevices[i];
        WCHAR* friendlyName = nullptr;
        UINT32 cch = 0;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &cch))) {
            DeviceInfo di;
            di.name = friendlyName;
            CoTaskMemFree(friendlyName);
            // id
            WCHAR* symId = nullptr;
            if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symId, &cch))) {
                di.id = symId;
                CoTaskMemFree(symId);
            }
            // vendor/manufacturer (if available)
            WCHAR* vendor = nullptr;
            if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID, &vendor, &cch))) {
                di.vendor = vendor;
                CoTaskMemFree(vendor);
            }
            // enumerate formats by activating source and querying media types using source reader
            IMFMediaSource* pSource = nullptr;
            if (SUCCEEDED(act->ActivateObject(IID_PPV_ARGS(&pSource)))) {
                IMFSourceReader* pReader = nullptr;
                if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(pSource, nullptr, &pReader))) {
                    // iterate native media types of first stream (stream index 0)
                    DWORD idx = 0;
                    while (true) {
                        IMFMediaType* pType = nullptr;
                        HRESULT hr2 = pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, &pType);
                        if (FAILED(hr2)) break;
                        VideoFormatInfo vfi{};
                        ParseMediaType(pType, vfi);
                        di.formats.push_back(vfi);
                        pType->Release();
                        ++idx;
                    }
                    pReader->Release();
                }
                pSource->Release();
            }
            list.push_back(std::move(di));
        }
        else {
            // fallback: still try to get symbolic link
            WCHAR* symId = nullptr;
            UINT32 cch2 = 0;
            if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symId, &cch2))) {
                DeviceInfo di;
                di.id = symId;
                CoTaskMemFree(symId);
                list.push_back(std::move(di));
            }
        }
        act->Release();
    }
    CoTaskMemFree(ppDevices);
    return list;
}

std::vector<DeviceInfo> EnumerateDevices() {
    return EnumerateDevicesInternal();
}
