@echo off
setlocal
pushd "%~dp0" || exit /b 1
set "pak_demo_exe=dist\pak-inspector.exe"
if not exist "%pak_demo_exe%" set "pak_demo_exe=build\Release\pak-inspector.exe"
if not exist "%pak_demo_exe%" set "pak_demo_exe=build\pak-inspector.exe"
if not exist "%pak_demo_exe%" (
    echo Executavel ausente. Consulte a secao Compilacao e testes no README.md.
    goto failed
)
echo === Quake PAK Inspector ^| Conteudo do pacote base ===
"%pak_demo_exe%" list examples\base.pak || goto failed
echo.
echo === Arquivo de configuracao em hexadecimal ===
"%pak_demo_exe%" inspect examples\base.pak config/arena.cfg || goto failed
echo.
echo === Comparacao com a versao reempacotada ===
"%pak_demo_exe%" diff examples\base.pak examples\repacked.pak || goto failed
echo.
echo === Pacote que fornece a configuracao apos o patch ===
"%pak_demo_exe%" overlay config/arena.cfg examples\base.pak examples\patch.pak || goto failed
popd
if /i not "%~1"=="--no-pause" pause
exit /b 0

:failed
popd
if /i not "%~1"=="--no-pause" pause
exit /b 1
