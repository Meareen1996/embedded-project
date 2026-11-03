#!/bin/bash
# 在 Ubuntu 中执行；生成的 ARMv7 程序通过共享目录交给 Mac。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/imx6ull"
mkdir -p "$OUT"
arm-linux-gnueabihf-gcc -Wall -Wextra -g -O0 \
  -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -static \
  "$ROOT/examples/imx6ull-hello/main.c" -o "$OUT/hello"
file "$OUT/hello"
arm-linux-gnueabihf-readelf -h "$OUT/hello" | sed -n '1,22p'
printf '\nOutput: %s/hello\n' "$OUT"
