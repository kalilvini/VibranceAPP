@echo off
setlocal
echo ========================================================
echo   Removendo Inicializacao com o Windows - CS2Vibrance
echo ========================================================
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CS2Vibrance" /f

if %errorlevel% equ 0 (
    echo.
    echo [SUCESSO] Inicializacao automatica removida com sucesso.
) else (
    echo.
    echo [AVISO] Nenhuma entrada de inicializacao encontrada ou ja removida.
)
echo.
pause
