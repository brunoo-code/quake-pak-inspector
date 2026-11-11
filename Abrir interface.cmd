@echo off
setlocal
pushd "%~dp0" || exit /b 1
set "pak_gui_exe=dist\pak-inspector-gui.exe"
if not exist "%pak_gui_exe%" set "pak_gui_exe=build\Release\pak-inspector-gui.exe"
if not exist "%pak_gui_exe%" set "pak_gui_exe=build\pak-inspector-gui.exe"
if not exist "%pak_gui_exe%" (
    echo Executavel ausente. Consulte Compilacao e testes no README.md.
    popd
    pause
    exit /b 1
)
start "" "%pak_gui_exe%" "examples\base.pak" "examples\repacked.pak"
popd
