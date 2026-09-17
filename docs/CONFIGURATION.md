# flex1500d configuration

`flex1500d` looks for `/etc/flex1500d/flex1500d.conf` when it is run without
one of the legacy mode commands. If the default file is absent, the built-in
fallback starts the same RX-only, tuning-enabled daemon on port 15000. An
explicitly named file must exist and be valid:

```sh
flex1500d --config /path/to/station.conf
```

Settings are applied in this order:

1. conservative built-in defaults;
2. the configuration file; and
3. command-line overrides.

The packaged file is installed automatically by the `.deb`. Both it and the
built-in fallback start the receive daemon with RX tuning enabled on TCP port
15000. Transmit and the browser test page remain disabled.

```ini
[daemon]
enabled=true

[radio]
mode=rx-tuning

[http]
bind=0.0.0.0
port=15000
test_page=false

[rtl_tcp]
enabled=false
bind=0.0.0.0
port=1234
```

Valid radio modes are:

- `offline`: serve status/API responses without opening the radio;
- `receive`: initialize the radio and stream IQ without allowing tuning;
- `rx-tuning`: initialize RX and allow frequency/filter changes; and
- `transmit`: explicitly prepare the PA path and enable the experimental TX
  facilities.

`transmit` has the same safety implications as the legacy
`--initialize-radio-and-enable-transmit` command. Never select it accidentally,
and follow the TX operator and hardware-safety documentation.

Validate a file without opening the radio or starting a listener:

```sh
flex1500d --config /path/to/station.conf --check-config
```

Print the merged file and command-line configuration without starting:

```sh
flex1500d --print-effective-config
```

Temporary overrides include:

```sh
flex1500d --http-port 16000 --enable-test-page
flex1500d --radio-mode receive --http-bind 127.0.0.1
flex1500d --no-daemon --print-effective-config
flex1500d --enable-rtl-tcp --rtl-tcp-port 1234
```

Boolean settings have matching enable/disable overrides so a value enabled in
the file can be disabled for one invocation. Unknown sections, unknown keys,
duplicates, malformed values, and invalid IPv4 bind addresses are errors.

The API remains unauthenticated. Binding to `0.0.0.0` makes it reachable from
other computers; use a firewall to restrict it to trusted LAN hosts. Runtime
state such as frequency, ownership leases, PTT state, and counters is not
stored in this file.

The experimental receive-only [`rtl_tcp` compatibility listener](RTL_TCP.md)
is independently controlled by its section and remains disabled by default.

## systemd service

The Debian package installs `flex1500d.service`, enables it for subsequent
boots, and leaves it stopped during installation. The service reads this
configuration file and does not add a radio-mode override, so the packaged
`rx-tuning` receive-only default remains authoritative. Review and validate any
configuration change before restarting the service, especially before setting
`mode=transmit`.

See the [systemd service guide](SYSTEMD_SERVICE.md) for service commands, logs,
failure recovery, and package behavior.

The earlier exact mode commands remain supported for scripts and testing.
