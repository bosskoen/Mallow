@echo off
cd /d %~dp0..

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

echo [1/2] configuring...
cmake -S . -B build -DCMAKE_BUILD_TYPE=%CONFIG% -DMLW_NO_DEFAULT_LIBS=ON 
if errorlevel 1 exit /b 1

echo [2/2] building...
cmake --build build --config %CONFIG%