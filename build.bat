@echo off
rem Configure + build + unit-test with the VS 2026 toolchain and its bundled CMake/Ninja.
rem   build.bat        build, copy runtime DLLs next to the exes, run tst_core
rem   build.bat dist   additionally stage dist\ and zip it
cd /d %~dp0
set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%PATH%;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 exit /b 1
cmake --build build
if errorlevel 1 exit /b 1

rem minimal runtime set so build\*.exe run standalone (verify against dumpbin /dependents)
for /d %%v in (C:\Qt\6.*) do for /d %%q in (%%v\msvc*_64) do (
  for %%d in (Qt6Core Qt6Gui Qt6Widgets Qt6SerialPort Qt6Test Qt6PrintSupport) do copy /y "%%q\bin\%%d.dll" build\ >nul
  if not exist build\platforms mkdir build\platforms
  copy /y "%%q\plugins\platforms\qwindows.dll" build\platforms\ >nul
  if not exist build\styles mkdir build\styles
  copy /y "%%q\plugins\styles\qmodernwindowsstyle.dll" build\styles\ >nul
  if not exist build\imageformats mkdir build\imageformats
  copy /y "%%q\plugins\imageformats\qjpeg.dll" build\imageformats\ >nul
)
for /d %%r in ("%VSROOT%\VC\Redist\MSVC\14.*") do for %%d in (msvcp140 vcruntime140 vcruntime140_1) do copy /y "%%r\x64\Microsoft.VC145.CRT\%%d.dll" build\ >nul

ctest --test-dir build --output-on-failure
if errorlevel 1 exit /b 1

if /i not "%1"=="dist" exit /b 0
if exist dist rmdir /s /q dist
mkdir dist\platforms dist\styles dist\imageformats
copy /y build\tinySA-App.exe dist\ >nul
for %%d in (Qt6Core Qt6Gui Qt6Widgets Qt6SerialPort Qt6Test Qt6PrintSupport msvcp140 vcruntime140 vcruntime140_1) do copy /y build\%%d.dll dist\ >nul
copy /y build\platforms\qwindows.dll dist\platforms\ >nul
copy /y build\styles\qmodernwindowsstyle.dll dist\styles\ >nul
copy /y build\imageformats\qjpeg.dll dist\imageformats\ >nul
powershell -NoProfile -Command "Compress-Archive -Path dist\* -DestinationPath tinySA-App.zip -Force" >nul
