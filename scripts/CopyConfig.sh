set -xeuo pipefail
echo 1. copy loliprofiler/python to build/cmake/bin/release
PythonPath=./python
cp -r $PythonPath/* $ReleasePath/Contents/MacOS/

echo 2. copy loli_profiler\plugins\Android\libs  to build/cmake/bin/release/remote

AndroidLibsPath=$AndroidPluginPath/libs
ReleaseRemotePath="$ReleasePath/Contents/MacOS/remote"

#rm -rf $ReleaseRemotePath
mkdir -p $ReleaseRemotePath

for i in armeabi-v7a arm64-v8a
do
cp -r $AndroidLibsPath/$i $ReleaseRemotePath/$i
done

mkdir $ReleasePath/Contents/Resources/
cp res/devices.icns $ReleasePath/Contents/Resources/