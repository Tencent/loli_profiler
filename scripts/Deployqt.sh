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

echo finish Deployqt.....