@echo off
REM docs.bat -- generate API docs into build\docs\html
REM Run from the repo root, or just double-click it there.
 
setlocal
 
REM Configure the build dir once, only if it isn't set up yet.
if not exist "build\CMakeCache.txt" (
    cmake -B build -S . || goto :error
)
 
REM Build only the docs target (skips compiling the rest).
cmake --build build --target docs || goto :error
 
echo.
echo Docs generated: build\docs\html\index.html
start "" "build\docs\html\index.html"
exit /b 0
 
:error
echo.
echo Docs build failed.
exit /b 1
 