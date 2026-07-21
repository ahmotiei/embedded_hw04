#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

load_node_config "${1:-}"

if [[ ! -x "$HANDLER_PATH" ]]; then
    make -C "$PROJECT_ROOT"
fi

if [[ ! -f "$DATABASE_PATH" ]]; then
    echo "Error: SQLite database not found: $DATABASE_PATH" >&2
    exit 1
fi

run_handler() {
    "$HANDLER_PATH" \
        --db "$DATABASE_PATH" \
        --node "$NODE_NAME" \
        --base-oid "$SNMP_BASE_OID" \
        "$@"
}

count_result="$(run_handler -g "$SNMP_BASE_OID.1.0")"
count_type="$(sed -n '2p' <<< "$count_result")"
count_value="$(sed -n '3p' <<< "$count_result")"

if [[ "$count_type" != "integer" || ! "$count_value" =~ ^[0-9]+$ || "$count_value" -le 0 ]]; then
    echo "FAIL: sensor count GET returned unexpected output:" >&2
    echo "$count_result" >&2
    exit 1
fi

for suffix in 3 4 5; do
    result="$(run_handler -g "$SNMP_BASE_OID.2.1.$suffix.1")"
    type="$(sed -n '2p' <<< "$result")"
    value="$(sed -n '3p' <<< "$result")"

    if [[ "$type" != "string" || -z "$value" ]]; then
        echo "FAIL: required column $suffix returned unexpected output:" >&2
        echo "$result" >&2
        exit 1
    fi
done

next_result="$(run_handler -n "$SNMP_BASE_OID")"
next_oid="$(sed -n '1p' <<< "$next_result")"

if [[ "$next_oid" != "$SNMP_BASE_OID.1.0" ]]; then
    echo "FAIL: GETNEXT did not return the first object:" >&2
    echo "$next_result" >&2
    exit 1
fi

set_result="$(run_handler -s "$SNMP_BASE_OID.1.0" integer 1 || true)"
if [[ "$set_result" != "not-writable" ]]; then
    echo "FAIL: SET was not rejected as read-only." >&2
    exit 1
fi

echo "PASS: direct Net-SNMP pass protocol test succeeded."
echo "Node: $NODE_NAME"
echo "Sensors: $count_value"
echo "Required fields: name, description, latest value"
