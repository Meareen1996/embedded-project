#!/bin/bash
# Mac 入口：./scripts/sensor-demos.sh run als --count 10
set -euo pipefail
trap 'exit 130' INT
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERIAL="${ANDROID_SERIAL:-100ask_IMX6ULL}"
usage() {
  echo '用法：scripts/sensor-demos.sh list | build | run <名称> [--count N] [--interval-ms N]'
  echo '名称：als（光线）, proximity（接近）, ir（红外）, accel（加速度）, gyro（陀螺仪）'
  echo '      imu-temp（IMU温度原始值）, adc（模拟量）, temperature（CPU温度）, all（依次运行）'
  echo '附加：keys（USER按键）, touch（触摸屏）；这两项 --count 表示秒数，最多600秒。'
  echo '默认每项5次、间隔500ms；--count 0持续采集（all不允许），Ctrl+C停止。'
}
action="${1:-list}"
case "$action" in
  list) usage; exit 0 ;;
  build|run) ;;
  *) usage >&2; exit 2 ;;
esac
shift
selected=all
if [ "$action" = run ]; then selected="${1:-all}"; if [ "$#" -gt 0 ]; then shift; fi; fi
case "$selected" in als|proximity|ir|accel|gyro|imu-temp|adc|temperature|keys|touch|all) ;; *) usage >&2; exit 2 ;; esac
# 只接受数值参数；绝不把任意用户输入直接拼接成远端 shell 命令。
count=5
interval=500
while [ "$#" -gt 0 ]; do
  if [ "$#" -lt 2 ] || ! [[ "$2" =~ ^[0-9]{1,7}$ ]]; then usage >&2; exit 2; fi
  case "$1" in
    --count) count=$((10#$2)); if [ "$count" -gt 1000000 ]; then usage >&2; exit 2; fi ;;
    --interval-ms) interval=$((10#$2)); if [ "$interval" -lt 20 ] || [ "$interval" -gt 60000 ]; then usage >&2; exit 2; fi ;;
    *) usage >&2; exit 2 ;;
  esac
  shift 2
done
if [ "$selected" = all ] && [ "$count" -eq 0 ]; then
  echo 'all 必须使用有限次数；持续采集请选择单项。' >&2; exit 2
fi
if [[ "$selected" = keys || "$selected" = touch ]] && { [ "$count" -eq 0 ] || [ "$count" -gt 600 ]; }; then
  echo '输入事件实验 --count 是观察秒数，只允许1..600。' >&2; exit 2
fi
limactl start embedded-dev
limactl shell --workdir "$ROOT" embedded-dev bash "$ROOT/scripts/build-sensor-demos.sh"
if [ "$action" = build ]; then exit 0; fi
adb -s "$SERIAL" get-state >/dev/null
adb -s "$SERIAL" shell 'mkdir -p /root/sensor-demos'
# 不覆盖仍在执行的旧程序。进程名保守检查，避免 ETXTBSY。
for binary in ap3216c imu adc temperature input_events; do
  running="$(adb -s "$SERIAL" shell "pidof $binary" | tr -d '\r')"
  if [ -n "$running" ]; then echo "$binary 正在运行，请先在它的终端 Ctrl+C 停止。" >&2; exit 1; fi
done
for binary in ap3216c imu adc temperature input_events; do
  adb -s "$SERIAL" push "$ROOT/build/sensor-demos/$binary" "/root/sensor-demos/$binary" >/dev/null
done
adb -s "$SERIAL" shell 'chmod +x /root/sensor-demos/*'
run_one() {
  local name="$1" binary extra=''
  case "$name" in
    als|proximity|ir) binary=ap3216c; extra="--sensor $name" ;;
    accel|gyro) binary=imu; extra="--sensor $name" ;;
    imu-temp) binary=imu; extra='--sensor temp' ;;
    adc|temperature) binary="$name" ;;
    keys|touch) binary=input_events; extra="--sensor $name" ;;
  esac
  echo "--- $name ---" >&2
  # 使用 PTY 让 Ctrl+C 送达远端，程序可恢复 AP3216C 模式。
  # 旧 adbd 不可靠地透传退出码，所以读取明确的结束标记。
  local timing="--interval-ms $interval"
  if [ "$binary" = input_events ]; then timing=''; fi
  adb -s "$SERIAL" shell -t "/root/sensor-demos/$binary $extra --count $count $timing; rc=\$?; printf '\\n__SENSOR_EXIT__:%s\\n' \"\$rc\"" | {
    while IFS= read -r line; do
      line="${line%$'\r'}"
      case "$line" in
        __SENSOR_EXIT__:*) exit "${line#__SENSOR_EXIT__:}" ;;
        *) printf '%s\n' "$line" ;;
      esac
    done
    echo 'ADB断开，未收到程序退出状态。' >&2; exit 1
  }
}
if [ "$selected" = all ]; then
  for name in als proximity ir accel gyro imu-temp adc temperature; do run_one "$name"; done
else
  run_one "$selected"
fi
