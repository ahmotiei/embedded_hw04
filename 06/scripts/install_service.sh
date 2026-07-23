#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/config/daemon.env}"
SERVICE_NAME="sensor-alert.service"
TEMPLATE="$PROJECT_ROOT/service/sensor-alert.service.template"
GENERATED="$PROJECT_ROOT/service/$SERVICE_NAME"

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Configuration file not found: $CONFIG_FILE" >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$CONFIG_FILE"
: "${ALERT_DATABASE:?ALERT_DATABASE is required}"

SERVICE_USER="${SERVICE_USER:-$USER}"
ALERT_DIRECTORY="$(dirname "$ALERT_DATABASE")"
mkdir -p "$ALERT_DIRECTORY"

make -C "$PROJECT_ROOT" clean all
"$SCRIPT_DIR/init_alert_db.sh" "$ALERT_DATABASE"

escape_sed() {
    printf '%s' "$1" | sed 's/[&|]/\\&/g'
}

sed \
    -e "s|@SERVICE_USER@|$(escape_sed "$SERVICE_USER")|g" \
    -e "s|@PROJECT_ROOT@|$(escape_sed "$PROJECT_ROOT")|g" \
    -e "s|@CONFIG_FILE@|$(escape_sed "$CONFIG_FILE")|g" \
    -e "s|@ALERT_DIRECTORY@|$(escape_sed "$ALERT_DIRECTORY")|g" \
    "$TEMPLATE" > "$GENERATED"

sudo install -m 0644 "$GENERATED" "/etc/systemd/system/$SERVICE_NAME"
sudo systemctl daemon-reload
sudo systemctl enable --now "$SERVICE_NAME"

sudo systemctl --no-pager --full status "$SERVICE_NAME"
