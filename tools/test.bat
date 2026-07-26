@echo off
setlocal
cd /d "%~dp0.."

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

echo [1/3] configuring (tests on)...
cmake -S . -B build
if errorlevel 1 exit /b 1

echo [2/3] building tests (%CONFIG%)...
cmake --build build --config %CONFIG% --target tests
if errorlevel 1 exit /b 1

echo [3/3] running tests...
build\tests\%CONFIG%\tests.exe