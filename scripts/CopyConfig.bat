@echo off

echo dir %cd%
rem 1. copy loliprofiler/python to build/cmake/bin/release
set PythonPath=%cd%\python

XCOPY /s/y %PythonPath% %RelasePath%

rem 2. copy loli_profiler\plugins\Android\libs  to build/cmake/bin/release/remote
set AndroidLibsPath=%AndroidPluginPath%\libs
set RelaseRemotePath="%RelasePath%\remote"

echo %RelaseRemotePath%
rmdir /s/q %RelaseRemotePath%
md %RelaseRemotePath%
md %RelaseRemotePath%\arm64-v8a
md %RelaseRemotePath%\armeabi-v7a

XCOPY /s/q %AndroidLibsPath%\arm64-v8a %RelaseRemotePath%\arm64-v8a
XCOPY /s/q %AndroidLibsPath%\armeabi-v7a %RelaseRemotePath%\armeabi-v7a

:Exit
exit /B 1