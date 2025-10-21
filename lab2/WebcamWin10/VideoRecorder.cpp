#include "VideoRecorder.h"
#include "Logger.h"
#include <mfapi.h>
#include <mfreadwrite.h>
#include <mfidl.h>
#include <comdef.h>
#include <chrono>
#include <thread>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
using Microsoft::WRL::ComPtr;

VideoRecorder::VideoRecorder(int deviceIndex) {}
VideoRecorder::~VideoRecorder() {}

HRESULT VideoRecorder::RecordToFile(const std::wstring& tmpPath, const std::wstring& finalPath, int seconds, std::wstring* usedDeviceName, VideoFormatInfo* usedFmt) {
    Logger::Instance().LogInfo(L"Начало записи видео...");
    // Упрощенная реализация на базе Sink Writer
    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);
    if (FAILED(hr)) return hr;
    ScopeGuard g([&] { if (pAttr) pAttr->Release(); });

    pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    if (FAILED(hr) || count == 0) { CoTaskMemFree(ppDevices); return E_FAIL; }
    IMFActivate* act = ppDevices[0];
    WCHAR* friendly = nullptr;
    if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, nullptr))) {
        if (usedDeviceName) *usedDeviceName = friendly;
        CoTaskMemFree(friendly);
    }
    IMFMediaSource* pSource = nullptr;
    hr = act->ActivateObject(IID_PPV_ARGS(&pSource));
    if (FAILED(hr)) { act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Create sink writer for tmpPath (MP4)
    ComPtr<IMFSinkWriter> sinkWriter;
    hr = MFCreateSinkWriterFromURL(tmpPath.c_str(), nullptr, nullptr, &sinkWriter);
    if (FAILED(hr)) {
        pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr;
    }

    // Get source reader and choose media type
    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromMediaSource(pSource, nullptr, &reader);
    if (FAILED(hr)) { pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    ComPtr<IMFMediaType> pNativeType;
    if (FAILED(reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNativeType))) {
        reader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return E_FAIL;
    }

    // Prepare output media type for sinkwriter (H264)
    ComPtr<IMFMediaType> pOutMediaType;
    hr = MFCreateMediaType(&pOutMediaType);
    if (FAILED(hr)) return hr;
    pOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOutMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    // try set 30fps or device fps
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(pNativeType.Get(), MF_MT_FRAME_SIZE, &w, &h);
    pOutMediaType->SetUINT32(MF_MT_AVG_BITRATE, 8000000); // 8 Mbps default
    pOutMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(pOutMediaType.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(pOutMediaType.Get(), MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(pOutMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD streamIndex = 0;
    hr = sinkWriter->AddStream(pOutMediaType.Get(), &streamIndex);
    if (FAILED(hr)) {
        // fallback to MJPEG/AVI not implemented fully, return error
        reader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr;
    }

    // Configure input media type for sink writer (match source)
    ComPtr<IMFMediaType> pInType;
    hr = MFCreateMediaType(&pInType);
    pInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pInType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32); // prefer RGB32
    MFSetAttributeSize(pInType.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(pInType.Get(), MF_MT_FRAME_RATE, 30, 1);
    pInType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    hr = sinkWriter->SetInputMediaType(streamIndex, pInType.Get(), nullptr);
    if (FAILED(hr)) {
        reader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr;
    }

    hr = sinkWriter->BeginWriting();
    if (FAILED(hr)) { reader->Release(); pSource->Release(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Recording loop: read samples and write to sinkWriter
    auto start = std::chrono::steady_clock::now();
    LONGLONG rtStart = 0;
    while (true) {
        ComPtr<IMFSample> pSample;
        DWORD dwStreamIndex = 0, dwFlags = 0;
        LONGLONG llTimeStamp = 0;
        HRESULT r = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &dwStreamIndex, &dwFlags, &llTimeStamp, &pSample);
        if (FAILED(r)) { break; }
        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (pSample) {
            // write sample to sinkwriter
            hr = sinkWriter->WriteSample(streamIndex, pSample.Get());
            if (FAILED(hr)) break;
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= seconds) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    sinkWriter->Finalize();
    reader->Release();
    pSource->Shutdown();
    pSource->Release();
    act->Release();
    CoTaskMemFree(ppDevices);

    // Move tmp -> final
    if (!MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        // if move failed, try Copy+Delete
        CopyFileW(tmpPath.c_str(), finalPath.c_str(), FALSE);
        DeleteFileW(tmpPath.c_str());
    }

    Logger::Instance().LogInfo(L"Запись завершена: " + finalPath);
    return S_OK;
}
