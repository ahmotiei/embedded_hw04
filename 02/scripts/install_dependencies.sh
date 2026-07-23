#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || {
        echo "Error: sudo is required to install packages." >&2
        exit 1
    }
    SUDO=(sudo)
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y \
    build-essential \
    gcc \
    g++ \
    make \
    pkg-config \
    sqlite3 \
    libsqlite3-dev \
    memcached \
    libmemcached-dev \
    curl \
    netcat-openbsd \
    python3

"${SUDO[@]}" systemctl enable --now memcached

echo "All Section 02 dependencies were installed successfully."
