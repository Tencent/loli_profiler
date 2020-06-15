@echo off

echo dir %cd%

set AndroidPluginPath="./plugins/Android"
set RelasePath=".\build\cmake\bin\release"
set WindeployqtPath=%QT5Path%/bin/windeployqt.exe

rem delete dir
rmdir /s/q .\build

call ./WinBuild/BuildProject.bat
call ./WinBuild/BuildAndroidLibs.bat
call ./WinBuild/CopyConfig.bat
call ./WinBuild/Deployqt.bat

echo finsih WinBuild %RelasePath%\LoliProfiler.exe

:Exit
exit /B 1