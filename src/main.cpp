#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include "types.h"
#include "camera_hardware.h"
#include "face_engine.h"
#include "face_database.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace rf_klaos;

// Identificadores de controles de la ventana nativa Win32
constexpr int ID_BTN_VERIFY       = 101;
constexpr int ID_BTN_REGISTER     = 102;
constexpr int ID_BTN_LOAD_BMP     = 103;
constexpr int ID_BTN_DELETE_USER  = 104;
constexpr int ID_EDIT_NAME        = 105;
constexpr int ID_LIST_USERS       = 106;
constexpr int ID_CHK_AUTO_VERIFY  = 107;
constexpr int ID_TIMER_CAMERA     = 1;

// Estado global de la aplicación de control de acceso
struct AppState {
    CameraHardware camera;
    FaceDatabase db{"rostros_autorizados.db"};
    ImageRGB currentFrame;
    FaceRect currentFace;
    FaceDescriptor currentDescriptor{};
    bool hasValidDescriptor = false;
    AccessControlResult lastAccessResult;
    bool autoVerify = true;

    HWND hMainWnd = nullptr;
    HWND hEditName = nullptr;
    HWND hListUsers = nullptr;
    HWND hChkAuto = nullptr;
    HFONT hFontTitle = nullptr;
    HFONT hFontNormal = nullptr;
    HFONT hFontBanner = nullptr;
};

static AppState g_app;

// Dibuja un rectángulo con grosor sobre un ImageRGB (100% código propio)
static void dibujarRectanguloRGB(
    ImageRGB& img,
    int rx, int ry, int rw, int rh,
    uint8_t r, uint8_t g, uint8_t b,
    int thickness = 3
) {
    if (!img.isValid()) return;
    int x1 = std::max(0, rx);
    int y1 = std::max(0, ry);
    int x2 = std::min(img.width - 1, rx + rw);
    int y2 = std::min(img.height - 1, ry + rh);

    for (int t = 0; t < thickness; ++t) {
        int ty1 = std::min(img.height - 1, y1 + t);
        int ty2 = std::max(0, y2 - t);
        for (int x = x1; x <= x2; ++x) {
            size_t idxTop = static_cast<size_t>((ty1 * img.width + x) * 3);
            size_t idxBot = static_cast<size_t>((ty2 * img.width + x) * 3);
            img.pixels[idxTop + 0] = r; img.pixels[idxTop + 1] = g; img.pixels[idxTop + 2] = b;
            img.pixels[idxBot + 0] = r; img.pixels[idxBot + 1] = g; img.pixels[idxBot + 2] = b;
        }

        int tx1 = std::min(img.width - 1, x1 + t);
        int tx2 = std::max(0, x2 - t);
        for (int y = y1; y <= y2; ++y) {
            size_t idxLeft  = static_cast<size_t>((y * img.width + tx1) * 3);
            size_t idxRight = static_cast<size_t>((y * img.width + tx2) * 3);
            img.pixels[idxLeft + 0]  = r; img.pixels[idxLeft + 1]  = g; img.pixels[idxLeft + 2]  = b;
            img.pixels[idxRight + 0] = r; img.pixels[idxRight + 1] = g; img.pixels[idxRight + 2] = b;
        }
    }
}

// Actualiza el listado visual de personas en la base de datos
static void actualizarListaUsuariosUI() {
    if (!g_app.hListUsers) return;
    SendMessageA(g_app.hListUsers, LB_RESETCONTENT, 0, 0);

    const auto& records = g_app.db.obtenerRegistros();
    for (const auto& rec : records) {
        char itemText[200];
        std::snprintf(itemText, sizeof(itemText), "ID #%u - %s", rec.id, rec.name);
        int idx = static_cast<int>(SendMessageA(g_app.hListUsers, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(itemText)));
        SendMessageA(g_app.hListUsers, LB_SETITEMDATA, idx, static_cast<LPARAM>(rec.id));
    }
}

// Procesa un fotograma: detecta la cara y, si procede, la compara con la base de datos
static void procesarFotogramaActual(bool forzarComparacion) {
    if (!g_app.currentFrame.isValid()) return;

    g_app.currentFace = FaceEngine::detectarCara(g_app.currentFrame);
    if (g_app.currentFace.detected) {
        g_app.hasValidDescriptor = FaceEngine::extraerDescriptorFacial(
            g_app.currentFrame,
            g_app.currentFace,
            g_app.currentDescriptor
        );

        if (g_app.hasValidDescriptor && (g_app.autoVerify || forzarComparacion)) {
            g_app.lastAccessResult = FaceEngine::compararCaraConBaseDeDatos(
                g_app.currentDescriptor,
                g_app.db.obtenerRegistros(),
                74.0f
            );
        }
    } else {
        g_app.hasValidDescriptor = false;
        if (g_app.autoVerify) {
            g_app.lastAccessResult.accessGranted = false;
            g_app.lastAccessResult.similarityScore = 0.0f;
            g_app.lastAccessResult.message = "ESPERANDO PERSONA FRENTE A LA CAMARA...";
        }
    }
}

// Renderiza la interfaz gráfica nativa con GDI (Doble Buffer para evitar parpadeo)
static void renderizarVentana(HDC hdcWindow, RECT clientRect) {
    int winW = clientRect.right - clientRect.left;
    int winH = clientRect.bottom - clientRect.top;
    if (winW <= 0 || winH <= 0) return;

    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcWindow, winW, winH);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hbmMem);

    // Fondo oscuro profesional
    HBRUSH bgBrush = CreateSolidBrush(RGB(24, 28, 36));
    FillRect(hdcMem, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(hdcMem, TRANSPARENT);

    // 1. Dibujar fotograma de la cámara web (640x480) en la zona izquierda (x=20, y=20)
    const int camViewX = 20;
    const int camViewY = 20;
    const int camViewW = 640;
    const int camViewH = 480;

    if (g_app.currentFrame.isValid()) {
        ImageRGB displayImg = g_app.currentFrame;

        // Si se detectó una cara, dibujamos el recuadro delimitador sobre la imagen
        if (g_app.currentFace.detected) {
            uint8_t boxR = g_app.lastAccessResult.accessGranted ? 30  : 235;
            uint8_t boxG = g_app.lastAccessResult.accessGranted ? 220 : 55;
            uint8_t boxB = g_app.lastAccessResult.accessGranted ? 90  : 55;
            dibujarRectanguloRGB(
                displayImg,
                g_app.currentFace.x,
                g_app.currentFace.y,
                g_app.currentFace.width,
                g_app.currentFace.height,
                boxR, boxG, boxB, 3
            );
        }

        // Convertimos RGB a BGR de 24 bits para StretchDIBits nativo de Windows
        int stride = (displayImg.width * 3 + 3) & ~3;
        std::vector<uint8_t> dibBuffer(static_cast<size_t>(stride * displayImg.height), 0);
        for (int y = 0; y < displayImg.height; ++y) {
            const uint8_t* srcRow = &displayImg.pixels[static_cast<size_t>(y * displayImg.width * 3)];
            uint8_t* dstRow = &dibBuffer[static_cast<size_t>(y * stride)];
            for (int x = 0; x < displayImg.width; ++x) {
                dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
                dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
                dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
            }
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = displayImg.width;
        bmi.bmiHeader.biHeight = -displayImg.height; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(hdcMem, HALFTONE);
        StretchDIBits(
            hdcMem,
            camViewX, camViewY, camViewW, camViewH,
            0, 0, displayImg.width, displayImg.height,
            dibBuffer.data(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    } else {
        RECT camRect = {camViewX, camViewY, camViewX + camViewW, camViewY + camViewH};
        HBRUSH blackBrush = CreateSolidBrush(RGB(10, 12, 16));
        FillRect(hdcMem, &camRect, blackBrush);
        DeleteObject(blackBrush);
    }

    // 2. Miniaturas comparativas: Rostro en Cámara (64x64) vs Foto en Base de Datos (64x64)
    int thumbY = 420;
    int thumbSize = 80;
    int stride64 = (FACE_CANONICAL_WIDTH * 3 + 3) & ~3;

    BITMAPINFO bmiThumb{};
    bmiThumb.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmiThumb.bmiHeader.biWidth = FACE_CANONICAL_WIDTH;
    bmiThumb.bmiHeader.biHeight = -FACE_CANONICAL_HEIGHT;
    bmiThumb.bmiHeader.biPlanes = 1;
    bmiThumb.bmiHeader.biBitCount = 24;
    bmiThumb.bmiHeader.biCompression = BI_RGB;

    if (g_app.hasValidDescriptor) {
        int thumbCamX = 680;
        std::vector<uint8_t> thumbBuf(static_cast<size_t>(stride64 * FACE_CANONICAL_HEIGHT), 0);
        for (int y = 0; y < FACE_CANONICAL_HEIGHT; ++y) {
            for (int x = 0; x < FACE_CANONICAL_WIDTH; ++x) {
                uint8_t g = g_app.currentDescriptor.normalizedPhoto[y * FACE_CANONICAL_WIDTH + x];
                thumbBuf[y * stride64 + x * 3 + 0] = g;
                thumbBuf[y * stride64 + x * 3 + 1] = g;
                thumbBuf[y * stride64 + x * 3 + 2] = g;
            }
        }

        StretchDIBits(
            hdcMem,
            thumbCamX, thumbY, thumbSize, thumbSize,
            0, 0, FACE_CANONICAL_WIDTH, FACE_CANONICAL_HEIGHT,
            thumbBuf.data(),
            &bmiThumb,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    }

    // Buscamos si hay una persona coincidente o seleccionada en la lista para mostrar su foto de la BD
    const AuthorizedPersonRecord* dbPhotoRec = nullptr;
    const auto& allRecords = g_app.db.obtenerRegistros();
    if (g_app.lastAccessResult.accessGranted) {
        for (const auto& r : allRecords) {
            if (r.id == g_app.lastAccessResult.matchedPersonId) {
                dbPhotoRec = &r;
                break;
            }
        }
    }
    if (!dbPhotoRec && g_app.hListUsers) {
        int selIdx = static_cast<int>(SendMessageA(g_app.hListUsers, LB_GETCURSEL, 0, 0));
        if (selIdx != LB_ERR) {
            uint32_t selId = static_cast<uint32_t>(SendMessageA(g_app.hListUsers, LB_GETITEMDATA, selIdx, 0));
            for (const auto& r : allRecords) {
                if (r.id == selId) {
                    dbPhotoRec = &r;
                    break;
                }
            }
        }
    }

    if (dbPhotoRec) {
        int thumbDbX = 770;
        std::vector<uint8_t> dbThumbBuf(static_cast<size_t>(stride64 * FACE_CANONICAL_HEIGHT), 0);
        for (int y = 0; y < FACE_CANONICAL_HEIGHT; ++y) {
            for (int x = 0; x < FACE_CANONICAL_WIDTH; ++x) {
                uint8_t g = dbPhotoRec->rawFacePhoto[y * FACE_CANONICAL_WIDTH + x];
                dbThumbBuf[y * stride64 + x * 3 + 0] = g;
                dbThumbBuf[y * stride64 + x * 3 + 1] = g;
                dbThumbBuf[y * stride64 + x * 3 + 2] = g;
            }
        }

        StretchDIBits(
            hdcMem,
            thumbDbX, thumbY, thumbSize, thumbSize,
            0, 0, FACE_CANONICAL_WIDTH, FACE_CANONICAL_HEIGHT,
            dbThumbBuf.data(),
            &bmiThumb,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    }

    SelectObject(hdcMem, g_app.hFontNormal);
    SetTextColor(hdcMem, RGB(190, 205, 225));
    RECT lblThumb = {860, thumbY + 4, 1010, thumbY + 95};
    char infoBuf[256];
    std::snprintf(
        infoBuf, sizeof(infoBuf),
        "Camara vs BD\nSimilitud: %.1f%%\nEn BD: %zu pers.",
        g_app.lastAccessResult.similarityScore,
        allRecords.size()
    );
    DrawTextA(hdcMem, infoBuf, -1, &lblThumb, DT_LEFT | DT_TOP);

    // 3. Banner inferior de Decisión de Control de Acceso (ACCESO PERMITIDO / DENEGADO)
    RECT bannerRect = {20, 515, 660, 595};
    COLORREF bannerColor = RGB(55, 65, 81);
    if (g_app.currentFace.detected) {
        bannerColor = g_app.lastAccessResult.accessGranted
            ? RGB(22, 135, 65)   // Verde: Acceso Permitido
            : RGB(175, 35, 45);  // Rojo: Acceso Denegado
    }

    HBRUSH bannerBrush = CreateSolidBrush(bannerColor);
    FillRect(hdcMem, &bannerRect, bannerBrush);
    DeleteObject(bannerBrush);

    SelectObject(hdcMem, g_app.hFontBanner);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    std::string bannerMsg = g_app.lastAccessResult.message.empty()
        ? "ESPERANDO PERSONA FRENTE A LA CAMARA..."
        : g_app.lastAccessResult.message;
    DrawTextA(hdcMem, bannerMsg.c_str(), -1, &bannerRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 4. Etiquetas del Panel de Control Derecho
    SelectObject(hdcMem, g_app.hFontTitle);
    SetTextColor(hdcMem, RGB(240, 245, 255));
    RECT titleRect = {680, 20, 1000, 50};
    DrawTextA(hdcMem, "CONTROL DE ACCESO RF-KLAOS", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, g_app.hFontNormal);
    SetTextColor(hdcMem, RGB(180, 195, 215));
    RECT lblName = {680, 105, 1000, 128};
    DrawTextA(hdcMem, "Nombre de persona autorizada:", -1, &lblName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT lblList = {680, 240, 1000, 262};
    DrawTextA(hdcMem, "Personas con permiso en Base de Datos:", -1, &lblList, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Volcamos el buffer en memoria a la ventana
    BitBlt(hdcWindow, 0, 0, winW, winH, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_app.hFontTitle  = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        g_app.hFontNormal = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        g_app.hFontBanner = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        g_app.hChkAuto = CreateWindowExA(
            0, "BUTTON", "Verificacion automatica en tiempo real",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            680, 55, 310, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHK_AUTO_VERIFY)), nullptr, nullptr
        );
        SendMessageA(g_app.hChkAuto, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(g_app.hChkAuto, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontNormal), TRUE);

        g_app.hEditName = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            680, 130, 310, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_NAME)), nullptr, nullptr
        );
        SendMessageA(g_app.hEditName, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontNormal), TRUE);

        HWND hBtnReg = CreateWindowExA(
            0, "BUTTON", "Registrar Rostro de Camara en BD",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            680, 165, 310, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_REGISTER)), nullptr, nullptr
        );
        SendMessageA(hBtnReg, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontNormal), TRUE);

        HWND hBtnLoadBmp = CreateWindowExA(
            0, "BUTTON", "Importar Foto .BMP a la Base de Datos",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            680, 202, 310, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_LOAD_BMP)), nullptr, nullptr
        );
        SendMessageA(hBtnLoadBmp, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontNormal), TRUE);

        g_app.hListUsers = CreateWindowExA(
            WS_EX_CLIENTEDGE, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            680, 265, 310, 110, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST_USERS)), nullptr, nullptr
        );
        SendMessageA(g_app.hListUsers, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontNormal), TRUE);

        HWND hBtnDel = CreateWindowExA(
            0, "BUTTON", "Eliminar Persona Seleccionada",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            680, 380, 310, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_DELETE_USER)), nullptr, nullptr
        );
        SendMessageA(hBtnDel, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontNormal), TRUE);

        HWND hBtnVerify = CreateWindowExA(
            0, "BUTTON", "Evaluar Acceso Ahora",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            680, 525, 310, 60, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_VERIFY)), nullptr, nullptr
        );
        SendMessageA(hBtnVerify, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFontTitle), TRUE);

        actualizarListaUsuariosUI();
        SetTimer(hwnd, ID_TIMER_CAMERA, 45, nullptr); // ~22 FPS
        return 0;
    }

    case WM_TIMER: {
        if (wParam == ID_TIMER_CAMERA) {
            if (g_app.camera.capturarImagen(g_app.currentFrame)) {
                procesarFotogramaActual(false);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_CHK_AUTO_VERIFY) {
            LRESULT checked = SendMessageA(g_app.hChkAuto, BM_GETCHECK, 0, 0);
            g_app.autoVerify = (checked == BST_CHECKED);
        } else if (id == ID_BTN_VERIFY) {
            procesarFotogramaActual(true);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (id == ID_BTN_REGISTER) {
            char nameBuf[128] = {0};
            GetWindowTextA(g_app.hEditName, nameBuf, sizeof(nameBuf));
            std::string personName(nameBuf);

            if (personName.empty()) {
                MessageBoxA(hwnd, "Por favor escribe el nombre de la persona antes de registrarla.",
                            "Nombre requerido", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!g_app.currentFace.detected || !g_app.hasValidDescriptor) {
                MessageBoxA(hwnd, "No se detecta un rostro frente a la camara. Colocate frente a la camara con buena iluminacion.",
                            "Rostro no detectado", MB_OK | MB_ICONWARNING);
                return 0;
            }

            uint32_t newId = 0;
            if (g_app.db.registrarPersonaAutorizada(personName, g_app.currentDescriptor, newId)) {
                SetWindowTextA(g_app.hEditName, "");
                actualizarListaUsuariosUI();
                procesarFotogramaActual(true);
                InvalidateRect(hwnd, nullptr, FALSE);

                std::string msg = "Persona '" + personName + "' registrada exitosamente en rostros_autorizados.db (ID #" + std::to_string(newId) + ").";
                MessageBoxA(hwnd, msg.c_str(), "Registro Exitoso", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxA(hwnd, "Error al guardar en el archivo de base de datos.", "Error", MB_OK | MB_ICONERROR);
            }
        } else if (id == ID_BTN_LOAD_BMP) {
            char nameBuf[128] = {0};
            GetWindowTextA(g_app.hEditName, nameBuf, sizeof(nameBuf));
            std::string personName(nameBuf);
            if (personName.empty()) {
                MessageBoxA(hwnd, "Primero ingresa el nombre de la persona en el campo de texto.",
                            "Nombre requerido", MB_OK | MB_ICONWARNING);
                return 0;
            }

            char filePath[MAX_PATH] = {0};
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Imagenes Bitmap 24-bit (*.bmp)\0*.bmp\0Todos los archivos (*.*)\0*.*\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameA(&ofn)) {
                ImageRGB bmpImg;
                if (!FaceDatabase::cargarImagenBMP(filePath, bmpImg)) {
                    MessageBoxA(hwnd, "No se pudo leer el archivo BMP. Asegurate de que sea un BMP de 24 bits sin comprimir.",
                                "Formato BMP no valido", MB_OK | MB_ICONERROR);
                    return 0;
                }

                FaceRect faceInBmp = FaceEngine::detectarCara(bmpImg);
                if (!faceInBmp.detected) {
                    // Si la foto ya viene recortada al rostro, usamos toda la imagen
                    faceInBmp.x = 0;
                    faceInBmp.y = 0;
                    faceInBmp.width = bmpImg.width;
                    faceInBmp.height = bmpImg.height;
                    faceInBmp.detected = true;
                }

                FaceDescriptor desc{};
                if (FaceEngine::extraerDescriptorFacial(bmpImg, faceInBmp, desc)) {
                    uint32_t newId = 0;
                    if (g_app.db.registrarPersonaAutorizada(personName, desc, newId)) {
                        SetWindowTextA(g_app.hEditName, "");
                        actualizarListaUsuariosUI();
                        MessageBoxA(hwnd, "Foto BMP importada y registrada en rostros_autorizados.db correctamente.",
                                    "Importacion Exitosa", MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
        } else if (id == ID_BTN_DELETE_USER) {
            int selIdx = static_cast<int>(SendMessageA(g_app.hListUsers, LB_GETCURSEL, 0, 0));
            if (selIdx == LB_ERR) {
                MessageBoxA(hwnd, "Selecciona una persona de la lista para eliminar su permiso de acceso.",
                            "Seleccion requerida", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            uint32_t userId = static_cast<uint32_t>(SendMessageA(g_app.hListUsers, LB_GETITEMDATA, selIdx, 0));
            if (g_app.db.eliminarPersonaPorId(userId)) {
                actualizarListaUsuariosUI();
                procesarFotogramaActual(true);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Evita parpadeo al redibujar

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        renderizarVentana(hdc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, ID_TIMER_CAMERA);
        g_app.camera.cerrarCamara();
        if (g_app.hFontTitle)  DeleteObject(g_app.hFontTitle);
        if (g_app.hFontNormal) DeleteObject(g_app.hFontNormal);
        if (g_app.hFontBanner) DeleteObject(g_app.hFontBanner);
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

// Modo de autodiagnóstico por consola (--verificar-sistema)
static int ejecutarVerificacionDeSistema() {
    std::cout << "=========================================================\n";
    std::cout << "  RF-KLAOS: Autodiagnostico de Hardware y Motor Facial\n";
    std::cout << "=========================================================\n";

    std::wstring camName;
    int camCount = 0;
    bool hasCam = CameraHardware::detectarCamaraEnEquipo(camName, camCount);

    std::cout << "[Paso 1] Deteccion de camara en el hardware del equipo:\n";
    if (!hasCam) {
        std::cout << "  -> RESULTADO: No se detecto ninguna camara conectada.\n";
        std::cout << "  -> MENSAJE DEL SISTEMA: No se puede ejecutar el programa porque el equipo no tiene camara.\n";
    } else {
        std::wcout << L"  -> RESULTADO: Camara detectada (" << camCount << L" dispositivo(s)): " << camName << L"\n";
    }

    // Prueba matemática del motor de detección, extracción y base de datos
    std::cout << "\n[Paso 2] Verificando Motor de Deteccion y Base de Datos Propia...\n";
    ImageRGB synthImg;
    synthImg.width = 320;
    synthImg.height = 240;
    synthImg.pixels.assign(320 * 240 * 3, 40);

    // Sintetizamos un rostro de prueba con tono de piel YCbCr y facciones (ojos, nariz, boca)
    for (int y = 50; y < 190; ++y) {
        for (int x = 100; x < 220; ++x) {
            size_t idx = static_cast<size_t>((y * 320 + x) * 3);
            uint8_t r = 215, g = 165, b = 135; // Tono epidérmico
            // Ojos más oscuros
            if (y >= 82 && y <= 105 && ((x >= 120 && x <= 148) || (x >= 172 && x <= 200))) {
                r = 45; g = 35; b = 30;
            }
            // Puente nasal iluminado
            if (y >= 82 && y <= 130 && (x >= 152 && x <= 168)) {
                r = 235; g = 188; b = 158;
            }
            // Boca
            if (y >= 152 && y <= 166 && (x >= 136 && x <= 184)) {
                r = 120; g = 50; b = 55;
            }
            synthImg.pixels[idx + 0] = r;
            synthImg.pixels[idx + 1] = g;
            synthImg.pixels[idx + 2] = b;
        }
    }

    FaceRect detected = FaceEngine::detectarCara(synthImg);
    std::cout << "  -> Deteccion de rostro: " << (detected.detected ? "OK" : "FALLO")
              << " (x=" << detected.x << ", y=" << detected.y
              << ", w=" << detected.width << ", h=" << detected.height << ")\n";

    FaceDescriptor descA{};
    bool extOk = FaceEngine::extraerDescriptorFacial(synthImg, detected, descA);
    std::cout << "  -> Extraccion de descriptor 320-D y foto 64x64: " << (extOk ? "OK" : "FALLO") << "\n";

    // Paso 3: Prueba de Control de Acceso (Persona Autorizada vs Persona No Reconocida)
    std::cout << "\n[Paso 3] Verificando Control de Acceso (Permitir vs Denegar)...\n";
    AuthorizedPersonRecord mockAuthorized{};
    mockAuthorized.id = 1;
    std::snprintf(mockAuthorized.name, sizeof(mockAuthorized.name), "Usuario Autorizado");
    std::memcpy(mockAuthorized.rawFacePhoto, descA.normalizedPhoto, FACE_PHOTO_BYTES);
    std::memcpy(mockAuthorized.descriptor, descA.features, sizeof(mockAuthorized.descriptor));

    std::vector<AuthorizedPersonRecord> mockDb = { mockAuthorized };

    // 3.1 Evaluamos la misma cara autorizada (con ligera variación de ruido)
    AccessControlResult resPermitido = FaceEngine::compararCaraConBaseDeDatos(descA, mockDb, 74.0f);
    std::cout << "  -> Prueba Persona Autorizada: "
              << (resPermitido.accessGranted ? "ACCESO PERMITIDO (OK)" : "ERROR")
              << " | Similitud: " << resPermitido.similarityScore << "% | " << resPermitido.message << "\n";

    // 3.2 Evaluamos una cara distinta (intruso con facciones diferentes)
    FaceDescriptor descIntruso{};
    for (int i = 0; i < FACE_PHOTO_BYTES; ++i) {
        descIntruso.normalizedPhoto[i] = static_cast<uint8_t>((i * 37) % 256);
    }
    for (int i = 0; i < FACE_DESCRIPTOR_SIZE; ++i) {
        descIntruso.features[i] = ((i % 2 == 0) ? 0.08f : 0.0f);
    }
    AccessControlResult resDenegado = FaceEngine::compararCaraConBaseDeDatos(descIntruso, mockDb, 74.0f);
    std::cout << "  -> Prueba Persona NO Autorizada: "
              << (!resDenegado.accessGranted ? "ACCESO DENEGADO (OK)" : "ERROR")
              << " | Similitud: " << resDenegado.similarityScore << "% | " << resDenegado.message << "\n";

    FaceDatabase testDb("rostros_autorizados.db");
    testDb.cargarBaseDeDatos();
    std::cout << "  -> Archivo de base de datos 'rostros_autorizados.db' listo ("
              << testDb.obtenerRegistros().size() << " registros actuales).\n";

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--verificar-sistema") {
        return ejecutarVerificacionDeSistema();
    }

    // =========================================================================
    // PASO 1 OBLIGATORIO:
    // Detectar si el equipo tiene cámara. Si no tiene cámara, enviar un mensaje
    // que diga que no se puede ejecutar el programa y terminar inmediatamente.
    // =========================================================================
    std::wstring nombreCamara;
    int cantidadCamaras = 0;
    if (!CameraHardware::detectarCamaraEnEquipo(nombreCamara, cantidadCamaras)) {
        const wchar_t* msgError =
            L"ERROR DE HARDWARE: No se detectó ninguna cámara web en este equipo.\n\n"
            L"No se puede ejecutar el programa de control de acceso por reconocimiento facial.";
        std::wcerr << L"\n[ERROR] " << msgError << L"\n";
        MessageBoxW(nullptr, msgError, L"RF-KLAOS - No se puede ejecutar el programa", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wcout << L"[OK] Cámara detectada en el equipo: " << nombreCamara
               << L" (" << cantidadCamaras << L" disponible(s)).\n";

    // Cargamos o inicializamos la base de datos propia de personas autorizadas
    g_app.db.cargarBaseDeDatos();

    // Inicializamos la captura de la cámara web
    if (!g_app.camera.inicializarCamara(0)) {
        const wchar_t* msgBusy =
            L"Se detectó una cámara en el equipo, pero no se pudo abrir el flujo de video "
            L"(puede estar siendo usada por otra aplicación).\n\nNo se puede ejecutar el programa.";
        std::wcerr << L"\n[ERROR] " << msgBusy << L"\n";
        MessageBoxW(nullptr, msgBusy, L"RF-KLAOS - Error al iniciar cámara", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Creamos la ventana nativa Win32 para el sistema de control de acceso
    HINSTANCE hInstance = GetModuleHandleA(nullptr);
    const char CLASS_NAME[] = "RF_KLAOS_AccessControlWindow";

    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_SHIELD);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExA(&wc);

    RECT winRect = {0, 0, 1020, 620};
    AdjustWindowRect(&winRect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    g_app.hMainWnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "RF-KLAOS | Control de Acceso por Reconocimiento Facial (100% Codigo Propio)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winRect.right - winRect.left,
        winRect.bottom - winRect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_app.hMainWnd) {
        g_app.camera.cerrarCamara();
        return 1;
    }

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
