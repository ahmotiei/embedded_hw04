#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"
PID_FILE="$LOG_DIR/nodes.pid"

mkdir -p "$LOG_DIR"

required_commands=(make g++ gcc sqlite3 curl stdbuf)

for command_name in "${required_commands[@]}"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Error: required command is not installed: $command_name"
        exit 1
    fi
done

cleanup() {
    local exit_code=$?

    if [[ -f "$PID_FILE" ]]; then
        while read -r pid; do
            if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
                kill "$pid" 2>/dev/null || true
            fi
        done < "$PID_FILE"

        rm -f "$PID_FILE"
    fi

    wait 2>/dev/null || true
    exit "$exit_code"
}

trap cleanup EXIT INT TERM

echo "Step 1/3: Initializing databases from CSV files..."
"$PROJECT_ROOT/scripts/init_all_databases.sh"

echo
echo "Step 2/3: Compiling Master and Slave programs with Make..."

for node in master slave1 slave2; do
    echo
    echo "Building $node..."
    make -C "$PROJECT_ROOT/$node" clean
    make -C "$PROJECT_ROOT/$node"
done

echo
echo "Step 3/3: Starting distributed database nodes..."

: > "$LOG_DIR/master.log"
: > "$LOG_DIR/slave1.log"
: > "$LOG_DIR/slave2.log"
: > "$PID_FILE"

(
    cd "$PROJECT_ROOT/slave1"
    exec stdbuf -oL -eL ./slave1 config
) > "$LOG_DIR/slave1.log" 2>&1 &
SLAVE1_PID=$!
echo "$SLAVE1_PID" >> "$PID_FILE"

(
    cd "$PROJECT_ROOT/slave2"
    exec stdbuf -oL -eL ./slave2 config
) > "$LOG_DIR/slave2.log" 2>&1 &
SLAVE2_PID=$!
echo "$SLAVE2_PID" >> "$PID_FILE"

sleep 1

(
    cd "$PROJECT_ROOT/master"
    exec stdbuf -oL -eL ./master config
) > "$LOG_DIR/master.log" 2>&1 &
MASTER_PID=$!
echo "$MASTER_PID" >> "$PID_FILE"

sleep 1

for entry in \
    "Master:$MASTER_PID:$LOG_DIR/master.log" \
    "Slave1:$SLAVE1_PID:$LOG_DIR/slave1.log" \
    "Slave2:$SLAVE2_PID:$LOG_DIR/slave2.log"; do

    IFS=':' read -r name pid log_file <<< "$entry"

    if ! kill -0 "$pid" 2>/dev/null; then
        echo "Error: $name failed to start."
        echo "Log output:"
        cat "$log_file"
        exit 1
    fi
done

echo
echo "All nodes started successfully."
echo "Master PID: $MASTER_PID"
echo "Slave1 PID: $SLAVE1_PID"
echo "Slave2 PID: $SLAVE2_PID"
echo
echo "Logs:"
echo "  $LOG_DIR/master.log"
echo "  $LOG_DIR/slave1.log"
echo "  $LOG_DIR/slave2.log"
echo
echo "Run the test script in another terminal:"
echo "  cd $PROJECT_ROOT"
echo "  ./scripts/test_requests.sh"
echo
echo "Press Ctrl+C here to stop all nodes."

wait
