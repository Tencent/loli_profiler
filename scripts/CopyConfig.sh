set -xeuo pipefail
echo 1. copy loliprofiler/python to build/cmake/bin/release
PythonPath=./python
cp -r $PythonPath/* $ReleasePath/Contents/MacOS/

echo 2. copy loli_profiler/plugins/Android  to build/cmake/bin/release/remote

AndroidLibsPath=$AndroidPluginPath
AndroidLibsPath2=$AndroidPluginPath2
ReleaseRemotePath="$ReleasePath/Contents/MacOS/remote"
ReleasePluginPath="$ReleasePath/Contents/MacOS/plugins"

#rm -rf $ReleaseRemotePath
mkdir -p $ReleaseRemotePath
mkdir -p $ReleaseRemotePath/llvm
mkdir -p $ReleaseRemotePath/gcc

mkdir -p $ReleasePluginPath
mkdir -p $ReleasePluginPath/jdwputil
mkdir -p $ReleasePluginPath/jdwputil/llvm
mkdir -p $ReleasePluginPath/jdwputil/gcc

for i in armeabi-v7a arm64-v8a armeabi
do
cp -r $AndroidLibsPath/gcc/$i $ReleaseRemotePath/gcc/$i
done

for i in armeabi-v7a arm64-v8a
do
cp -r $AndroidLibsPath/llvm/$i $ReleaseRemotePath/llvm/$i
cp -r $AndroidLibsPath2/llvm/$i $ReleasePluginPath/jdwputil/llvm/$i
cp -r $AndroidLibsPath2/gcc/$i $ReleasePluginPath/jdwputil/gcc/$i
done

cp -R template/* $ReleasePluginPath

mkdir $ReleasePath/Contents/Resources/
cp res/devices.icns $ReleasePath/Contents/Resources/