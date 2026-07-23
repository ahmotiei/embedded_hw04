#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/master/config}"
DATA_DIR="${DATA_DIR:-$PROJECT_ROOT/data}"
OUTPUT_DIR="${OUTPUT_DIR:-$PROJECT_ROOT/test-output}"
MASTER_HTTP_HOST="${MASTER_HTTP_HOST:-127.0.0.1}"

# Reuse the safe configuration parser and dynamic sensor discovery.
# shellcheck source=mqtt_common.sh
source "$SCRIPT_DIR/mqtt_common.sh"
mqtt_load_config "$CONFIG_FILE"

for command_name in curl awk sed nc; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Error: required command is not installed: $command_name" >&2
        exit 1
    }
done

shopt -s nullglob
CSV_FILES=("$DATA_DIR"/*.csv)
shopt -u nullglob

if [[ "${#CSV_FILES[@]}" -eq 0 ]]; then
    echo "Error: no CSV files found in $DATA_DIR" >&2
    exit 1
fi

mapfile -t SENSORS < <(mqtt_all_unique_sensors "${CSV_FILES[@]}")
mkdir -p "$OUTPUT_DIR"
OUTPUT="$OUTPUT_DIR/cache_speed_result.csv"
MASTER_URL="http://${MASTER_HTTP_HOST}:${PORT}/api/sensor"

mqtt_flush_master_cache
printf '%s\n' 'round,sensor_id,sensor_type,source,response_time_us' > "$OUTPUT"

for round in 1 2; do
    echo "========== HTTP cache round $round =========="

    for sensor in "${SENSORS[@]}"; do
        IFS=$'\t' read -r sensor_id sensor_type <<< "$sensor"
        response="$(curl --fail --silent --show-error --max-time 5 \
            "${MASTER_URL}?type=${sensor_type}&id=${sensor_id}")"
        source_value="$(mqtt_extract_string "$response" source)"
        time_value="$(mqtt_extract_integer "$response" response_time_us)"

        if [[ -z "$source_value" || -z "$time_value" ]]; then
            echo "Error: malformed HTTP response for sensor $sensor_id: $response" >&2
            exit 1
        fi

        printf '%s,%s,%s,%s,%s\n' \
            "$round" "$sensor_id" "$sensor_type" "$source_value" "$time_value" \
            >> "$OUTPUT"

        echo "Sensor $sensor_id -> $source_value (${time_value} us)"
    done
done

echo "Result saved in $OUTPUT"
