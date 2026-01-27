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

echo Copying Python analysis scripts...
copy /Y "%~dp0..\analyze_memory_diff.py" "%DeployPath%\LoliProfiler\"
copy /Y "%~dp0..\preprocess_memory_diff.py" "%DeployPath%\LoliProfiler\"

powershell Compress-Archive -Path %DeployPath%\LoliProfiler -DestinationPath %DeployPath%\LoliProfiler.zip -Update

echo finish Deployqt.....

:Exit
exit /b %errorlevel%