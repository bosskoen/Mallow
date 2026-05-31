@echo off
cd /d %~dp0..

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

call tools\build.bat %CONFIG%
if errorlevel 1 exit /b 1

echo running...
build\modules\core\entry\%CONFIG%\Mallow.exe