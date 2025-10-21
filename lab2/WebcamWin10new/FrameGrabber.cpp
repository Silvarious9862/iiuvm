// FrameGrabber.cpp
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "FrameGrabber.h"
#include "Logger.h"
#include "ScopeGuard.h"

#include <windows.h>
#include <objbase.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>

#include <wrl/client.h>
#include <wincodec.h>
#include <atlbase.h>
#include <vector>
#include <sstream>
#include <memory>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

// Helper to build filename timestamp (unused here but left for compatibility)
static std::wstring MakeTimestampFilename(const std::wstring& ext, const std::wstring& outputDir) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[128];
    swprintf_s(buf, L"%04d-%02d-%02d_%02d-%02d-%02d%s",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext.c_str());
    std::wstring filename = buf;
    std::wstring out = outputDir;
    if (!out.empty()) {
        if (out.back() != L'\\' && out.back() != L'/') out += L"\\";
    }
    out += filename;
    return out;
}

FrameGrabber::FrameGrabber(int deviceIndex) : deviceIndex_(deviceIndex) {}
FrameGrabber::~FrameGrabber() {}

HRESULT FrameGrabber::CaptureToJpeg(const std::wstring& outPath, UINT quality, std::wstring* usedDeviceName, VideoFormatInfo* usedFmt) {
    Logger::Instance().Verbose(L"Starting capture to JPEG");

    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateAttributes failed: " + std::to_wstring((long)hr)); return hr; }
    ScopeGuard gAttr([&] { if (pAttr) pAttr->Release(); });

    hr = pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { Logger::Instance().Error(L"SetGUID failed: " + std::to_wstring((long)hr)); return hr; }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFEnumDeviceSources failed: " + std::to_wstring((long)hr)); return hr; }
    if (count == 0) { CoTaskMemFree(ppDevices); Logger::Instance().Error(L"No devices found"); return E_FAIL; }
    if (deviceIndex_ < 0 || deviceIndex_ >= static_cast<int>(count)) { CoTaskMemFree(ppDevices); Logger::Instance().Error(L"Invalid device index"); return E_INVALIDARG; }

    IMFActivate* act = ppDevices[deviceIndex_];
    ScopeGuard gAct([&] { if (act) act->Release(); CoTaskMemFree(ppDevices); });

    WCHAR* friendly = nullptr;
    hr = act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, nullptr);
    if (SUCCEEDED(hr) && usedDeviceName) *usedDeviceName = friendly;
    if (friendly) CoTaskMemFree(friendly);

    ComPtr<IMFMediaSource> spSource;
    hr = act->ActivateObject(IID_PPV_ARGS(&spSource));
    if (FAILED(hr)) { Logger::Instance().Error(L"ActivateObject failed: " + std::to_wstring((long)hr)); return hr; }

    // We want a SourceReader with default attributes.
    ComPtr<IMFSourceReader> spReader;
    hr = MFCreateSourceReaderFromMediaSource(spSource.Get(), nullptr, &spReader);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateSourceReaderFromMediaSource failed: " + std::to_wstring((long)hr)); return hr; }

    // Prefer RGB32 (one-plane BGRX), fall back to RGB24, then NV12
    ComPtr<IMFMediaType> pTypeOut;
    hr = MFCreateMediaType(&pTypeOut);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateMediaType failed: " + std::to_wstring((long)hr)); return hr; }
    hr = pTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) { Logger::Instance().Error(L"SetGUID major type failed: " + std::to_wstring((long)hr)); return hr; }

    bool chosenRGB32 = false;
    bool chosenRGB24 = false;
    bool chosenNV12 = false;

    // Try RGB32
    hr = pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) {
        hr = spReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTypeOut.Get());
        if (SUCCEEDED(hr)) {
            chosenRGB32 = true;
            Logger::Instance().Verbose(L"Using RGB32 output from SourceReader");
        }
        else {
            Logger::Instance().Verbose(L"RGB32 not supported");
        }
    }

    if (!chosenRGB32) {
        // Try RGB24
        hr = pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
        if (SUCCEEDED(hr)) {
            hr = spReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTypeOut.Get());
            if (SUCCEEDED(hr)) {
                chosenRGB24 = true;
                Logger::Instance().Verbose(L"Using RGB24 output from SourceReader");
            }
            else {
                Logger::Instance().Verbose(L"RGB24 not supported");
            }
        }
    }

    if (!chosenRGB32 && !chosenRGB24) {
        // Try NV12 as last resort
        hr = pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr)) {
            hr = spReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTypeOut.Get());
            if (SUCCEEDED(hr)) {
                chosenNV12 = true;
                Logger::Instance().Verbose(L"Using NV12 fallback from SourceReader");
            }
            else {
                Logger::Instance().Error(L"NV12 fallback not supported");
                return hr;
            }
        }
        else {
            Logger::Instance().Error(L"Failed to set NV12 subtype");
            return hr;
        }
    }

    ComPtr<IMFMediaType> pFinalType;
    hr = spReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pFinalType);
    if (FAILED(hr)) { Logger::Instance().Error(L"GetCurrentMediaType failed: " + std::to_wstring((long)hr)); return hr; }

    VideoFormatInfo vf{};
    ParseMediaType(pFinalType.Get(), vf);
    if (usedFmt) *usedFmt = vf;
    Logger::Instance().Verbose(L"Selected format: " + std::to_wstring(vf.width) + L"x" + std::to_wstring(vf.height));

    // Ensure video stream selected
    hr = spReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
        Logger::Instance().Error(L"SetStreamSelection failed: " + std::to_wstring((long)hr));
        return hr;
    }

    // Read sample loop: some cameras need warm-up
    const DWORD kTimeoutMs = 5000;
    const DWORD kPollIntervalMs = 30;
    DWORD waited = 0;
    DWORD streamIndex = 0, flags = 0;
    LONGLONG llTimeStamp = 0;
    ComPtr<IMFSample> spSample;

    while (waited < kTimeoutMs) {
        flags = 0;
        spSample.Reset();
        hr = spReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &spSample);
        if (FAILED(hr)) {
            Logger::Instance().Error(L"ReadSample failed: " + std::to_wstring((long)hr));
            break;
        }
        if (spSample) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Logger::Instance().Error(L"ReadSample signalled EOS");
            break;
        }
        Sleep(kPollIntervalMs);
        waited += kPollIntervalMs;
    }

    if (!spSample) {
        Logger::Instance().Error(L"No sample received after waiting " + std::to_wstring(waited) + L" ms");
        return E_FAIL;
    }

    ComPtr<IMFMediaBuffer> spBuffer;
    hr = spSample->ConvertToContiguousBuffer(&spBuffer);
    if (FAILED(hr)) { Logger::Instance().Error(L"ConvertToContiguousBuffer failed: " + std::to_wstring((long)hr)); return hr; }

    BYTE* pData = nullptr; DWORD maxLen = 0, curLen = 0;
    hr = spBuffer->Lock(&pData, &maxLen, &curLen);
    if (FAILED(hr)) { Logger::Instance().Error(L"Buffer Lock failed: " + std::to_wstring((long)hr)); return hr; }
    ScopeGuard gUnlock([&] { if (spBuffer) spBuffer->Unlock(); });

    if (vf.width == 0 || vf.height == 0) {
        Logger::Instance().Error(L"Invalid frame dimensions");
        return E_FAIL;
    }

    // Create WIC imaging factory
    ComPtr<IWICImagingFactory> spWIC;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spWIC));
    if (FAILED(hr)) { Logger::Instance().Error(L"WIC CreateInstance failed: " + std::to_wstring((long)hr)); return hr; }

    // Determine pixel format and stride
    GUID finalSub = { 0 };
    pFinalType->GetGUID(MF_MT_SUBTYPE, &finalSub);
    bool useRgb32 = (finalSub == MFVideoFormat_RGB32);
    bool useRgb24 = (finalSub == MFVideoFormat_RGB24);
    bool useNv12 = (finalSub == MFVideoFormat_NV12);

    UINT bpp = useRgb32 ? 4u : 3u;
    UINT expectedStride = vf.width * bpp;
    UINT expectedBytes = expectedStride * vf.height;

    Logger::Instance().Verbose(L"Frame buffer info: curLen=" + std::to_wstring(curLen));

    // Only use the first frame worth of bytes
    UINT useBytes = (curLen >= expectedBytes) ? expectedBytes : curLen;

    // Select WIC pixel format
    WICPixelFormatGUID pixfmt = useRgb32 ? GUID_WICPixelFormat32bppBGR : GUID_WICPixelFormat24bppBGR;

    // NV12 software conversion to BGR24 if needed
    std::shared_ptr<std::vector<BYTE>> convPtr; // to hold converted buffer lifetime
    if (useNv12) {
        Logger::Instance().Verbose(L"NV12 frame captured; converting to BGR24");

        UINT yPlaneSize = vf.width * vf.height;
        UINT uvPlaneSize = yPlaneSize / 2;
        if (curLen < static_cast<DWORD>(yPlaneSize + uvPlaneSize)) {
            Logger::Instance().Error(L"NV12 buffer too small");
            return E_FAIL;
        }

        UINT bpp_conv = 3;
        UINT stride_conv = vf.width * bpp_conv;
        UINT bytes_conv = stride_conv * vf.height;
        std::vector<BYTE> convBuf(bytes_conv);
        if (convBuf.empty()) {
            Logger::Instance().Error(L"Failed to allocate conversion buffer");
            return E_OUTOFMEMORY;
        }

        const BYTE* yPlane = pData;
        const BYTE* uvPlane = pData + yPlaneSize;

        for (UINT row = 0; row < vf.height; ++row) {
            const BYTE* yRow = yPlane + row * vf.width;
            const BYTE* uvRow = uvPlane + (row / 2) * vf.width;
            BYTE* outRow = convBuf.data() + row * stride_conv;
            for (UINT col = 0; col < vf.width; ++col) {
                int Y = (int)yRow[col];
                int uvIndex = (col & ~1);
                int U = (int)uvRow[uvIndex + 0];
                int V = (int)uvRow[uvIndex + 1];

                int C = Y - 16;
                int D = U - 128;
                int E = V - 128;

                int R = (298 * C + 409 * E + 128) >> 8;
                int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
                int B = (298 * C + 516 * D + 128) >> 8;

                if (R < 0) R = 0; else if (R > 255) R = 255;
                if (G < 0) G = 0; else if (G > 255) G = 255;
                if (B < 0) B = 0; else if (B > 255) B = 255;

                outRow[col * 3 + 0] = (BYTE)B;
                outRow[col * 3 + 1] = (BYTE)G;
                outRow[col * 3 + 2] = (BYTE)R;
            }
        }

        convPtr = std::make_shared<std::vector<BYTE>>(std::move(convBuf));
        pData = convPtr->data();
        curLen = static_cast<DWORD>(bytes_conv);
        useBytes = bytes_conv;
        useNv12 = false;
        useRgb24 = true;
        useRgb32 = false;
        expectedStride = stride_conv;
        expectedBytes = bytes_conv;
        pixfmt = GUID_WICPixelFormat24bppBGR;

        ScopeGuard holdConv([convPtr]() {});
        Logger::Instance().Verbose(L"Conversion done");
    }

    // Try to create bitmap from memory for RGB24/RGB32 using useBytes and expectedStride
    ComPtr<IWICBitmap> spBitmap;
    bool createdFromMemory = false;
    if (!useNv12) {
        hr = spWIC->CreateBitmapFromMemory(vf.width, vf.height, pixfmt, expectedStride, useBytes, pData, &spBitmap);
        if (SUCCEEDED(hr)) {
            createdFromMemory = true;
            Logger::Instance().Verbose(L"CreateBitmapFromMemory succeeded");
        }
        else {
            Logger::Instance().Verbose(L"CreateBitmapFromMemory failed, fallback will be used");
        }
    }

    if (!createdFromMemory) {
        // Fallback: create empty bitmap and copy only useBytes
        hr = spWIC->CreateBitmap(vf.width, vf.height, pixfmt, WICBitmapCacheOnLoad, &spBitmap);
        if (FAILED(hr)) {
            Logger::Instance().Error(L"Fallback CreateBitmap failed");
            return hr;
        }
        ComPtr<IWICBitmapLock> lock;
        WICRect rect = { 0,0,(INT)vf.width,(INT)vf.height };
        hr = spBitmap->Lock(&rect, WICBitmapLockWrite, &lock);
        if (SUCCEEDED(hr)) {
            UINT cbBufferSize = 0;
            BYTE* pv = nullptr;
            hr = lock->GetDataPointer(&cbBufferSize, &pv);
            if (SUCCEEDED(hr)) {
                UINT toCopy = min(cbBufferSize, useBytes);
                memcpy(pv, pData, toCopy);
                if (cbBufferSize > toCopy) memset(pv + toCopy, 0, cbBufferSize - toCopy);
                lock->Release();
                Logger::Instance().Verbose(L"Copied pixel data into WIC bitmap");
            }
            else {
                Logger::Instance().Error(L"BitmapLock GetDataPointer failed");
                lock->Release();
                return hr;
            }
        }
        else {
            Logger::Instance().Error(L"Bitmap Lock failed");
            return hr;
        }
    }

    // create stream and encoder
    ComPtr<IWICStream> spStream;
    hr = spWIC->CreateStream(&spStream);
    if (FAILED(hr)) { Logger::Instance().Error(L"CreateStream failed"); return hr; }

    hr = spStream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) { Logger::Instance().Error(L"InitializeFromFilename failed"); return hr; }

    ComPtr<IWICBitmapEncoder> spEncoder;
    hr = spWIC->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &spEncoder);
    if (FAILED(hr)) { Logger::Instance().Error(L"CreateEncoder failed"); return hr; }

    hr = spEncoder->Initialize(spStream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) { Logger::Instance().Error(L"Encoder Initialize failed"); return hr; }

    ComPtr<IWICBitmapFrameEncode> spFrame;
    hr = spEncoder->CreateNewFrame(&spFrame, nullptr);
    if (FAILED(hr)) { Logger::Instance().Error(L"CreateNewFrame failed"); return hr; }

    hr = spFrame->Initialize(nullptr);
    if (FAILED(hr)) { Logger::Instance().Error(L"Frame Initialize failed"); return hr; }

    hr = spFrame->SetSize(vf.width, vf.height);
    if (FAILED(hr)) { Logger::Instance().Error(L"SetSize failed"); return hr; }

    WICPixelFormatGUID targetFmt = pixfmt; // prefer to write same format
    hr = spFrame->SetPixelFormat(&targetFmt);
    if (FAILED(hr)) { Logger::Instance().Error(L"SetPixelFormat failed"); return hr; }

    // If WIC requires different targetFmt, perform conversion
    bool needConversion = (targetFmt != pixfmt);
    ComPtr<IWICBitmap> spBitmapToWrite = spBitmap;
    if (needConversion) {
        Logger::Instance().Verbose(L"Pixel format conversion required by encoder");
        ComPtr<IWICFormatConverter> spConv;
        hr = spWIC->CreateFormatConverter(&spConv);
        if (FAILED(hr)) { Logger::Instance().Error(L"CreateFormatConverter failed"); return hr; }
        hr = spConv->Initialize(spBitmap.Get(), targetFmt, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) { Logger::Instance().Error(L"FormatConverter Initialize failed"); return hr; }
        ComPtr<IWICBitmap> spConvBitmap;
        hr = spWIC->CreateBitmapFromSource(spConv.Get(), WICBitmapCacheOnLoad, &spConvBitmap);
        if (FAILED(hr)) { Logger::Instance().Error(L"CreateBitmapFromSource failed"); return hr; }
        spBitmapToWrite = spConvBitmap;
    }

    // Write frame
    hr = spFrame->WriteSource(spBitmapToWrite.Get(), nullptr);
    if (FAILED(hr)) {
        Logger::Instance().Verbose(L"WriteSource failed, trying WritePixels fallback");
        ComPtr<IWICBitmapLock> lock;
        WICRect rect = { 0,0,(INT)vf.width,(INT)vf.height };
        hr = spBitmapToWrite->Lock(&rect, WICBitmapLockRead, &lock);
        if (FAILED(hr)) { Logger::Instance().Error(L"Bitmap Lock for Read failed"); return hr; }
        UINT cbBufferSize = 0;
        BYTE* pv = nullptr;
        hr = lock->GetDataPointer(&cbBufferSize, &pv);
        if (FAILED(hr)) { Logger::Instance().Error(L"GetDataPointer failed"); lock->Release(); return hr; }
        UINT rowStride = expectedStride;
        UINT totalToWrite = min(cbBufferSize, useBytes);
        hr = spFrame->WritePixels(vf.height, rowStride, totalToWrite, pv);
        lock->Release();
        if (FAILED(hr)) { Logger::Instance().Error(L"WritePixels failed"); return hr; }
    }

    hr = spFrame->Commit();
    if (FAILED(hr)) { Logger::Instance().Error(L"Frame Commit failed"); return hr; }

    hr = spEncoder->Commit();
    if (FAILED(hr)) { Logger::Instance().Error(L"Encoder Commit failed"); return hr; }

    // Release resources to flush underlying stream
    spFrame.Reset();
    spEncoder.Reset();
    spStream.Reset();
    spBitmap.Reset();
    spBitmapToWrite.Reset();

    // Diagnostic: check file exists
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(outPath.c_str(), GetFileExInfoStandard, &fad)) {
        Logger::Instance().Verbose(L"Saved image: " + outPath);
        return S_OK;
    }
    else {
        DWORD gle = GetLastError();
        Logger::Instance().Error(L"Failed to write file: " + outPath);
        return HRESULT_FROM_WIN32(gle ? gle : ERROR_FILE_NOT_FOUND);
    }
}
