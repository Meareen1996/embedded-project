#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
./linux start
exec ./linux shell
