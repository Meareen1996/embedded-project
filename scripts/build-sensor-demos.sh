#!/bin/bash
# 在 embedded-dev Ubuntu 虚拟机内执行。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build/sensor-demos"
for demo in ap3216c imu adc temperature input_events; do
  arm-linux-gnueabihf-gcc -std=c11 -Wall -Wextra -Werror -g -O0 -static \
    -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
    "$ROOT/apps/sensor-demos/$demo.c" -o "$ROOT/build/sensor-demos/$demo"
  echo "Built: $demo"
done
