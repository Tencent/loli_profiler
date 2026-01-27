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

echo finish Deployqt.....