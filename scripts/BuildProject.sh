set -xeuo pipefail

echo makeProject...

cmake . -B "build/cmake" -DCMAKE_PREFIX_PATH=$QT5_Clang

echo BuildProject...
cd ./build/cmake
make install -j12

echo finish BuildProject...