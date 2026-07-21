#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

load_node_config "${1:-}"

if [[ ! -x "$HANDLER_PATH" ]]; then
    echo "Error: SNMP pass handler is not built: $HANDLER_PATH" >&2
    echo "Run: make -C $PROJECT_ROOT" >&2
    exit 1
fi

if [[ ! -f "$DATABASE_PATH" ]]; then
    echo "Error: SQLite database not found: $DATABASE_PATH" >&2
    exit 1
fi

mkdir -p "$GENERATED_DIR"

content="$(<"$TEMPLATE_PATH")"
content="${content//@SNMP_LISTEN_ADDRESS@/$SNMP_LISTEN_ADDRESS}"
content="${content//@SNMP_COMMUNITY@/$SNMP_COMMUNITY}"
content="${content//@SNMP_ALLOWED_NETWORK@/$SNMP_ALLOWED_NETWORK}"
content="${content//@SNMP_BASE_OID@/$SNMP_BASE_OID}"
content="${content//@SNMP_SYS_LOCATION@/$SNMP_SYS_LOCATION}"
content="${content//@SNMP_SYS_CONTACT@/$SNMP_SYS_CONTACT}"
content="${content//@NODE_NAME@/$NODE_NAME}"
content="${content//@HANDLER_PATH@/$HANDLER_PATH}"
content="${content//@DATABASE_PATH@/$DATABASE_PATH}"

printf '%s\n' "$content" > "$GENERATED_CONFIG"
chmod 600 "$GENERATED_CONFIG"

echo "Generated SNMP configuration: $GENERATED_CONFIG"
