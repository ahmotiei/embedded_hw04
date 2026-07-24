#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/master/config}"
DATA_DIR="${DATA_DIR:-$PROJECT_ROOT/data}"

# shellcheck source=mqtt_common.sh
source "$SCRIPT_DIR/mqtt_common.sh"

mqtt_load_config "$CONFIG_FILE"
mqtt_require_commands

for csv_file in \
    "$DATA_DIR/master_sensors.csv" \
    "$DATA_DIR/slave1_sensors.csv" \
    "$DATA_DIR/slave2_sensors.csv"; do
    [[ -f "$csv_file" ]] || {
        echo "Error: data file not found: $csv_file" >&2
        exit 1
    }
done

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mqtt_flush_master_cache

read -r MASTER_ID MASTER_TYPE < <(
    mqtt_first_sensor_from_csv "$DATA_DIR/master_sensors.csv"
)
read -r SLAVE1_ID SLAVE1_TYPE < <(
    mqtt_first_sensor_from_csv "$DATA_DIR/slave1_sensors.csv"
)
read -r SLAVE2_ID SLAVE2_TYPE < <(
    mqtt_first_sensor_from_csv "$DATA_DIR/slave2_sensors.csv"
)

passed=0
failed=0

run_case() {
    local label="$1"
    local sensor_id="$2"
    local sensor_type="$3"
    local expected_success="$4"
    local expected_source="$5"
    local response_file="$TMP_DIR/${label}.json"

    printf '%-28s' "$label"

    if ! mqtt_request "$sensor_id" "$sensor_type" "$response_file"; then
        echo "FAILED (no valid response)"
        failed=$((failed + 1))
        return
    fi

    if [[ "$MQTT_SUCCESS" != "$expected_success" ]]; then
        echo "FAILED (success=$MQTT_SUCCESS)"
        failed=$((failed + 1))
        return
    fi

    if [[ -n "$expected_source" && "$MQTT_SOURCE" != "$expected_source" ]]; then
        echo "FAILED (source=$MQTT_SOURCE, expected=$expected_source)"
        failed=$((failed + 1))
        return
    fi

    echo "PASSED (source=$MQTT_SOURCE, service=${MQTT_SERVICE_TIME_US} us, RTT=${MQTT_ROUND_TRIP_US} us)"
    passed=$((passed + 1))
}

echo "============================================"
echo "MQTT functional test"
echo "Broker: ${MQTT_BROKER_HOST}:${MQTT_BROKER_PORT}"
echo "Version: MQTT 3.1.1 | QoS: $MQTT_QOS"
echo "============================================"

run_case "Master database lookup" "$MASTER_ID" "$MASTER_TYPE" true master
run_case "Slave1 distributed lookup" "$SLAVE1_ID" "$SLAVE1_TYPE" true slave1
run_case "Slave2 distributed lookup" "$SLAVE2_ID" "$SLAVE2_TYPE" true slave2
run_case "Repeated request uses cache" "$MASTER_ID" "$MASTER_TYPE" true cache
run_case "Unknown sensor" "999999" "unknown" false not_found

echo "--------------------------------------------"
echo "Passed: $passed"
echo "Failed: $failed"

if [[ "$failed" -ne 0 ]]; then
    exit 1
fi

echo "All MQTT functional tests passed."
