@echo off
rem Copyright (c) 2026 Nordic Semiconductor ASA
rem SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

py -3.12 -c "import struct; import sys; sys.exit(0 if struct.calcsize('P') == 8 else '64-bit Python 3.12 is required.')"
if errorlevel 1 goto failed

py -3.12 -m venv .venv-build
if errorlevel 1 goto failed

".venv-build\Scripts\python.exe" -m pip install -r requirements-build.txt
if errorlevel 1 goto failed

set QT_QPA_PLATFORM=offscreen
".venv-build\Scripts\python.exe" -m unittest -v
if errorlevel 1 goto failed

".venv-build\Scripts\python.exe" -m PyInstaller --noconfirm --clean --onefile --windowed --noupx --name MultichannelAuracast --distpath dist --workpath build usb_audio_source.py
if errorlevel 1 goto failed
if not exist "dist\MultichannelAuracast.exe" goto failed

echo.
echo Build succeeded: "%CD%\dist\MultichannelAuracast.exe"
echo Copy this executable to a Windows 10/11 x64 PC; Python is not required.
popd
pause
exit /b 0

:failed
echo.
echo Build failed. Review the error above.
echo Install 64-bit Python 3.12 with the Python launcher before retrying.
popd
pause
exit /b 1
