@echo off
rem [DA_PORT] Сборка прослойки NGX. Нужна ТОЛЬКО при правке da_ngx_shim.cpp.
rem Готовая da_ngx.dll лежит рядом и коммитится, поэтому обычная сборка игры MSVC не требует.
rem
rem advapi32 и user32 обязательны: NGX читает реестр (Reg*) и зовёт GetWindowThreadProcessId.
rem Без них линковка падает на четырёх символах, и выглядит это как поломка самой NGX.

setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [!] Не найден vcvars64.bat: "%VCVARS%"
    echo     Поправь путь здесь, если Visual Studio стоит в другом месте.
    exit /b 1
)
call "%VCVARS%" >nul

set "NGX=%~dp0..\nvngx"

cl /nologo /LD /EHsc /MD /O2 /W3 ^
   /I"%NGX%\include" /I"%~dp0." /DDA_NGX_BUILDING ^
   da_ngx_shim.cpp ^
   /link "%NGX%\lib\Windows_x86_64\x64\nvsdk_ngx_d.lib" ^
   d3d11.lib advapi32.lib user32.lib ^
   /OUT:da_ngx.dll

if errorlevel 1 (
    echo [!] Сборка прослойки не удалась
    exit /b 1
)

del /q da_ngx_shim.obj da_ngx_shim.exp 2>nul
echo [+] da_ngx.dll собрана
endlocal
