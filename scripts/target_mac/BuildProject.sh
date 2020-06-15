set -xeuo pipefail

echo makeProject...

cmake . -B "build/cmake" -DCMAKE_INSTALL_PREFIX:PATH=bin -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_PREFIX_PATH=$QT5_Clang

echo BuildProject...
cd ./build/cmake
make

echo finish BuildProject...