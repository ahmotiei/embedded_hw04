#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

for node in master slave1 slave2; do
    if [[ -f "$PROJECT_ROOT/$node/Makefile" ]]; then
        make -C "$PROJECT_ROOT/$node" clean || true
    fi
 done

rm -rf "$RUN_DIR"
rm -f "$LOG_DIR"/*.log
rm -f "$PROJECT_ROOT/cache_speed_result.csv"
rm -f "$PROJECT_ROOT/cache_speed_summary.csv"

find "$PROJECT_ROOT" -type f \
    \( -name '*~' -o -name '.~lock.*' -o -name '*.swp' \) \
    -delete

echo "Generated build, PID, log, benchmark, and editor-temporary files were removed."
