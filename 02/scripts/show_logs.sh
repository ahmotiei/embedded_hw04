#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

TARGET="${1:-all}"
LINES="${LINES:-200}"

show_one() {
    local node="$1"
    local log_file

    validate_node "$node"
    log_file="$LOG_DIR/$node.log"

    echo "============================================================"
    echo "$node log: $log_file"

    if [[ -f "$log_file" ]]; then
        tail -n "$LINES" "$log_file"
    else
        echo "Log file not found. The node may have been run in foreground mode."
    fi

    echo
}

if [[ "$TARGET" == "all" ]]; then
    for node in master slave1 slave2; do
        show_one "$node"
    done
else
    show_one "$TARGET"
fi
