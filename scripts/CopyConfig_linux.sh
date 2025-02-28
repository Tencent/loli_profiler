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