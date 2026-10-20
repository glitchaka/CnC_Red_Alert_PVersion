@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

if exist "RA-Framework.exe" del /q "RA-Framework.exe" >nul 2>nul

cmake -S . -B build -A x64
if errorlevel 1 goto :error

cmake --build build --config Release --target ra_sandbox --parallel
if errorlevel 1 goto :error

if not exist "build\bin\Release\ra_sandbox.exe" (
    echo ERROR: la compilacion termino sin generar ra_sandbox.exe.
    exit /b 1
)

copy /y "build\bin\Release\ra_sandbox.exe" "RA-Framework.exe" >nul
if exist "build\bin\Release\assets" (
    if exist "assets" rmdir /s /q "assets"
    xcopy /e /i /y "build\bin\Release\assets" "assets" >nul
)

echo.
echo LISTO: %CD%\RA-Framework.exe
start "" "%CD%\RA-Framework.exe"
exit /b 0

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
