#include "face_engine.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace rf_klaos {

namespace {

// Evalúa la suma de un rectángulo [x1..x2, y1..y2] en O(1) usando una Imagen Integral
inline int64_t rectSum(
    const std::vector<int64_t>& integral,
    int stride,
    int x1, int y1, int x2, int y2
) {
    return integral[y2 * stride + x2]
         - integral[y1 * stride + x2]
         - integral[y2 * stride + x1]
         + integral[y1 * stride + x1];
}

// Evalúa el promedio de píxeles dentro de un rectángulo usando una Imagen Integral
inline float rectMean(
    const std::vector<int64_t>& integral,
    int stride,
    int x1, int y1, int x2, int y2
) {
    int area = (x2 - x1) * (y2 - y1);
    if (area <= 0) return 0.0f;
    return static_cast<float>(rectSum(integral, stride, x1, y1, x2, y2)) / static_cast<float>(area);
}

// Determina si un píxel RGB corresponde al rango cromático de piel humana en espacio YCbCr
inline bool esPielHumanaYCbCr(uint8_t r, uint8_t g, uint8_t b) {
    int y  = ( 77 * r + 150 * g +  29 * b) >> 8;
    int cb = ((-43 * r -  85 * g + 128 * b) >> 8) + 128;
    int cr = ((128 * r - 107 * g -  21 * b) >> 8) + 128;

    if (y < 30 || y > 245) return false;
    return (cb >= 77 && cb <= 132) && (cr >= 131 && cr <= 178) && (r > g && r > b);
}

} // namespace

ImageGray FaceEngine::convertirAGrises(const ImageRGB& rgb) {
    ImageGray gray;
    if (!rgb.isValid()) return gray;

    gray.width = rgb.width;
    gray.height = rgb.height;
    gray.pixels.resize(static_cast<size_t>(rgb.width * rgb.height));

    const uint8_t* src = rgb.pixels.data();
    uint8_t* dst = gray.pixels.data();
    size_t totalPixels = static_cast<size_t>(rgb.width * rgb.height);

    for (size_t i = 0; i < totalPixels; ++i) {
        int r = src[i * 3 + 0];
        int g = src[i * 3 + 1];
        int b = src[i * 3 + 2];
        dst[i] = static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
    }
    return gray;
}

FaceRect FaceEngine::detectarCara(const ImageRGB& frame) {
    FaceRect bestFace;
    if (!frame.isValid() || frame.width < 80 || frame.height < 80) {
        return bestFace;
    }

    const int W = frame.width;
    const int H = frame.height;
    const int stride = W + 1;

    // 1. Construimos Imágenes Integrales en una sola pasada O(W * H):
    //    - lumInt: suma acumulada de luminancia Y
    //    - lumSqInt: suma acumulada de Y^2 (para calcular varianza / contraste rápido)
    //    - skinInt: suma acumulada de píxeles con crominancia de piel
    //    - edgeInt: suma acumulada de bordes horizontales (ojos, cejas, boca)
    std::vector<int64_t> lumInt(static_cast<size_t>((W + 1) * (H + 1)), 0);
    std::vector<int64_t> lumSqInt(static_cast<size_t>((W + 1) * (H + 1)), 0);
    std::vector<int64_t> skinInt(static_cast<size_t>((W + 1) * (H + 1)), 0);
    std::vector<int64_t> edgeInt(static_cast<size_t>((W + 1) * (H + 1)), 0);

    std::vector<uint8_t> grayMap(static_cast<size_t>(W * H), 0);

    for (int y = 0; y < H; ++y) {
        int64_t rowLum = 0;
        int64_t rowLumSq = 0;
        int64_t rowSkin = 0;
        int64_t rowEdge = 0;

        for (int x = 0; x < W; ++x) {
            size_t idx3 = static_cast<size_t>((y * W + x) * 3);
            uint8_t r = frame.pixels[idx3 + 0];
            uint8_t g = frame.pixels[idx3 + 1];
            uint8_t b = frame.pixels[idx3 + 2];

            int lum = (77 * r + 150 * g + 29 * b) >> 8;
            grayMap[static_cast<size_t>(y * W + x)] = static_cast<uint8_t>(lum);

            int skin = esPielHumanaYCbCr(r, g, b) ? 1 : 0;
            int edge = 0;
            if (y > 0 && x > 0) {
                int prevY = grayMap[static_cast<size_t>((y - 1) * W + x)];
                int prevX = grayMap[static_cast<size_t>(y * W + (x - 1))];
                edge = std::abs(lum - prevY) + std::abs(lum - prevX);
            }

            rowLum += lum;
            rowLumSq += static_cast<int64_t>(lum) * lum;
            rowSkin += skin;
            rowEdge += edge;

            size_t intIdx = static_cast<size_t>((y + 1) * stride + (x + 1));
            size_t prevRowIdx = static_cast<size_t>(y * stride + (x + 1));

            lumInt[intIdx]   = lumInt[prevRowIdx]   + rowLum;
            lumSqInt[intIdx] = lumSqInt[prevRowIdx] + rowLumSq;
            skinInt[intIdx]  = skinInt[prevRowIdx]  + rowSkin;
            edgeInt[intIdx]  = edgeInt[prevRowIdx]  + rowEdge;
        }
    }

    // 2. Búsqueda multiescala mediante ventana deslizante con características tipo Haar + Simetría
    int minDim = std::min(W, H);
    int minWin = std::max(72, minDim / 5);
    int maxWin = (minDim * 4) / 5;
    int stepSize = std::max(8, (maxWin - minWin) / 8);

    float bestScore = -1.0f;

    for (int winSize = minWin; winSize <= maxWin; winSize += stepSize) {
        int winW = winSize;
        int winH = (winSize * 11) / 10; // Proporción natural de un rostro frontal (1 : 1.1)
        if (winH >= H || winW >= W) continue;

        int strideXY = std::max(6, winSize / 12);

        for (int y = 4; y + winH < H - 4; y += strideXY) {
            for (int x = 4; x + winW < W - 4; x += strideXY) {
                int area = winW * winH;

                // Filtro 1: Densidad de piel en la ventana (o textura fuerte si es cámara IR/monocroma)
                float skinRatio = rectMean(skinInt, stride, x, y, x + winW, y + winH);
                if (skinRatio < 0.18f) continue;

                // Filtro 2: Desviación estándar de luminancia (descarta fondos planos como paredes)
                float meanLum = rectMean(lumInt, stride, x, y, x + winW, y + winH);
                float meanSqLum = rectMean(lumSqInt, stride, x, y, x + winW, y + winH);
                float variance = meanSqLum - (meanLum * meanLum);
                if (variance < 100.0f) continue;

                // Filtro 3: Característica Haar #1 -> Franja de ojos más oscura que frente y pómulos
                int eyeY1   = y + (winH * 20) / 100;
                int eyeY2   = y + (winH * 45) / 100;
                int cheekY1 = y + (winH * 46) / 100;
                int cheekY2 = y + (winH * 70) / 100;
                int innerX1 = x + (winW * 15) / 100;
                int innerX2 = x + (winW * 85) / 100;

                float eyeStripLum   = rectMean(lumInt, stride, innerX1, eyeY1, innerX2, eyeY2);
                float cheekStripLum = rectMean(lumInt, stride, innerX1, cheekY1, innerX2, cheekY2);
                float haarEyeCheek  = cheekStripLum - eyeStripLum;
                if (haarEyeCheek < 1.5f) continue;

                // Filtro 4: Característica Haar #2 -> Ojo Izquierdo y Derecho más oscuros que el puente nasal
                int leftEyeX1  = x + (winW * 16) / 100;
                int leftEyeX2  = x + (winW * 42) / 100;
                int bridgeX1   = x + (winW * 42) / 100;
                int bridgeX2   = x + (winW * 58) / 100;
                int rightEyeX1 = x + (winW * 58) / 100;
                int rightEyeX2 = x + (winW * 84) / 100;

                float leftEyeLum  = rectMean(lumInt, stride, leftEyeX1, eyeY1, leftEyeX2, eyeY2);
                float bridgeLum   = rectMean(lumInt, stride, bridgeX1, eyeY1, bridgeX2, eyeY2);
                float rightEyeLum = rectMean(lumInt, stride, rightEyeX1, eyeY1, rightEyeX2, eyeY2);

                float haarBridge = bridgeLum - 0.5f * (leftEyeLum + rightEyeLum);
                if (haarBridge < 0.5f) continue;

                // Filtro 5: Simetría bilateral entre el lado izquierdo y derecho del rostro
                float eyeSymmetryDiff = std::abs(leftEyeLum - rightEyeLum);
                if (eyeSymmetryDiff > 35.0f) continue;

                // Filtro 6: Densidad de bordes (ojos, nariz y boca tienen gradiente)
                int mouthY1 = y + (winH * 65) / 100;
                int mouthY2 = y + (winH * 88) / 100;
                float eyeEdge   = rectMean(edgeInt, stride, innerX1, eyeY1, innerX2, eyeY2);
                float mouthEdge = rectMean(edgeInt, stride, innerX1, mouthY1, innerX2, mouthY2);
                if (eyeEdge < 1.5f || mouthEdge < 1.0f) continue;

                // Puntuación combinada ponderada + preferencia suave por rostros centrados
                float centerBiasX = 1.0f - 0.35f * std::abs((x + winW * 0.5f) - W * 0.5f) / (W * 0.5f);
                float centerBiasY = 1.0f - 0.35f * std::abs((y + winH * 0.5f) - H * 0.5f) / (H * 0.5f);

                float score = (skinRatio * 45.0f)
                            + (std::min(haarEyeCheek, 35.0f) * 1.4f)
                            + (std::min(haarBridge, 25.0f) * 1.2f)
                            + (std::min(eyeEdge + mouthEdge, 60.0f) * 0.5f)
                            - (eyeSymmetryDiff * 0.6f);

                score *= (centerBiasX * centerBiasY);

                if (score > bestScore) {
                    bestScore = score;
                    bestFace.x = x;
                    bestFace.y = y;
                    bestFace.width = winW;
                    bestFace.height = winH;
                    bestFace.confidence = std::min(99.9f, score);
                    bestFace.detected = true;
                }
            }
        }
    }

    return bestFace;
}

bool FaceEngine::extraerDescriptorFacial(
    const ImageRGB& frame,
    const FaceRect& faceRect,
    FaceDescriptor& outDescriptor
) {
    if (!frame.isValid() || !faceRect.detected || faceRect.width < 20 || faceRect.height < 20) {
        return false;
    }

    // Recortamos ligeramente hacia el interior (8%) para aislar ojos, nariz y boca sin el fondo
    int marginX = (faceRect.width * 8) / 100;
    int marginY = (faceRect.height * 8) / 100;
    int cropX = std::max(0, faceRect.x + marginX);
    int cropY = std::max(0, faceRect.y + marginY);
    int cropW = std::min(frame.width - cropX, faceRect.width - 2 * marginX);
    int cropH = std::min(frame.height - cropY, faceRect.height - 2 * marginY);

    if (cropW <= 8 || cropH <= 8) return false;

    // 1. Escalado Bilineal propio a 64x64 en escala de grises
    uint8_t rawGray[FACE_PHOTO_BYTES];
    for (int dstY = 0; dstY < FACE_CANONICAL_HEIGHT; ++dstY) {
        float srcYf = cropY + (static_cast<float>(dstY) / (FACE_CANONICAL_HEIGHT - 1)) * (cropH - 1);
        int y0 = static_cast<int>(srcYf);
        int y1 = std::min(frame.height - 1, y0 + 1);
        float wy = srcYf - y0;

        for (int dstX = 0; dstX < FACE_CANONICAL_WIDTH; ++dstX) {
            float srcXf = cropX + (static_cast<float>(dstX) / (FACE_CANONICAL_WIDTH - 1)) * (cropW - 1);
            int x0 = static_cast<int>(srcXf);
            int x1 = std::min(frame.width - 1, x0 + 1);
            float wx = srcXf - x0;

            auto getGray = [&](int px, int py) -> float {
                size_t idx = static_cast<size_t>((py * frame.width + px) * 3);
                return 0.299f * frame.pixels[idx + 0]
                     + 0.587f * frame.pixels[idx + 1]
                     + 0.114f * frame.pixels[idx + 2];
            };

            float top = (1.0f - wx) * getGray(x0, y0) + wx * getGray(x1, y0);
            float bot = (1.0f - wx) * getGray(x0, y1) + wx * getGray(x1, y1);
            float val = (1.0f - wy) * top + wy * bot;
            rawGray[dstY * FACE_CANONICAL_WIDTH + dstX] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
        }
    }

    // 2. Ecualización de Histograma propia para invariancia a la iluminación
    int hist[256] = {0};
    for (int i = 0; i < FACE_PHOTO_BYTES; ++i) {
        hist[rawGray[i]]++;
    }
    int cdf[256] = {0};
    cdf[0] = hist[0];
    int cdfMin = (cdf[0] > 0) ? cdf[0] : FACE_PHOTO_BYTES;
    for (int i = 1; i < 256; ++i) {
        cdf[i] = cdf[i - 1] + hist[i];
        if (cdf[i] > 0 && cdf[i] < cdfMin) {
            cdfMin = cdf[i];
        }
    }

    for (int i = 0; i < FACE_PHOTO_BYTES; ++i) {
        int v = rawGray[i];
        int eq = ((cdf[v] - cdfMin) * 255) / std::max(1, FACE_PHOTO_BYTES - cdfMin);
        outDescriptor.normalizedPhoto[i] = static_cast<uint8_t>(std::clamp(eq, 0, 255));
    }

    // 3. Cálculo del descriptor LBP (Local Binary Patterns) espacial en cuadrícula 4x4 (256 valores)
    std::memset(outDescriptor.features, 0, sizeof(outDescriptor.features));

    const int dx[8] = {-1,  0,  1, 1, 1, 0, -1, -1};
    const int dy[8] = {-1, -1, -1, 0, 1, 1,  1,  0};

    // Pesos de importancia facial para cada celda de la cuadrícula 4x4 (ojos y nariz pesan más)
    const float cellWeights[4][4] = {
        {0.8f, 1.2f, 1.2f, 0.8f},
        {1.2f, 2.0f, 2.0f, 1.2f}, // Zona de ojos y puente nasal
        {0.9f, 1.5f, 1.5f, 0.9f}, // Zona de nariz y pómulos
        {0.6f, 1.3f, 1.3f, 0.6f}  // Zona de boca y mentón
    };

    for (int y = 1; y < FACE_CANONICAL_HEIGHT - 1; ++y) {
        int cellY = (y * 4) / FACE_CANONICAL_HEIGHT;
        for (int x = 1; x < FACE_CANONICAL_WIDTH - 1; ++x) {
            int cellX = (x * 4) / FACE_CANONICAL_WIDTH;
            uint8_t center = outDescriptor.normalizedPhoto[y * FACE_CANONICAL_WIDTH + x];

            uint8_t lbpCode = 0;
            for (int bit = 0; bit < 8; ++bit) {
                uint8_t neighbor = outDescriptor.normalizedPhoto[(y + dy[bit]) * FACE_CANONICAL_WIDTH + (x + dx[bit])];
                if (neighbor >= center) {
                    lbpCode |= (1 << bit);
                }
            }

            int bin = lbpCode >> 4; // 16 bins por cada celda espacial
            int featureIdx = (cellY * 4 + cellX) * 16 + bin;
            outDescriptor.features[featureIdx] += cellWeights[cellY][cellX];
        }
    }

    // 4. Perfil geométrico de intensidades y gradientes en subbloques 8x8 (64 valores -> índices 256..319)
    for (int by = 0; by < 8; ++by) {
        for (int bx = 0; bx < 8; ++bx) {
            float sum = 0.0f;
            for (int py = by * 8; py < (by + 1) * 8; ++py) {
                for (int px = bx * 8; px < (bx + 1) * 8; ++px) {
                    sum += static_cast<float>(outDescriptor.normalizedPhoto[py * FACE_CANONICAL_WIDTH + px]);
                }
            }
            outDescriptor.features[256 + by * 8 + bx] = (sum / (64.0f * 255.0f));
        }
    }

    // Normalización L2 de los primeros 256 valores (LBP) y de los 64 valores geométricos
    float normLbp = 0.0f;
    for (int i = 0; i < 256; ++i) {
        normLbp += outDescriptor.features[i] * outDescriptor.features[i];
    }
    normLbp = std::sqrt(normLbp);
    if (normLbp > 1e-6f) {
        for (int i = 0; i < 256; ++i) {
            outDescriptor.features[i] /= normLbp;
        }
    }

    float normGeo = 0.0f;
    for (int i = 256; i < FACE_DESCRIPTOR_SIZE; ++i) {
        normGeo += outDescriptor.features[i] * outDescriptor.features[i];
    }
    normGeo = std::sqrt(normGeo);
    if (normGeo > 1e-6f) {
        for (int i = 256; i < FACE_DESCRIPTOR_SIZE; ++i) {
            outDescriptor.features[i] /= normGeo;
        }
    }

    return true;
}

float FaceEngine::calcularSimilitudRostros(
    const FaceDescriptor& caraActual,
    const AuthorizedPersonRecord& registroBD
) {
    // 1. Similitud Coseno en el histograma LBP espacial (índices 0..255)
    float dotLbp = 0.0f;
    for (int i = 0; i < 256; ++i) {
        dotLbp += caraActual.features[i] * registroBD.descriptor[i];
    }
    dotLbp = std::clamp(dotLbp, 0.0f, 1.0f);

    // 2. Similitud Coseno en el perfil geométrico 8x8 (índices 256..319)
    float dotGeo = 0.0f;
    for (int i = 256; i < FACE_DESCRIPTOR_SIZE; ++i) {
        dotGeo += caraActual.features[i] * registroBD.descriptor[i];
    }
    dotGeo = std::clamp(dotGeo, 0.0f, 1.0f);

    // 3. Correlación Cruzada Normalizada (NCC) directa contra la foto 64x64 guardada en la Base de Datos
    float meanA = 0.0f, meanB = 0.0f;
    for (int i = 0; i < FACE_PHOTO_BYTES; ++i) {
        meanA += caraActual.normalizedPhoto[i];
        meanB += registroBD.rawFacePhoto[i];
    }
    meanA /= FACE_PHOTO_BYTES;
    meanB /= FACE_PHOTO_BYTES;

    float num = 0.0f, denA = 0.0f, denB = 0.0f;
    for (int i = 0; i < FACE_PHOTO_BYTES; ++i) {
        float da = caraActual.normalizedPhoto[i] - meanA;
        float db = registroBD.rawFacePhoto[i] - meanB;
        num  += da * db;
        denA += da * da;
        denB += db * db;
    }
    float ncc = 0.0f;
    if (denA > 1e-5f && denB > 1e-5f) {
        ncc = num / std::sqrt(denA * denB);
    }
    // Transformamos NCC [-1..1] a escala [0..1]
    float photoSim = std::clamp((ncc + 1.0f) * 0.5f, 0.0f, 1.0f);

    // Puntuación final combinada (0 a 100%)
    float combined = (0.45f * dotLbp + 0.25f * dotGeo + 0.30f * photoSim) * 100.0f;
    return std::clamp(combined, 0.0f, 100.0f);
}

AccessControlResult FaceEngine::compararCaraConBaseDeDatos(
    const FaceDescriptor& caraActual,
    const std::vector<AuthorizedPersonRecord>& registrosAutorizados,
    float umbralSimilitud
) {
    AccessControlResult result;
    result.accessGranted = false;
    result.similarityScore = 0.0f;

    if (registrosAutorizados.empty()) {
        result.message = "ACCESO DENEGADO: La base de datos de personas autorizadas está vacía.";
        return result;
    }

    float bestScore = -1.0f;
    const AuthorizedPersonRecord* bestMatch = nullptr;

    for (const AuthorizedPersonRecord& record : registrosAutorizados) {
        float sim = calcularSimilitudRostros(caraActual, record);
        if (sim > bestScore) {
            bestScore = sim;
            bestMatch = &record;
        }
    }

    result.similarityScore = bestScore;

    if (bestMatch && bestScore >= umbralSimilitud) {
        result.accessGranted = true;
        result.matchedPersonId = bestMatch->id;
        result.matchedPersonName = bestMatch->name;
        result.message = "ACCESO PERMITIDO: Bienvenido/a " + result.matchedPersonName;
    } else {
        result.accessGranted = false;
        result.message = "ACCESO DENEGADO: Rostro no reconocido en la base de datos.";
    }

    return result;
}

} // namespace rf_klaos
