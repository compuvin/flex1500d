#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only

set -eu

package=${1:-}
if [ -z "$package" ] || [ ! -f "$package" ]; then
    echo "usage: $0 PACKAGE.deb" >&2
    exit 2
fi

for command in dpkg dpkg-deb fakeroot; do
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
fake_bin=$work/bin
systemctl_log=$root/var/log/flex1500d-systemctl.log
enabled_link=$root/etc/systemd/system/multi-user.target.wants/flex1500d.service

mkdir -p "$admin/updates" "$fake_bin" "$root/var/log"
touch "$admin/status"

cp "$(dirname "$0")/fake_systemctl.sh" "$fake_bin/systemctl"
chmod 0755 "$fake_bin/systemctl"

run_dpkg()
{
    PATH="$fake_bin:/usr/sbin:/usr/bin:/sbin:/bin" \
        fakeroot dpkg --root="$root" --admindir="$admin" \
        --log="$root/var/log/dpkg.log" \
        --force-not-root --force-script-chrootless --force-depends "$@"
}

assert_file()
{
    if [ ! -f "$root$1" ]; then
        echo "expected installed file is missing: $1" >&2
        exit 1
    fi
}

assert_log_contains()
{
    if ! grep -Fx "$1" "$systemctl_log" >/dev/null; then
        echo "systemctl log is missing: $1" >&2
        sed -n '1,120p' "$systemctl_log" >&2
        exit 1
    fi
}

assert_no_start()
{
    if grep -E '^(start|restart|try-restart) ' "$systemctl_log" >/dev/null; then
        echo "package unexpectedly requested an immediate service start" >&2
        sed -n '1,120p' "$systemctl_log" >&2
        exit 1
    fi
}

echo "[package-test] initial installation"
run_dpkg --install "$package"
assert_file /usr/bin/flex1500d
assert_file /usr/lib/systemd/system/flex1500d.service
assert_file /etc/flex1500d/flex1500d.conf
grep -Fx 'mode=rx-tuning' "$root/etc/flex1500d/flex1500d.conf" >/dev/null
test -L "$enabled_link"
assert_log_contains 'daemon-reload'
assert_log_contains 'enable flex1500d.service'
assert_no_start

echo "[package-test] locally modified configuration and disabled service"
sed -i 's/^mode=rx-tuning$/mode=offline/' \
    "$root/etc/flex1500d/flex1500d.conf"
rm "$enabled_link"

upgrade_tree=$work/upgrade-tree
upgrade_package=$work/flex1500d-upgrade.deb
dpkg-deb --raw-extract "$package" "$upgrade_tree"
old_version=$(dpkg-deb --field "$package" Version)
new_version="${old_version}+lifecycle1"
sed -i "s/^Version: .*/Version: $new_version/" \
    "$upgrade_tree/DEBIAN/control"
printf '\n# package lifecycle upgrade marker\n' >> \
    "$upgrade_tree/etc/flex1500d/flex1500d.conf"
rm -f "$upgrade_tree/DEBIAN/md5sums"
fakeroot dpkg-deb --build "$upgrade_tree" "$upgrade_package" >/dev/null

echo "[package-test] upgrade with conffile preservation"
: > "$systemctl_log"
run_dpkg --force-confold --install "$upgrade_package"
grep -Fx 'mode=offline' "$root/etc/flex1500d/flex1500d.conf" >/dev/null
grep -Fx '# package lifecycle upgrade marker' \
    "$root/etc/flex1500d/flex1500d.conf.dpkg-dist" >/dev/null
test ! -e "$enabled_link"
assert_log_contains 'daemon-reload'
if grep -E '^(enable|disable|start|restart|try-restart) ' \
        "$systemctl_log" >/dev/null; then
    echo "upgrade unexpectedly changed service enablement or runtime state" >&2
    sed -n '1,120p' "$systemctl_log" >&2
    exit 1
fi

echo "[package-test] removal"
: > "$systemctl_log"
run_dpkg --remove flex1500d
test ! -e "$root/usr/bin/flex1500d"
test ! -e "$root/usr/lib/systemd/system/flex1500d.service"
test -f "$root/etc/flex1500d/flex1500d.conf"
assert_log_contains 'disable --now flex1500d.service'
assert_log_contains 'daemon-reload'
assert_no_start

echo "[package-test] purge"
: > "$systemctl_log"
run_dpkg --purge flex1500d
test ! -e "$root/etc/flex1500d/flex1500d.conf"
assert_log_contains 'daemon-reload'
assert_no_start

echo "Debian package lifecycle test passed"
