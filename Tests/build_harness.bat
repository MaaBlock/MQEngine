@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl.exe /std:c++20 /utf-8 /EHsc /MDd /Zi /Od /wd4251 /wd4275 ^
  -DABSL_CONSUME_DLL -D_DEBUG ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\generated-include" ^
  -I"G:\MQEngine\MQEngine\Engine\Core\src" ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\include-src\src" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_RHI\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Base\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Node\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Node\node\src" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Node\node\deps\v8\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Node\node\deps\uv\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_SAI\include" ^
  -I"G:\MQEngine\MQEngine\FCT\NBR\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_PyBri\include" ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\include\python3.11" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_IMGUI\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_IMGUI\imgui" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_IMGUI\imgui\backends" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_IMGUI\imnodes" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_RHI\ThirdParty\work_contract\src\library\work_contract\..\.." ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\include" ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\include\physx" ^
  -I"G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\include\spirv_cross" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Profiling\include" ^
  -I"G:\MQEngine\MQEngine\FCT\FCT_Profiling\ThirdParty\tracy\public" ^
  "G:\MQEngine\MQEngine\Tests\Milestone2CompositeHarness.cpp" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\Engine\Core\Engine.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_RHI\FCT.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Profiling\FCT_Profiling.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Base\FCT_Base.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Node\FCT_Node.lib" ^
  "G:\MQEngine\MQEngine\FCT\FCT_Node\node\Win\bin\debug\libnode.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\debug\lib\boost_serialization-vc143-mt-gd-x64-1_86.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_PyBri\FCT_PyBri.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\lib\python311.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\FCT\FCT_Profiling\tracy\TracyClient.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\lib\work_contract.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\debug\lib\abseil_dll.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\debug\lib\spdlogd.lib" ^
  "G:\MQEngine\MQEngine\Output\build-windows-debug\vcpkg_installed\x64-windows\debug\lib\fmtd.lib" ^
  /Fe:"G:\MQEngine\MQEngine\Output\build-windows-debug\Applications\Editor\Milestone2CompositeHarness.exe" ^
  /Fo:"G:\MQEngine\MQEngine\Output\build-windows-debug\Applications\Editor\Milestone2CompositeHarness.obj"

exit /b %ERRORLEVEL%
