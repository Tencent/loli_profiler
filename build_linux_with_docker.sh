#!/usr/bin/bash

docker build -t loli_profiler .
docker run -v $(pwd):/workspace/loli_profiler loli_profiler /bin/bash build_linux.sh