@echo off
echo Deployqt.....
echo dir %cd%

call %WindeployqtPath% %RelasePath%\LoliProfiler.exe

echo finish Deployqt.....

:Exit
exit /B 1