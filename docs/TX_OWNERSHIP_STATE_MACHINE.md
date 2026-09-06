<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Exclusive transmitter ownership state machine

This engineering design implements persistent station control plus one active
transmitter operation at a time. The station lease covers radio-control
authority and eligibility to request TX; subordinate Tune/general-TX leases
correlate individual operations. It is
not a README project goal and does not by itself enable physical microphone TX.

## Owners and local priority

The defined owners are fixed Tune, physical microphone PTT, and general network
TX used by direct HTTP and SoapySDR clients. A physical PTT press is itself an ownership request;
the operator does not need a separate lease because KB1JDX established the
operating assumption that the person holding the microphone is also controlling
the radio's current frequency and mode.

Physical PTT has local priority. If another owner is transmitting, a PTT press
must first run that owner's complete unkey cleanup. Only after cleanup succeeds
may the physical microphone acquire ownership. Cleanup failure enters the
faulted state and prevents the physical transmission from starting.

An API or SoapySDR client used by the microphone holder remains connected and
continues to be the control plane for frequency, mode, drive, and other station
settings. Pressing physical PTT must not disconnect that client or revoke its
control session. It only preempts an actually keyed competing TX operation,
such as active Tune. Receive/IQ delivery may pause while the hardware is keyed
and resume after unkey, but the network session should remain established where
the client protocol permits it.

While keyed, frequency, mode, drive, microphone-gain, compressor, timeout, and
filter-bandwidth changes are rejected; they do not cause the client to be
disconnected. No change is implicitly staged. The controlling client must
unkey, apply the new setting, and then key again. Receive squelch is host-only
and may still change because it cannot alter radio hardware or transmitted
samples.

## Controlling-client contract

There is exactly one network station owner. The first TX-capable browser or
SoapySDR station to connect acquires it and renews it every five seconds. It
persists across PTT stop and TX stream deactivation until explicit release,
client disconnect/15-second expiry, daemon recovery, or shutdown. Non-owners
are receive-only: they cannot change hardware frequency, mode, gain or TX
settings and cannot request Tune or general TX. They may independently tune and
demodulate within the owner's 48 kHz IQ window without a radio write.

Within that station lease, individual TX operations are represented as follows:

- Physical microphone PTT owns TX from its accepted press edge through its
  release edge. It has no network lease and does not disconnect or revoke the
  API or Soapy session used by the microphone holder to configure the radio.
- Tune uses its existing opaque lease and keepalive watchdog. Lease expiry,
  explicit stop, client abandonment, maximum-key timeout, shutdown, or USB
  failure invokes the same unkey cleanup.
- HTTP voice/data TX acquires an opaque, unpredictable ownership
  token before keying. Every keepalive, audio submission, stop, and release
  request must present that token. Because HTTP requests are not necessarily a
  persistent connection, loss is defined by a short, non-disableable lease
  timeout rather than by one TCP socket closing.
- SoapySDR TX uses an HTTP raw-I/Q lease bound to one activated TX stream
  instance. Deactivating or closing that stream releases the TX operation but
  not station control. Destroying the device releases station control after TX
  cleanup; losing transport or exceeding a stream-data timeout unkeys safely.

An HTTP token or Soapy stream belonging to a non-owner is rejected and cannot
stop, renew, or feed another owner's transmission. Failed acquisition does not
disturb the current owner. Ownership tokens are safety correlation values, not
authentication credentials or security boundaries.

Physical PTT retains the local-priority rule above. It may preempt an active
network or Tune owner only by completing that owner's unkey and cleanup first.
Network owners never preempt physical PTT; they receive a busy response until
the microphone is released and cleanup completes.

## States and interlocks

The states are startup-inhibit, RX, preparing, transmitting, unkeying,
recovering, and faulted. The generic controller starts inhibited. The daemon
arms physical PTT only after initialization or USB recovery has explicitly
established an unkeyed RX state and completed TX preparation. The FLEX-1500
reports PTT changes rather than an initial released state, so waiting for a
release event would incorrectly discard the first legitimate press.

Button release immediately invokes unkey cleanup and relinquishes physical
ownership. Every owner is subject to a non-disableable maximum-key duration:
180 seconds by default, configurable from 30 through 1800 seconds through the
API only while unkeyed. The setting survives automatic USB recovery.
Shutdown invokes the active owner's stop callback. Competing non-physical
owners are rejected rather than allowed to overlap.

Every owner passes the same explicit interlocks before `preparing` can begin:

- the daemon was started with `--initialize-radio-and-enable-transmit`;
- initialization or USB recovery has positively established RX/unkeyed state
  and completed TX preparation;
- the radio has a known, permitted transmit frequency and a matching PA filter;
- the requested modulation mode and audio/IQ source are implemented and valid;
- drive and all source-specific level settings are within enforced limits;
- the controller is neither recovering nor faulted; and
- no other owner holds TX, except for the reviewed physical-PTT preemption
  sequence.

Failure of any interlock rejects the request without keying. A failure after
preparation begins invokes the complete unkey/RX-restoration cleanup and enters
`faulted` if cleanup cannot be confirmed. Recovery must construct a fresh,
unowned controller; a pre-disconnect lease or stream can never resume TX
automatically. Physical PTT is re-armed only after recovery again establishes
the safe prepared state.

The maximum-key timer starts when the controller accepts the owner and is not
extended by audio traffic, HTTP keepalives, or Soapy writes. Its expiry always
attempts unkey and releases ownership. Shorter per-owner liveness watchdogs
(Tune lease, future HTTP lease, and future Soapy stream-data timeout) may stop
TX earlier. Process signals and normal daemon shutdown use the identical owner
stop path before TX preparation is disabled.

## Offline evidence

`tx-control` verifies generic held-PTT startup inhibition, explicit safe-state
arming, release-to-arm fallback, exclusive ownership, competing-owner
rejection, safe physical preemption of Tune,
physical release/unkey, maximum-key timeout, startup failure cleanup, shutdown,
and faulted-state inhibition.

The fixed Tune API now acquires and releases the shared `tune` owner rather than
calling the USB Tune backend directly. Its 15-second lease watchdog, 60-second
hard limit, cleanup, API behavior, and diagnostics remain in place. This makes
the already validated Tune operation the first production user of the shared
controller.

## Physical microphone integration

The continuous microphone DSP component and live USB scheduler are connected
to the shared `physical_mic` owner. Only a changed endpoint-`0x83` microphone
PTT edge can request that owner, and only in explicitly enabled TX mode. The
daemon freezes frequency, sideband, drive, and microphone gain for the complete
transmission and rejects frequency, mode, drive, or microphone-gain changes
while any owner is keyed. Current review findings and remaining validation work
are recorded in [TX_STATE_MACHINE_REVIEW.md](TX_STATE_MACHINE_REVIEW.md).
