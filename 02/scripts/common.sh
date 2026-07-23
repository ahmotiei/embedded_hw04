#!/usr/bin/env bash

# Shared helper functions for Section 02 scripts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"
RUN_DIR="$PROJECT_ROOT/run"
DATA_DIR="${DATA_DIR:-$PROJECT_ROOT/data}"

mkdir -p "$LOG_DIR" "$RUN_DIR"

die() {
    echo "Error: $*" >&2
    exit 1
}

info() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

warn() {
    echo "Warning: $*" >&2
}

require_command() {
    local command_name="$1"
    command -v "$command_name" >/dev/null 2>&1 || \
        die "required command is not installed: $command_name"
}

validate_node() {
    local node="$1"
    case "$node" in
        master|slave1|slave2) ;;
        *) die "invalid node '$node'. Use master, slave1, or slave2." ;;
    esac
}

node_binary_name() {
    local node="$1"
    validate_node "$node"
    printf '%s\n' "$node"
}

node_directory() {
    local node="$1"
    validate_node "$node"
    printf '%s\n' "$PROJECT_ROOT/$node"
}

node_config_path() {
    local node="$1"
    validate_node "$node"
    printf '%s\n' "$PROJECT_ROOT/$node/config"
}

node_database_path() {
    local node="$1"
    local node_dir
    local config_file
    local configured_path

    validate_node "$node"
    node_dir="$(node_directory "$node")"
    config_file="$(node_config_path "$node")"
    configured_path="$(read_config_value "$config_file" DATABASE || true)"

    if [[ -z "$configured_path" ]]; then
        configured_path="$node.db"
    fi

    if [[ "$configured_path" = /* ]]; then
        printf '%s\n' "$configured_path"
    else
        printf '%s\n' "$node_dir/$configured_path"
    fi
}

node_csv_path() {
    local node="$1"
    validate_node "$node"
    printf '%s\n' "$DATA_DIR/${node}_sensors.csv"
}

node_init_script_path() {
    local node="$1"
    validate_node "$node"

    case "$node" in
        master) printf '%s\n' "$PROJECT_ROOT/master/master_init_db.sh" ;;
        slave1) printf '%s\n' "$PROJECT_ROOT/slave1/slave1_init_db.sh" ;;
        slave2) printf '%s\n' "$PROJECT_ROOT/slave2/slave2_init_db.sh" ;;
    esac
}

read_config_value() {
    local config_file="$1"
    local key="$2"

    [[ -f "$config_file" ]] || return 1

    awk -F= -v requested_key="$key" '
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*$/ { next }
        $1 ~ "^[[:space:]]*" requested_key "[[:space:]]*$" {
            value = substr($0, index($0, "=") + 1)
            sub(/[[:space:]]*#.*/, "", value)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$config_file"
}

require_config_value() {
    local config_file="$1"
    local key="$2"
    local value

    value="$(read_config_value "$config_file" "$key" || true)"
    [[ -n "$value" ]] || die "$key is missing from $config_file"
    printf '%s\n' "$value"
}

is_local_host() {
    local host="$1"
    case "$host" in
        127.0.0.1|localhost|::1) return 0 ;;
        *) return 1 ;;
    esac
}

wait_for_tcp() {
    local host="$1"
    local port="$2"
    local timeout_seconds="${3:-10}"
    local start_time

    require_command python3
    start_time="$(date +%s)"

    while true; do
        if python3 - "$host" "$port" <<'PY' >/dev/null 2>&1
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])

with socket.create_connection((host, port), timeout=0.5):
    pass
PY
        then
            return 0
        fi

        if (( $(date +%s) - start_time >= timeout_seconds )); then
            return 1
        fi

        sleep 0.25
    done
}

ensure_memcached_for_node() {
    local node="$1"
    local config_file
    local host
    local port

    validate_node "$node"
    config_file="$(node_config_path "$node")"
    [[ -f "$config_file" ]] || die "config file not found: $config_file"

    host="$(require_config_value "$config_file" MEMCACHED_HOST)"
    port="$(require_config_value "$config_file" MEMCACHED_PORT)"

    if wait_for_tcp "$host" "$port" 1; then
        info "Memcached is reachable at $host:$port"
        return 0
    fi

    if ! is_local_host "$host"; then
        die "Memcached is not reachable at $host:$port and the host is not local"
    fi

    require_command systemctl

    if ! command -v memcached >/dev/null 2>&1; then
        die "memcached is not installed. Run scripts/install_dependencies.sh first."
    fi

    info "Starting the local Memcached service"

    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        systemctl enable --now memcached
    else
        require_command sudo
        sudo systemctl enable --now memcached
    fi

    wait_for_tcp "$host" "$port" 10 || \
        die "Memcached service started but $host:$port is still unreachable"

    info "Memcached is ready at $host:$port"
}

flush_memcached_endpoint() {
    local host="$1"
    local port="$2"

    require_command python3

    python3 - "$host" "$port" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])

with socket.create_connection((host, port), timeout=3) as sock:
    sock.sendall(b"flush_all\r\n")
    response = sock.recv(1024).decode("utf-8", errors="replace").strip()

if response != "OK":
    raise SystemExit(f"unexpected Memcached response: {response!r}")

print("OK")
PY
}

urlencode_query() {
    local value="$1"
    require_command python3
    python3 - "$value" <<'PY'
import sys
import urllib.parse
print(urllib.parse.quote(sys.argv[1], safe=""))
PY
}
