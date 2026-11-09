#!/bin/bash
# 在 Ubuntu 中执行。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/light-monitor"
mkdir -p "$OUT" "$HOME/build"
# 测试状态判断逻辑；测试程序在 Ubuntu 本机运行。
gcc -std=c11 -Wall -Wextra -Werror "$ROOT/tests/light-state-test.c" -o "$HOME/build/light-state-test"
"$HOME/build/light-state-test"
arm-linux-gnueabihf-gcc -std=c11 -Wall -Wextra -Werror -g -O0 \
  -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -static \
  "$ROOT/apps/light-monitor/main.c" -o "$OUT/light-monitor"
file "$OUT/light-monitor"
