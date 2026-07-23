#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NODE="${1:-}"
CONFIG_FILE="${2:-}"
DATA_DIR="${DATA_DIR:-$PROJECT_ROOT/data}"

usage() {
    cat <<'EOF_USAGE'
Usage:
  ./scripts/build_and_run.sh master [config-file]
  ./scripts/build_and_run.sh slave1 [config-file]
  ./scripts/build_and_run.sh slave2 [config-file]

Run this script inside the corresponding VM. It initializes that node's
database, compiles it with Makefile, validates required local services,
and starts the program in the foreground.
EOF_USAGE
}

if [[ "$NODE" != "master" && "$NODE" != "slave1" && "$NODE" != "slave2" ]]; then
    usage
    exit 2
fi

CONFIG_FILE="${CONFIG_FILE:-$PROJECT_ROOT/$NODE/config}"

for command_name in make sqlite3 g++ gcc; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Error: required command is not installed: $command_name" >&2
        exit 1
    }
done

read_config_value() {
    local key="$1"
    local file="$2"

    awk -F'=' -v wanted="$key" '
        /^[[:space:]]*#/ { next }
        $1 ~ "^[[:space:]]*" wanted "[[:space:]]*$" {
            value = $2
            sub(/[[:space:]]*#.*/, "", value)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$file"
}

check_tcp_port() {
    local host="$1"
    local port="$2"
    local label="$3"

    if ! timeout 2 bash -c "</dev/tcp/${host}/${port}" 2>/dev/null; then
        echo "Error: $label is not reachable at ${host}:${port}." >&2
        return 1
    fi
}

[[ -f "$CONFIG_FILE" ]] || {
    echo "Error: configuration file not found: $CONFIG_FILE" >&2
    exit 1
}

CONFIG_FILE="$(realpath "$CONFIG_FILE")"
DATABASE_VALUE="$(read_config_value DATABASE "$CONFIG_FILE")"

if [[ -z "$DATABASE_VALUE" ]]; then
    echo "Error: DATABASE is missing from $CONFIG_FILE" >&2
    exit 1
fi

if [[ "$DATABASE_VALUE" = /* ]]; then
    DB_FILE="$DATABASE_VALUE"
else
    DB_FILE="$PROJECT_ROOT/$NODE/$DATABASE_VALUE"
fi

case "$NODE" in
    master)
        CSV_FILE="$DATA_DIR/master_sensors.csv"
        INIT_SCRIPT="$PROJECT_ROOT/master/master_init_db.sh"
        ;;
    slave1)
        CSV_FILE="$DATA_DIR/slave1_sensors.csv"
        INIT_SCRIPT="$PROJECT_ROOT/slave1/slave1_init_db.sh"
        ;;
    slave2)
        CSV_FILE="$DATA_DIR/slave2_sensors.csv"
        INIT_SCRIPT="$PROJECT_ROOT/slave2/slave2_init_db.sh"
        ;;
esac

[[ -f "$CSV_FILE" ]] || {
    echo "Error: CSV data file not found: $CSV_FILE" >&2
    exit 1
}

echo "[1/4] Initializing $NODE database from $CSV_FILE"
"$INIT_SCRIPT" "$DB_FILE" "$CSV_FILE"

echo "[2/4] Compiling $NODE with Makefile"
make -C "$PROJECT_ROOT/$NODE" clean
make -C "$PROJECT_ROOT/$NODE"

MEMCACHED_HOST="$(read_config_value MEMCACHED_HOST "$CONFIG_FILE")"
MEMCACHED_PORT="$(read_config_value MEMCACHED_PORT "$CONFIG_FILE")"

echo "[3/4] Checking local services"
check_tcp_port "$MEMCACHED_HOST" "$MEMCACHED_PORT" "Memcached"

if [[ "$NODE" == "master" ]]; then
    MQTT_BROKER_HOST="$(read_config_value MQTT_BROKER_HOST "$CONFIG_FILE")"
    MQTT_BROKER_PORT="$(read_config_value MQTT_BROKER_PORT "$CONFIG_FILE")"
    check_tcp_port "$MQTT_BROKER_HOST" "$MQTT_BROKER_PORT" "MQTT Broker"
fi

echo "[4/4] Starting $NODE"
echo "Press Ctrl+C to stop the program."
cd "$PROJECT_ROOT/$NODE"
exec "./$NODE" "$CONFIG_FILE"
