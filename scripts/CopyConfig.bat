@echo off

echo dir %cd%
rem 1. copy loliprofiler/python to build/cmake/bin/release
set PythonPath=%cd%\python

XCOPY /s/y %PythonPath% %RelasePath%\

rem 2. copy loli_profiler\plugins\Android  to build/cmake/bin/release/remote
set AndroidLibsPath=%AndroidPluginPath%
set RelaseRemotePath="%RelasePath:"=%\remote"

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

:Exit
exit /b %errorlevel%