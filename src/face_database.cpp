#include "face_database.h"
#include <fstream>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <direct.h>

namespace rf_klaos {

namespace {

#pragma pack(push, 1)
struct DatabaseFileHeader {
    char magic[8];          // "RFKLAOS"
    uint32_t version;       // 1
    uint32_t recordCount;   // Cantidad de personas autorizadas
};

struct BMPFileHeader {
    uint16_t bfType;        // 0x4D42 ('BM')
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};

struct BMPInfoHeader {
    uint32_t biSize;        // 40
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;      // 1
    uint16_t biBitCount;    // 24
    uint32_t biCompression; // 0 (BI_RGB)
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

constexpr char DB_MAGIC[8] = {'R', 'F', 'K', 'L', 'A', 'O', 'S', '\0'};
constexpr uint32_t DB_VERSION = 1;

} // namespace

FaceDatabase::FaceDatabase(const std::string& dbFilePath)
    : m_dbFilePath(dbFilePath) {}

bool FaceDatabase::cargarBaseDeDatos() {
    m_records.clear();

    std::ifstream in(m_dbFilePath, std::ios::binary);
    if (!in.is_open()) {
        // Si no existe aún, creamos un archivo de base de datos vacío válido
        return guardarBaseDeDatos();
    }

    DatabaseFileHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in.good() || std::memcmp(header.magic, DB_MAGIC, 8) != 0 || header.version != DB_VERSION) {
        return false;
    }

    if (header.recordCount > 100000) {
        return false;
    }

    m_records.resize(header.recordCount);
    for (uint32_t i = 0; i < header.recordCount; ++i) {
        in.read(reinterpret_cast<char*>(&m_records[i]), sizeof(AuthorizedPersonRecord));
        if (!in.good()) {
            m_records.resize(i);
            return false;
        }
    }

    return true;
}

bool FaceDatabase::guardarBaseDeDatos() const {
    std::ofstream out(m_dbFilePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    DatabaseFileHeader header{};
    std::memcpy(header.magic, DB_MAGIC, 8);
    header.version = DB_VERSION;
    header.recordCount = static_cast<uint32_t>(m_records.size());

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const AuthorizedPersonRecord& rec : m_records) {
        out.write(reinterpret_cast<const char*>(&rec), sizeof(AuthorizedPersonRecord));
    }

    return out.good();
}

bool FaceDatabase::registrarPersonaAutorizada(
    const std::string& nombre,
    const FaceDescriptor& descriptor,
    uint32_t& outNewId
) {
    if (nombre.empty()) return false;

    uint32_t nextId = 1;
    for (const auto& rec : m_records) {
        if (rec.id >= nextId) {
            nextId = rec.id + 1;
        }
    }

    AuthorizedPersonRecord newRec{};
    newRec.id = nextId;
    std::strncpy(newRec.name, nombre.c_str(), sizeof(newRec.name) - 1);
    newRec.name[sizeof(newRec.name) - 1] = '\0';
    newRec.registeredTimestamp = static_cast<uint64_t>(std::time(nullptr));
    std::memcpy(newRec.rawFacePhoto, descriptor.normalizedPhoto, FACE_PHOTO_BYTES);
    std::memcpy(newRec.descriptor, descriptor.features, sizeof(newRec.descriptor));

    m_records.push_back(newRec);
    outNewId = nextId;

    // Guardamos también respaldo visual BMP en carpeta Rostros_DB/
    _mkdir("Rostros_DB");
    std::string safeName = nombre;
    for (char& c : safeName) {
        if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
    }
    std::string bmpPath = "Rostros_DB/persona_" + std::to_string(nextId) + "_" + safeName + ".bmp";
    exportarFotoRostroBMP(newRec.rawFacePhoto, bmpPath);

    return guardarBaseDeDatos();
}

bool FaceDatabase::eliminarPersonaPorId(uint32_t id) {
    auto it = std::remove_if(m_records.begin(), m_records.end(),
        [id](const AuthorizedPersonRecord& r) { return r.id == id; });
    if (it == m_records.end()) {
        return false;
    }
    m_records.erase(it, m_records.end());
    return guardarBaseDeDatos();
}

bool FaceDatabase::exportarFotoRostroBMP(
    const uint8_t normalizedPhoto[FACE_PHOTO_BYTES],
    const std::string& outputBmpPath
) {
    const int W = FACE_CANONICAL_WIDTH;
    const int H = FACE_CANONICAL_HEIGHT;
    const int rowStride = (W * 3 + 3) & ~3;
    const int dataSize = rowStride * H;

    BMPFileHeader fileHeader{};
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + dataSize;

    BMPInfoHeader infoHeader{};
    infoHeader.biSize = sizeof(BMPInfoHeader);
    infoHeader.biWidth = W;
    infoHeader.biHeight = H; // Bottom-up
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = 0;
    infoHeader.biSizeImage = dataSize;

    std::ofstream out(outputBmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    std::vector<uint8_t> rowBuf(static_cast<size_t>(rowStride), 0);
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            uint8_t g = normalizedPhoto[y * W + x];
            rowBuf[x * 3 + 0] = g;
            rowBuf[x * 3 + 1] = g;
            rowBuf[x * 3 + 2] = g;
        }
        out.write(reinterpret_cast<const char*>(rowBuf.data()), rowStride);
    }

    return out.good();
}

bool FaceDatabase::cargarImagenBMP(
    const std::string& bmpPath,
    ImageRGB& outImage
) {
    std::ifstream in(bmpPath, std::ios::binary);
    if (!in.is_open()) return false;

    BMPFileHeader fileHeader{};
    BMPInfoHeader infoHeader{};
    in.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    in.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

    if (!in.good() || fileHeader.bfType != 0x4D42 || infoHeader.biBitCount != 24 || infoHeader.biCompression != 0) {
        return false;
    }

    int W = infoHeader.biWidth;
    int H = std::abs(infoHeader.biHeight);
    bool bottomUp = (infoHeader.biHeight > 0);

    if (W <= 0 || H <= 0 || W > 4096 || H > 4096) return false;

    outImage.width = W;
    outImage.height = H;
    outImage.pixels.resize(static_cast<size_t>(W * H * 3));

    int rowStride = (W * 3 + 3) & ~3;
    std::vector<uint8_t> rowBuf(static_cast<size_t>(rowStride));

    in.seekg(fileHeader.bfOffBits, std::ios::beg);
    for (int row = 0; row < H; ++row) {
        in.read(reinterpret_cast<char*>(rowBuf.data()), rowStride);
        if (!in.good()) return false;
        int dstY = bottomUp ? (H - 1 - row) : row;
        uint8_t* dstRow = &outImage.pixels[static_cast<size_t>(dstY * W * 3)];
        for (int x = 0; x < W; ++x) {
            dstRow[x * 3 + 0] = rowBuf[x * 3 + 2]; // R
            dstRow[x * 3 + 1] = rowBuf[x * 3 + 1]; // G
            dstRow[x * 3 + 2] = rowBuf[x * 3 + 0]; // B
        }
    }

    return true;
}

} // namespace rf_klaos

