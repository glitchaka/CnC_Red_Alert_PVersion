@echo off
setlocal
cd /d "%~dp0"
set CC_HOST=

echo.
echo CLICK COMMAND - HOST ONLINE
echo ===========================
echo Puerto UDP: 46000
echo.
call build.cmd
