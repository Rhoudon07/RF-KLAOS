# RF-KLAOS — Sistema de Control de Acceso por Reconocimiento Facial (100% Código Propio)

Sistema de reconocimiento facial y control de acceso escrito en **C++ Puro (C++17)** sin librerías externas, sin frameworks y comunicándose directamente con el hardware del sistema operativo mediante las APIs nativas de Windows (**Win32 + Media Foundation**).

## Características Principales

1. **Verificación de Hardware al Inicio (`CameraHardware::detectarCamaraEnEquipo`)**:
   - Lo primero que hace el programa al ejecutarse es interrogar al bus de dispositivos de video de Windows (`MFEnumDeviceSources`).
   - Si el equipo **no tiene cámara conectada**, muestra un mensaje de error en consola y en ventana emergente indicando que **no se puede ejecutar el programa**, y finaliza de inmediato.

2. **Captura de Cámara Web en Crudo (`CameraHardware`)**:
   - Captura fotogramas directamente desde la cámara web (`RGB32`, `RGB24`, `YUY2` o `NV12`) y realiza la conversión matemática propia YUV $\rightarrow$ RGB en memoria.

3. **Detección de Rostro 100% Propia (`FaceEngine::detectarCara`)**:
   - Calcula **Imágenes Integrales (Summed-Area Tables)** en tiempo $O(W \times H)$ para evaluar cualquier región rectangular en $O(1)$.
   - Aplica segmentación cromática epidérmica en espacio **YCbCr**, filtros geométricos tipo **Haar** (contraste ojos-pómulos y ojos-puente nasal), simetría bilateral izquierda-derecha y densidad de gradientes en ojos y boca.

4. **Extracción y Comparación Facial (`FaceEngine::extraerDescriptorFacial` y `compararCaraConBaseDeDatos`)**:
   - Normaliza el rostro detectado a una matriz canónica de `64x64` píxeles mediante **interpolación bilineal propia** y **ecualización de histograma** (invariante a cambios de iluminación).
   - Extrae un vector matemático de **320 características** combinando **Patrones Binarios Locales (LBP)** ponderados por región facial y perfiles geométricos `8x8`, además de **Correlación Cruzada Normalizada (NCC)** contra la foto almacenada en la base de datos.
   - Si la similitud supera el umbral de seguridad, muestra **ACCESO PERMITIDO** (verde); en caso contrario, muestra **ACCESO DENEGADO** (rojo).

5. **Base de Datos Binaria Propia (`FaceDatabase` / `rostros_autorizados.db`)**:
   - Motor de base de datos propio que almacena en un único archivo binario (`rostros_autorizados.db`) el ID, nombre, fecha de registro, **la foto real del rostro (`64x64`)** y el vector descriptor de cada persona con permiso de entrar.
   - Incluye codificador y decodificador propio de archivos `.bmp` de 24 bits sin usar librerías externas.

## Estructura del Proyecto

- [`src/types.h`](src/types.h): Estructuras de datos puras (`ImageRGB`, `ImageGray`, `FaceRect`, `FaceDescriptor`, `AuthorizedPersonRecord`, `AccessControlResult`).
- [`src/camera_hardware.h`](src/camera_hardware.h) / [`src/camera_hardware.cpp`](src/camera_hardware.cpp): Detección física de cámara en el equipo y captura de fotogramas.
- [`src/face_engine.h`](src/face_engine.h) / [`src/face_engine.cpp`](src/face_engine.cpp): Algoritmo de detección de rostros, extracción de descriptores LBP y comparación contra la base de datos.
- [`src/face_database.h`](src/face_database.h) / [`src/face_database.cpp`](src/face_database.cpp): Lectura/escritura del archivo `rostros_autorizados.db` y soporte de imágenes `.bmp`.
- [`src/main.cpp`](src/main.cpp): Flujo principal de arranque, verificación obligatoria de cámara e interfaz gráfica nativa Win32.
- [`build.bat`](build.bat): Script de compilación directa con el compilador `cl.exe` de Visual Studio.

## Compilación y Ejecución

Para compilar desde consola:

```bat
build.bat
```

Para ejecutar el autodiagnóstico de cámara y motor facial:

```bat
.\bin\RF-KLAOS.exe --verificar-sistema
```

Para abrir el sistema de control de acceso en tiempo real con tu cámara web:

```bat
.\bin\RF-KLAOS.exe
```

