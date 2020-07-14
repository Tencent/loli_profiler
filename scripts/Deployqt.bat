@echo off
echo Deployqt.....
echo dir %cd%

call %WindeployqtPath% %RelasePath%\LoliProfiler.exe

if exist %DeployPath% (
    rmdir /s/q %DeployPath%
)

mkdir %DeployPath%

mkdir %DeployPath%\LoliProfiler\

echo %DeployPath%

xcopy /S /Q %RelasePath%\* %DeployPath%\LoliProfiler\*

powershell Compress-Archive -Path %DeployPath%\LoliProfiler -DestinationPath %DeployPath%\LoliProfiler.zip -Update

echo finish Deployqt.....

:Exit
exit /b %errorlevel%