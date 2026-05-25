@echo off
cd /d %~dp0..

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

call tools\clean.bat
call tools\build.bat %CONFIG%