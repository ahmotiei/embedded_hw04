#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/config/daemon.env}"
shift || true

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Configuration file not found: $CONFIG_FILE" >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$CONFIG_FILE"

: "${SOURCE_DATABASES:?SOURCE_DATABASES is required}"
: "${ALERT_DATABASE:?ALERT_DATABASE is required}"
: "${RULES_FILE:?RULES_FILE is required}"

IFS=':' read -r -a source_databases <<< "$SOURCE_DATABASES"
arguments=(--alert-db "$ALERT_DATABASE" --rules "$RULES_FILE")
for database in "${source_databases[@]}"; do
    [[ -n "$database" ]] || continue
    arguments+=(--source-db "$database")
done

if [[ -n "${CHECK_INTERVAL_SECONDS:-}" ]]; then
    arguments+=(--interval "$CHECK_INTERVAL_SECONDS")
fi

exec "$PROJECT_ROOT/daemon/sensor_alert_daemon" "${arguments[@]}" "$@"
