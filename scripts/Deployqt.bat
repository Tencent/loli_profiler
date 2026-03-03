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
copy /Y "%~dp0..\markdown_to_html.py" "%DeployPath%\LoliProfiler\"
copy /Y "%~dp0..\analyze_heap.py" "%DeployPath%\LoliProfiler\"
copy /Y "%~dp0..\requirements.txt" "%DeployPath%\LoliProfiler\"

echo Copying MCP server files...
mkdir "%DeployPath%\LoliProfiler\mcp_server"
copy /Y "%~dp0..\mcp_server\__init__.py" "%DeployPath%\LoliProfiler\mcp_server\"
copy /Y "%~dp0..\mcp_server\tree_model.py" "%DeployPath%\LoliProfiler\mcp_server\"
copy /Y "%~dp0..\mcp_server\heap_explorer_server.py" "%DeployPath%\LoliProfiler\mcp_server\"
copy /Y "%~dp0..\.mcp.json" "%DeployPath%\LoliProfiler\"

powershell Compress-Archive -Path %DeployPath%\LoliProfiler -DestinationPath %DeployPath%\LoliProfiler.zip -Update

echo finish Deployqt.....

:Exit
exit /b %errorlevel%