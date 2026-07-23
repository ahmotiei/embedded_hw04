#!/usr/bin/env bash
set -euo pipefail

DATABASE="${1:-}"
if [[ -z "$DATABASE" ]]; then
    echo "Usage: $0 <alert-database>" >&2
    exit 2
fi

mkdir -p "$(dirname "$DATABASE")"

sqlite3 "$DATABASE" <<'SQL'
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

CREATE TABLE IF NOT EXISTS alerts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sensor_id TEXT NOT NULL,
    sensor_name TEXT NOT NULL,
    alert_type TEXT NOT NULL,
    sensor_value TEXT NOT NULL,
    created_at TEXT NOT NULL,
    status TEXT NOT NULL,
    source_recorded_at TEXT NOT NULL,
    source_db TEXT NOT NULL,
    details TEXT NOT NULL,
    UNIQUE(sensor_id, sensor_name, alert_type, source_recorded_at, source_db)
);

CREATE INDEX IF NOT EXISTS idx_alerts_status_created
    ON alerts(status, created_at);

CREATE INDEX IF NOT EXISTS idx_alerts_sensor
    ON alerts(sensor_id, sensor_name);
SQL

echo "Alert database initialized: $DATABASE"
