// MFHelpers.cpp
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00                // требуем Windows 10 API
#endif

#include "MFHelpers.h"                     

#include <windows.h>                       // базовый WinAPI
#include <objbase.h>                       // COM
#include <mfapi.h>                         // Media Foundation
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>

#include <wrl/client.h>                    // ComPtr
#include <shlwapi.h>                       // Path утилиты
#include <propvarutil.h>                   // PROPVARIANT helpers
#include <comdef.h>                        // _com_error
#include <sstream>                         // stringstream дл€ форматировани€
#include <iomanip>                         // манипул€торы вывода

#pragma comment(lib, "mfplat.lib")         // линковка необходимых библиотек MF и shlwapi
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;              // умный указатель COM

// ѕреобразует GUID в строку вида {XXXXXXXX-...}
std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};                  // буфер дл€ строки GUID
    if (0 == StringFromGUID2(g, buf, (int)std::size(buf))) { // форматируем GUID
        return std::wstring(L"{?}");       // при ошибке возвращаем placeholder
    }
    return std::wstring(buf);              // возвращаем строковое представление
}

// „итает 64-битный или 32-битный целочисленный атрибут из IMFAttributes
static bool GetAttributeUINT64(IMFAttributes* attr, const GUID& key, UINT64& out) {
    if (!attr) return false;               // защита от nullptr
    PROPVARIANT var;
    PropVariantInit(&var);                  // инициализаци€ PROPVARIANT
    HRESULT hr = attr->GetItem(key, &var);  // получаем значение по ключу
    if (FAILED(hr)) return false;           // нет атрибута или ошибка
    if (var.vt == VT_UI8) out = var.uhVal.QuadPart; // 64-битное значение
    else if (var.vt == VT_UI4) out = var.ulVal;     // 32-битное значение
    else { PropVariantClear(&var); return false; }  // неподдерживаемый тип
    PropVariantClear(&var);                 // очищаем PROPVARIANT
    return true;                            // успешно прочитано
}

// –азбирает IMFMediaType и заполн€ет VideoFormatInfo (ширина/высота/fps/subtype/bitDepth)
void ParseMediaType(IMFMediaType* pType, VideoFormatInfo& out) {
    if (!pType) return;                     // защита от nullptr
    UINT32 width = 0, height = 0;
    if (SUCCEEDED(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height))) {
        out.width = width; out.height = height; // читаем размер кадра
    }
    else {
        out.width = out.height = 0;          // нет информации о размере
    }

    UINT32 num = 0, den = 0;
    if (SUCCEEDED(MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &den))) {
        out.fpsNumerator = num; out.fpsDenominator = den; // читаем FPS (num/den)
    }
    else {
        out.fpsNumerator = out.fpsDenominator = 0; // нет информации о частоте
    }

    GUID subtype = { 0 };
    if (SUCCEEDED(pType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
        out.subtype = subtype;                // читаем подтип (формат пикселей)
    }
    else {
        out.subtype = GUID_NULL;              // неизвестный подтип
    }

#ifdef MF_MT_BITS_PER_SAMPLE
    UINT32 bitDepth = 0;
    if (SUCCEEDED(pType->GetUINT32(MF_MT_BITS_PER_SAMPLE, &bitDepth))) out.bitDepth = bitDepth; // битность, если доступна
    else out.bitDepth = 0;
#else
    out.bitDepth = 0;                        // если ключ не определЄн Ч 0
#endif
}

// ¬нутренн€€ реализаци€ перечислени€ устройств Ч возвращает вектор DeviceInfo
static std::vector<DeviceInfo> EnumerateDevicesInternal() {
    std::vector<DeviceInfo> list;            // результат

    ComPtr<IMFAttributes> spAttr;
    if (FAILED(MFCreateAttributes(&spAttr, 2))) return list; // создаЄм атрибуты

    // ‘ильтрируем только видеоустройства (видеокамеры)
    if (FAILED(spAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) return list;

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(spAttr.Get(), &ppDevices, &count); // перечисл€ем устройства
    if (FAILED(hr) || count == 0) {
        if (ppDevices) CoTaskMemFree(ppDevices); // освобождаем при необходимости
        return list;                            // пустой список при ошибке или отсутствии устройств
    }

    for (UINT32 i = 0; i < count; ++i) {
        IMFActivate* act = ppDevices[i];       // берем IMFActivate дл€ i-го устройства
        DeviceInfo di;                         // структура дл€ заполнени€

        WCHAR* friendlyName = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, nullptr))) {
            di.name = friendlyName;            // читаем им€
            CoTaskMemFree(friendlyName);       // освобождаем строку, выделенную MF
        }

        WCHAR* symId = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symId, nullptr))) {
            di.id = symId;                     // присваиваем символическую ссылку (идентификатор)
            CoTaskMemFree(symId);
        }

#ifdef MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID
        WCHAR* vendor = nullptr;
        if (SUCCEEDED(act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_VENDOR_GUID, &vendor, nullptr))) {
            di.vendor = vendor;                // читаем vendor GUID как строку (если доступно)
            CoTaskMemFree(vendor);
        }
#else
        (void)0;
#endif

        // ќпционально активируем источник чтобы прочитать нативные форматы
        ComPtr<IMFMediaSource> spSource;
        if (SUCCEEDED(act->ActivateObject(IID_PPV_ARGS(&spSource)))) {
            ComPtr<IMFSourceReader> spReader;
            if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(spSource.Get(), nullptr, &spReader))) {
                DWORD idx = 0;
                while (true) {
                    ComPtr<IMFMediaType> spType;
                    HRESULT hr2 = spReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, &spType); // читаем нативный тип по индексу
                    if (FAILED(hr2)) break;            // выход при исчерпании типов
                    VideoFormatInfo vfi{};
                    ParseMediaType(spType.Get(), vfi); // парсим формат в VideoFormatInfo
                    di.formats.push_back(vfi);         // сохран€ем формат в список устройства
                    ++idx;
                }
            }
        }

        list.push_back(std::move(di));          // добавл€ем DeviceInfo в результирующий вектор
        act->Release();                         // €вно релизим IMFActivate
    }

    CoTaskMemFree(ppDevices);                   // освобождаем массив IMFActivate*
    return list;                                // возвращаем список устройств
}

// ѕублична€ обЄртка Ч вызывает внутреннюю реализацию
std::vector<DeviceInfo> EnumerateDevices() {
    return EnumerateDevicesInternal();
}
