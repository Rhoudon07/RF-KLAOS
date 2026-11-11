#ifndef RF_KLAOS_FACE_ENGINE_H
#define RF_KLAOS_FACE_ENGINE_H

#include "types.h"
#include <vector>

namespace rf_klaos {

class FaceEngine {
public:
    // Convierte una imagen RGB a escala de grises
    static ImageGray convertirAGrises(const ImageRGB& rgb);

    // FUNCIÓN DE DETECCIÓN DE CARA (100% código propio sin librerías externas):
    // Utiliza Imágenes Integrales (Summed-Area Tables), segmentación cromática YCbCr,
    // características geométricas tipo Haar (ojos-mejillas, puente nasal, boca) y simetría bilateral.
    static FaceRect detectarCara(const ImageRGB& frame);

    // Extrae la foto normalizada 64x64 y el vector matemático de características (LBP + Gradientes)
    static bool extraerDescriptorFacial(
        const ImageRGB& frame,
        const FaceRect& faceRect,
        FaceDescriptor& outDescriptor
    );

    // Calcula el porcentaje de similitud (0.0 a 100.0) entre dos rostros
    static float calcularSimilitudRostros(
        const FaceDescriptor& caraActual,
        const AuthorizedPersonRecord& registroBD
    );

    // FUNCIÓN DE COMPARACIÓN CON LA BASE DE DATOS:
    // Compara el rostro detectado en la cámara con todos los registros de la base de datos
    // y determina si se permite o se deniega el acceso.
    static AccessControlResult compararCaraConBaseDeDatos(
        const FaceDescriptor& caraActual,
        const std::vector<AuthorizedPersonRecord>& registrosAutorizados,
        float umbralSimilitud = 74.0f
    );
};

} // namespace rf_klaos

#endif // RF_KLAOS_FACE_ENGINE_H

