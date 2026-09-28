#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only

set -eu

package=${1:-}
if [ -z "$package" ] || [ ! -f "$package" ]; then
    echo "usage: $0 PACKAGE.deb" >&2
    exit 2
fi

for command in dpkg fakeroot; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "required command not found: $command" >&2
        exit 2
    fi
done

package=$(readlink -f "$package")
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

root=$work/root
admin=$root/var/lib/dpkg
mkdir -p "$admin/updates" "$root/var/log"
touch "$admin/status"

run_dpkg()
{
    fakeroot dpkg --root="$root" --admindir="$admin" \
        --log="$root/var/log/dpkg.log" \
        --force-not-root --force-depends "$@"
}

echo "[client-package-test] installation"
run_dpkg --install "$package"
test -x "$root/usr/bin/flex1500-client"
test -f "$root/usr/share/doc/flex1500-client/README.md"
test -f "$root/usr/share/doc/flex1500-client/CLIENT_INTEGRATION_GUIDE.md"
test -f "$root/usr/share/doc/flex1500-client/COMPANION_CLIENT_TESTING.md"
test -f "$root/usr/share/doc/flex1500-client/copyright"
test ! -e "$root/usr/bin/flex1500d"
test ! -e "$root/lib/systemd/system/flex1500d.service"
test ! -e "$root/etc/flex1500d/flex1500d.conf"

echo "[client-package-test] removal"
run_dpkg --remove flex1500-client
test ! -e "$root/usr/bin/flex1500-client"

echo "Thin-client Debian package lifecycle test passed"
