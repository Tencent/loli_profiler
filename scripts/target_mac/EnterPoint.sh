set -xeuo pipefail

export AndroidPluginPath="./plugins/Android"
export ReleasePath="./build/cmake/bin/LoliProfiler.app"
export MacdeployqtPath=`find $QT5Path -name "macdeployqt"`
export QT5_Clang=`find $QT5Path -name "clang_64"`
echo deleting dir
rm -rf ./build


sh scripts/target_mac/BuildProject.sh
sh scripts/target_mac/BuildAndroidLibs.sh
sh scripts/target_mac/CopyConfig.sh
sh scripts/target_mac/Deployqt.sh