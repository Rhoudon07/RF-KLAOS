#ifndef RF_KLAOS_CAMERA_HARDWARE_H
#define RF_KLAOS_CAMERA_HARDWARE_H

#include "types.h"
#include <string>

// Predeclaraciones de interfaces nativas COM de Windows Media Foundation
struct IMFSourceReader;
struct IMFMediaSource;

namespace rf_klaos {

class CameraHardware {
public:
    CameraHardware();
    ~CameraHardware();

    // PASO 1 REQUERIDO:
    // Detecta directamente en el hardware del equipo si existe al menos una cámara web conectada.
    // Devuelve true si hay cámara disponible, o false si el equipo no tiene cámara.
    static bool detectarCamaraEnEquipo(std::wstring& outCameraName, int& outCameraCount);

    // Inicializa la captura de hardware con la primera cámara disponible
    bool inicializarCamara(int deviceIndex = 0);

    // Captura un fotograma actual de la cámara web y lo convierte a RGB puro
    bool capturarImagen(ImageRGB& outFrame);

    // Libera los recursos de hardware de la cámara
    void cerrarCamara();

    bool estaActiva() const { return m_initialized; }
    std::wstring obtenerNombreCamara() const { return m_cameraName; }
    int obtenerAncho() const { return m_width; }
    int obtenerAlto() const { return m_height; }

private:
    IMFSourceReader* m_reader = nullptr;
    bool m_initialized = false;
    bool m_comInitialized = false;
    bool m_mfInitialized = false;
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    uint32_t m_videoFormatFourCC = 0; // RGB32, RGB24, YUY2 o NV12
    std::wstring m_cameraName;
};

} // namespace rf_klaos

#endif // RF_KLAOS_CAMERA_HARDWARE_H

