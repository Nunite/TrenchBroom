@echo off
set BUILD_DIR=%1
if "%BUILD_DIR%"=="" set BUILD_DIR=build-release-codex

call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

cmake -S . -B "%BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl.exe ^
  -DCMAKE_CXX_COMPILER=cl.exe ^
  -DCMAKE_RC_COMPILER="D:/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" ^
  -DCMAKE_MT="D:/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe" ^
  -DCMAKE_PREFIX_PATH="D:/Qtx/6.9.3/msvc2022_64"
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --target TrenchBroom --config Release --parallel
exit /b %errorlevel%
