@echo off
echo Deployqt.....
echo dir %cd%

call %WindeployqtPath% %RelasePath%\LoliProfiler.exe

if not exist %DeployPath% (
    mkdir %DeployPath%
)

powershell Compress-Archive -Path %RelasePath% -DestinationPath %DeployPath%\LoliProfiler.zip -Update

echo finish Deployqt.....

:Exit
exit /b %errorlevel%