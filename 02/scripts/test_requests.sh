#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

require_command curl
require_command python3

MASTER_CONFIG="${MASTER_CONFIG:-$(node_config_path master)}"
[[ -f "$MASTER_CONFIG" ]] || die "Master config file not found: $MASTER_CONFIG"

MASTER_HOST="${MASTER_HOST:-127.0.0.1}"
MASTER_PORT="${MASTER_PORT:-$(require_config_value "$MASTER_CONFIG" PORT)}"
BASE_URL="http://$MASTER_HOST:$MASTER_PORT"

for node in master slave1 slave2; do
    [[ -f "$(node_csv_path "$node")" ]] || \
        die "sensor CSV file not found: $(node_csv_path "$node")"
done

# Pick one sensor that is unique to each node. This avoids hard-coded IDs and
# remains valid when the instructor replaces the input CSV files.
mapfile -t TEST_SENSORS < <(
    python3 - \
        "$(node_csv_path master)" \
        "$(node_csv_path slave1)" \
        "$(node_csv_path slave2)" <<'PY'
import csv
import sys

nodes = ["master", "slave1", "slave2"]
paths = sys.argv[1:]
sets = []
ordered = []

for path in paths:
    node_rows = []
    node_set = set()
    with open(path, newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        required = {"sensor_id", "sensor_type"}
        if not required.issubset(reader.fieldnames or []):
            raise SystemExit(f"missing required columns in {path}: {sorted(required)}")
        for row in reader:
            pair = (row["sensor_id"].strip(), row["sensor_type"].strip())
            if pair[0] and pair[1] and pair not in node_set:
                node_set.add(pair)
                node_rows.append(pair)
    sets.append(node_set)
    ordered.append(node_rows)

for index, node in enumerate(nodes):
    earlier = set().union(*sets[:index]) if index else set()
    pair = next((item for item in ordered[index] if item not in earlier), None)
    if pair is None:
        print(f"{node}\t\t")
    else:
        print(f"{node}\t{pair[0]}\t{pair[1]}")
PY
)

# Flush only the Master cache. After this, the first request must traverse the
# normal lookup path and the repeated request must be served by Master cache.
"$SCRIPT_DIR/flush_cache.sh" master

TMP_BODY="$(mktemp)"
trap 'rm -f "$TMP_BODY"' EXIT

PASSED=0
FAILED=0
SKIPPED=0

json_value() {
    local file="$1"
    local key="$2"

    python3 - "$file" "$key" <<'PY'
import json
import sys

path, key = sys.argv[1], sys.argv[2]
try:
    with open(path, encoding="utf-8") as handle:
        payload = json.load(handle)
except Exception:
    print("")
    raise SystemExit(0)

value = payload.get(key, "")
if value is None:
    value = ""
print(value)
PY
}

request_sensor() {
    local sensor_id="$1"
    local sensor_type="$2"

    curl \
        --silent \
        --show-error \
        --connect-timeout 2 \
        --max-time 10 \
        --get \
        --data-urlencode "id=$sensor_id" \
        --data-urlencode "type=$sensor_type" \
        --output "$TMP_BODY" \
        --write-out "%{http_code}" \
        "$BASE_URL/api/sensor"
}

assert_sensor_request() {
    local title="$1"
    local sensor_id="$2"
    local sensor_type="$3"
    local expected_source="$4"
    local status
    local actual_source

    echo "============================================================"
    echo "Test: $title"
    echo "Sensor: id=$sensor_id, type=$sensor_type"

    if ! status="$(request_sensor "$sensor_id" "$sensor_type")"; then
        echo "Result: FAILED (curl error)"
        FAILED=$((FAILED + 1))
        return
    fi

    echo "HTTP status: $status"
    echo "Response:"
    cat "$TMP_BODY"
    echo

    actual_source="$(json_value "$TMP_BODY" source)"

    if [[ "$status" == "200" && "$actual_source" == "$expected_source" ]]; then
        echo "Result: PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "Result: FAILED"
        echo "Expected status/source: 200 / $expected_source"
        echo "Actual status/source:   $status / $actual_source"
        FAILED=$((FAILED + 1))
    fi
}

for entry in "${TEST_SENSORS[@]}"; do
    IFS=$'\t' read -r node sensor_id sensor_type <<< "$entry"

    if [[ -z "$sensor_id" || -z "$sensor_type" ]]; then
        echo "============================================================"
        echo "Test for $node: SKIPPED"
        echo "No sensor unique to $node was found in the current CSV files."
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # Clear Master cache before testing the original source.
    "$SCRIPT_DIR/flush_cache.sh" master >/dev/null
    assert_sensor_request "First request reaches $node" "$sensor_id" "$sensor_type" "$node"
    assert_sensor_request "Repeated request is served by cache" "$sensor_id" "$sensor_type" "cache"
done

run_raw_test() {
    local title="$1"
    local expected_status="$2"
    local expected_error="$3"
    shift 3

    local status
    local actual_error

    echo "============================================================"
    echo "Test: $title"

    if ! status="$(
        curl \
            --silent \
            --show-error \
            --connect-timeout 2 \
            --max-time 10 \
            --output "$TMP_BODY" \
            --write-out "%{http_code}" \
            "$@"
    )"; then
        echo "Result: FAILED (curl error)"
        FAILED=$((FAILED + 1))
        return
    fi

    echo "HTTP status: $status"
    echo "Response:"
    cat "$TMP_BODY"
    echo

    actual_error="$(json_value "$TMP_BODY" error)"

    if [[ "$status" == "$expected_status" && "$actual_error" == "$expected_error" ]]; then
        echo "Result: PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "Result: FAILED"
        echo "Expected status/error: $expected_status / $expected_error"
        echo "Actual status/error:   $status / $actual_error"
        FAILED=$((FAILED + 1))
    fi
}

run_raw_test \
    "Sensor not found in any node" \
    "404" \
    "sensor reading not found in any node" \
    --get \
    --data-urlencode "id=__missing_sensor__" \
    --data-urlencode "type=__missing_type__" \
    "$BASE_URL/api/sensor"

run_raw_test \
    "Missing type query parameter" \
    "400" \
    "id and type query parameters are required" \
    --get \
    --data-urlencode "id=__test__" \
    "$BASE_URL/api/sensor"

run_raw_test \
    "Unknown API route" \
    "404" \
    "route not found" \
    "$BASE_URL/api/unknown"

echo "============================================================"
echo "Test summary"
echo "Passed:  $PASSED"
echo "Failed:  $FAILED"
echo "Skipped: $SKIPPED"

(( FAILED == 0 )) || exit 1

echo "All executable tests passed successfully."
