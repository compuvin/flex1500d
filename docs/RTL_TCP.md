# Experimental rtl_tcp compatibility

`flex1500d` includes an experimental, receive-only `rtl_tcp` listener for
clients that cannot use the native API or SoapySDR adapter. It is disabled by
default while interoperability is tested.

Enable it in the configuration file:

```ini
[rtl_tcp]
enabled=true
bind=0.0.0.0
port=1234
```

Or enable it for one configured-daemon invocation:

```sh
./build/flex1500d --enable-rtl-tcp
```

The listener sends the standard 12-byte `RTL0` header and unsigned 8-bit
interleaved I/Q at the FLEX-1500's fixed 48 ksample/s rate. It reports an
unknown tuner with no indexed gain table because the radio is not an RTL-SDR.

Current experimental limitations:

- one `rtl_tcp` client at a time;
- receive only, with no TX or PTT commands;
- native RF bandwidth fixed at 48 kHz; client rates from 48 ksample/s through
  3.072 MS/s, including 250 and 960 ksample/s, are produced by a stateful
  filtered compatibility resampler without gaining wider RF coverage;
- frequency commands may physically retune the receiver only when RX tuning is
  enabled and no API/Soapy station-control owner is active;
- frequency requests are rejected in the daemon log while another station
  owner is active because classic `rtl_tcp` has no suitable ownership token or
  command-error response;
- gain, correction, AGC, direct-sampling, bias-tee, and test-mode commands are
  currently ignored; and
- slow clients are disconnected rather than being allowed to stall USB RX.

The stream converts the daemon's complex floating-point IQ to the protocol's
unsigned 8-bit representation and corrects the FLEX-1500 wire Q orientation to
the conventional orientation expected by SDR clients. It therefore has less
dynamic range than the native API. The API remains available on its own port
while `rtl_tcp` is enabled.

## Displayed bandwidth warning

Compatibility resampling changes the byte rate, not the radio's coverage. For
example, a client requesting 960 ksample/s may draw a 960 kHz-wide spectrum,
but only the center 48 kHz (approximately +/-24 kHz) comes from the radio.
The resampling filter suppresses images outside that center window, but it
cannot create spectrum the radio did not capture; signals displayed beyond
the native window are not trustworthy.

Some clients tune locally anywhere inside the bandwidth they believe they
received and send no new `SET_FREQUENCY` command. In that case selecting a
signal more than 24 kHz from center does not retune the FLEX-1500. The operator
must use the client's recenter or hardware-tune action—VibeSDR testing found
that switching away from a band and back forces such a retune—and confirm the
new physical center in the daemon log:

```text
[rtl_tcp] tuned RX to 7330000 Hz; RX filter 6 selected
```

This limitation is inherent when a client refuses the native 48 ksample/s rate
and classic `rtl_tcp` provides no separate hardware-center and local-VFO
controls.

This listener has no authentication or encryption. Bind it only to a trusted
network interface or restrict its port with a firewall. Do not expose it to the
public internet.
