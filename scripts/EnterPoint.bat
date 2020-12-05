@echo off

echo dir %cd%

set AndroidPluginPath="./plugins/Android"
set RelasePath=".\build\cmake\bin\release"
set WindeployqtPath="%QT5Path:"=%\bin\windeployqt.exe"
set DeployPath=".\dist"

rem delete dir
rmdir /s/q .\build

call ./scripts/BuildProject.bat
call ./scripts/BuildAndroidLibs.bat
call ./scripts/CopyConfig.bat
call ./scripts/Deployqt.bat

echo finish scripts %RelasePath:"=%\LoliProfiler.exe

:Exit
exit /b %errorlevel%