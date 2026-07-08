@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > NUL
cd /d "D:\vscode_project\MonitorTool C++"
rmdir /s /q out\build\x64-Debug 2>nul
cmake -B out\build\x64-Debug -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out\build\x64-Debug --config Debug
echo.
echo === BUILD DONE ===
