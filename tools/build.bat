@echo off
cd /d %~dp0..

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

if not exist build\CMakeCache.txt (
    echo [1/2] configuring...
    cmake -S . -B build
    if errorlevel 1 exit /b 1
)


echo [2/2] building...
cmake --build build --config %CONFIG%