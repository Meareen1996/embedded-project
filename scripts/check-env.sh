#!/bin/bash
set -euo pipefail
printf '\n=== Linux 系统 ===\n'
cat /etc/os-release
uname -m
printf '\n=== 开发工具 ===\n'
for tool in gcc g++ make cmake ninja gdb gdb-multiarch git python3 dtc minicom; do
  command -v "$tool"
done
gcc --version | head -n 1
printf '\n=== C 语言构建与运行 ===\n'
bash "$(dirname "$0")/run-demo.sh"
