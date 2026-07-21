#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/config/api.env}"

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Configuration file not found: $CONFIG_FILE" >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$CONFIG_FILE"
: "${API_LISTEN_URL:?API_LISTEN_URL is required}"

BASE_URL="${API_LISTEN_URL/http:\/\/0.0.0.0/http:\/\/127.0.0.1}"
BASE_URL="${BASE_URL/http:\/\/\[::\]/http:\/\/127.0.0.1}"

curl --fail --silent --show-error "$BASE_URL/health"
echo
curl --silent --show-error --get "$BASE_URL/api/sensor-logs" \
    --data-urlencode "sensor_name=temperature" \
    --data-urlencode "sensor_id=101" \
    --data-urlencode "date=2026-06-01"
echo
