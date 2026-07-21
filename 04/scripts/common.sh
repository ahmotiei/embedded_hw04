#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

absolute_from_config() {
    local config_dir="$1"
    local value="$2"

    if [[ "$value" = /* ]]; then
        realpath -m "$value"
    else
        realpath -m "$config_dir/$value"
    fi
}

load_node_config() {
    local config_file="${1:-}"

    if [[ -z "$config_file" ]]; then
        echo "Usage: $0 <node-config-file>" >&2
        exit 2
    fi

    config_file="$(realpath -m "$config_file")"

    if [[ ! -f "$config_file" ]]; then
        echo "Error: node configuration file not found: $config_file" >&2
        exit 1
    fi

    local config_dir
    config_dir="$(dirname "$config_file")"

    # Defaults may be overridden by the node configuration file.
    SNMP_LISTEN_ADDRESS="udp:1161"
    SNMP_COMMUNITY="hotelMonitor"
    SNMP_ALLOWED_NETWORK="127.0.0.1"
    SNMP_BASE_OID=".1.3.6.1.4.1.8072.9999.4"
    SNMP_SYS_LOCATION="Hotel monitoring laboratory"
    SNMP_SYS_CONTACT="embedded-lab"

    # shellcheck disable=SC1090
    source "$config_file"

    : "${NODE_NAME:?NODE_NAME is required in $config_file}"
    : "${DATABASE_PATH:?DATABASE_PATH is required in $config_file}"
    : "${SNMP_LISTEN_ADDRESS:?SNMP_LISTEN_ADDRESS is required}"
    : "${SNMP_COMMUNITY:?SNMP_COMMUNITY is required}"
    : "${SNMP_ALLOWED_NETWORK:?SNMP_ALLOWED_NETWORK is required}"
    : "${SNMP_BASE_OID:?SNMP_BASE_OID is required}"

    DATABASE_PATH="$(absolute_from_config "$config_dir" "$DATABASE_PATH")"
    HANDLER_PATH="$(realpath -m "$PROJECT_ROOT/snmp/sensor_snmp_pass")"
    TEMPLATE_PATH="$(realpath -m "$PROJECT_ROOT/snmp/snmpd.conf.template")"

    GENERATED_DIR="$PROJECT_ROOT/generated/$NODE_NAME"
    GENERATED_CONFIG="$GENERATED_DIR/snmpd.conf"
    PID_FILE="$GENERATED_DIR/snmpd.pid"
    LOG_FILE="$GENERATED_DIR/snmpd.log"

    if [[ "$DATABASE_PATH" =~ [[:space:]] || "$HANDLER_PATH" =~ [[:space:]] ]]; then
        echo "Error: Net-SNMP pass command paths must not contain whitespace." >&2
        echo "Database: $DATABASE_PATH" >&2
        echo "Handler:  $HANDLER_PATH" >&2
        exit 1
    fi
}
