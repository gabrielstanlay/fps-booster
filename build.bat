@echo off
setlocal
title Boost - compilar
cd /d "%~dp0"

echo ==================================================
echo   FPS Booster - compilando a source
echo ==================================================
echo.

REM ---------------------------------------------------------------
REM  1) procura o compilador da Microsoft (MSVC)
REM ---------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
set "VCVARS="

if not exist "%VSWHERE%" goto :sem_compilador

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSPATH=%%i"
if defined VSPATH goto :achou_vs

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath 2^>nul`) do set "VSPATH=%%i"

:achou_vs
if not defined VSPATH goto :sem_compilador
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :sem_compilador
set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"

echo Compilador: %VSPATH%
echo.
call "%VCVARS%" >nul 2>&1

REM ---------------------------------------------------------------
REM  2) compila
REM ---------------------------------------------------------------
if not exist "build\obj" mkdir "build\obj"

rc.exe /nologo /fo build\obj\version.res src\version.rc
if errorlevel 1 goto :erro_compilacao

cl.exe /nologo /std:c++17 /EHsc /MT /MP /O2 /W3 /utf-8 ^
    /DNDEBUG /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    /I"." /I"backends" /I"src" ^
    /Fobuild\obj\ /Febuild\fpsbooster.exe ^
    src\main.cpp src\menu.cpp src\theme.cpp src\icons.cpp src\cleaner.cpp src\optimizer.cpp ^
    imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp ^
    backends\imgui_impl_win32.cpp backends\imgui_impl_dx11.cpp build\obj\version.res ^
    /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup ^
    /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest ^
    /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" ^
    d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib

if errorlevel 1 goto :erro_compilacao

echo.
echo ==================================================
echo   [OK] Compilado em: build\fpsbooster.exe
echo ==================================================
echo.
choice /c SN /n /m "Abrir o programa agora? [S/N] "
if errorlevel 2 goto :fim
start "" "build\fpsbooster.exe"
goto :fim

REM ---------------------------------------------------------------
:sem_compilador
echo [ERRO] Nao encontrei o compilador MSVC neste computador.
echo.
echo Instale o "Visual Studio Build Tools" ou o "Visual Studio Community"
echo marcando a carga de trabalho "Desenvolvimento para desktop com C++":
echo https://visualstudio.microsoft.com/pt-br/downloads/
echo.
pause
exit /b 1

REM ---------------------------------------------------------------
:erro_compilacao
echo.
echo [ERRO] A compilacao falhou. Veja as mensagens acima.
echo.
pause
exit /b 1

REM ---------------------------------------------------------------
:fim
endlocal
