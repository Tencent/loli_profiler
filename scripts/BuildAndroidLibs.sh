set -xeuo pipefail

echo BuildAndroidLibs...

$Ndk_R16_CMD NDK_PROJECT_PATH=$AndroidPluginPath -B NDK_LIBS_OUT=$AndroidPluginPath/gcc

echo finish build android library with gcc......

$Ndk_R20_CMD NDK_PROJECT_PATH=$AndroidPluginPath -B LLVM=1 NDK_LIBS_OUT=$AndroidPluginPath/llvm

echo finish build android library with llvm......

$Ndk_R16_CMD NDK_PROJECT_PATH=$AndroidPluginPath2 -B NDK_LIBS_OUT=$AndroidPluginPath2/gcc

echo finish build android library with gcc......

$Ndk_R20_CMD NDK_PROJECT_PATH=$AndroidPluginPath2 -B LLVM=1 NDK_LIBS_OUT=$AndroidPluginPath2/llvm

echo finish build android library with llvm......