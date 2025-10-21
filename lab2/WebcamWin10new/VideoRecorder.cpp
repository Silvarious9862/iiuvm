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
    Logger::Instance().Verbose(L"Starting recording");

    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateAttributes failed: " + std::to_wstring((long)hr)); return hr; }
    ScopeGuard gAttr([&] { if (pAttr) pAttr->Release(); });

    pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    if (FAILED(hr) || count == 0) { Logger::Instance().Error(L"MFEnumDeviceSources failed or no devices"); CoTaskMemFree(ppDevices); return E_FAIL; }

    IMFActivate* act = ppDevices[0];
    WCHAR* friendly = nullptr;
    if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, nullptr))) {
        if (usedDeviceName) *usedDeviceName = friendly;
        CoTaskMemFree(friendly);
    }

    ComPtr<IMFMediaSource> spSource;
    hr = act->ActivateObject(IID_PPV_ARGS(&spSource));
    if (FAILED(hr)) { Logger::Instance().Error(L"ActivateObject failed: " + std::to_wstring((long)hr)); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Create SourceReader with attributes (allow video processing)
    ComPtr<IMFAttributes> readerAttr;
    hr = MFCreateAttributes(&readerAttr, 1);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateAttributes(reader) failed: " + std::to_wstring((long)hr)); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    readerAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromMediaSource(spSource.Get(), readerAttr.Get(), &reader);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateSourceReaderFromMediaSource failed: " + std::to_wstring((long)hr)); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    // Query native/current media type and select an input format for sink writer
    ComPtr<IMFMediaType> pNativeType;
    hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pNativeType);
    if (FAILED(hr) || !pNativeType) {
        hr = reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNativeType);
    }
    if (FAILED(hr) || !pNativeType) {
        Logger::Instance().Error(L"Could not get native media type: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    UINT32 width = 0, height = 0;
    MFGetAttributeSize(pNativeType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    UINT32 num = 0, den = 0;
    MFGetAttributeRatio(pNativeType.Get(), MF_MT_FRAME_RATE, &num, &den);
    if (num == 0) { num = 30; den = 1; }

    // Preferred input for H264 encoder is NV12. Try to set reader to NV12; fallback to RGB32.
    GUID preferredSub = MFVideoFormat_NV12;
    ComPtr<IMFMediaType> pTryType;
    hr = MFCreateMediaType(&pTryType);
    if (SUCCEEDED(hr)) {
        pTryType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pTryType->SetGUID(MF_MT_SUBTYPE, preferredSub);
        MFSetAttributeSize(pTryType.Get(), MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(pTryType.Get(), MF_MT_FRAME_RATE, num, den);
        // try to set reader to NV12
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTryType.Get());
    }
    bool usingNV12 = SUCCEEDED(hr);

    if (!usingNV12) {
        Logger::Instance().Verbose(L"NV12 not available, trying RGB32");
        // try RGB32
        hr = MFCreateMediaType(&pTryType);
        if (SUCCEEDED(hr)) {
            pTryType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pTryType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            MFSetAttributeSize(pTryType.Get(), MF_MT_FRAME_SIZE, width, height);
            MFSetAttributeRatio(pTryType.Get(), MF_MT_FRAME_RATE, num, den);
            hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTryType.Get());
        }
        if (FAILED(hr)) {
            Logger::Instance().Error(L"Failed to set reader output type to NV12 or RGB32: " + std::to_wstring((long)hr));
            reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
            return hr;
        }
    }

    // Create sink writer
    ComPtr<IMFSinkWriter> sinkWriter;
    // ensure sink writer target has .mp4 extension so MF picks MP4 muxer/encoder
    std::wstring tmpForSink = tmpPath;
    WCHAR ext[_MAX_EXT]{};
    _wsplitpath_s(tmpPath.c_str(), nullptr, 0, nullptr, 0, nullptr, 0, ext, _MAX_EXT);
    if (_wcsicmp(ext, L".mp4") != 0) {
        // append .mp4 to temporary filename used by SinkWriter
        tmpForSink = tmpPath + L".mp4";
    }

    // create sink writer to tmpForSink
    hr = MFCreateSinkWriterFromURL(tmpForSink.c_str(), nullptr, nullptr, &sinkWriter);

    if (FAILED(hr)) {
        Logger::Instance().Error(L"MFCreateSinkWriterFromURL failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    // Prepare output media type (H.264) using same resolution/framerate
    ComPtr<IMFMediaType> pOutMediaType;
    hr = MFCreateMediaType(&pOutMediaType);
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateMediaType(out) failed: " + std::to_wstring((long)hr)); return hr; }
    pOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOutMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(pOutMediaType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(pOutMediaType.Get(), MF_MT_FRAME_RATE, num, den);
    pOutMediaType->SetUINT32(MF_MT_AVG_BITRATE, 4000000); // 4 Mbps
    pOutMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    DWORD outStreamIndex = 0;
    hr = sinkWriter->AddStream(pOutMediaType.Get(), &outStreamIndex);
    if (FAILED(hr)) {
        Logger::Instance().Error(L"AddStream failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    // Configure input media type for sink writer — must match what reader now outputs
    ComPtr<IMFMediaType> pReaderType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pReaderType);
    if (FAILED(hr) || !pReaderType) {
        Logger::Instance().Error(L"GetCurrentMediaType failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    // Use pReaderType as input type for sink writer (the sink writer will insert required MFTs)
    hr = sinkWriter->SetInputMediaType(outStreamIndex, pReaderType.Get(), nullptr);
    if (FAILED(hr)) {
        Logger::Instance().Error(L"SetInputMediaType failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    hr = sinkWriter->BeginWriting();
    if (FAILED(hr)) {
        Logger::Instance().Error(L"BeginWriting failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    // Recording loop: read samples and write to sinkWriter
    auto start = std::chrono::steady_clock::now();
    while (true) {
        ComPtr<IMFSample> pSample;
        DWORD dwStreamIndex = 0, dwFlags = 0;
        LONGLONG llTimeStamp = 0;
        HRESULT r = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &dwStreamIndex, &dwFlags, &llTimeStamp, &pSample);
        if (FAILED(r)) {
            Logger::Instance().Error(L"ReadSample failed during recording: " + std::to_wstring((long)r));
            break;
        }
        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Logger::Instance().Verbose(L"Source signalled EOS");
            break;
        }
        if (pSample) {
            hr = sinkWriter->WriteSample(outStreamIndex, pSample.Get());
            if (FAILED(hr)) {
                Logger::Instance().Error(L"WriteSample failed: " + std::to_wstring((long)hr));
                break;
            }
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= seconds) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    hr = sinkWriter->Finalize();
    if (FAILED(hr)) Logger::Instance().Error(L"Finalize failed: " + std::to_wstring((long)hr));

    // tidy up
    reader.Reset();
    spSource->Shutdown();
    spSource.Reset();
    act->Release();
    CoTaskMemFree(ppDevices);

    // Move tmp -> final
    if (!MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        CopyFileW(tmpPath.c_str(), finalPath.c_str(), FALSE);
        DeleteFileW(tmpPath.c_str());
    }

    Logger::Instance().Verbose(L"Recording saved: " + finalPath);
    return S_OK;
}
