#!/usr/bin/env bash
# Flash the macropad firmware to an RP2040 board already sitting in BOOTSEL
# mass-storage mode. Terminal-only: no drag-and-drop required.
#
# `qmk flash` (via util/uf2conv.py) only looks for the RPI-RP2 volume under
# /mnt, /media, /media/$USER, or /run/media/$USER — so if it isn't already
# mounted there, this script mounts it with udisksctl first.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

KB="nacitar/macropad"
KM="default"

# Ensure the keyboard symlink into the QMK submodule exists.
make link

echo "Looking for an RP2040 in BOOTSEL mode (label RPI-RP2)..."

dev="$(lsblk -rno PATH,LABEL | awk '$2 == "RPI-RP2" {print $1; exit}')"

if [ -z "${dev:-}" ]; then
    echo "No RPI-RP2 device found." >&2
    echo "Hold BOOTSEL, plug in the board (or press RESET while held), then re-run this script." >&2
    exit 1
fi

echo "Found $dev"

mountpoint="$(lsblk -rno PATH,MOUNTPOINT | awk -v d="$dev" '$1 == d {print $2}')"

if [ -z "${mountpoint:-}" ]; then
    echo "Mounting $dev..."
    udisksctl mount -b "$dev"
else
    echo "Already mounted at $mountpoint"
fi

echo "Flashing (AUTOMATION_MODE=${AUTOMATION_MODE_VALUE:-1}, MOUSEKEY_ENABLE=${MOUSEKEY_ENABLE_VALUE:-no})..."
uv run qmk flash -kb "$KB" -km "$KM" \
    -e "AUTOMATION_MODE=${AUTOMATION_MODE_VALUE:-1}" \
    -e "MOUSEKEY_ENABLE=${MOUSEKEY_ENABLE_VALUE:-no}"
