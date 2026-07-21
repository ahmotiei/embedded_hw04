#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NODES_CONFIG="${1:-$PROJECT_ROOT/config/nodes.env}"

if ! command -v snmpget >/dev/null 2>&1 || ! command -v snmpwalk >/dev/null 2>&1; then
    echo "Error: snmpget/snmpwalk are not installed." >&2
    exit 1
fi

NODES_CONFIG="$(realpath -m "$NODES_CONFIG")"
if [[ ! -f "$NODES_CONFIG" ]]; then
    echo "Error: nodes configuration file not found: $NODES_CONFIG" >&2
    exit 1
fi

SNMP_VERSION="2c"
SNMP_COMMUNITY="hotelMonitor"
SNMP_BASE_OID=".1.3.6.1.4.1.8072.9999.4"
SNMP_TIMEOUT="2"
SNMP_RETRIES="1"
NODES=()

# shellcheck disable=SC1090
source "$NODES_CONFIG"

failures=0

for entry in "${NODES[@]}"; do
    IFS='|' read -r node_name host port <<< "$entry"
    endpoint="udp:$host:$port"

    echo "Testing $node_name at $host:$port ..."

    count_output="$(snmpget -v "$SNMP_VERSION" -c "$SNMP_COMMUNITY" -On -Oqv \
        -t "$SNMP_TIMEOUT" -r "$SNMP_RETRIES" \
        "$endpoint" "$SNMP_BASE_OID.1.0" 2>/dev/null || true)"

    count="$(printf '%s' "$count_output" | tr -cd '0-9')"

    if [[ -z "$count" || "$count" -le 0 ]]; then
        echo "  FAIL: invalid sensor count: ${count_output:-<empty>}"
        failures=$((failures + 1))
        continue
    fi

    walk_output="$(snmpwalk -v "$SNMP_VERSION" -c "$SNMP_COMMUNITY" -On \
        -t "$SNMP_TIMEOUT" -r "$SNMP_RETRIES" \
        "$endpoint" "$SNMP_BASE_OID.2.1" 2>/dev/null || true)"

    escaped_base="${SNMP_BASE_OID//./\\.}"
    names="$(grep -c "^${escaped_base}\.2\.1\.3\." <<< "$walk_output" || true)"
    descriptions="$(grep -c "^${escaped_base}\.2\.1\.4\." <<< "$walk_output" || true)"
    values="$(grep -c "^${escaped_base}\.2\.1\.5\." <<< "$walk_output" || true)"

    if [[ "$names" -eq "$count" && "$descriptions" -eq "$count" && "$values" -eq "$count" ]]; then
        echo "  PASS: $count sensors; name/description/latest-value are available for all."
    else
        echo "  FAIL: count=$count names=$names descriptions=$descriptions values=$values"
        failures=$((failures + 1))
    fi

done

if (( failures > 0 )); then
    echo "$failures SNMP node test(s) failed."
    exit 1
fi

echo "All SNMP node tests passed."
