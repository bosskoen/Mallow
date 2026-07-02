@echo off
cd /d %~dp0..

echo [1/6] configuring test_runner...
cmake -S tools/test_runner -B tools/test_runner/build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

echo [2/6] building test_runner...
cmake --build tools/test_runner/build --config Release
if errorlevel 1 exit /b 1

echo [3/6] running test_runner...
tools\test_runner\build\Release\test_runner.exe
if errorlevel 1 exit /b 1

echo [4/6] configuring generated tests...
cmake -S generated/tests -B generated/tests/build
if errorlevel 1 exit /b 1

echo [5/6] building generated tests...
cmake --build generated/tests/build --config Release
if errorlevel 1 exit /b 1

echo [6/6] running tests...
generated\tests\build\Release\all_tests.exe