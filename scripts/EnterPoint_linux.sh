set -xeuo pipefail

export AndroidPluginPath="./plugins/Android"
export ReleasePath="./build/cmake/bin/release"
export MacdeployqtPath=`find $QT5Path -name "macdeployqt"`
export QT5_Clang=`find $QT5Path -name "clang_64"`
export DeployPath="./dist"
echo deleting dir
rm -rf ./build
rm -rf ./dist

mkdir -p $ReleasePath

bash scripts/BuildProject.sh
bash scripts/BuildAndroidLibs.sh
bash scripts/CopyConfig_linux.sh