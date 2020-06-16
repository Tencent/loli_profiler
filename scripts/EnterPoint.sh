set -xeuo pipefail

export AndroidPluginPath="./plugins/Android"
export ReleasePath="./build/cmake/bin/release/LoliProfiler.app"
export MacdeployqtPath=`find $QT5Path -name "macdeployqt"`
export QT5_Clang=`find $QT5Path -name "clang_64"`
echo deleting dir
rm -rf ./build


sh scripts/BuildProject.sh
sh scripts/BuildAndroidLibs.sh
sh scripts/CopyConfig.sh
sh scripts/Deployqt.sh