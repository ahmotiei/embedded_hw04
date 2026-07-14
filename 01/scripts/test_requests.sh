#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MASTER_CONFIG="${MASTER_CONFIG:-$PROJECT_ROOT/master/config}"

if ! command -v curl >/dev/null 2>&1; then
    echo "Error: curl is not installed."
    exit 1
fi

if [[ ! -f "$MASTER_CONFIG" ]]; then
    echo "Error: Master config file not found: $MASTER_CONFIG"
    exit 1
fi

read_config_value() {
    local key="$1"

    awk -F= -v requested_key="$key" '
        /^[[:space:]]*#/ { next }
        $1 ~ "^[[:space:]]*" requested_key "[[:space:]]*$" {
            value = substr($0, index($0, "=") + 1)
            sub(/[[:space:]]*#.*/, "", value)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$MASTER_CONFIG"
}

MASTER_HOST="${MASTER_HOST:-127.0.0.1}"
MASTER_PORT="${MASTER_PORT:-$(read_config_value PORT)}"

if [[ -z "$MASTER_PORT" ]]; then
    echo "Error: PORT is missing from $MASTER_CONFIG"
    exit 1
fi

BASE_URL="http://$MASTER_HOST:$MASTER_PORT"
TEMP_BODY="$(mktemp)"
trap 'rm -f "$TEMP_BODY"' EXIT

PASSED=0
FAILED=0

run_test() {
    local title="$1"
    local path="$2"
    local expected_status="$3"
    local expected_text="${4:-}"

    echo "============================================================"
    echo "Test: $title"
    echo "Request: GET $BASE_URL$path"

    local status

    if ! status="$(
        curl \
            --silent \
            --show-error \
            --connect-timeout 2 \
            --max-time 8 \
            --output "$TEMP_BODY" \
            --write-out "%{http_code}" \
            "$BASE_URL$path"
    )"; then
        echo "Result: FAILED (curl could not complete the request)"
        FAILED=$((FAILED + 1))
        echo
        return
    fi

    echo "HTTP status: $status"
    echo "Response:"
    cat "$TEMP_BODY"
    echo

    if [[ "$status" != "$expected_status" ]]; then
        echo "Result: FAILED"
        echo "Expected HTTP status: $expected_status"
        FAILED=$((FAILED + 1))
        echo
        return
    fi

    if [[ -n "$expected_text" ]] &&
       ! grep -Fq "$expected_text" "$TEMP_BODY"; then
        echo "Result: FAILED"
        echo "Expected response text: $expected_text"
        FAILED=$((FAILED + 1))
        echo
        return
    fi

    echo "Result: PASSED"
    PASSED=$((PASSED + 1))
    echo
}

run_test \
    "Reading found in Master" \
    "/api/sensor?id=101&type=temperature" \
    "200" \
    '"source":"master"'

run_test \
    "Reading found in Slave1 through Master" \
    "/api/sensor?id=204&type=co2" \
    "200" \
    '"source":"slave1"'

run_test \
    "Reading found in Slave2 through Master" \
    "/api/sensor?id=304&type=smoke" \
    "200" \
    '"source":"slave2"'

run_test \
    "Reading not found in any node" \
    "/api/sensor?id=999&type=temperature" \
    "404" \
    '"error":"sensor reading not found in any node"'

run_test \
    "Invalid request with missing type" \
    "/api/sensor?id=101" \
    "400" \
    '"error":"id and type query parameters are required"'

run_test \
    "Unknown API route" \
    "/api/unknown" \
    "404" \
    '"error":"route not found"'

echo "============================================================"
echo "Test summary"
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if (( FAILED > 0 )); then
    exit 1
fi

echo "All tests passed successfully."
