#include "VideoRecorder.h"                
#include "Logger.h"                       
#include <mfapi.h>                        // Media Foundation API
#include <mfreadwrite.h>                  // SourceReader / SinkWriter
#include <mfidl.h>                        // MF интерфейсы
#include <comdef.h>                       // _com_error
#include <chrono>                         // измерение времени записи
#include <thread>                         // sleep_for
#pragma comment(lib, "mfplat.lib")        // линковка MF
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
using Microsoft::WRL::ComPtr;             // ComPtr для удобного управления COM-указателями

// Конструктор сохраняет индекс устройства
VideoRecorder::VideoRecorder(int deviceIndex) {}
VideoRecorder::~VideoRecorder() {}

// Основная функция записи видео в файл
HRESULT VideoRecorder::RecordToFile(const std::wstring& tmpPath, const std::wstring& finalPath, int seconds, std::wstring* usedDeviceName, VideoFormatInfo* usedFmt) {
    Logger::Instance().Verbose(L"Starting recording"); 

    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);   // создаём атрибуты для перечисления устройств
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateAttributes failed: " + std::to_wstring((long)hr)); return hr; }
    ScopeGuard gAttr([&] { if (pAttr) pAttr->Release(); }); // релиз атрибутов при выходе

    pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID); // фильтр — видеоустройства
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count); // перечисляем устройства
    if (FAILED(hr) || count == 0) { Logger::Instance().Error(L"MFEnumDeviceSources failed or no devices"); CoTaskMemFree(ppDevices); return E_FAIL; }

    IMFActivate* act = ppDevices[0];               // берём первое устройство
    WCHAR* friendly = nullptr;
    if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, nullptr))) { // читаем имя
        if (usedDeviceName) *usedDeviceName = friendly; // возвращаем имя вызывающему при запросе
        CoTaskMemFree(friendly);                    // освобождаем строку
    }

    ComPtr<IMFMediaSource> spSource;
    hr = act->ActivateObject(IID_PPV_ARGS(&spSource)); // активируем медиавход (камера)
    if (FAILED(hr)) { Logger::Instance().Error(L"ActivateObject failed: " + std::to_wstring((long)hr)); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    ComPtr<IMFAttributes> readerAttr;
    hr = MFCreateAttributes(&readerAttr, 1);       // создаём атрибуты для SourceReader
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateAttributes(reader) failed: " + std::to_wstring((long)hr)); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices); return hr; }
    readerAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE); // разрешаем HW трансформы

    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromMediaSource(spSource.Get(), readerAttr.Get(), &reader); // создаём SourceReader
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateSourceReaderFromMediaSource failed: " + std::to_wstring((long)hr)); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices); return hr; }

    ComPtr<IMFMediaType> pNativeType;
    hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pNativeType); // пробуем получить текущий тип
    if (FAILED(hr) || !pNativeType) {
        hr = reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNativeType); // откат на нативный тип 0
    }
    if (FAILED(hr) || !pNativeType) {           // если не удалось — ошибка
        Logger::Instance().Error(L"Could not get native media type: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    UINT32 width = 0, height = 0;
    MFGetAttributeSize(pNativeType.Get(), MF_MT_FRAME_SIZE, &width, &height); // читаем размеры кадра
    UINT32 num = 0, den = 0;
    MFGetAttributeRatio(pNativeType.Get(), MF_MT_FRAME_RATE, &num, &den);     // читаем частоту кадров
    if (num == 0) { num = 30; den = 1; }                                      // дефолт 30fps при отсутствии данных

    GUID preferredSub = MFVideoFormat_NV12;   // предпочитаемый подтип для записи/кодирования
    ComPtr<IMFMediaType> pTryType;
    hr = MFCreateMediaType(&pTryType);        // пробуем установить NV12 как выход SourceReader
    if (SUCCEEDED(hr)) {
        pTryType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pTryType->SetGUID(MF_MT_SUBTYPE, preferredSub);
        MFSetAttributeSize(pTryType.Get(), MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(pTryType.Get(), MF_MT_FRAME_RATE, num, den);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTryType.Get()); // применяем
    }
    bool usingNV12 = SUCCEEDED(hr);           // флаг, удалось ли установить NV12

    if (!usingNV12) {                         // если NV12 не доступен — пробуем RGB32
        Logger::Instance().Verbose(L"NV12 not available, trying RGB32");
        hr = MFCreateMediaType(&pTryType);
        if (SUCCEEDED(hr)) {
            pTryType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pTryType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            MFSetAttributeSize(pTryType.Get(), MF_MT_FRAME_SIZE, width, height);
            MFSetAttributeRatio(pTryType.Get(), MF_MT_FRAME_RATE, num, den);
            hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTryType.Get()); // применяем RGB32
        }
        if (FAILED(hr)) {                     // если оба варианта не прошли — ошибка
            Logger::Instance().Error(L"Failed to set reader output type to NV12 or RGB32: " + std::to_wstring((long)hr));
            reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
            return hr;
        }
    }

    ComPtr<IMFSinkWriter> sinkWriter;
    std::wstring tmpForSink = tmpPath;
    WCHAR ext[_MAX_EXT]{};                    // буфер для расширения
    _wsplitpath_s(tmpPath.c_str(), nullptr, 0, nullptr, 0, nullptr, 0, ext, _MAX_EXT); // извлекаем расширение tmpPath
    if (_wcsicmp(ext, L".mp4") != 0) {        // если tmpPath не .mp4 — добавляем .mp4 для SinkWriter
        tmpForSink = tmpPath + L".mp4";
    }

    hr = MFCreateSinkWriterFromURL(tmpForSink.c_str(), nullptr, nullptr, &sinkWriter); // создаём SinkWriter
    if (FAILED(hr)) {
        Logger::Instance().Error(L"MFCreateSinkWriterFromURL failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    ComPtr<IMFMediaType> pOutMediaType;
    hr = MFCreateMediaType(&pOutMediaType);   // создаём выходной медиатип для кодирования H.264
    if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateMediaType(out) failed: " + std::to_wstring((long)hr)); return hr; }
    pOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOutMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264); // кодек H.264
    MFSetAttributeSize(pOutMediaType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(pOutMediaType.Get(), MF_MT_FRAME_RATE, num, den);
    pOutMediaType->SetUINT32(MF_MT_AVG_BITRATE, 4000000);      // средний битрейт 4 Mbps
    pOutMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive); // прогрессивная развёртка

    DWORD outStreamIndex = 0;
    hr = sinkWriter->AddStream(pOutMediaType.Get(), &outStreamIndex); // добавляем поток в SinkWriter
    if (FAILED(hr)) {
        Logger::Instance().Error(L"AddStream failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    ComPtr<IMFMediaType> pReaderType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pReaderType); // получаем тип входа от SourceReader
    if (FAILED(hr) || !pReaderType) {
        Logger::Instance().Error(L"GetCurrentMediaType failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    hr = sinkWriter->SetInputMediaType(outStreamIndex, pReaderType.Get(), nullptr); // сообщаем SinkWriter ожидаемый входной тип
    if (FAILED(hr)) {
        Logger::Instance().Error(L"SetInputMediaType failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    hr = sinkWriter->BeginWriting();            // начинаем запись
    if (FAILED(hr)) {
        Logger::Instance().Error(L"BeginWriting failed: " + std::to_wstring((long)hr));
        reader.Reset(); spSource->Shutdown(); spSource.Reset(); act->Release(); CoTaskMemFree(ppDevices);
        return hr;
    }

    auto start = std::chrono::steady_clock::now(); // фиксируем время старта
    while (true) {                               // основной цикл чтения/записи
        ComPtr<IMFSample> pSample;
        DWORD dwStreamIndex = 0, dwFlags = 0;
        LONGLONG llTimeStamp = 0;
        HRESULT r = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &dwStreamIndex, &dwFlags, &llTimeStamp, &pSample); // читаем видео
        if (FAILED(r)) {
            Logger::Instance().Error(L"ReadSample failed during recording: " + std::to_wstring((long)r));
            break;
        }
        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) { // источник сигнализировал завершение
            Logger::Instance().Verbose(L"Source signalled EOS");
            break;
        }
        if (pSample) {                            // если есть видео — пишем в SinkWriter
            hr = sinkWriter->WriteSample(outStreamIndex, pSample.Get());
            if (FAILED(hr)) {
                Logger::Instance().Error(L"WriteSample failed: " + std::to_wstring((long)hr));
                break;
            }
        }
        auto elapsed = std::chrono::steady_clock::now() - start; // проверяем время записи
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= seconds) break; // останов по таймеру
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    hr = sinkWriter->Finalize();                 // финализируем запись (mux, flush)
    if (FAILED(hr)) Logger::Instance().Error(L"Finalize failed: " + std::to_wstring((long)hr));

    reader.Reset();                              // очищаем SourceReader
    spSource->Shutdown();                        // корректно завершаем источник
    spSource.Reset();
    act->Release();                              // релизим IMFActivate
    CoTaskMemFree(ppDevices);                    // освобождаем массив устройств

    // Перемещаем временный файл в финальный путь, с откатом на Copy/Delete
    if (!MoveFileExW(tmpForSink.c_str(), finalPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        // если MoveFileEx не сработал, пробуем Copy+Delete
        if (CopyFileW(tmpForSink.c_str(), finalPath.c_str(), FALSE)) {
            DeleteFileW(tmpForSink.c_str());
        }
        else {
            // как крайний случай, пытаемся переместить исходный tmpPath (на случай, если SinkWriter писал прямо в tmpPath)
            MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
        }
    }


    Logger::Instance().Verbose(L"Recording saved: " + finalPath); 
    return S_OK;                                 // возвращаем успех
}
