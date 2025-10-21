// FrameGrabber.cpp
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00               // требуем Windows 10 API
#endif

#include "FrameGrabber.h"                  
#include "Logger.h"                        
#include "ScopeGuard.h"                    

#include <windows.h>                       // базовый WinAPI
#include <objbase.h>                       // CoCreateInstance и т.п.

#include <mfapi.h>                         // Media Foundation
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>

#include <wrl/client.h>                    // ComPtr
#include <wincodec.h>                      // WIC для записи JPEG
#include <atlbase.h>                       // вспомогательно (COM)
#include <vector>                          // std::vector
#include <sstream>                         // string streams
#include <memory>                          // shared_ptr, unique_ptr

#pragma comment(lib, "mfplat.lib")         // линковка MF и WIC
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;             // умный указатель из COM

// Генерирация имени файла
static std::wstring MakeTimestampFilename(const std::wstring& ext, const std::wstring& outputDir) {
	SYSTEMTIME st;
	GetLocalTime(&st);                      // текущее локальное время
	wchar_t buf[128];
	swprintf_s(buf, L"%04d-%02d-%02d_%02d-%02d-%02d%s",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext.c_str()); // формат имени
	std::wstring filename = buf;            // имя файла
	std::wstring out = outputDir;           // копируем директорию
	if (!out.empty()) {
		if (out.back() != L'\\' && out.back() != L'/') out += L"\\"; // добавляем слэш если нужно
	}
	out += filename;                        // итоговый путь
	return out;
}

FrameGrabber::FrameGrabber(int deviceIndex) : deviceIndex_(deviceIndex) {} // сохраняем индекс устройства
FrameGrabber::~FrameGrabber() {}               // деструктор ничего не делает

// Захват одного кадра и сохранение в JPEG
HRESULT FrameGrabber::CaptureToJpeg(const std::wstring& outPath, UINT quality, std::wstring* usedDeviceName, VideoFormatInfo* usedFmt) {
	Logger::Instance().Verbose(L"Starting capture to JPEG"); 

	IMFAttributes* pAttr = nullptr;
	HRESULT hr = MFCreateAttributes(&pAttr, 1); // создаём атрибуты для перечисления устройств
	if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateAttributes failed: " + std::to_wstring((long)hr)); return hr; }
	ScopeGuard gAttr([&] { if (pAttr) pAttr->Release(); }); // гарантированное освобождение атрибутов

	hr = pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID); // запрос на получение видеокамер
	if (FAILED(hr)) { Logger::Instance().Error(L"SetGUID failed: " + std::to_wstring((long)hr)); return hr; }

	IMFActivate** ppDevices = nullptr;
	UINT32 count = 0;
	hr = MFEnumDeviceSources(pAttr, &ppDevices, &count); // перечисляем устройства
	if (FAILED(hr)) { Logger::Instance().Error(L"MFEnumDeviceSources failed: " + std::to_wstring((long)hr)); return hr; }
	if (count == 0) { CoTaskMemFree(ppDevices); Logger::Instance().Error(L"No devices found"); return E_FAIL; } // нет камер
	if (deviceIndex_ < 0 || deviceIndex_ >= static_cast<int>(count)) { CoTaskMemFree(ppDevices); Logger::Instance().Error(L"Invalid device index"); return E_INVALIDARG; } // неверный индекс

	IMFActivate* act = ppDevices[deviceIndex_]; // выбираем активацию нужного устройства
	ScopeGuard gAct([&] { if (act) act->Release(); CoTaskMemFree(ppDevices); }); // освобождение массива и активации

	WCHAR* friendly = nullptr;
	hr = act->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, nullptr); // получаем имя
	if (SUCCEEDED(hr) && usedDeviceName) *usedDeviceName = friendly; // возвращаем имя
	if (friendly) CoTaskMemFree(friendly);    // освобождаем строку

	ComPtr<IMFMediaSource> spSource;
	hr = act->ActivateObject(IID_PPV_ARGS(&spSource)); // активируем источник (камера)
	if (FAILED(hr)) { Logger::Instance().Error(L"ActivateObject failed: " + std::to_wstring((long)hr)); return hr; }

	ComPtr<IMFSourceReader> spReader;
	hr = MFCreateSourceReaderFromMediaSource(spSource.Get(), nullptr, &spReader); // создаём SourceReader
	if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateSourceReaderFromMediaSource failed: " + std::to_wstring((long)hr)); return hr; }

	ComPtr<IMFMediaType> pTypeOut;
	hr = MFCreateMediaType(&pTypeOut);         // медиатип для запроса формата
	if (FAILED(hr)) { Logger::Instance().Error(L"MFCreateMediaType failed: " + std::to_wstring((long)hr)); return hr; }
	hr = pTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); // ставим major type = video
	if (FAILED(hr)) { Logger::Instance().Error(L"SetGUID major type failed: " + std::to_wstring((long)hr)); return hr; }

	bool chosenRGB32 = false;
	bool chosenRGB24 = false;
	bool chosenNV12 = false;

	// Пробуем получить RGB32 напрямую из SourceReader
	hr = pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) {
		hr = spReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pTypeOut.Get()); // применяем запрос
		if (SUCCEEDED(hr)) {
			chosenRGB32 = true;
			Logger::Instance().Verbose(L"Using RGB32 output from SourceReader");
		}
		else {
			Logger::Instance().Verbose(L"RGB32 not supported");
		}
	}

	// Если RGB32 не доступен — пробуем RGB24
	if (!chosenRGB32) {
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

	// Если ни RGB32 ни RGB24 не прошли — пробуем NV12 и делаем софт-конвертацию
	if (!chosenRGB32 && !chosenRGB24) {
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
	hr = spReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pFinalType); // получаем фактический тип
	if (FAILED(hr)) { Logger::Instance().Error(L"GetCurrentMediaType failed: " + std::to_wstring((long)hr)); return hr; }

	VideoFormatInfo vf{};
	ParseMediaType(pFinalType.Get(), vf);       // парсим ширину/высоту/fps/подтип
	if (usedFmt) *usedFmt = vf;                 // возвращаем формат при запросе
	Logger::Instance().Verbose(L"Selected format: " + std::to_wstring(vf.width) + L"x" + std::to_wstring(vf.height));

	hr = spReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE); // включаем поток чтения
	if (FAILED(hr)) {
		Logger::Instance().Error(L"SetStreamSelection failed: " + std::to_wstring((long)hr));
		return hr;
	}

	const DWORD kTimeoutMs = 5000;              // таймаут ожидания кадра
	const DWORD kPollIntervalMs = 30;           // интервал опроса
	DWORD waited = 0;
	DWORD streamIndex = 0, flags = 0;
	LONGLONG llTimeStamp = 0;
	ComPtr<IMFSample> spSample;

	// Цикл чтения одного кадра с таймаутом
	while (waited < kTimeoutMs) {
		flags = 0;
		spSample.Reset();                        // очищаем предыдущий кадр
		hr = spReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &spSample); // запрашиваем кадр
		if (FAILED(hr)) {
			Logger::Instance().Error(L"ReadSample failed: " + std::to_wstring((long)hr));
			break;
		}
		if (spSample) break;                     // получили кадр — выходим
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			Logger::Instance().Error(L"ReadSample signalled EOS"); // конец потока
			break;
		}
		Sleep(kPollIntervalMs);                  // ждём и повторяем
		waited += kPollIntervalMs;
	}

	if (!spSample) {                             // нет кадра — ошибка
		Logger::Instance().Error(L"No sample received after waiting " + std::to_wstring(waited) + L" ms");
		return E_FAIL;
	}

	ComPtr<IMFMediaBuffer> spBuffer;
	hr = spSample->ConvertToContiguousBuffer(&spBuffer); // получаем непрерывный буфер пикселей
	if (FAILED(hr)) { Logger::Instance().Error(L"ConvertToContiguousBuffer failed: " + std::to_wstring((long)hr)); return hr; }

	BYTE* pData = nullptr; DWORD maxLen = 0, curLen = 0;
	hr = spBuffer->Lock(&pData, &maxLen, &curLen);      // блокируем буфер чтобы получить указатель
	if (FAILED(hr)) { Logger::Instance().Error(L"Buffer Lock failed: " + std::to_wstring((long)hr)); return hr; }
	ScopeGuard gUnlock([&] { if (spBuffer) spBuffer->Unlock(); }); // гарантия разблокировки

	if (vf.width == 0 || vf.height == 0) {              // проверяем размеры кадра
		Logger::Instance().Error(L"Invalid frame dimensions");
		return E_FAIL;
	}

	ComPtr<IWICImagingFactory> spWIC;
	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spWIC)); // создаём WIC фабрику
	if (FAILED(hr)) { Logger::Instance().Error(L"WIC CreateInstance failed: " + std::to_wstring((long)hr)); return hr; }

	GUID finalSub = { 0 };
	pFinalType->GetGUID(MF_MT_SUBTYPE, &finalSub);      // получаем подтип пиксельного формата
	bool useRgb32 = (finalSub == MFVideoFormat_RGB32);
	bool useRgb24 = (finalSub == MFVideoFormat_RGB24);
	bool useNv12 = (finalSub == MFVideoFormat_NV12);

	UINT bpp = useRgb32 ? 4u : 3u;                      // байт на пиксель
	UINT expectedStride = vf.width * bpp;               // ожидаемый stride
	UINT expectedBytes = expectedStride * vf.height;    // ожидаемый размер данных

	Logger::Instance().Verbose(L"Frame buffer info: curLen=" + std::to_wstring(curLen)); // лог длины буфера

	UINT useBytes = (curLen >= expectedBytes) ? expectedBytes : curLen; // сколько байт реально использовать

	WICPixelFormatGUID pixfmt = useRgb32 ? GUID_WICPixelFormat32bppBGR : GUID_WICPixelFormat24bppBGR; // WIC формат

	std::shared_ptr<std::vector<BYTE>> convPtr;   // место для буфера конверсии, если NV12
	if (useNv12) {                                // преобразуем NV12 -> BGR24 в памяти
		Logger::Instance().Verbose(L"NV12 frame captured; converting to BGR24");

		UINT yPlaneSize = vf.width * vf.height;   // размер Y-плоскости
		UINT uvPlaneSize = yPlaneSize / 2;        // размер UV-плоскости
		if (curLen < static_cast<DWORD>(yPlaneSize + uvPlaneSize)) {
			Logger::Instance().Error(L"NV12 buffer too small"); // недостаточно данных
			return E_FAIL;
		}

		UINT bpp_conv = 3;
		UINT stride_conv = vf.width * bpp_conv;  // stride для BGR24
		UINT bytes_conv = stride_conv * vf.height;
		std::vector<BYTE> convBuf(bytes_conv);   // временный буфер для конверсии
		if (convBuf.empty()) {
			Logger::Instance().Error(L"Failed to allocate conversion buffer");
			return E_OUTOFMEMORY;
		}

		const BYTE* yPlane = pData;               // Y плоскость в начале буфера
		const BYTE* uvPlane = pData + yPlaneSize; // UV плоскость сразу после Y

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

				int R = (298 * C + 409 * E + 128) >> 8; // YUV->RGB преобразование
				int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
				int B = (298 * C + 516 * D + 128) >> 8;

				if (R < 0) R = 0; else if (R > 255) R = 255; // обрезаем по диапазону
				if (G < 0) G = 0; else if (G > 255) G = 255;
				if (B < 0) B = 0; else if (B > 255) B = 255;

				outRow[col * 3 + 0] = (BYTE)B;          // порядок BGR
				outRow[col * 3 + 1] = (BYTE)G;
				outRow[col * 3 + 2] = (BYTE)R;
			}
		}

		convPtr = std::make_shared<std::vector<BYTE>>(std::move(convBuf)); // держим буфер в shared_ptr
		pData = convPtr->data();                    // перенаправляем pData на новый буфер
		curLen = static_cast<DWORD>(bytes_conv);
		useBytes = bytes_conv;
		useNv12 = false;                            // теперь у нас BGR24
		useRgb24 = true;
		useRgb32 = false;
		expectedStride = stride_conv;
		expectedBytes = bytes_conv;
		pixfmt = GUID_WICPixelFormat24bppBGR;

		ScopeGuard holdConv([convPtr]() {});        // удерживаем convPtr до выхода из функции
		Logger::Instance().Verbose(L"Conversion done");
	}

	ComPtr<IWICBitmap> spBitmap;
	bool createdFromMemory = false;
	if (!useNv12) {                                // пытаемся создать WIC bitmap напрямую из памяти
		hr = spWIC->CreateBitmapFromMemory(vf.width, vf.height, pixfmt, expectedStride, useBytes, pData, &spBitmap);
		if (SUCCEEDED(hr)) {
			createdFromMemory = true;
			Logger::Instance().Verbose(L"CreateBitmapFromMemory succeeded");
		}
		else {
			Logger::Instance().Verbose(L"CreateBitmapFromMemory failed, fallback will be used");
		}
	}

	if (!createdFromMemory) {                      // откат: создать пустую bitmap и скопировать данные
		hr = spWIC->CreateBitmap(vf.width, vf.height, pixfmt, WICBitmapCacheOnLoad, &spBitmap);
		if (FAILED(hr)) {
			Logger::Instance().Error(L"Fallback CreateBitmap failed");
			return hr;
		}
		ComPtr<IWICBitmapLock> lock;
		WICRect rect = { 0,0,(INT)vf.width,(INT)vf.height };
		hr = spBitmap->Lock(&rect, WICBitmapLockWrite, &lock); // блокируем для записи
		if (SUCCEEDED(hr)) {
			UINT cbBufferSize = 0;
			BYTE* pv = nullptr;
			hr = lock->GetDataPointer(&cbBufferSize, &pv); // получаем указатель и размер буфера
			if (SUCCEEDED(hr)) {
				UINT toCopy = min(cbBufferSize, useBytes); // копируем безопасное количество байт
				memcpy(pv, pData, toCopy);
				if (cbBufferSize > toCopy) memset(pv + toCopy, 0, cbBufferSize - toCopy); // дополняем нулями если требуется
				lock->Release();                    // освобождаем блокировку
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

	ComPtr<IWICStream> spStream;
	hr = spWIC->CreateStream(&spStream);									// создаём поток для файла
	if (FAILED(hr)) { Logger::Instance().Error(L"CreateStream failed"); return hr; }

	hr = spStream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);	// открываем файл для записи
	if (FAILED(hr)) { Logger::Instance().Error(L"InitializeFromFilename failed"); return hr; }

	ComPtr<IWICBitmapEncoder> spEncoder;
	hr = spWIC->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &spEncoder); // создаём JPEG энкодер
	if (FAILED(hr)) { Logger::Instance().Error(L"CreateEncoder failed"); return hr; }

	hr = spEncoder->Initialize(spStream.Get(), WICBitmapEncoderNoCache);	// инициализируем энкодер
	if (FAILED(hr)) { Logger::Instance().Error(L"Encoder Initialize failed"); return hr; }

	ComPtr<IWICBitmapFrameEncode> spFrame;
	hr = spEncoder->CreateNewFrame(&spFrame, nullptr);						// создаём фрейм для кодирования
	if (FAILED(hr)) { Logger::Instance().Error(L"CreateNewFrame failed"); return hr; }

	hr = spFrame->Initialize(nullptr);										// инициализируем фрейм
	if (FAILED(hr)) { Logger::Instance().Error(L"Frame Initialize failed"); return hr; }

	hr = spFrame->SetSize(vf.width, vf.height);								// задаём размер кадра
	if (FAILED(hr)) { Logger::Instance().Error(L"SetSize failed"); return hr; }

	WICPixelFormatGUID targetFmt = pixfmt;	
	hr = spFrame->SetPixelFormat(&targetFmt);								// заявляем формат пикселей энкодеру
	if (FAILED(hr)) { Logger::Instance().Error(L"SetPixelFormat failed"); return hr; }

	bool needConversion = (targetFmt != pixfmt);							// проверяем, потребует ли энкодер конвертацию
	ComPtr<IWICBitmap> spBitmapToWrite = spBitmap;							// bitmap, которую будем передавать энкодеру
	if (needConversion) {
		Logger::Instance().Verbose(L"Pixel format conversion required by encoder");
		ComPtr<IWICFormatConverter> spConv;
		hr = spWIC->CreateFormatConverter(&spConv);							// создаём конвертер
		if (FAILED(hr)) { Logger::Instance().Error(L"CreateFormatConverter failed"); return hr; }
		hr = spConv->Initialize(spBitmap.Get(), targetFmt, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom); // настраиваем
		if (FAILED(hr)) { Logger::Instance().Error(L"FormatConverter Initialize failed"); return hr; }
		ComPtr<IWICBitmap> spConvBitmap;
		hr = spWIC->CreateBitmapFromSource(spConv.Get(), WICBitmapCacheOnLoad, &spConvBitmap); // создаём bitmap из конвертера
		if (FAILED(hr)) { Logger::Instance().Error(L"CreateBitmapFromSource failed"); return hr; }
		spBitmapToWrite = spConvBitmap;										// используем конвертированную bitmap
	}

	hr = spFrame->WriteSource(spBitmapToWrite.Get(), nullptr);		// пытаемся записать источник напрямую
	if (FAILED(hr)) {
		Logger::Instance().Verbose(L"WriteSource failed, trying WritePixels fallback");
		ComPtr<IWICBitmapLock> lock;
		WICRect rect = { 0,0,(INT)vf.width,(INT)vf.height };
		hr = spBitmapToWrite->Lock(&rect, WICBitmapLockRead, &lock); // блокируем для чтения
		if (FAILED(hr)) { Logger::Instance().Error(L"Bitmap Lock for Read failed"); return hr; }
		UINT cbBufferSize = 0;
		BYTE* pv = nullptr;
		hr = lock->GetDataPointer(&cbBufferSize, &pv);				// получаем указатель на данные
		if (FAILED(hr)) { Logger::Instance().Error(L"GetDataPointer failed"); lock->Release(); return hr; }
		UINT rowStride = expectedStride;
		UINT totalToWrite = min(cbBufferSize, useBytes);
		hr = spFrame->WritePixels(vf.height, rowStride, totalToWrite, pv); // пишем построчно
		lock->Release();
		if (FAILED(hr)) { Logger::Instance().Error(L"WritePixels failed"); return hr; }
	}

	hr = spFrame->Commit();											// подтверждаем фрейм
	if (FAILED(hr)) { Logger::Instance().Error(L"Frame Commit failed"); return hr; }

	hr = spEncoder->Commit();										// подтверждаем энкодер (запись в файл)
	if (FAILED(hr)) { Logger::Instance().Error(L"Encoder Commit failed"); return hr; }

	spFrame.Reset();												// очищаем COM объекты
	spEncoder.Reset();
	spStream.Reset();
	spBitmap.Reset();
	spBitmapToWrite.Reset();

	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (GetFileAttributesExW(outPath.c_str(), GetFileExInfoStandard, &fad)) { // проверяем что файл записан
		Logger::Instance().Verbose(L"Saved image: " + outPath);
		return S_OK;                                
	}
	else {
		DWORD gle = GetLastError();
		Logger::Instance().Error(L"Failed to write file: " + outPath); // лог ошибки файловой системы
		return HRESULT_FROM_WIN32(gle ? gle : ERROR_FILE_NOT_FOUND);   // возвращаем HRESULT из LastError
	}
}
