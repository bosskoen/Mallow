@echo off
cd /d %~dp0..

echo cleaning build...
if exist build rmdir /s /q build

echo cleaning generated...
if exist generated rmdir /s /q generated

echo done