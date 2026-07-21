#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/config/api.env}"

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Configuration file not found: $CONFIG_FILE" >&2
    echo "Copy config/api.env.example to config/api.env and edit it." >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$CONFIG_FILE"

: "${API_LISTEN_URL:?API_LISTEN_URL is required}"
: "${DATABASE_PATHS:?DATABASE_PATHS is required}"

export MONGOOSE_VERSION="${MONGOOSE_VERSION:-7.21}"
if [[ ! -f "$PROJECT_ROOT/api/mongoose/mongoose.c" ||
      ! -f "$PROJECT_ROOT/api/mongoose/mongoose.h" ]]; then
    "$SCRIPT_DIR/fetch_mongoose.sh"
fi

make -C "$PROJECT_ROOT" clean all

IFS=':' read -r -a databases <<< "$DATABASE_PATHS"
arguments=(--listen "$API_LISTEN_URL")
for database in "${databases[@]}"; do
    [[ -n "$database" ]] || continue
    arguments+=(--db "$database")
done

exec "$PROJECT_ROOT/api/sensor_log_api" "${arguments[@]}"
