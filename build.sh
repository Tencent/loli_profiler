set -xeuo pipefail

export QT5Path=/Users/zstar/Qt5.14.1
export Ndk_R16_CMD=/Users/zstar/Desktop/android-ndk-r17/ndk-build

#if [ ! -d $QT5Path ];then
#    echo QT5Path not found ERROR: $QT5Path 
#    exit -1
#fi
#
if [ ! -f $Ndk_R16_CMD ];then
    echo Ndk_R16_CMD not found ERROR: $Ndk_R16_CMD
    exit -1
fi

echo QT5Path path: $QT5Path
echo Ndk_R16_CMD: $Ndk_R16_CMD

sh scripts/target_mac/EnterPoint.sh