#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only

set -eu

root=${DPKG_ROOT:?DPKG_ROOT is required for isolated systemctl simulation}
log=$root/var/log/flex1500d-systemctl.log
mkdir -p "$(dirname "$log")"
printf '%s\n' "$*" >> "$log"

case ${1:-} in
daemon-reload)
    ;;
enable)
    mkdir -p "$root/etc/systemd/system/multi-user.target.wants"
    ln -sf /usr/lib/systemd/system/flex1500d.service \
        "$root/etc/systemd/system/multi-user.target.wants/flex1500d.service"
    ;;
disable)
    rm -f "$root/etc/systemd/system/multi-user.target.wants/flex1500d.service"
    ;;
start|restart|try-restart)
    echo "isolated package test refuses to start services" >&2
    exit 1
    ;;
*)
    echo "unexpected systemctl operation: $*" >&2
    exit 1
    ;;
esac
