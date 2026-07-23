#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

NODE="${1:-}"
[[ -n "$NODE" ]] || die "usage: $0 <master|slave1|slave2>"
validate_node "$NODE"

CONFIG_FILE="$(node_config_path "$NODE")"
[[ -f "$CONFIG_FILE" ]] || die "config file not found: $CONFIG_FILE"

HOST="$(require_config_value "$CONFIG_FILE" MEMCACHED_HOST)"
PORT="$(require_config_value "$CONFIG_FILE" MEMCACHED_PORT)"

wait_for_tcp "$HOST" "$PORT" 2 || \
    die "Memcached is not reachable at $HOST:$PORT"

info "Flushing cache configured for $NODE at $HOST:$PORT"
flush_memcached_endpoint "$HOST" "$PORT" >/dev/null
info "$NODE cache was flushed successfully"
