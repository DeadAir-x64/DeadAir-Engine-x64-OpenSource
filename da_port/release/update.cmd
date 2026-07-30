@echo off
rem Dead Air x64 updater - launcher for updater.ps1
rem
rem Only ASCII here on purpose: a .cmd is read in the system ANSI codepage, and Cyrillic in a .cmd
rem turns to garbage on machines with another codepage. All messages live in the .ps1, which is
rem saved as UTF-8 with BOM and therefore always readable.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0updater.ps1"
echo.
pause
