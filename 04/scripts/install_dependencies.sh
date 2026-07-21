#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
    build-essential \
    libsqlite3-dev \
    snmp \
    snmpd

echo "SNMP section dependencies installed."
