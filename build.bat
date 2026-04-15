@echo off
setlocal

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build --config Release
if errorlevel 1 exit /b 1

endlocal
