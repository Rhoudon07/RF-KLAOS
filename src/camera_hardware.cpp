#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include "camera_hardware.h"
#include <algorithm>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace rf_klaos {

namespace {

inline uint8_t clampByte(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return static_cast<uint8_t>(val);
}

// Conversión matemática propia de YUV (BT.601) a RGB
inline void yuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    int c = static_cast<int>(y) - 16;
    int d = static_cast<int>(u) - 128;
    int e = static_cast<int>(v) - 128;

    r = clampByte((298 * c + 409 * e + 128) >> 8);
    g = clampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = clampByte((298 * c + 516 * d + 128) >> 8);
}

} // namespace

CameraHardware::CameraHardware() = default;

CameraHardware::~CameraHardware() {
    cerrarCamara();
}

bool CameraHardware::detectarCamaraEnEquipo(std::wstring& outCameraName, int& outCameraCount) {
    outCameraName.clear();
    outCameraCount = 0;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool coInitHere = SUCCEEDED(hrCo);

    HRESULT hrMf = MFStartup(MF_VERSION);
    if (FAILED(hrMf)) {
        if (coInitHere) CoUninitialize();
        return false;
    }

    IMFAttributes* pConfig = nullptr;
    HRESULT hr = MFCreateAttributes(&pConfig, 1);
    if (SUCCEEDED(hr)) {
        hr = pConfig->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(hr)) {
        hr = MFEnumDeviceSources(pConfig, &ppDevices, &count);
    }

    if (SUCCEEDED(hr) && count > 0) {
        outCameraCount = static_cast<int>(count);
        WCHAR* friendlyName = nullptr;
        UINT32 nameLen = 0;
        if (SUCCEEDED(ppDevices[0]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &nameLen)) && friendlyName) {
            outCameraName = friendlyName;
            CoTaskMemFree(friendlyName);
        } else {
            outCameraName = L"Cámara Web Genérica";
        }
    }

    if (ppDevices) {
        for (UINT32 i = 0; i < count; ++i) {
            if (ppDevices[i]) ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    }
    if (pConfig) pConfig->Release();

    MFShutdown();
    if (coInitHere) CoUninitialize();

    return (outCameraCount > 0);
}

bool CameraHardware::inicializarCamara(int deviceIndex) {
    cerrarCamara();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr)) {
        m_comInitialized = true;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        cerrarCamara();
        return false;
    }
    m_mfInitialized = true;

    IMFAttributes* pConfig = nullptr;
    hr = MFCreateAttributes(&pConfig, 1);
    if (FAILED(hr)) {
        cerrarCamara();
        return false;
    }

    hr = pConfig->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(hr)) {
        hr = MFEnumDeviceSources(pConfig, &ppDevices, &count);
    }
    pConfig->Release();

    if (FAILED(hr) || count == 0 || deviceIndex < 0 || static_cast<UINT32>(deviceIndex) >= count) {
        if (ppDevices) {
            for (UINT32 i = 0; i < count; ++i) {
                if (ppDevices[i]) ppDevices[i]->Release();
            }
            CoTaskMemFree(ppDevices);
        }
        cerrarCamara();
        return false;
    }

    WCHAR* friendlyName = nullptr;
    UINT32 nameLen = 0;
    if (SUCCEEDED(ppDevices[deviceIndex]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &nameLen)) && friendlyName) {
        m_cameraName = friendlyName;
        CoTaskMemFree(friendlyName);
    } else {
        m_cameraName = L"Cámara Web";
    }

    IMFMediaSource* pSource = nullptr;
    hr = ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&pSource));

    for (UINT32 i = 0; i < count; ++i) {
        if (ppDevices[i]) ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    if (FAILED(hr) || !pSource) {
        cerrarCamara();
        return false;
    }

    IMFAttributes* pReaderAttrs = nullptr;
    MFCreateAttributes(&pReaderAttrs, 1);
    if (pReaderAttrs) {
        pReaderAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttrs, &m_reader);
    if (pReaderAttrs) pReaderAttrs->Release();
    pSource->Release();

    if (FAILED(hr) || !m_reader) {
        cerrarCamara();
        return false;
    }

    // Intentamos configurar formato sin comprimir: primero RGB32, si no YUY2, si no NV12
    const GUID preferredFormats[] = {
        MFVideoFormat_RGB32,
        MFVideoFormat_YUY2,
        MFVideoFormat_NV12,
        MFVideoFormat_RGB24
    };

    bool formatSet = false;
    for (const GUID& subType : preferredFormats) {
        IMFMediaType* pType = nullptr;
        if (SUCCEEDED(MFCreateMediaType(&pType))) {
            pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pType->SetGUID(MF_MT_SUBTYPE, subType);
            MFSetAttributeSize(pType, MF_MT_FRAME_SIZE, 640, 480);
            if (SUCCEEDED(m_reader->SetCurrentMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, pType))) {
                formatSet = true;
                pType->Release();
                break;
            }
            // Si 640x480 directo no es aceptado, probamos solo fijando el subtipo
            pType->DeleteItem(MF_MT_FRAME_SIZE);
            if (SUCCEEDED(m_reader->SetCurrentMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, pType))) {
                formatSet = true;
                pType->Release();
                break;
            }
            pType->Release();
        }
    }

    // Leemos el tipo de medio resultante para conocer resolución y formato exacto
    IMFMediaType* pCurrentType = nullptr;
    hr = m_reader->GetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &pCurrentType);
    if (FAILED(hr) || !pCurrentType) {
        cerrarCamara();
        return false;
    }

    UINT32 w = 0, h = 0;
    MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &w, &h);
    m_width = static_cast<int>(w);
    m_height = static_cast<int>(h);

    GUID subType = GUID_NULL;
    pCurrentType->GetGUID(MF_MT_SUBTYPE, &subType);
    m_videoFormatFourCC = subType.Data1;

    INT32 defaultStride = 0;
    if (SUCCEEDED(pCurrentType->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&defaultStride)))) {
        m_stride = static_cast<int>(defaultStride);
    } else {
        LONG lStride = 0;
        if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(subType.Data1, w, &lStride))) {
            m_stride = static_cast<int>(lStride);
        } else {
            m_stride = m_width * 4;
        }
    }

    pCurrentType->Release();

    if (m_width <= 0 || m_height <= 0 || !formatSet) {
        cerrarCamara();
        return false;
    }

    m_initialized = true;
    return true;
}

bool CameraHardware::capturarImagen(ImageRGB& outFrame) {
    if (!m_initialized || !m_reader) return false;

    // Intentamos leer hasta obtener una muestra de video válida
    for (int attempt = 0; attempt < 15; ++attempt) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG llTimeStamp = 0;
        IMFSample* pSample = nullptr;

        HRESULT hr = m_reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &streamIndex,
            &flags,
            &llTimeStamp,
            &pSample
        );

        if (FAILED(hr)) {
            if (pSample) pSample->Release();
            return false;
        }

        if (!pSample) {
            Sleep(15);
            continue;
        }

        IMFMediaBuffer* pBuffer = nullptr;
        hr = pSample->ConvertToContiguousBuffer(&pBuffer);
        if (FAILED(hr) || !pBuffer) {
            pSample->Release();
            continue;
        }

        BYTE* pData = nullptr;
        DWORD maxLen = 0, currentLen = 0;
        hr = pBuffer->Lock(&pData, &maxLen, &currentLen);
        if (FAILED(hr) || !pData) {
            pBuffer->Release();
            pSample->Release();
            continue;
        }

        outFrame.width = m_width;
        outFrame.height = m_height;
        outFrame.pixels.resize(static_cast<size_t>(m_width * m_height * 3));

        bool decoded = false;
        int absStride = (m_stride < 0) ? -m_stride : m_stride;
        bool bottomUp = (m_stride < 0);

        if (m_videoFormatFourCC == MFVideoFormat_RGB32.Data1) {
            // Formato BGRA (4 bytes por píxel)
            int rowStride = (absStride > 0) ? absStride : (m_width * 4);
            if (currentLen >= static_cast<DWORD>(m_width * m_height * 4)) {
                for (int y = 0; y < m_height; ++y) {
                    int srcY = bottomUp ? (m_height - 1 - y) : y;
                    const BYTE* srcRow = pData + srcY * rowStride;
                    uint8_t* dstRow = &outFrame.pixels[static_cast<size_t>(y * m_width * 3)];
                    // Espejo horizontal (modo espejo natural para cámara frontal)
                    for (int x = 0; x < m_width; ++x) {
                        int mirrorX = m_width - 1 - x;
                        dstRow[mirrorX * 3 + 0] = srcRow[x * 4 + 2]; // R
                        dstRow[mirrorX * 3 + 1] = srcRow[x * 4 + 1]; // G
                        dstRow[mirrorX * 3 + 2] = srcRow[x * 4 + 0]; // B
                    }
                }
                decoded = true;
            }
        } else if (m_videoFormatFourCC == MFVideoFormat_RGB24.Data1) {
            // Formato BGR (3 bytes por píxel)
            int rowStride = (absStride > 0) ? absStride : (m_width * 3);
            if (currentLen >= static_cast<DWORD>(m_width * m_height * 3)) {
                for (int y = 0; y < m_height; ++y) {
                    int srcY = bottomUp ? (m_height - 1 - y) : y;
                    const BYTE* srcRow = pData + srcY * rowStride;
                    uint8_t* dstRow = &outFrame.pixels[static_cast<size_t>(y * m_width * 3)];
                    for (int x = 0; x < m_width; ++x) {
                        int mirrorX = m_width - 1 - x;
                        dstRow[mirrorX * 3 + 0] = srcRow[x * 3 + 2];
                        dstRow[mirrorX * 3 + 1] = srcRow[x * 3 + 1];
                        dstRow[mirrorX * 3 + 2] = srcRow[x * 3 + 0];
                    }
                }
                decoded = true;
            }
        } else if (m_videoFormatFourCC == MFVideoFormat_YUY2.Data1) {
            // Formato YUY2 empaquetado (Y0 U0 Y1 V0 cada 2 píxeles)
            int rowStride = (absStride > 0) ? absStride : (m_width * 2);
            if (currentLen >= static_cast<DWORD>(m_width * m_height * 2)) {
                for (int y = 0; y < m_height; ++y) {
                    const BYTE* srcRow = pData + y * rowStride;
                    uint8_t* dstRow = &outFrame.pixels[static_cast<size_t>(y * m_width * 3)];
                    for (int x = 0; x < m_width; x += 2) {
                        uint8_t y0 = srcRow[x * 2 + 0];
                        uint8_t u  = srcRow[x * 2 + 1];
                        uint8_t y1 = srcRow[x * 2 + 2];
                        uint8_t v  = srcRow[x * 2 + 3];

                        int m0 = m_width - 1 - x;
                        int m1 = m_width - 1 - (x + 1);
                        yuvToRgb(y0, u, v, dstRow[m0 * 3 + 0], dstRow[m0 * 3 + 1], dstRow[m0 * 3 + 2]);
                        if (x + 1 < m_width) {
                            yuvToRgb(y1, u, v, dstRow[m1 * 3 + 0], dstRow[m1 * 3 + 1], dstRow[m1 * 3 + 2]);
                        }
                    }
                }
                decoded = true;
            }
        } else if (m_videoFormatFourCC == MFVideoFormat_NV12.Data1) {
            // Formato NV12 planar (plano Y seguido de plano UV intercalado)
            int rowStride = (absStride > 0) ? absStride : m_width;
            if (currentLen >= static_cast<DWORD>(m_width * m_height * 3 / 2)) {
                const BYTE* yPlane = pData;
                const BYTE* uvPlane = pData + (rowStride * m_height);
                for (int y = 0; y < m_height; ++y) {
                    const BYTE* yRow = yPlane + y * rowStride;
                    const BYTE* uvRow = uvPlane + (y / 2) * rowStride;
                    uint8_t* dstRow = &outFrame.pixels[static_cast<size_t>(y * m_width * 3)];
                    for (int x = 0; x < m_width; ++x) {
                        uint8_t yVal = yRow[x];
                        int uvIdx = (x / 2) * 2;
                        uint8_t uVal = uvRow[uvIdx + 0];
                        uint8_t vVal = uvRow[uvIdx + 1];
                        int mirrorX = m_width - 1 - x;
                        yuvToRgb(yVal, uVal, vVal,
                                 dstRow[mirrorX * 3 + 0],
                                 dstRow[mirrorX * 3 + 1],
                                 dstRow[mirrorX * 3 + 2]);
                    }
                }
                decoded = true;
            }
        }

        pBuffer->Unlock();
        pBuffer->Release();
        pSample->Release();

        if (decoded) {
            return true;
        }
    }

    return false;
}

void CameraHardware::cerrarCamara() {
    if (m_reader) {
        m_reader->Release();
        m_reader = nullptr;
    }
    if (m_mfInitialized) {
        MFShutdown();
        m_mfInitialized = false;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
    m_initialized = false;
}

} // namespace rf_klaos

