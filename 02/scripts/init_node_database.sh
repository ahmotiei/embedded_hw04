#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

NODE="${1:-}"
[[ -n "$NODE" ]] || die "usage: $0 <master|slave1|slave2> [csv_file] [database_file]"
validate_node "$NODE"

CSV_FILE="${2:-$(node_csv_path "$NODE")}"
DB_FILE="${3:-$(node_database_path "$NODE")}"
INIT_SCRIPT="$(node_init_script_path "$NODE")"

require_command sqlite3
[[ -x "$INIT_SCRIPT" ]] || die "database initialization script is not executable: $INIT_SCRIPT"
[[ -f "$CSV_FILE" ]] || die "CSV file not found: $CSV_FILE"

info "Initializing $NODE database"
"$INIT_SCRIPT" "$DB_FILE" "$CSV_FILE"

info "$NODE database is ready: $DB_FILE"
