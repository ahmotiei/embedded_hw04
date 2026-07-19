#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"

for log_name in master slave1 slave2; do
    log_file="$LOG_DIR/$log_name.log"

    echo "============================================================"
    echo "$log_name log"

    if [[ -f "$log_file" ]]; then
        cat "$log_file"
    else
        echo "Log file not found: $log_file"
    fi

    echo
done
