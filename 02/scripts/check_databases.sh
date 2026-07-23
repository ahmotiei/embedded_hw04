#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

TARGET="${1:-all}"
require_command sqlite3

check_one() {
    local node="$1"
    local db

    validate_node "$node"
    db="$(node_database_path "$node")"
    [[ -f "$db" ]] || die "database not found: $db"

    echo "============================================================"
    echo "Node: $node"
    echo "Database: $db"

    sqlite3 -header -column "$db" <<'SQL'
PRAGMA foreign_keys;
PRAGMA integrity_check;

SELECT node_name, node_role FROM node_info;
SELECT COUNT(*) AS sensors_count FROM sensors;
SELECT COUNT(*) AS readings_count FROM sensor_readings;

SELECT
    s.sensor_id,
    s.sensor_type,
    s.sensor_name,
    s.location,
    r.value,
    s.unit,
    r.recorded_at
FROM sensors AS s
JOIN sensor_readings AS r
    ON r.sensor_id = s.sensor_id
WHERE r.id = (
    SELECT r2.id
    FROM sensor_readings AS r2
    WHERE r2.sensor_id = s.sensor_id
    ORDER BY r2.recorded_at DESC, r2.id DESC
    LIMIT 1
)
ORDER BY s.sensor_id;
SQL
    echo
}

if [[ "$TARGET" == "all" ]]; then
    for node in master slave1 slave2; do
        check_one "$node"
    done
else
    check_one "$TARGET"
fi
