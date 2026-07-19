#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

databases=(
    "$PROJECT_ROOT/master/master.db"
    "$PROJECT_ROOT/slave1/slave1.db"
    "$PROJECT_ROOT/slave2/slave2.db"
)

for db in "${databases[@]}"; do
    if [[ ! -f "$db" ]]; then
        echo "Error: database not found: $db"
        exit 1
    fi

    echo "============================================================"
    echo "Database: $db"

    sqlite3 -header -column "$db" <<'EOF'
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
EOF

    echo
done
