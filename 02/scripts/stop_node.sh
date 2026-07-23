#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

NODE="${1:-}"
[[ -n "$NODE" ]] || die "usage: $0 <master|slave1|slave2|all>"

stop_one() {
    local node="$1"
    local pid_file="$RUN_DIR/$node.pid"
    local pid

    validate_node "$node"

    if [[ ! -f "$pid_file" ]]; then
        info "$node does not have a PID file; nothing to stop"
        return 0
    fi

    pid="$(cat "$pid_file" 2>/dev/null || true)"

    if [[ -z "$pid" ]]; then
        rm -f "$pid_file"
        info "$node PID file was empty and has been removed"
        return 0
    fi

    if ! kill -0 "$pid" 2>/dev/null; then
        rm -f "$pid_file"
        info "$node process is not running; stale PID file removed"
        return 0
    fi

    info "Stopping $node (PID $pid)"
    kill "$pid"

    for _ in {1..20}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$pid_file"
            info "$node stopped successfully"
            return 0
        fi
        sleep 0.25
    done

    warn "$node did not stop after SIGTERM; sending SIGKILL"
    kill -9 "$pid" 2>/dev/null || true
    rm -f "$pid_file"
}

if [[ "$NODE" == "all" ]]; then
    for node in master slave1 slave2; do
        stop_one "$node"
    done
else
    stop_one "$NODE"
fi
