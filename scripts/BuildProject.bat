@echo off

echo BuildProject.....
echo dir %cd%

cmake -G "Visual Studio 15 2017" -A x64 -B "build/cmake" -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_PREFIX_PATH=%QT5Path%

echo makeProject...

%MSBUILD_EXE% .\build\cmake\INSTALL.vcxproj /t:Rebuild /t:Clean /property:Configuration=Release /property:Platform=x64

echo finish BuildProject...

:Exit
exit /B 1