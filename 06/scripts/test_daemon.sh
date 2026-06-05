#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DB="$PROJECT_ROOT/tests/source.db"
ALERT_DB="$PROJECT_ROOT/tests/alerts.db"
RULES_FILE="$PROJECT_ROOT/config/alert_rules.conf"

"$SCRIPT_DIR/create_test_db.sh" "$SOURCE_DB"
rm -f "$ALERT_DB"

"$PROJECT_ROOT/daemon/sensor_alert_daemon" \
    --source-db "$SOURCE_DB" \
    --alert-db "$ALERT_DB" \
    --rules "$RULES_FILE" \
    --once

count="$(sqlite3 "$ALERT_DB" "SELECT COUNT(*) FROM alerts WHERE status='OPEN';")"
if [[ "$count" -ne 3 ]]; then
    echo "FAIL: expected 3 open alerts, got $count" >&2
    sqlite3 -header -column "$ALERT_DB" \
        "SELECT sensor_id, sensor_name, alert_type, sensor_value, status FROM alerts;"
    exit 1
fi

# Running the same cycle again must not duplicate alerts for the same reading.
"$PROJECT_ROOT/daemon/sensor_alert_daemon" \
    --source-db "$SOURCE_DB" \
    --alert-db "$ALERT_DB" \
    --rules "$RULES_FILE" \
    --once >/dev/null

count_after="$(sqlite3 "$ALERT_DB" "SELECT COUNT(*) FROM alerts;")"
if [[ "$count_after" -ne 3 ]]; then
    echo "FAIL: duplicate alerts were inserted" >&2
    exit 1
fi

sqlite3 -header -column "$ALERT_DB" \
    "SELECT sensor_id, sensor_name, alert_type, sensor_value, status FROM alerts ORDER BY id;"
echo "PASS: daemon alert test succeeded."
