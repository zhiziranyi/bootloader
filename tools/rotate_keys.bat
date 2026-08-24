@echo off
rem One-click FlashSafe Pro key rotation.
rem Double-click this file.
setlocal
cd /d "%~dp0.."

where python >nul 2>nul
if errorlevel 1 (
    echo Python was not found in PATH. Please install Python 3 and add it to PATH.
    pause
    exit /b 1
)

python tools\regenerate_keys.py %*
if errorlevel 1 (
    echo.
    echo Key rotation FAILED. See messages above.
    pause
    exit /b 1
)

echo.
echo Done. You can close this window.
pause
