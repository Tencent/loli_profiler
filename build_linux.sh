set -xeuo pipefail

# export QT5Path=/Users/xinhou/Qt5.14.1
# for gcc
# export Ndk_R16_CMD=/Users/xinhou/Documents/sdk/android-ndk-r16b/ndk-build
# for llvm
# export Ndk_R20_CMD=/Users/xinhou/Documents/sdk/android-ndk-r20b/ndk-build

# if [ ! -f $QT5Path ];then
#     echo QT5Path not found ERROR: $QT5Path
#     exit -1
# fi
# if [ ! -f $Ndk_R16_CMD ];then
#     echo Ndk_R16_CMD not found ERROR: $Ndk_R16_CMD
#     exit -1
# fi
# if [ ! -f $Ndk_R20_CMD ];then
#     echo Ndk_R20_CMD not found ERROR: $Ndk_R20_CMD
#     exit -1
# fi

echo QT5Path path: $QT5Path
echo Ndk_R16_CMD: $Ndk_R16_CMD
echo Ndk_R20_CMD: $Ndk_R20_CMD

bash scripts/EnterPoint_linux.sh