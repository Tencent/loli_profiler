@echo off

echo BuildAndroidLibs.....
echo dir %cd%

call %Ndk_R16_CMD% NDK_PROJECT_PATH=%AndroidPluginPath%

echo finish BuildAndroidLibs.....

:Exit
exit /B 1