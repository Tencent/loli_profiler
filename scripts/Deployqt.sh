set -xeuo pipefail
echo Deployqt.....
pwd
pushd .
cd $ReleasePath
cd ../
$MacdeployqtPath LoliProfiler.app -dmg -always-overwrite

popd

mkdir $DeployPath

cp $ReleasePath/../LoliProfiler.dmg $DeployPath

echo Copying Python analysis scripts...
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cp "$SCRIPT_DIR/../analyze_memory_diff.py" "$DeployPath/"
cp "$SCRIPT_DIR/../preprocess_memory_diff.py" "$DeployPath/"
cp "$SCRIPT_DIR/../markdown_to_html.py" "$DeployPath/"
cp "$SCRIPT_DIR/../analyze_heap.py" "$DeployPath/"
cp "$SCRIPT_DIR/../requirements.txt" "$DeployPath/"

echo Copying MCP server files...
mkdir -p "$DeployPath/mcp_server"
cp "$SCRIPT_DIR/../mcp_server/__init__.py" "$DeployPath/mcp_server/"
cp "$SCRIPT_DIR/../mcp_server/tree_model.py" "$DeployPath/mcp_server/"
cp "$SCRIPT_DIR/../mcp_server/heap_explorer_server.py" "$DeployPath/mcp_server/"

echo finish Deployqt.....