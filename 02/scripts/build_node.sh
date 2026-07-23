#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

NODE="${1:-}"
[[ -n "$NODE" ]] || die "usage: $0 <master|slave1|slave2> [--no-clean]"
validate_node "$NODE"

CLEAN_BUILD=1
if [[ "${2:-}" == "--no-clean" ]]; then
    CLEAN_BUILD=0
elif [[ -n "${2:-}" ]]; then
    die "unknown option: ${2}"
fi

NODE_DIR="$(node_directory "$NODE")"
[[ -f "$NODE_DIR/Makefile" ]] || die "Makefile not found: $NODE_DIR/Makefile"

require_command make
require_command g++
require_command gcc

if (( CLEAN_BUILD )); then
    info "Cleaning previous $NODE build"
    make -C "$NODE_DIR" clean
fi

info "Building $NODE"
make -C "$NODE_DIR"

BINARY="$NODE_DIR/$(node_binary_name "$NODE")"
[[ -x "$BINARY" ]] || die "build completed but executable was not created: $BINARY"

info "$NODE was built successfully"
