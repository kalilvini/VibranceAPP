@echo off
setlocal
echo ========================================================
echo   Configurando Inicializacao com o Windows - CS2Vibrance
echo ========================================================
set "EXE_PATH=%~dp0bin\CS2Vibrance.exe"
if not exist "%EXE_PATH%" (
    set "EXE_PATH=%~dp0CS2Vibrance.exe"
)
if not exist "%EXE_PATH%" (
    echo [ERRO] O arquivo CS2Vibrance.exe nao foi encontrado nesta pasta.
    pause
    exit /b 1
)

reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CS2Vibrance" /t REG_SZ /d "\"%EXE_PATH%\"" /f

if %errorlevel% equ 0 (
    echo.
    echo [SUCESSO] CS2Vibrance configurado para iniciar automaticamente com o Windows!
    echo O aplicativo ficara na bandeja do sistema (proximo ao relogio) consumindo ~1 MB de RAM.
) else (
    echo.
    echo [ERRO] Falha ao registrar no Registro do Windows.
)
echo.
pause
