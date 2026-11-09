#!/bin/bash
# 在 Mac 执行：默认采集 20 次，传入 0 持续运行，Ctrl+C 停止。
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COUNT="${1:-20}"
SERIAL="${ANDROID_SERIAL:-100ask_IMX6ULL}"
if [ "$#" -gt 1 ] || ! [[ "$COUNT" =~ ^[0-9]{1,7}$ ]] || [ "$((10#$COUNT))" -gt 1000000 ]; then
  echo "Usage: $0 [sample-count: 0..1000000]" >&2
  exit 2
fi
COUNT="$((10#$COUNT))"
limactl start embedded-dev
limactl shell --workdir "$ROOT" embedded-dev bash "$ROOT/scripts/build-light-monitor.sh"
adb -s "$SERIAL" get-state >/dev/null
# 避免覆盖正在执行的程序。
RUNNING="$(adb -s "$SERIAL" shell 'pidof light-monitor' | tr -d '\r')"
if [ -n "$RUNNING" ]; then
  echo "Collector already running. Stop it with Ctrl+C in its terminal first." >&2
  exit 1
fi
adb -s "$SERIAL" shell 'mkdir -p /root/light-monitor'
adb -s "$SERIAL" push "$ROOT/build/light-monitor/light-monitor" /root/light-monitor/light-monitor
adb -s "$SERIAL" shell 'chmod +x /root/light-monitor/light-monitor'
# 老版本 adbd 不一定返回远端退出码，显式回传状态并保留实时 JSON 输出。
adb -s "$SERIAL" shell -t "/root/light-monitor/light-monitor --count $COUNT; rc=\$?; printf '\\n__LIGHT_EXIT__:%s\\n' \"\$rc\"" | {
  while IFS= read -r line; do
    line="${line%$'\r'}"
    case "$line" in
      __LIGHT_EXIT__:*) exit "${line#__LIGHT_EXIT__:}" ;;
      *) printf '%s\n' "$line" ;;
    esac
  done
  echo "ADB disconnected before the collector reported its exit status." >&2
  exit 1
}
