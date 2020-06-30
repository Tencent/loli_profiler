@echo off

echo dir %cd%

rem set QT5Path=D:/SDK/QT/5.14.1/msvc2017_64
rem set MSBUILD_EXE="%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe"
rem set Ndk_R16_CMD="E:/SDK/AndroidNDKForUnity/android-ndk-r16b/ndk-build.cmd"
rem set Ndk_R20_CMD="E:/SDK/AndroidNDKForUnity/android-ndk-r20/ndk-build.cmd"

if not exist %QT5Path% (
	echo QT5Path not found ERROR: %QT5Path%
	goto Exit
)

if not exist %MSBUILD_EXE% (
	echo MSBUILD_EXE not found ERROR: %MSBUILD_EXE%
	goto Exit
)

if not exist %Ndk_R16_CMD% (
	echo Ndk_R16_CMD not found ERROR: %Ndk_R16_CMD%
	goto Exit
)

if not exist %Ndk_R20_CMD% (
    echo Ndk_R20_CMD not found ERROR: %Ndk_R20_CMD%
    goto Exit
)

echo QT5Path path : %QT5Path%
echo MSBUILD_EXE path : %MSBUILD_EXE%
echo Ndk_R16_CMD path : %Ndk_R16_CMD%
echo Ndk_R20_CMD path : %Ndk_R20_CMD%

call ./scripts/EnterPoint.bat

:Exit
exit /b %errorlevel%