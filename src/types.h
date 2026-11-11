#ifndef RF_KLAOS_TYPES_H
#define RF_KLAOS_TYPES_H

#include <stdint.h>
#include <vector>
#include <string>

namespace rf_klaos {

// Dimensiones canónicas de la foto de rostro almacenada en la base de datos
constexpr int FACE_CANONICAL_WIDTH  = 64;
constexpr int FACE_CANONICAL_HEIGHT = 64;
constexpr int FACE_PHOTO_BYTES      = FACE_CANONICAL_WIDTH * FACE_CANONICAL_HEIGHT;

// Tamaño del descriptor matemático de características faciales:
// Cuadrícula espacial 4x4 (16 celdas) x 16 bins de patrones binarios locales (LBP) = 256 valores
// + 64 valores de perfil geométrico/gradiente = 320 flotantes
constexpr int FACE_DESCRIPTOR_SIZE  = 320;

// Imagen en color RGB de 24 bits (3 bytes por píxel: R, G, B)
struct ImageRGB {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // Tamaño: width * height * 3

    bool isValid() const {
        return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width * height * 3);
    }
};

// Imagen en escala de grises de 8 bits (1 byte por píxel: 0..255)
struct ImageGray {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // Tamaño: width * height

    bool isValid() const {
        return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width * height);
    }
};

// Rectángulo delimitador para la detección del rostro
struct FaceRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float confidence = 0.0f;
    bool detected = false;
};

// Vector matemático de características de un rostro
struct FaceDescriptor {
    float features[FACE_DESCRIPTOR_SIZE];
    uint8_t normalizedPhoto[FACE_PHOTO_BYTES]; // Foto 64x64 ecualizada del rostro
};

// Registro de una persona autorizada dentro del archivo de Base de Datos (.db)
struct AuthorizedPersonRecord {
    uint32_t id = 0;
    char name[128] = {0};
    uint64_t registeredTimestamp = 0;
    uint8_t rawFacePhoto[FACE_PHOTO_BYTES] = {0}; // Foto real 64x64 guardada en la BD
    float descriptor[FACE_DESCRIPTOR_SIZE] = {0.0f};
};

// Resultado de comparar un rostro capturado contra la base de datos
struct AccessControlResult {
    bool accessGranted = false;
    uint32_t matchedPersonId = 0;
    std::string matchedPersonName;
    float similarityScore = 0.0f; // 0.0 a 100.0 (%)
    std::string message;
};

} // namespace rf_klaos

#endif // RF_KLAOS_TYPES_H

