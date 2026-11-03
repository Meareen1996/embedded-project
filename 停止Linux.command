#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
./linux stop
printf '\nLinux 已停止，可以关闭这个终端窗口。\n'
