@echo off
rem Start the FlashSafe Pro key-rotation web UI (localhost only).
rem Double-click this file, then open http://127.0.0.1:8765
setlocal
cd /d "%~dp0.."

where python >nul 2>nul
if errorlevel 1 (
    echo Python was not found in PATH. Please install Python 3 and add it to PATH.
    pause
    exit /b 1
)

echo Starting FlashSafe Pro web UI at http://127.0.0.1:8765
echo Keep this window open while using the page. Close it to stop the server.
echo.
python tools\webui.py --port 8765
pause
