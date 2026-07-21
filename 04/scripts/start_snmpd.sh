#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

load_node_config "${1:-}"

if ! command -v snmpd >/dev/null 2>&1; then
    echo "Error: snmpd is not installed." >&2
    echo "Install it with: sudo apt update && sudo apt install -y snmp snmpd" >&2
    exit 1
fi

"$SCRIPT_DIR/generate_snmpd_config.sh" "$1"

if [[ -f "$PID_FILE" ]]; then
    old_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    if [[ -n "$old_pid" ]] && kill -0 "$old_pid" 2>/dev/null; then
        echo "SNMP agent for $NODE_NAME is already running with PID $old_pid."
        exit 0
    fi
    rm -f "$PID_FILE"
fi

: > "$LOG_FILE"

# The custom agent runs as the current user. Net-SNMP normally writes its
# persistent files and executable cache under /var/lib/snmp, which is not
# writable by an ordinary user. Use a private writable directory per node.
PERSISTENT_DIR="$GENERATED_DIR/persistent"
mkdir -p "$PERSISTENT_DIR"

MIBS="" SNMP_PERSISTENT_DIR="$PERSISTENT_DIR" snmpd \
    -f \
    -Lo \
    -C \
    -I -smux \
    -c "$GENERATED_CONFIG" \
    -p "$PID_FILE" \
    >> "$LOG_FILE" 2>&1 &

launcher_pid=$!
sleep 1

if ! kill -0 "$launcher_pid" 2>/dev/null; then
    echo "Error: snmpd failed to start for $NODE_NAME." >&2
    cat "$LOG_FILE" >&2
    exit 1
fi

if [[ ! -s "$PID_FILE" ]]; then
    echo "$launcher_pid" > "$PID_FILE"
fi

echo "SNMP agent started."
echo "Node:       $NODE_NAME"
echo "Database:   $DATABASE_PATH"
echo "Listen:     $SNMP_LISTEN_ADDRESS"
echo "Base OID:   $SNMP_BASE_OID"
echo "PID:        $(cat "$PID_FILE")"
echo "Log:        $LOG_FILE"
