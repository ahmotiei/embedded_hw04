#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

usage() {
    cat <<USAGE
Usage:
  $0 <master|slave1|slave2> [options]

Options:
  --background    Run the node in the background and write PID/log files.
  --foreground    Run the node in the foreground (default).
  --no-init       Do not recreate the SQLite database.
  --no-clean      Do not run make clean before building.
  --no-flush      Do not flush this node's Memcached cache before startup.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

NODE="${1:-}"
[[ -n "$NODE" ]] || {
    usage
    exit 1
}
shift
validate_node "$NODE"

RUN_MODE="foreground"
DO_INIT=1
DO_CLEAN=1
DO_FLUSH=1

while (( $# > 0 )); do
    case "$1" in
        --background) RUN_MODE="background" ;;
        --foreground) RUN_MODE="foreground" ;;
        --no-init) DO_INIT=0 ;;
        --no-clean) DO_CLEAN=0 ;;
        --no-flush) DO_FLUSH=0 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
 done

NODE_DIR="$(node_directory "$NODE")"
CONFIG_FILE="$(node_config_path "$NODE")"
BINARY="$NODE_DIR/$(node_binary_name "$NODE")"
LOG_FILE="$LOG_DIR/$NODE.log"
PID_FILE="$RUN_DIR/$NODE.pid"

[[ -f "$CONFIG_FILE" ]] || \
    die "config file not found: $CONFIG_FILE. Copy config.example to config and set the real values."

PORT="$(require_config_value "$CONFIG_FILE" PORT)"
require_config_value "$CONFIG_FILE" DATABASE >/dev/null
require_config_value "$CONFIG_FILE" MEMCACHED_HOST >/dev/null
require_config_value "$CONFIG_FILE" MEMCACHED_PORT >/dev/null
require_config_value "$CONFIG_FILE" CACHE_TTL >/dev/null

if [[ "$NODE" == "master" ]]; then
    require_config_value "$CONFIG_FILE" SLAVE1_PORT >/dev/null
    require_config_value "$CONFIG_FILE" SLAVE2_PORT >/dev/null

    SHARED_SLAVE_IP="$(read_config_value "$CONFIG_FILE" SLAVE_IP || true)"
    SLAVE1_IP="$(read_config_value "$CONFIG_FILE" SLAVE1_IP || true)"
    SLAVE2_IP="$(read_config_value "$CONFIG_FILE" SLAVE2_IP || true)"

    if [[ -z "$SHARED_SLAVE_IP" && ( -z "$SLAVE1_IP" || -z "$SLAVE2_IP" ) ]]; then
        die "Master config must define SLAVE_IP or both SLAVE1_IP and SLAVE2_IP"
    fi
fi

if (( DO_INIT )); then
    "$SCRIPT_DIR/init_node_database.sh" "$NODE"
fi

ensure_memcached_for_node "$NODE"

if (( DO_FLUSH )); then
    "$SCRIPT_DIR/flush_cache.sh" "$NODE"
fi

if (( DO_CLEAN )); then
    "$SCRIPT_DIR/build_node.sh" "$NODE"
else
    "$SCRIPT_DIR/build_node.sh" "$NODE" --no-clean
fi

if [[ -f "$PID_FILE" ]]; then
    OLD_PID="$(cat "$PID_FILE" 2>/dev/null || true)"
    if [[ -n "$OLD_PID" ]] && kill -0 "$OLD_PID" 2>/dev/null; then
        die "$NODE is already running with PID $OLD_PID. Stop it with scripts/stop_node.sh $NODE"
    fi
    rm -f "$PID_FILE"
fi

info "Starting $NODE on port $PORT"

if [[ "$RUN_MODE" == "background" ]]; then
    : > "$LOG_FILE"
    (
        cd "$NODE_DIR"
        exec stdbuf -oL -eL "$BINARY" "$CONFIG_FILE"
    ) >>"$LOG_FILE" 2>&1 &

    NODE_PID=$!
    echo "$NODE_PID" > "$PID_FILE"

    sleep 1
    if ! kill -0 "$NODE_PID" 2>/dev/null; then
        echo "----- $NODE log -----" >&2
        cat "$LOG_FILE" >&2 || true
        rm -f "$PID_FILE"
        die "$NODE failed to start"
    fi

    info "$NODE started successfully in the background"
    echo "PID: $NODE_PID"
    echo "Log: $LOG_FILE"
else
    cd "$NODE_DIR"
    exec "$BINARY" "$CONFIG_FILE"
fi
