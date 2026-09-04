<!-- SPDX-License-Identifier: GPL-3.0-only -->

# API and SoapySDR authentication notes

This is a design note for the separate authentication and transport-encryption
project. It does not describe functionality currently implemented by API
version 1.

## Separate authentication from TX ownership

Authentication and transmitter ownership solve different problems:

- An API credential establishes which user or client may use protected daemon
  operations.
- A short-lived TX ownership lease establishes which already-authorized client
  currently controls the transmitter.

The TX lease is a safety correlation value, not an authentication credential.
It must not grant access when presented without valid authentication. The
daemon should not expose an active owner's lease value through status or logs.

## HTTP API proposal

The initial practical design is a securely generated API token sent in the
standard HTTP `Authorization` header:

```http
Authorization: Bearer API_TOKEN
```

Protected requests should use constant-time credential comparison and return a
generic authorization failure without revealing whether a token is partly
correct. Tokens must be replaceable and revocable without rebuilding the
daemon.

Bearer tokens require transport encryption. Without TLS, any host capable of
observing the connection can copy and reuse the token. The eventual deployment
design therefore needs TLS either in the daemon or through a deliberately
configured local reverse proxy. Until both authentication and encryption are
implemented, port 15000 remains suitable only for a trusted LAN protected by a
host firewall.

Read-only access and transmit authority may use separate policy scopes. In
particular, possession of a receive-only credential should not imply permission
to acquire TX ownership.

## SoapySDR proposal

SoapySDR provides a device and streaming interface but does not itself define
authentication for this daemon protocol. The `flex1500Support` module can
authenticate because it is the API client and constructs the HTTP requests.

The module may accept the daemon address and a credential reference as device
configuration. Passing the secret directly in a device string such as
`driver=flex1500,token=...` is discouraged because applications may save the
string in profiles or logs, and command-line arguments may be visible to other
local processes.

Preferred credential sources, in approximate implementation order, are:

1. A token file whose permissions restrict it to the local user.
2. A named local credential profile referenced by the Soapy device arguments.
3. An environment variable, with documentation warning about inheritance and
   accidental diagnostic output.
4. Mutual TLS for installations needing stronger per-client identity.

The module should resolve the credential locally and send it in the same
`Authorization` header used by other API clients. It must redact credentials
from discovery results, exceptions, diagnostics, and application-visible device
metadata.

When future Soapy TX support is added, authentication permits the module to ask
for TX; activation of one particular Soapy TX stream then acquires a separate
short-lived ownership lease. Stream deactivation, connection loss, data
timeout, or device destruction must unkey and release that lease. A receive
stream or authenticated control connection alone must never acquire TX.

## Security properties still to decide

Implementation work must define token generation and storage, credential
rotation, receive-versus-transmit authorization policy, TLS certificate setup,
rate limiting, audit logging with secret redaction, and safe migration from the
current unauthenticated trusted-LAN API.
