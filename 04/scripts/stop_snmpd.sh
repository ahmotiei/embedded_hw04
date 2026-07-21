#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

load_node_config "${1:-}"

if [[ ! -f "$PID_FILE" ]]; then
    echo "SNMP agent for $NODE_NAME is not running (PID file not found)."
    exit 0
fi

pid="$(cat "$PID_FILE" 2>/dev/null || true)"

if [[ -z "$pid" ]]; then
    rm -f "$PID_FILE"
    echo "Removed empty PID file."
    exit 0
fi

if kill -0 "$pid" 2>/dev/null; then
    kill "$pid"

    for _ in {1..20}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
    fi
fi

rm -f "$PID_FILE"
echo "SNMP agent stopped for $NODE_NAME."
