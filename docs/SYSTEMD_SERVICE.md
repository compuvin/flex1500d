<!-- SPDX-License-Identifier: GPL-3.0-only -->

# systemd service

The Debian package installs `/usr/lib/systemd/system/flex1500d.service` and
enables it for subsequent boots. Package installation deliberately does not
start the service: this gives the operator time to connect the radio, apply the
udev rule, review network exposure, and validate the configuration first.

The service runs:

```text
/usr/bin/flex1500d --config /etc/flex1500d/flex1500d.conf
```

The packaged configuration defaults to `mode=rx-tuning`. It initializes the
radio for receive and permits receive-frequency tuning, while general transmit,
Tune, the browser test page, and `rtl_tcp` remain disabled. The service command
does not override that configuration or enable transmit.

## First start

After installing the package, unplug and reconnect the FLEX-1500 so the udev
rule applies. Validate the installed configuration without opening the radio:

```sh
sudo /usr/bin/flex1500d --config /etc/flex1500d/flex1500d.conf --check-config
```

Then start the already-enabled service when ready:

```sh
sudo systemctl start flex1500d
sudo systemctl status flex1500d
```

If it is not started manually, systemd starts it at the next normal boot. Only
one process may own the FLEX-1500, so stop foreground instances and other radio
software before starting the service.

## Operation and logs

Common commands are:

```sh
sudo systemctl stop flex1500d
sudo systemctl restart flex1500d
sudo systemctl enable flex1500d
sudo systemctl disable flex1500d
journalctl -u flex1500d
journalctl -u flex1500d -f
```

Stopping the service sends `SIGTERM`. The daemon handles it through its normal
shutdown path, stops network activity, unkeys any active transmitter, restores
receive state where possible, disables the PA path, and closes USB. systemd
allows up to 15 seconds for that cleanup before forcefully stopping a process
that has failed to exit.

The unit uses `Restart=on-failure` with a five-second delay. A clean operator
stop or normal exit is not restarted; an unexpected failure is. The daemon's
own bounded USB reconnection handling runs before a fatal device failure causes
systemd to restart the process.

## Configuration changes

Edit `/etc/flex1500d/flex1500d.conf`, validate it, and then restart:

```sh
sudo /usr/bin/flex1500d --config /etc/flex1500d/flex1500d.conf --check-config
sudo systemctl restart flex1500d
```

The API is unauthenticated and unencrypted. Keep it on a trusted LAN and use a
firewall to limit access. Selecting `mode=transmit` is a separate, deliberate
operator decision with the safety implications documented in the
[transmit operator guide](TX_OPERATOR_GUIDE.md). Installing or enabling the
service does not change the configured radio mode. The packaged configuration
defaults to receive-only; if an operator deliberately changes it to
`mode=transmit`, subsequent service starts will use transmit-enabled mode.

## Package lifecycle

The first Debian package installation reloads systemd and enables the unit, but
does not start it. An upgrade reloads systemd without changing whether the
operator enabled or disabled the service, and preserves a locally modified
configuration through normal Debian conffile handling. Removing the package
stops and disables the unit before its files are removed. A source-tree build
installs the unit with `cmake --install`, but automatic enablement is specific
to the Debian package's maintainer scripts.

## Isolated package validation

The repository includes an automated lifecycle test that uses `dpkg` under
`fakeroot` with a temporary alternate root. A simulated `systemctl` changes
only the temporary root and refuses every attempt to start a service. The test
verifies:

- initial installation requests boot enablement without starting the daemon;
- the binary, receive-only configuration, service unit, and enablement link are
  installed in their expected locations;
- an upgrade preserves a locally modified conffile and does not re-enable a
  service that the operator disabled;
- package removal requests a stop/disable operation and removes program files
  while normal Debian conffile retention keeps the local configuration; and
- package purge removes the retained configuration.

After generating a package, run the test without root privileges:

```sh
tests/debian_package_lifecycle.sh ./flex1500d_0.2.1_amd64.deb
```

This optional developer test requires the standard Debian package tools and
`fakeroot`; neither is a runtime requirement for an installed `flex1500d`
package. GitHub Actions installs `fakeroot` only in its temporary build runner.

The test does not open USB, access the FLEX-1500, install anything on the host,
or communicate with the host systemd instance. GitHub Actions runs it for each
native build architecture.
