set -xeuo pipefail

echo BuildAndroidLibs...

$Ndk_R16_CMD NDK_PROJECT_PATH=$AndroidPluginPath

echo finish BuildAndroidLibs......
