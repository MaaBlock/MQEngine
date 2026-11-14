@echo off
set "PATH=%PATH:C:\Strawberry\c\bin;=%"
set "PATH=%PATH:C:\Strawberry\perl\bin;=%"
set "PATH=%PATH:C:\Strawberry\perl\site\bin;=%"

call "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl.exe /std:c++20 /utf-8 /EHsc /MDd /Zi /Od /wd4251 /wd4275 ^
  -DABSL_CONSUME_DLL -D_DEBUG ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\generated-include" ^
  -I"G:\MQEngine\MQEngine\Tests\E2E\Common" ^
  -I"G:\MQEngine\MQEngine\Tests\E2E" ^
  -I"G:\MQEngine\MQEngine\Engine\Core\src" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_RHI\src" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Base\src" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Base\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Profiling\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Profiling\ThirdParty\tracy\public" ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\include" ^
  "G:\MQEngine\MQEngine\Tests\Milestone1E2EChallengerTest.cpp" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\Tests\E2E\CMakeFiles\Test_E2E_Tier1.dir\Common\MockRhiContext.cpp.obj" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Profiling\FCT_Profiling.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Base\FCT_Base.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Profiling\tracy\TracyClient.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\debug\lib\abseil_dll.lib" ^
  /link /subsystem:console ^
  /out:"G:\MQEngine\MQEngine\Output\build-windows-debug\Tests\E2E\Milestone1E2EChallengerTest.exe"

exit /b %ERRORLEVEL%
