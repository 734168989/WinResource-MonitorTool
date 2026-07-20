@echo off
cd /d "%~dp0"
echo ============================================
echo  MonitorTool V3.4 — Build & Generate VS Solution
echo ============================================

rem Step 1: Generate VS solution from CMakeLists.txt
echo [1/2] Generating VS solution...
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configure failed!
    pause
    exit /b 1
)

rem Step 2: Build Release
echo [2/2] Building Release...
cmake --build build --config Release
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Build complete!
echo    Executable: out\MonitorTool.exe
echo    Solution:   MonitorTool.sln
echo                 ^(opens build\*.vcxproj projects^)
echo ============================================
pause
