#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for node in master slave1 slave2; do
    "$SCRIPT_DIR/init_node_database.sh" "$node"
    echo
 done

echo "All databases were initialized successfully."
