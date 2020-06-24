@echo off

echo BuildAndroidLibs.....
echo dir %cd%

call %Ndk_R16_CMD% NDK_PROJECT_PATH=%AndroidPluginPath% -B NDK_LIBS_OUT=%AndroidPluginPath%/gcc

echo finish build android library with gcc......

call %Ndk_R20_CMD% NDK_PROJECT_PATH=%AndroidPluginPath% -B LLVM=1 NDK_LIBS_OUT=%AndroidPluginPath%/llvm

echo finish build android library with llvm......

:Exit
exit /B 1