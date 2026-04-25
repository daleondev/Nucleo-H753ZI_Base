#!/usr/bin/env bash
# Provision the tap0 device used by the Linux simulation and bridge OPC UA
# port 4840 onto 0.0.0.0 so the server is reachable from outside the
# devcontainer.
#
# Idempotent: re-running just refreshes the addressing and re-launches socat.

set -euo pipefail

TAP_DEV="${TAP_DEV:-tap0}"
TAP_HOST_IP="${TAP_HOST_IP:-10.10.10.1/24}"
APP_IP="${APP_IP:-10.10.10.2}"
OPCUA_PORT="${OPCUA_PORT:-4840}"

# Ensure the tun module exposes /dev/net/tun (already mounted into the
# container by devcontainer.json).
if [[ ! -e /dev/net/tun ]]; then
    echo "setup_tap: /dev/net/tun missing - is the host kernel's tun module loaded?" >&2
    exit 1
fi

# Create or refresh tap device.
if ! ip link show "${TAP_DEV}" >/dev/null 2>&1; then
    ip tuntap add dev "${TAP_DEV}" mode tap user "${USER:-vscode}"
fi
ip link set dev "${TAP_DEV}" up
ip addr flush dev "${TAP_DEV}" || true
ip addr add "${TAP_HOST_IP}" dev "${TAP_DEV}"

# Allow forwarding so socat can bridge external traffic into the tap.
sysctl -wq net.ipv4.ip_forward=1 || true

# Restart socat so a fresh listener picks up the latest addressing.
pkill -f "socat.*:${OPCUA_PORT}" >/dev/null 2>&1 || true
nohup socat \
    "TCP-LISTEN:${OPCUA_PORT},reuseaddr,fork,bind=0.0.0.0" \
    "TCP:${APP_IP}:${OPCUA_PORT}" \
    >/tmp/socat-opcua.log 2>&1 &
disown || true

echo "setup_tap: ${TAP_DEV} ready (host ${TAP_HOST_IP}, app ${APP_IP})"
echo "setup_tap: socat bridging 0.0.0.0:${OPCUA_PORT} -> ${APP_IP}:${OPCUA_PORT}"
