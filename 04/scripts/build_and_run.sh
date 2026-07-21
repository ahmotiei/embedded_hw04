#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-}"

if [[ -z "$CONFIG_FILE" ]]; then
    echo "Usage: $0 <node-config-file>" >&2
    echo "Example: $0 $PROJECT_ROOT/config/master.env" >&2
    exit 2
fi

required_commands=(make g++ realpath)
for command_name in "${required_commands[@]}"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Error: required command is not installed: $command_name" >&2
        exit 1
    fi
done

echo "Step 1/2: Building the C++ SNMP pass handler..."
make -C "$PROJECT_ROOT" clean all

echo
echo "Step 2/2: Starting the node-specific snmpd agent..."
"$SCRIPT_DIR/start_snmpd.sh" "$CONFIG_FILE"
