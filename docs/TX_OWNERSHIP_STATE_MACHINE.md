<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Exclusive transmitter ownership state machine

This engineering design implements one keyed-transmitter owner at a time. TX
ownership is separate from client connection and radio-control authority. It is
not a README project goal and does not by itself enable physical microphone TX.

## Owners and local priority

The defined owners are fixed Tune, physical microphone PTT, future general HTTP
TX, and future SoapySDR TX. A physical PTT press is itself an ownership request;
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

While keyed, unsafe frequency, mode, drive, and filter changes are rejected or
staged according to the eventual command policy; they do not cause the client
to be disconnected.

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
