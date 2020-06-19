set -xeuo pipefail
echo Deployqt.....
pwd
cd $ReleasePath
cd ../
$MacdeployqtPath LoliProfiler.app -dmg -always-overwrite

echo finish Deployqt.....