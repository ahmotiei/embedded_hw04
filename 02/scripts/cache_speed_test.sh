#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

require_command curl
require_command python3

usage() {
    cat <<USAGE
Usage:
  $0 [options]

Options:
  --host HOST       Master host used by curl. Default: MASTER_HOST or 127.0.0.1
  --port PORT       Master port. Default: MASTER_PORT or master/config PORT
  --output FILE     Detailed CSV output path.
  --summary FILE    Summary CSV output path.
  --no-flush        Do not flush Master cache before round 1.
  --no-strict       Do not fail when a round-2 response is not from cache.

The script reads every sensor dynamically from data/master_sensors.csv,
data/slave1_sensors.csv, and data/slave2_sensors.csv. No sensor ID, type,
IP address, or port is hard-coded.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

MASTER_CONFIG="${MASTER_CONFIG:-$(node_config_path master)}"
[[ -f "$MASTER_CONFIG" ]] || die "Master config file not found: $MASTER_CONFIG"

MASTER_HOST="${MASTER_HOST:-127.0.0.1}"
MASTER_PORT="${MASTER_PORT:-$(require_config_value "$MASTER_CONFIG" PORT)}"
OUTPUT_FILE="${OUTPUT_FILE:-$PROJECT_ROOT/cache_speed_result.csv}"
SUMMARY_FILE="${SUMMARY_FILE:-$PROJECT_ROOT/cache_speed_summary.csv}"
DO_FLUSH=1
STRICT_CACHE=1

while (( $# > 0 )); do
    case "$1" in
        --host) [[ $# -ge 2 ]] || die "--host requires a value"; MASTER_HOST="$2"; shift ;;
        --port) [[ $# -ge 2 ]] || die "--port requires a value"; MASTER_PORT="$2"; shift ;;
        --output) [[ $# -ge 2 ]] || die "--output requires a value"; OUTPUT_FILE="$2"; shift ;;
        --summary) [[ $# -ge 2 ]] || die "--summary requires a value"; SUMMARY_FILE="$2"; shift ;;
        --no-flush) DO_FLUSH=0 ;;
        --no-strict) STRICT_CACHE=0 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
 done

BASE_URL="http://$MASTER_HOST:$MASTER_PORT/api/sensor"
mkdir -p "$(dirname "$OUTPUT_FILE")" "$(dirname "$SUMMARY_FILE")"

for node in master slave1 slave2; do
    [[ -f "$(node_csv_path "$node")" ]] || \
        die "sensor CSV file not found: $(node_csv_path "$node")"
done

SENSOR_LIST="$(mktemp)"
TEMP_BODY="$(mktemp)"
trap 'rm -f "$SENSOR_LIST" "$TEMP_BODY"' EXIT

python3 - \
    "$(node_csv_path master)" \
    "$(node_csv_path slave1)" \
    "$(node_csv_path slave2)" > "$SENSOR_LIST" <<'PY'
import csv
import sys

nodes = ["master", "slave1", "slave2"]
seen = set()

for node, path in zip(nodes, sys.argv[1:]):
    with open(path, newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        required = {"sensor_id", "sensor_type"}
        if not required.issubset(reader.fieldnames or []):
            raise SystemExit(f"missing required columns in {path}: {sorted(required)}")

        for row in reader:
            sensor_id = row["sensor_id"].strip()
            sensor_type = row["sensor_type"].strip()
            key = (sensor_id, sensor_type)

            if not sensor_id or not sensor_type or key in seen:
                continue

            seen.add(key)
            print(f"{node}\t{sensor_id}\t{sensor_type}")
PY

SENSOR_COUNT="$(wc -l < "$SENSOR_LIST" | tr -d ' ')"
(( SENSOR_COUNT > 0 )) || die "no sensors were found in the CSV files"

if (( DO_FLUSH )); then
    # The benchmark is intended to run on the Master VM, where the Master
    # Memcached instance is reachable through master/config.
    "$SCRIPT_DIR/flush_cache.sh" master
else
    warn "Master cache was not flushed; round 1 may contain cache hits"
fi

python3 - "$OUTPUT_FILE" <<'PY'
import csv
import sys

with open(sys.argv[1], "w", newline="", encoding="utf-8") as handle:
    writer = csv.writer(handle)
    writer.writerow([
        "round",
        "expected_node",
        "sensor_id",
        "sensor_type",
        "http_status",
        "source",
        "server_response_time_us",
        "client_total_time_us",
        "value",
        "recorded_at",
        "error",
    ])
PY

REQUEST_FAILURES=0
ROUND2_NON_CACHE=0

append_result() {
    local round="$1"
    local expected_node="$2"
    local sensor_id="$3"
    local sensor_type="$4"
    local http_status="$5"
    local client_time_us="$6"
    local body_file="$7"

    python3 - \
        "$OUTPUT_FILE" \
        "$round" \
        "$expected_node" \
        "$sensor_id" \
        "$sensor_type" \
        "$http_status" \
        "$client_time_us" \
        "$body_file" <<'PY'
import csv
import json
import sys

(
    output_file,
    round_number,
    expected_node,
    sensor_id,
    sensor_type,
    http_status,
    client_time_us,
    body_file,
) = sys.argv[1:]

payload = {}
parse_error = ""
try:
    with open(body_file, encoding="utf-8") as handle:
        payload = json.load(handle)
except Exception as exc:
    parse_error = f"invalid JSON: {exc}"

source = payload.get("source", "")
server_time = payload.get("response_time_us", "")
value = payload.get("value", "")
recorded_at = payload.get("recorded_at", "")
error = payload.get("error", parse_error)

with open(output_file, "a", newline="", encoding="utf-8") as handle:
    writer = csv.writer(handle)
    writer.writerow([
        round_number,
        expected_node,
        sensor_id,
        sensor_type,
        http_status,
        source,
        server_time,
        client_time_us,
        value,
        recorded_at,
        error,
    ])

print(f"{source}\t{server_time}\t{error}")
PY
}

run_round() {
    local round="$1"

    echo
    echo "============================================================"
    if [[ "$round" == "1" ]]; then
        echo "Round 1: cold Master cache / normal lookup path"
    else
        echo "Round 2: warm Master cache"
    fi
    echo "============================================================"

    while IFS=$'\t' read -r expected_node sensor_id sensor_type; do
        local curl_meta
        local http_status
        local total_seconds
        local client_time_us
        local parsed
        local source
        local server_time
        local error

        if ! curl_meta="$(
            curl \
                --silent \
                --show-error \
                --connect-timeout 2 \
                --max-time 15 \
                --get \
                --data-urlencode "id=$sensor_id" \
                --data-urlencode "type=$sensor_type" \
                --output "$TEMP_BODY" \
                --write-out $'%{http_code}\t%{time_total}' \
                "$BASE_URL"
        )"; then
            http_status="000"
            total_seconds="0"
            printf '{"error":"curl request failed"}\n' > "$TEMP_BODY"
        else
            IFS=$'\t' read -r http_status total_seconds <<< "$curl_meta"
        fi

        client_time_us="$(python3 - "$total_seconds" <<'PY'
import sys
try:
    print(round(float(sys.argv[1]) * 1_000_000))
except Exception:
    print(0)
PY
)"

        parsed="$(append_result \
            "$round" \
            "$expected_node" \
            "$sensor_id" \
            "$sensor_type" \
            "$http_status" \
            "$client_time_us" \
            "$TEMP_BODY")"

        IFS=$'\t' read -r source server_time error <<< "$parsed"

        if [[ "$http_status" != "200" || -n "$error" ]]; then
            REQUEST_FAILURES=$((REQUEST_FAILURES + 1))
            echo "Sensor $sensor_id/$sensor_type -> FAILED status=$http_status error=$error"
            continue
        fi

        if [[ "$round" == "2" && "$source" != "cache" ]]; then
            ROUND2_NON_CACHE=$((ROUND2_NON_CACHE + 1))
        fi

        echo "Sensor $sensor_id/$sensor_type -> source=$source, server=${server_time}us, client=${client_time_us}us"
    done < "$SENSOR_LIST"
}

info "Testing $SENSOR_COUNT unique sensors through $BASE_URL"
run_round 1
run_round 2

python3 - "$OUTPUT_FILE" "$SUMMARY_FILE" <<'PY'
import csv
import statistics
import sys

input_file, summary_file = sys.argv[1:]
rows = []

with open(input_file, newline="", encoding="utf-8") as handle:
    for row in csv.DictReader(handle):
        if row["http_status"] != "200":
            continue
        try:
            row["server_response_time_us"] = float(row["server_response_time_us"])
            row["client_total_time_us"] = float(row["client_total_time_us"])
        except (TypeError, ValueError):
            continue
        rows.append(row)

summary_rows = []
for round_number in ("1", "2"):
    group = [row for row in rows if row["round"] == round_number]
    server_values = [row["server_response_time_us"] for row in group]
    client_values = [row["client_total_time_us"] for row in group]
    cache_hits = sum(row["source"] == "cache" for row in group)

    if not group:
        summary_rows.append({
            "round": round_number,
            "successful_requests": 0,
            "cache_hits": 0,
            "server_mean_us": "",
            "server_median_us": "",
            "server_min_us": "",
            "server_max_us": "",
            "client_mean_us": "",
        })
        continue

    summary_rows.append({
        "round": round_number,
        "successful_requests": len(group),
        "cache_hits": cache_hits,
        "server_mean_us": round(statistics.mean(server_values), 2),
        "server_median_us": round(statistics.median(server_values), 2),
        "server_min_us": round(min(server_values), 2),
        "server_max_us": round(max(server_values), 2),
        "client_mean_us": round(statistics.mean(client_values), 2),
    })

with open(summary_file, "w", newline="", encoding="utf-8") as handle:
    fieldnames = list(summary_rows[0].keys())
    writer = csv.DictWriter(handle, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(summary_rows)

print("\n============================================================")
print("Benchmark summary")
print("============================================================")
for row in summary_rows:
    print(
        f"Round {row['round']}: requests={row['successful_requests']}, "
        f"cache_hits={row['cache_hits']}, "
        f"server_mean={row['server_mean_us']} us, "
        f"client_mean={row['client_mean_us']} us"
    )

if all(row["server_mean_us"] != "" for row in summary_rows):
    first = float(summary_rows[0]["server_mean_us"])
    second = float(summary_rows[1]["server_mean_us"])
    if first > 0:
        reduction = ((first - second) / first) * 100
        speedup = first / second if second > 0 else float("inf")
        print(f"Server-time reduction: {reduction:.2f}%")
        print(f"Server-time speedup: {speedup:.2f}x")
PY

echo
echo "Detailed results: $OUTPUT_FILE"
echo "Summary results:  $SUMMARY_FILE"

if (( REQUEST_FAILURES > 0 )); then
    die "$REQUEST_FAILURES request(s) failed during the benchmark"
fi

if (( STRICT_CACHE && ROUND2_NON_CACHE > 0 )); then
    die "$ROUND2_NON_CACHE round-2 response(s) were not served by cache"
fi

if (( ROUND2_NON_CACHE > 0 )); then
    warn "$ROUND2_NON_CACHE round-2 response(s) were not served by cache"
fi

echo "Cache benchmark completed successfully."
