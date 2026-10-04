#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_dir="${IDF_PATH:-${HOME}/esp/esp-idf}"
flash_port="${ESP_FLASH_PORT:-/dev/ttyACM0}"
monitor_port="${ESP_MONITOR_PORT:-/dev/ttyUSB0}"
action="${1:-build}"

source "${idf_dir}/export.sh" >/dev/null
cd "${repo_dir}/tests/s3_smoke"

case "${action}" in
  build) idf.py build ;;
  flash) idf.py -p "${flash_port}" flash ;;
  monitor) idf.py -p "${monitor_port}" monitor ;;
  flash-monitor)
    idf.py -p "${flash_port}" flash
    idf.py -p "${monitor_port}" monitor
    ;;
  *) echo "usage: $0 {build|flash|monitor|flash-monitor}" >&2; exit 2 ;;
esac
