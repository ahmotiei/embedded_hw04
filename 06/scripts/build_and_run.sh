#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/config/daemon.env}"
shift || true

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Configuration file not found: $CONFIG_FILE" >&2
    echo "Copy config/daemon.env.example to config/daemon.env and edit it." >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$CONFIG_FILE"
: "${ALERT_DATABASE:?ALERT_DATABASE is required}"

make -C "$PROJECT_ROOT" clean all
"$SCRIPT_DIR/init_alert_db.sh" "$ALERT_DATABASE"
exec "$SCRIPT_DIR/run_daemon.sh" "$CONFIG_FILE" "$@"
