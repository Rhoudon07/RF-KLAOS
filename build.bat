@echo off
setlocal

echo =========================================================
echo   Compilando RF-KLAOS (C++ Puro - Sin Librerias Externas)
echo =========================================================

set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if "%VCVARS%"=="" (
    echo [ERROR] No se encontro vcvars64.bat de Visual Studio.
    exit /b 1
)

call "%VCVARS%" >nul

if not exist "bin" mkdir "bin"

cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS ^
    src\main.cpp ^
    src\camera_hardware.cpp ^
    src\face_engine.cpp ^
    src\face_database.cpp ^
    /Fe:bin\RF-KLAOS.exe ^
    /Fo:bin\ ^
    mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib user32.lib gdi32.lib comdlg32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [OK] Compilacion exitosa: bin\RF-KLAOS.exe
) else (
    echo.
    echo [ERROR] Fallo la compilacion.
    exit /b %ERRORLEVEL%
)

