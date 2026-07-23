#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="${1:-$PROJECT_ROOT/master/config}"
DATA_DIR="${DATA_DIR:-$PROJECT_ROOT/data}"
OUTPUT_DIR="${OUTPUT_DIR:-$PROJECT_ROOT/test-output}"
RESULT_CSV="$OUTPUT_DIR/mqtt_speed_results.csv"
SUMMARY_MD="$OUTPUT_DIR/mqtt_speed_summary.md"

# shellcheck source=mqtt_common.sh
source "$SCRIPT_DIR/mqtt_common.sh"

mqtt_load_config "$CONFIG_FILE"
mqtt_require_commands

shopt -s nullglob
CSV_FILES=("$DATA_DIR"/*.csv)
shopt -u nullglob

if [[ "${#CSV_FILES[@]}" -eq 0 ]]; then
    echo "Error: no CSV data files were found in $DATA_DIR." >&2
    exit 1
fi

mapfile -t SENSORS < <(mqtt_all_unique_sensors "${CSV_FILES[@]}")

if [[ "${#SENSORS[@]}" -eq 0 ]]; then
    echo "Error: no sensors were found in the CSV data files." >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mqtt_flush_master_cache

printf '%s\n' \
    'round,sensor_id,sensor_type,success,source,service_time_us,round_trip_us' \
    > "$RESULT_CSV"

failures=0

run_round() {
    local round="$1"
    local expected_source="$2"
    local sensor_line sensor_id sensor_type response_file

    echo
    echo "========== Round $round =========="

    for sensor_line in "${SENSORS[@]}"; do
        IFS=$'\t' read -r sensor_id sensor_type <<< "$sensor_line"
        response_file="$TMP_DIR/round_${round}_${sensor_id}.json"

        printf 'Sensor %-8s %-14s ' "$sensor_id" "$sensor_type"

        if ! mqtt_request "$sensor_id" "$sensor_type" "$response_file"; then
            echo "FAILED"
            printf '%s,%s,%s,false,error,0,0\n' \
                "$round" "$sensor_id" "$sensor_type" >> "$RESULT_CSV"
            failures=$((failures + 1))
            continue
        fi

        printf '%s,%s,%s,%s,%s,%s,%s\n' \
            "$round" \
            "$sensor_id" \
            "$sensor_type" \
            "$MQTT_SUCCESS" \
            "$MQTT_SOURCE" \
            "$MQTT_SERVICE_TIME_US" \
            "$MQTT_ROUND_TRIP_US" \
            >> "$RESULT_CSV"

        if [[ "$MQTT_SUCCESS" != "true" ]]; then
            echo "FAILED (success=$MQTT_SUCCESS, source=$MQTT_SOURCE)"
            failures=$((failures + 1))
            continue
        fi

        if [[ "$round" -eq 1 && "$MQTT_SOURCE" == "cache" ]]; then
            echo "FAILED (Round 1 unexpectedly used cache)"
            failures=$((failures + 1))
            continue
        fi

        if [[ "$round" -eq 2 && "$MQTT_SOURCE" != "$expected_source" ]]; then
            echo "FAILED (Round 2 source=$MQTT_SOURCE, expected=$expected_source)"
            failures=$((failures + 1))
            continue
        fi

        echo "source=$MQTT_SOURCE, service=${MQTT_SERVICE_TIME_US} us, RTT=${MQTT_ROUND_TRIP_US} us"
    done
}

run_round 1 ""
sleep 1
run_round 2 "cache"

read -r R1_SERVICE R1_RTT R1_COUNT < <(
    awk -F',' '
        NR > 1 && $1 == 1 && $4 == "true" {
            service += $6
            rtt += $7
            count++
        }
        END {
            if (count == 0) print "0 0 0"
            else printf "%.2f %.2f %d\n", service / count, rtt / count, count
        }
    ' "$RESULT_CSV"
)

read -r R2_SERVICE R2_RTT R2_COUNT < <(
    awk -F',' '
        NR > 1 && $1 == 2 && $4 == "true" {
            service += $6
            rtt += $7
            count++
        }
        END {
            if (count == 0) print "0 0 0"
            else printf "%.2f %.2f %d\n", service / count, rtt / count, count
        }
    ' "$RESULT_CSV"
)

SERVICE_RATIO="$(awk -v first="$R1_SERVICE" -v second="$R2_SERVICE" \
    'BEGIN { if (second > 0) printf "%.2f", first / second; else print "N/A" }')"
RTT_RATIO="$(awk -v first="$R1_RTT" -v second="$R2_RTT" \
    'BEGIN { if (second > 0) printf "%.2f", first / second; else print "N/A" }')"
SERVICE_REDUCTION="$(awk -v first="$R1_SERVICE" -v second="$R2_SERVICE" \
    'BEGIN { if (first > 0) printf "%.2f", 100 * (first - second) / first; else print "N/A" }')"
RTT_REDUCTION="$(awk -v first="$R1_RTT" -v second="$R2_RTT" \
    'BEGIN { if (first > 0) printf "%.2f", 100 * (first - second) / first; else print "N/A" }')"

cat > "$SUMMARY_MD" <<EOF_SUMMARY
# MQTT Two-Round Benchmark Summary

- MQTT version: 3.1.1
- QoS: $MQTT_QOS
- Sensors tested: ${#SENSORS[@]}
- Successful Round 1 responses: $R1_COUNT
- Successful Round 2 responses: $R2_COUNT

| Metric | Round 1 | Round 2 | Round 1 / Round 2 | Reduction |
|---|---:|---:|---:|---:|
| Master service time | ${R1_SERVICE} us | ${R2_SERVICE} us | ${SERVICE_RATIO}x | ${SERVICE_REDUCTION}% |
| MQTT end-to-end time | ${R1_RTT} us | ${R2_RTT} us | ${RTT_RATIO}x | ${RTT_REDUCTION}% |

Round 1 starts after flushing the Master cache. Round 2 repeats the same dynamically discovered sensors and requires the Master response source to be cache.

Detailed results: mqtt_speed_results.csv.
EOF_SUMMARY

echo
echo "========== Comparison =========="
echo "Sensors tested: ${#SENSORS[@]}"
echo "Round 1 average service time: ${R1_SERVICE} us"
echo "Round 2 average service time: ${R2_SERVICE} us"
echo "Service-time speedup: ${SERVICE_RATIO}x"
echo "Service-time reduction: ${SERVICE_REDUCTION}%"
echo "Round 1 average MQTT RTT: ${R1_RTT} us"
echo "Round 2 average MQTT RTT: ${R2_RTT} us"
echo "RTT speedup: ${RTT_RATIO}x"
echo "RTT reduction: ${RTT_REDUCTION}%"
echo "Results: $RESULT_CSV"
echo "Summary: $SUMMARY_MD"

if [[ "$failures" -ne 0 ]]; then
    echo "Benchmark completed with $failures validation failure(s)." >&2
    exit 1
fi

echo "Benchmark completed successfully."
