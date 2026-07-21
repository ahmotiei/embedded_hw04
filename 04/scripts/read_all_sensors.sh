#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NODES_CONFIG="${1:-$PROJECT_ROOT/config/nodes.env}"

if ! command -v snmpwalk >/dev/null 2>&1; then
    echo "Error: snmpwalk is not installed." >&2
    echo "Install it with: sudo apt update && sudo apt install -y snmp" >&2
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

if (( ${#NODES[@]} == 0 )); then
    echo "Error: NODES array is empty in $NODES_CONFIG" >&2
    exit 1
fi

mkdir -p "$PROJECT_ROOT/test-output"

for entry in "${NODES[@]}"; do
    IFS='|' read -r node_name host port <<< "$entry"

    if [[ -z "$node_name" || -z "$host" || -z "$port" ]]; then
        echo "Error: invalid node entry: $entry" >&2
        exit 1
    fi

    output_file="$PROJECT_ROOT/test-output/${node_name}_snmpwalk.txt"

    echo
echo "============================================================"
    echo "Node: $node_name ($host:$port)"
    echo "============================================================"

    snmpwalk \
        -v "$SNMP_VERSION" \
        -c "$SNMP_COMMUNITY" \
        -On \
        -t "$SNMP_TIMEOUT" \
        -r "$SNMP_RETRIES" \
        "udp:$host:$port" \
        "$SNMP_BASE_OID" \
        | tee "$output_file"

done

echo
echo "SNMP walk outputs were saved in: $PROJECT_ROOT/test-output"
