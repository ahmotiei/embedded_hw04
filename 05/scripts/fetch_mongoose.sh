#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DESTINATION="$PROJECT_ROOT/api/mongoose"
VERSION="${MONGOOSE_VERSION:-7.21}"
BASE_URL="https://raw.githubusercontent.com/cesanta/mongoose/refs/tags/${VERSION}"

mkdir -p "$DESTINATION"

fetch() {
    local url="$1"
    local output="$2"

    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 --retry-delay 2 "$url" -o "$output"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$output" "$url"
    else
        echo "Error: curl or wget is required to download Mongoose." >&2
        exit 1
    fi
}

fetch "$BASE_URL/mongoose.c" "$DESTINATION/mongoose.c.tmp"
fetch "$BASE_URL/mongoose.h" "$DESTINATION/mongoose.h.tmp"
fetch "https://raw.githubusercontent.com/cesanta/mongoose/refs/tags/${VERSION}/LICENSE" \
      "$DESTINATION/LICENSE.tmp"

mv "$DESTINATION/mongoose.c.tmp" "$DESTINATION/mongoose.c"
mv "$DESTINATION/mongoose.h.tmp" "$DESTINATION/mongoose.h"
mv "$DESTINATION/LICENSE.tmp" "$DESTINATION/LICENSE"

echo "Mongoose ${VERSION} downloaded to $DESTINATION"
