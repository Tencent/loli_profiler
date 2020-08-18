@echo off

echo dir %cd%
rem 1. copy loliprofiler/python to build/cmake/bin/release
set PythonPath=%cd%\python

XCOPY /s/y %PythonPath% %RelasePath%\

rem 2. copy loli_profiler\plugins\Android  to build/cmake/bin/release/remote
set AndroidLibsPath=%AndroidPluginPath%
set AndroidLibsPath2=%AndroidPluginPath2%
set RelaseRemotePath="%RelasePath:"=%\remote"
set RelasePluginPath="%RelasePath:"=%\plugins"

echo %RelaseRemotePath%
rmdir /s/q %RelaseRemotePath%
md %RelaseRemotePath%
md %RelaseRemotePath%\llvm
md %RelaseRemotePath%\llvm\arm64-v8a
md %RelaseRemotePath%\llvm\armeabi-v7a
md %RelaseRemotePath%\gcc
md %RelaseRemotePath%\gcc\arm64-v8a
md %RelaseRemotePath%\gcc\armeabi-v7a
md %RelaseRemotePath%\gcc\armeabi

XCOPY /s/q %AndroidLibsPath%\gcc\arm64-v8a %RelaseRemotePath%\gcc\arm64-v8a
XCOPY /s/q %AndroidLibsPath%\gcc\armeabi-v7a %RelaseRemotePath%\gcc\armeabi-v7a
XCOPY /s/q %AndroidLibsPath%\gcc\armeabi %RelaseRemotePath%\gcc\armeabi

XCOPY /s/q %AndroidLibsPath%\llvm\arm64-v8a %RelaseRemotePath%\llvm\arm64-v8a
XCOPY /s/q %AndroidLibsPath%\llvm\armeabi-v7a %RelaseRemotePath%\llvm\armeabi-v7a

echo %RelasePluginPath%
rmdir /s/q %RelasePluginPath%
md %RelasePluginPath%\jdwputil
md %RelasePluginPath%\jdwputil\llvm
md %RelasePluginPath%\jdwputil\llvm\arm64-v8a
md %RelasePluginPath%\jdwputil\llvm\armeabi-v7a
md %RelasePluginPath%\jdwputil\gcc
md %RelasePluginPath%\jdwputil\gcc\arm64-v8a
md %RelasePluginPath%\jdwputil\gcc\armeabi-v7a

XCOPY /s/q %AndroidLibsPath2%\gcc\arm64-v8a %RelasePluginPath%\jdwputil\gcc\arm64-v8a
XCOPY /s/q %AndroidLibsPath2%\gcc\armeabi-v7a %RelasePluginPath%\jdwputil\gcc\armeabi-v7a

XCOPY /s/q %AndroidLibsPath2%\llvm\arm64-v8a %RelasePluginPath%\jdwputil\llvm\arm64-v8a
XCOPY /s/q %AndroidLibsPath2%\llvm\armeabi-v7a %RelasePluginPath%\jdwputil\llvm\armeabi-v7a

XCOPY /S/Q template\* %RelasePluginPath%\*

:Exit
exit /b %errorlevel%