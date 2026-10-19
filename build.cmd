@echo off
setlocal

cd /d "%~dp0"

echo [1/2] Configuring Red Alert Framework...
cmake -S . -B build -A x64
if errorlevel 1 goto :error

echo [2/2] Building framework sandbox...
cmake --build build --config Release --target ra_sandbox --parallel
if errorlevel 1 goto :error

echo.
echo Build finished.
echo Sandbox: build\bin\Release\ra_sandbox.exe
echo Library: build\lib\Release\ra_engine.lib
exit /b 0

:error
echo.
echo Build failed.
exit /b 1
