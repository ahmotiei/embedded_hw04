#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:-}"
if [[ -z "$OUTPUT" ]]; then
    echo "Usage: $0 <output-database>" >&2
    exit 2
fi

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"

sqlite3 "$OUTPUT" <<'SQL'
CREATE TABLE sensors (
    sensor_id TEXT PRIMARY KEY,
    sensor_type TEXT NOT NULL,
    sensor_name TEXT NOT NULL,
    location TEXT,
    unit TEXT,
    node_name TEXT,
    is_active INTEGER DEFAULT 1
);

CREATE TABLE sensor_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sensor_id TEXT NOT NULL,
    value TEXT NOT NULL,
    recorded_at TEXT NOT NULL
);

INSERT INTO sensors(sensor_id, sensor_type, sensor_name, location, unit, node_name)
VALUES
    ('101', 'temperature', 'temperature', 'floor-1', 'C', 'master'),
    ('201', 'humidity', 'humidity', 'floor-1', '%', 'master'),
    ('301', 'temperature', 'temperature-invalid', 'floor-2', 'C', 'master');

INSERT INTO sensor_readings(sensor_id, value, recorded_at)
VALUES
    ('101', '35.2', datetime('now')),
    ('201', '18.5', datetime('now')),
    ('301', 'not-a-number', datetime('now'));
SQL

echo "Created daemon test database: $OUTPUT"
