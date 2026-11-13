#!/bin/bash
# 准备 MQTT 入门实验的 ARM 静态客户端库；不替换板载 Broker。
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$(uname -s)" = Darwin ]; then
  limactl start embedded-dev
  exec limactl shell --workdir "$ROOT" embedded-dev bash "$0"
fi
if [ "$(uname -s)" != Linux ]; then
  echo '请从 Mac 或 embedded-dev Ubuntu 执行本脚本。' >&2; exit 1
fi
VERSION=2.0.22
CACHE="$ROOT/build/mqtt-deps"
SDK="$ROOT/build/mqtt-sdk"
ARCHIVE="$CACHE/mosquitto-$VERSION.tar.gz"
mkdir -p "$CACHE"
if ! tar -tzf "$ARCHIVE" >/dev/null 2>&1; then
  curl --fail --location --continue-at - --max-time 60 \
    "https://mosquitto.org/files/source/mosquitto-$VERSION.tar.gz" -o "$ARCHIVE"
  tar -tzf "$ARCHIVE" >/dev/null
fi
tar -xzf "$ARCHIVE" -C "$CACHE"
# 当前实验使用板内明文 MQTT；需要 TLS 时重新构建 TLS 版本及其依赖。
make -C "$CACHE/mosquitto-$VERSION/lib" -j2 \
  CROSS_COMPILE=arm-linux-gnueabihf- CC=gcc AR=ar \
  CFLAGS='-O2 -g -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard' \
  WITH_SHARED_LIBRARIES=no WITH_STATIC_LIBRARIES=yes \
  WITH_TLS=no WITH_SRV=no WITH_THREADING=yes WITH_DOCS=no
mkdir -p "$SDK/include" "$SDK/lib"
cp "$CACHE/mosquitto-$VERSION/include/mosquitto.h" \
   "$CACHE/mosquitto-$VERSION/include/mqtt_protocol.h" "$SDK/include/"
cp "$CACHE/mosquitto-$VERSION/lib/libmosquitto.a" "$SDK/lib/"
echo "MQTT ARM SDK ready: $SDK"
