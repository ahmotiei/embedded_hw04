#!/usr/bin/env bash

mqtt_trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "$value"
}

mqtt_load_config() {
    local config_file="$1"

    if [[ ! -f "$config_file" ]]; then
        echo "Error: configuration file not found: $config_file" >&2
        return 1
    fi

    while IFS='=' read -r raw_key raw_value; do
        local key value
        key="$(mqtt_trim "$raw_key")"
        value="$(mqtt_trim "${raw_value%%#*}")"

        [[ -z "$key" || "$key" == \#* ]] && continue

        if [[ ! "$key" =~ ^[A-Z][A-Z0-9_]*$ ]]; then
            echo "Error: invalid configuration key: $key" >&2
            return 1
        fi

        printf -v "$key" '%s' "$value"
    done < "$config_file"

    : "${MQTT_BROKER_HOST:?MQTT_BROKER_HOST is missing}"
    : "${MQTT_BROKER_PORT:?MQTT_BROKER_PORT is missing}"
    : "${MQTT_REQUEST_TOPIC:?MQTT_REQUEST_TOPIC is missing}"
    : "${MQTT_RESPONSE_PREFIX:?MQTT_RESPONSE_PREFIX is missing}"
    : "${MQTT_QOS:?MQTT_QOS is missing}"
    : "${MQTT_VERSION:?MQTT_VERSION is missing}"
    : "${MEMCACHED_HOST:?MEMCACHED_HOST is missing}"
    : "${MEMCACHED_PORT:?MEMCACHED_PORT is missing}"

    if [[ "$MQTT_VERSION" != "4" ]]; then
        echo "Error: this project is configured for MQTT 3.1.1 (MQTT_VERSION=4)." >&2
        return 1
    fi

    if [[ ! "$MQTT_QOS" =~ ^[012]$ ]]; then
        echo "Error: MQTT_QOS must be 0, 1, or 2." >&2
        return 1
    fi

    MQTT_CLI_VERSION="mqttv311"
    RESPONSE_TIMEOUT_SECONDS="${RESPONSE_TIMEOUT_SECONDS:-5}"
}

mqtt_require_commands() {
    local missing=0
    local command_name

    for command_name in mosquitto_pub mosquitto_sub timeout sed grep awk date nc; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "Error: required command is not installed: $command_name" >&2
            missing=1
        fi
    done

    return "$missing"
}

mqtt_flush_master_cache() {
    local response

    response="$({ printf 'flush_all\r\nquit\r\n'; } | \
        nc -w 2 "$MEMCACHED_HOST" "$MEMCACHED_PORT" 2>/dev/null || true)"

    if [[ "$response" != *OK* ]]; then
        echo "Error: could not flush Master Memcached at ${MEMCACHED_HOST}:${MEMCACHED_PORT}." >&2
        return 1
    fi

    echo "Master cache flushed: ${MEMCACHED_HOST}:${MEMCACHED_PORT}"
}

mqtt_extract_string() {
    local json="$1"
    local key="$2"

    printf '%s' "$json" | sed -n \
        "s/.*\"${key}\":\"\([^\"]*\)\".*/\1/p" | head -n 1
}

mqtt_extract_boolean() {
    local json="$1"
    local key="$2"

    printf '%s' "$json" | sed -n \
        "s/.*\"${key}\":\(true\|false\).*/\1/p" | head -n 1
}

mqtt_extract_integer() {
    local json="$1"
    local key="$2"

    printf '%s' "$json" | sed -n \
        "s/.*\"${key}\":\([0-9][0-9]*\).*/\1/p" | head -n 1
}

mqtt_request() {
    local sensor_id="$1"
    local sensor_type="$2"
    local output_file="$3"
    local response_topic="${MQTT_RESPONSE_PREFIX}/${sensor_id}"
    local start_ns end_ns subscriber_pid subscriber_status

    : > "$output_file"

    timeout "${RESPONSE_TIMEOUT_SECONDS}s" \
        mosquitto_sub \
            -h "$MQTT_BROKER_HOST" \
            -p "$MQTT_BROKER_PORT" \
            -V "$MQTT_CLI_VERSION" \
            -q "$MQTT_QOS" \
            -t "$response_topic" \
            -C 1 \
        > "$output_file" 2>/dev/null &

    subscriber_pid=$!
    sleep 0.20

    start_ns="$(date +%s%N)"

    if ! mosquitto_pub \
        -h "$MQTT_BROKER_HOST" \
        -p "$MQTT_BROKER_PORT" \
        -V "$MQTT_CLI_VERSION" \
        -q "$MQTT_QOS" \
        -t "$MQTT_REQUEST_TOPIC" \
        -m "{\"sensor_type\":\"${sensor_type}\",\"sensor_id\":\"${sensor_id}\"}"; then

        kill "$subscriber_pid" 2>/dev/null || true
        wait "$subscriber_pid" 2>/dev/null || true
        echo "Error: MQTT publish failed for sensor $sensor_id." >&2
        return 1
    fi

    if wait "$subscriber_pid"; then
        subscriber_status=0
    else
        subscriber_status=$?
    fi

    end_ns="$(date +%s%N)"

    if [[ "$subscriber_status" -ne 0 ]]; then
        echo "Error: timed out waiting for response on $response_topic." >&2
        return 1
    fi

    MQTT_RESPONSE="$(cat "$output_file")"
    MQTT_ROUND_TRIP_US=$(( (end_ns - start_ns) / 1000 ))
    MQTT_SUCCESS="$(mqtt_extract_boolean "$MQTT_RESPONSE" success)"
    MQTT_SOURCE="$(mqtt_extract_string "$MQTT_RESPONSE" source)"
    MQTT_SERVICE_TIME_US="$(mqtt_extract_integer "$MQTT_RESPONSE" response_time_us)"
    MQTT_RESPONSE_SENSOR_ID="$(mqtt_extract_string "$MQTT_RESPONSE" sensor_id)"

    if [[ -z "$MQTT_SUCCESS" || -z "$MQTT_SOURCE" || -z "$MQTT_SERVICE_TIME_US" ]]; then
        echo "Error: malformed MQTT response: $MQTT_RESPONSE" >&2
        return 1
    fi

    if [[ "$MQTT_RESPONSE_SENSOR_ID" != "$sensor_id" ]]; then
        echo "Error: response sensor_id mismatch: expected $sensor_id, got $MQTT_RESPONSE_SENSOR_ID." >&2
        return 1
    fi
}

mqtt_first_sensor_from_csv() {
    local csv_file="$1"

    awk -F',' '
        FNR == 2 {
            gsub(/\r/, "", $0)
            print $1 "\t" $2
            exit
        }
    ' "$csv_file"
}

mqtt_all_unique_sensors() {
    awk -F',' '
        FNR > 1 && NF >= 2 {
            gsub(/\r/, "", $0)
            key = $1 SUBSEP $2
            if (!seen[key]++) {
                print $1 "\t" $2
            }
        }
    ' "$@"
}
