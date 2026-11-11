#ifndef RF_KLAOS_FACE_DATABASE_H
#define RF_KLAOS_FACE_DATABASE_H

#include "types.h"
#include <string>
#include <vector>

namespace rf_klaos {

class FaceDatabase {
public:
    explicit FaceDatabase(const std::string& dbFilePath = "rostros_autorizados.db");

    // Carga el archivo de base de datos desde disco (o lo crea si no existe)
    bool cargarBaseDeDatos();

    // Guarda todos los registros (incluyendo las fotos embebidas) en el archivo .db
    bool guardarBaseDeDatos() const;

    // Registra una nueva persona autorizada con su foto y descriptor facial en la BD
    bool registrarPersonaAutorizada(
        const std::string& nombre,
        const FaceDescriptor& descriptor,
        uint32_t& outNewId
    );

    // Elimina una persona por su ID de la base de datos
    bool eliminarPersonaPorId(uint32_t id);

    // Obtiene la lista de todas las personas con permiso de entrar
    const std::vector<AuthorizedPersonRecord>& obtenerRegistros() const {
        return m_records;
    }

    // Exporta la foto 64x64 almacenada en la BD a un archivo .bmp estándar (100% código propio)
    static bool exportarFotoRostroBMP(
        const uint8_t normalizedPhoto[FACE_PHOTO_BYTES],
        const std::string& outputBmpPath
    );

    // Carga una imagen .bmp de 24 bits desde disco a ImageRGB (100% código propio)
    static bool cargarImagenBMP(
        const std::string& bmpPath,
        ImageRGB& outImage
    );

private:
    std::string m_dbFilePath;
    std::vector<AuthorizedPersonRecord> m_records;
};

} // namespace rf_klaos

#endif // RF_KLAOS_FACE_DATABASE_H

