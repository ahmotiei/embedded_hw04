#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DATA_DIR="${1:-$PROJECT_ROOT/data}"

required_files=(
    "$DATA_DIR/master_sensors.csv"
    "$DATA_DIR/slave1_sensors.csv"
    "$DATA_DIR/slave2_sensors.csv"
)

for file in "${required_files[@]}"; do
    if [[ ! -f "$file" ]]; then
        echo "Error: required CSV file not found: $file"
        echo "Copy the instructor CSV files into: $PROJECT_ROOT/data"
        exit 1
    fi
done

"$PROJECT_ROOT/master/master_init_db.sh" \
    "$PROJECT_ROOT/master/master.db" \
    "$DATA_DIR/master_sensors.csv"

echo

"$PROJECT_ROOT/slave1/slave1_init_db.sh" \
    "$PROJECT_ROOT/slave1/slave1.db" \
    "$DATA_DIR/slave1_sensors.csv"

echo

"$PROJECT_ROOT/slave2/slave2_init_db.sh" \
    "$PROJECT_ROOT/slave2/slave2.db" \
    "$DATA_DIR/slave2_sensors.csv"

echo
echo "All databases were initialized successfully."
