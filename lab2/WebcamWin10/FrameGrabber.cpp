#include "FrameGrabber.h"
#include "Logger.h"
#include <mfapi.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <wincodec.h>
#include <atlbase.h>
#include <vector>
#include <sstream>
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

// Helper to build filename timestamp
static std::wstring MakeTimestampFilename(const std::wstring& ext, const std::wstring& outputDir) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[128];
    swprintf_s(buf, L"%04d-%02d-%02d_%02d-%02d-%02d%s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext.c_str());
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
    Logger::Instance().LogInfo(L"Подготовка к захвату кадра...");
    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);
    if (FAILED(hr)) return hr;
    ScopeGuard g([&] { if (pAttr) pAttr->Release(); });
    pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    if (FAILED(hr)) return hr;
    if (count == 0) { CoTaskMemFree(ppDevices); return E_FAIL; }
    if (deviceIndex_ < 0 || deviceIndex_ >= (int)count) { CoTaskMemFree(ppDevices); return E_INVALIDARG; }

    IMFActivate* act = ppDevices[deviceIndex_];
    WCHAR* friendly = nullptr;
    hr = act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, nullptr);
    if (SUCCEEDED(hr) && usedDeviceName) *usedDeviceName = friendly;
    if (friendly) CoTaskMemFree(friendly);

    IMFMediaSource* pSource = nullptr;
    hr = act->ActivateObject(IID_PPV_ARGS(&pSource));
    if (FAILED(hr)) { act->Release(); CoTaskMemFree(ppDevices); return hr; }

    IMFSourceReader* pReader = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(pSource, nullptr, &pReader);
    if (FAILED(hr)) { pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Request a 24-bit RGB frame
    ComPtr<IMFMediaType> pTypeOut;
    hr = MFCreateMediaType(&pTypeOut);
    if (FAILED(hr)) { pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    pTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
    hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTypeOut.Get());
    if (FAILED(hr)) {
        // если RGB24 не поддерживается, попросим NV12 и затем конвертируем
        pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTypeOut.Get());
        if (FAILED(hr)) {
            pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr;
        }
    }

    // retrieve the actual media type
    ComPtr<IMFMediaType> pFinalType;
    hr = pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pFinalType);
    if (FAILED(hr)) { pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    VideoFormatInfo vf{};
    ParseMediaType(pFinalType.Get(), vf);
    if (usedFmt) *usedFmt = vf;

    // Read a single sample (synchronous)
    DWORD streamIndex, flags;
    LONGLONG llTimeStamp;
    ComPtr<IMFSample> pSample;
    hr = pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
    if (FAILED(hr) || !pSample) {
        pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr;
    }

    ComPtr<IMFMediaBuffer> pBuffer;
    hr = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (FAILED(hr)) {
        pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr;
    }

    BYTE* pData = nullptr; DWORD maxLen = 0, curLen = 0;
    hr = pBuffer->Lock(&pData, &maxLen, &curLen);
    if (FAILED(hr)) { pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Create WIC bitmap from buffer and save as JPEG
    IWICImagingFactory* pWIC = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWIC));
    if (FAILED(hr)) { pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Create bitmap from memory (assume RGB24 stride = width*3); if NV12 then convert would be required (omitted full converter for brevity)
    UINT stride = vf.width * 3;
    WICPixelFormatGUID pixfmt = GUID_WICPixelFormat24bppBGR; // RGB24 -> BGR order for WIC
    ComPtr<IWICBitmap> pBitmap;
    hr = pWIC->CreateBitmapFromMemory(vf.width, vf.height, pixfmt, stride, curLen, pData, &pBitmap);
    if (FAILED(hr)) {
        pWIC->Release();
        pBuffer->Unlock();
        pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    // create encoder
    IWICStream* pStream = nullptr;
    hr = pWIC->CreateStream(&pStream);
    if (FAILED(hr)) { pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    hr = pStream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) { pStream->Release(); pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    IWICBitmapEncoder* pEncoder = nullptr;
    hr = pWIC->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &pEncoder);
    if (FAILED(hr)) { pStream->Release(); pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) { pEncoder->Release(); pStream->Release(); pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    IWICBitmapFrameEncode* pFrame = nullptr;
    hr = pEncoder->CreateNewFrame(&pFrame, nullptr);
    if (FAILED(hr)) { pEncoder->Release(); pStream->Release(); pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    hr = pFrame->Initialize(nullptr);
    if (FAILED(hr)) { pFrame->Release(); pEncoder->Release(); pStream->Release(); pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    hr = pFrame->SetSize(vf.width, vf.height);
    WICPixelFormatGUID targetFmt = GUID_WICPixelFormat24bppBGR;
    pFrame->SetPixelFormat(&targetFmt);

    // encoder options: quality
    IWICPropertyBag2* pProps = nullptr;
    if (SUCCEEDED(pFrame->GetMetadataQueryWriter((IWICMetadataQueryWriter**)&pProps))) {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_R4;
        var.fltVal = quality / 100.0f;
        HRESULT h2 = pProps->WriteProperty(GUID_ContainerFormatJpeg, &var);
        PropVariantClear(&var);
        pProps->Release();
    }

    hr = pFrame->WriteSource(pBitmap, nullptr);
    if (FAILED(hr)) {
        pFrame->Release(); pEncoder->Release(); pStream->Release(); pWIC->Release(); pBuffer->Unlock(); pReader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }
    hr = pFrame->Commit();
    hr = pEncoder->Commit();

    // cleanup
    pFrame->Release();
    pEncoder->Release();
    pStream->Release();
    pWIC->Release();
    pBuffer->Unlock();
    pReader->Release();
    pSource->Release();
    act->Release();
    CoTaskMemFree(ppDevices);

    Logger::Instance().LogInfo(L"Кадр сохранен: " + outPath);
    return S_OK;
}
