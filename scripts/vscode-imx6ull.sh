#!/bin/bash
# VS Code 在 Mac 调用本脚本；guest-build 子命令只在 Ubuntu 内执行。
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ACTION="${1:-help}"
SOURCE="${2:-}"
VM=embedded-dev

source_paths() {
    case "$SOURCE" in
        "$ROOT"/*.c) ;;
        *) echo "请选择当前项目中的一个已保存 .c 文件。" >&2; exit 2 ;;
    esac
    if [ ! -f "$SOURCE" ]; then
        echo "找不到源码，请先保存文件：$SOURCE" >&2
        exit 2
    fi
    RELATIVE="${SOURCE#"$ROOT"/}"
    STEM="${RELATIVE%.c}"
    OUTPUT="$ROOT/build/active/$STEM"
}

case "$ACTION" in
    prepare)
        limactl start "$VM"
        mkdir -p "$ROOT/build/vscode-sysroot/usr"
        # 复制 Linux ARM32 的头文件供 Mac 上的编辑器解析，不复制工具链到 Mac。
        limactl shell --workdir "$ROOT" "$VM" bash -c \
            'tar -C /usr/arm-linux-gnueabihf -cf "$1/build/vscode-headers.tar" include' \
            bash "$ROOT"
        tar -xf "$ROOT/build/vscode-headers.tar" -C "$ROOT/build/vscode-sysroot/usr"
        echo "ARM32 Linux 编辑器头文件已同步到 build/vscode-sysroot/usr/include。"
        ;;
    build|run)
        source_paths
        limactl start "$VM"
        limactl shell --workdir "$ROOT" "$VM" bash "$0" guest-build "$SOURCE"
        if [ "$ACTION" = run ]; then
            SERIAL="${ANDROID_SERIAL:-100ask_IMX6ULL}"
            # 所有学习程序保存在独立目录，不修改板载系统或启动配置。
            REMOTE=/root/embedded-project/current-program
            adb -s "$SERIAL" get-state >/dev/null
            RUNNING="$(adb -s "$SERIAL" shell 'pidof current-program' | tr -d '\r')"
            if [ -n "$RUNNING" ]; then
                echo "current-program 正在运行，请先在它的终端按 Ctrl+C 停止。" >&2
                exit 1
            fi
            adb -s "$SERIAL" shell 'mkdir -p /root/embedded-project'
            adb -s "$SERIAL" push "$OUTPUT" "$REMOTE"
            # 老版本 adbd 不回传进程退出码，显式加入结束标记。
            adb -s "$SERIAL" shell -t \
                "chmod +x $REMOTE && $REMOTE; rc=\$?; printf '\\n__IMX_EXIT__:%s\\n' \"\$rc\"" | {
                while IFS= read -r line; do
                    line="${line%$'\r'}"
                    case "$line" in
                        __IMX_EXIT__:*) exit "${line#__IMX_EXIT__:}" ;;
                        *) printf '%s\n' "$line" ;;
                    esac
                done
                echo "ADB 连接结束，未收到程序退出状态。" >&2
                exit 1
            }
        fi
        ;;
    guest-build)
        if [ "$(uname -s)" != Linux ]; then
            echo "guest-build 只能在 Ubuntu 内执行。" >&2
            exit 2
        fi
        source_paths
        mkdir -p "$(dirname "$OUTPUT")"
        # 先写临时输出，编译失败后不留下旧的同名程序供误运行。
        rm -f "$OUTPUT"
        TEMP_OUTPUT="$OUTPUT.tmp.$$"
        trap 'rm -f "$TEMP_OUTPUT"' EXIT
        EXTRA_ARGS=()
        case "$RELATIVE" in
            apps/mqtt-demo/*.c)
                MQTT_SDK="$ROOT/build/mqtt-sdk"
                if [ ! -f "$MQTT_SDK/include/mosquitto.h" ] || [ ! -f "$MQTT_SDK/lib/libmosquitto.a" ]; then
                    echo "请先执行：$ROOT/scripts/prepare-mqtt.sh" >&2
                    exit 1
                fi
                EXTRA_ARGS=("-I$MQTT_SDK/include" "$MQTT_SDK/lib/libmosquitto.a" -pthread)
                ;;
        esac
        arm-linux-gnueabihf-gcc -std=c11 -Wall -Wextra -g -O0 -static \
            -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
            "$SOURCE" "${EXTRA_ARGS[@]}" -o "$TEMP_OUTPUT"
        mv "$TEMP_OUTPUT" "$OUTPUT"
        file "$OUTPUT"
        echo "编译成功：$OUTPUT"
        ;;
    *)
        echo "用法：$0 prepare | build 文件.c | run 文件.c" >&2
        exit 2
        ;;
esac
