#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# 在 Linux 自己的磁盘构建，源码通过共享目录保存于 Mac。
BUILD="$HOME/build/embedded-hello"
cmake -S "$ROOT/examples/hello" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD"
"$BUILD/hello"
file "$BUILD/hello"
