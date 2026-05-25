@echo off
cd /d %~dp0..

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

call tools\build.bat %CONFIG%

echo running...
build\modules\core\entry\%CONFIG%\Mallow.exe