// MFHelpers.cpp
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00                // ������� Windows 10 API
#endif

#include "MFHelpers.h"                     

#include <windows.h>                       // ������� WinAPI
#include <objbase.h>                       // COM
#include <mfapi.h>                         // Media Foundation
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>

#include <wrl/client.h>                    // ComPtr
#include <shlwapi.h>                       // Path �������
#include <propvarutil.h>                   // PROPVARIANT helpers
#include <comdef.h>                        // _com_error
#include <sstream>                         // stringstream ��� ��������������
#include <iomanip>                         // ������������ ������

#pragma comment(lib, "mfplat.lib")         // �������� ����������� ��������� MF � shlwapi
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;              // ����� ��������� COM

// ����������� GUID � ������ ���� {XXXXXXXX-...}
std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};                  // ����� ��� ������ GUID
    if (0 == StringFromGUID2(g, buf, (int)std::size(buf))) { // ����������� GUID
        return std::wstring(L"{?}");       // ��� ������ ���������� placeholder
    }
    return std::wstring(buf);              // ���������� ��������� �������������
}

// ������ 64-������ ��� 32-������ ������������� ������� �� IMFAttributes
static bool GetAttributeUINT64(IMFAttributes* attr, const GUID& key, UINT64& out) {
    if (!attr) return false;               // ������ �� nullptr
    PROPVARIANT var;
    PropVariantInit(&var);                  // ������������� PROPVARIANT
    HRESULT hr = attr->GetItem(key, &var);  // �������� �������� �� �����
    if (FAILED(hr)) return false;           // ��� �������� ��� ������
    if (var.vt == VT_UI8) out = var.uhVal.QuadPart; // 64-������ ��������
    else if (var.vt == VT_UI4) out = var.ulVal;     // 32-������ ��������
    else { PropVariantClear(&var); return false; }  // ���������������� ���
    PropVariantClear(&var);                 // ������� PROPVARIANT
    return true;                            // ������� ���������
}

// ��������� IMFMediaType � ��������� VideoFormatInfo (������/������/fps/subtype/bitDepth)
void ParseMediaType(IMFMediaType* pType, VideoFormatInfo& out) {
    if (!pType) return;                     // ������ �� nullptr
    UINT32 width = 0, height = 0;
    if (SUCCEEDED(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height))) {
        out.width = width; out.height = height; // ������ ������ �����
    }
    else {
        out.width = out.height = 0;          // ��� ���������� � �������
    }

    UINT32 num = 0, den = 0;
    if (SUCCEEDED(MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &den))) {
        out.fpsNumerator = num; out.fpsDenominator = den; // ������ FPS (num/den)
    }
    else {
        out.fpsNumerator = out.fpsDenominator = 0; // ��� ���������� � �������
    }

    GUID subtype = { 0 };
    if (SUCCEEDED(pType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
        out.subtype = subtype;                // ������ ������ (������ ��������)
    }
    else {
        out.subtype = GUID_NULL;              // ����������� ������
    }

#ifdef MF_MT_BITS_PER_SAMPLE
    UINT32 bitDepth = 0;
    if (SUCCEEDED(pType->GetUINT32(MF_MT_BITS_PER_SAMPLE, &bitDepth))) out.bitDepth = bitDepth; // ��������, ���� ��������
    else out.bitDepth = 0;
#else
    out.bitDepth = 0;                        // ���� ���� �� �������� � 0
#endif
}

// ���������� ���������� ������������ ��������� � ���������� ������ DeviceInfo
static std::vector<DeviceInfo> EnumerateDevicesInternal() {
    std::vector<DeviceInfo> list;            // ���������

    ComPtr<IMFAttributes> spAttr;
    if (FAILED(MFCreateAttributes(&spAttr, 2))) return list; // ������ ��������

    // ����������� ������ ��������������� (�����������)
    if (FAILED(spAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) return list;

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(spAttr.Get(), &ppDevices, &count); // ����������� ����������
    if (FAILED(hr) || count == 0) {
        if (ppDevices) CoTaskMemFree(ppDevices); // ����������� ��� �������������
        return list;                            // ������ ������ ��� ������ ��� ���������� ���������
    }

    for (UINT32 i = 0; i < count; ++i) {
        IMFActivate* act = ppDevices[i];       // ����� IMFActivate ��� i-�� ����������
        DeviceInfo di;                         // ��������� ��� ����������

        WCHAR* friendlyName = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, nullptr))) {
            di.name = friendlyName;            // ������ ���
            CoTaskMemFree(friendlyName);       // ����������� ������, ���������� MF
        }

        WCHAR* symId = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symId, nullptr))) {
            di.id = symId;                     // ����������� ������������� ������ (�������������)
            CoTaskMemFree(symId);
        }

#ifdef MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID
        WCHAR* vendor = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID, &vendor, nullptr))) {
            di.vendor = vendor;                // ������ vendor GUID ��� ������ (���� ��������)
            CoTaskMemFree(vendor);
        }
#else
        (void)0;
#endif

        // ����������� ���������� �������� ����� ��������� �������� �������
        ComPtr<IMFMediaSource> spSource;
        if (SUCCEEDED(act->ActivateObject(IID_PPV_ARGS(&spSource)))) {
            ComPtr<IMFSourceReader> spReader;
            if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(spSource.Get(), nullptr, &spReader))) {
                DWORD idx = 0;
                while (true) {
                    ComPtr<IMFMediaType> spType;
                    HRESULT hr2 = spReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, &spType); // ������ �������� ��� �� �������
                    if (FAILED(hr2)) break;            // ����� ��� ���������� �����
                    VideoFormatInfo vfi{};
                    ParseMediaType(spType.Get(), vfi); // ������ ������ � VideoFormatInfo
                    di.formats.push_back(vfi);         // ��������� ������ � ������ ����������
                    ++idx;
                }
            }
        }

        list.push_back(std::move(di));          // ��������� DeviceInfo � �������������� ������
        act->Release();                         // ���� ������� IMFActivate
    }

    CoTaskMemFree(ppDevices);                   // ����������� ������ IMFActivate*
    return list;                                // ���������� ������ ���������
}

// ��������� ������ � �������� ���������� ����������
std::vector<DeviceInfo> EnumerateDevices() {
    return EnumerateDevicesInternal();
}
