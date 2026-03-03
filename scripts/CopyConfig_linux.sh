set -xeuo pipefail
echo 1. copy loliprofiler/python to build/cmake/bin/release
PythonPath=./python
cp -r $PythonPath/* $ReleasePath/

echo 2. copy loli_profiler/plugins/Android  to build/cmake/bin/release/remote

AndroidLibsPath=$AndroidPluginPath
ReleaseRemotePath="$ReleasePath/remote"

#rm -rf $ReleaseRemotePath
mkdir -p $ReleaseRemotePath
mkdir -p $ReleaseRemotePath/llvm
mkdir -p $ReleaseRemotePath/gcc

for i in armeabi-v7a arm64-v8a armeabi
do
cp -r $AndroidLibsPath/gcc/$i $ReleaseRemotePath/gcc/$i
done

for i in armeabi-v7a arm64-v8a
do
cp -r $AndroidLibsPath/llvm/$i $ReleaseRemotePath/llvm/$i
done

mkdir $ReleasePath/Resources/
cp res/devices.icns $ReleasePath/Resources/

echo 3. copy Python analysis scripts and MCP server to release
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cp "$SCRIPT_DIR/../analyze_memory_diff.py" "$ReleasePath/"
cp "$SCRIPT_DIR/../preprocess_memory_diff.py" "$ReleasePath/"
cp "$SCRIPT_DIR/../markdown_to_html.py" "$ReleasePath/"
cp "$SCRIPT_DIR/../analyze_heap.py" "$ReleasePath/"
cp "$SCRIPT_DIR/../requirements.txt" "$ReleasePath/"

mkdir -p "$ReleasePath/mcp_server"
cp "$SCRIPT_DIR/../mcp_server/__init__.py" "$ReleasePath/mcp_server/"
cp "$SCRIPT_DIR/../mcp_server/tree_model.py" "$ReleasePath/mcp_server/"
cp "$SCRIPT_DIR/../mcp_server/heap_explorer_server.py" "$ReleasePath/mcp_server/"
cp "$SCRIPT_DIR/../.mcp.json" "$ReleasePath/"