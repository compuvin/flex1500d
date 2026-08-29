# GitHub publishing checklist

This checklist tracks the remaining work before the first public experimental
release of `flex1500d`.

## Project presentation

- [x] Restructure the main README for the initial public experimental release.
- [x] Add a prominent experimental-status and use-with-care disclaimer.
- [x] State that this project is unaffiliated with, unendorsed by, and
  unsupported by FlexRadio Systems.
- [x] Clearly distinguish the receive-only daemon/API from isolated,
  default-disabled transmit research sources.
- [x] Review and document prerequisites, build/test, udev setup and rollback,
  offline checks, foreground daemon operation, API routes, browser testing,
  limitations, and project documentation from a new user's perspective.

## Licensing and repository setup

- [x] Select GNU GPL version 3 as the project license.
- [x] Add the complete GPLv3 license text as `LICENSE`.
- [x] Initialize and inspect the real Git repository with `main` as the initial
  branch; no commit or remote has been created.
- [x] Review `.gitignore` before the first commit and add local-tool,
  editor/OS, swap-file, and compilation-database exclusions.
- [x] Add `SPDX-License-Identifier: GPL-3.0-only` to C sources, headers, tests,
  Python tools, and `CMakeLists.txt`.

## Private and machine-specific material

- [x] Review the saved origin chat for obvious credentials and private machine
  details; none were found.
- [x] Preserve the origin chat as `research/Chat.txt` for project history.
- [x] Generalize the absolute local installation path in `RULE_CHANGES.txt`.
- [x] Remove historical USB bus/device numbers from `RULE_CHANGES.txt` because
  Linux assigns them dynamically and they add no reproducible value.
- [x] Perform a final credential, private-key, identity, and local-path scan;
  no secrets or unintended identity/path disclosures were found. KB1JDX is an
  intentional project and safety attribution.

## Captures and repository size

- [x] Ignore generated builds, Python cache files, raw IQ captures, and WAV
  outputs.
- [x] Keep the roughly 42 MB of source USB packet captures local and exclude
  `/pcaps/` from Git; the protocol-analysis documents remain publishable.
- [x] Packet-capture metadata review is unnecessary for the initial release
  because no capture files will be uploaded.
- [x] Confirm that no proprietary Windows binaries, drivers, libraries,
  archives, firmware images, object files, or other binary artifacts are
  included in the prospective commit.

## Safety and transmit isolation

- [x] Keep the daemon and network API receive-only with TX/PTT routes absent.
- [x] Test that representative TX/PTT API requests remain unavailable.
- [x] Put all standalone TX-owned research probes behind the opt-in
  `FLEX1500_BUILD_TX_RESEARCH=ON` CMake option, disabled by default.
- [x] Explain that probe arming strings are safety interlocks, not
  authentication, access control, or security boundaries; require a suitable
  dummy load and a separately reviewed and authorized fixed test plan.
- [x] Re-audit the default build, tests, daemon call paths, API routes, and
  documented commands: no TX research executable is built by default, the
  daemon has no TX/sample-OUT/PTT call path, and representative TX/PTT routes
  remain 404.

## Quality and release automation

- [x] Clean default build passes 17/17 tests with TX research targets absent;
  the explicit research build passes all 22 offline/interlock tests.
- [x] Replace test-side `assert(...)` with an always-active `CHECK(...)` so
  Release builds execute setup and report failures correctly under `NDEBUG`.
- [x] Add a minimal GitHub Actions workflow for Debug and Release builds/tests
  with TX research explicitly disabled and warnings treated as errors.
- [x] Build and test a clean snapshot of exactly the prospective Git contents
  using the documented dependencies; ignored captures/artifacts were absent.
- [x] Pass compiler-warning, whitespace/formatting, final-newline, SPDX,
  workflow-YAML, ignored-artifact, publishable-binary, and Markdown-link audits.
- [x] Choose `v0.1.0` as the initial version/tag and write release notes.

## Final publication review

- [x] Verify the Git commit identity: retain the configured author name and set
  the repository-local email to `compuvin@users.noreply.github.com`.
- [x] Review copyright attribution and third-party provenance; add `NOTICE.md`
  identifying KB1JDX, the pinned public PowerSDR research source, system-only
  dependencies, absent proprietary binaries, and product-name ownership.
- [x] Approve the GitHub description: “Experimental receive-only Linux daemon
  and network SDR API for the FlexRadio FLEX-1500.”
- [x] Approve GitHub topics: `flexradio`, `flex-1500`, `sdr`, `network-sdr`,
  `ham-radio`, `amateur-radio`, `linux`, `libusb`, `reverse-engineering`, and
  `software-defined-radio`.
- [x] Review the complete staged first commit: 73 text files, no forbidden
  binaries/captures/build artifacts, no secrets/private paths, no unstaged or
  untracked publishable files, and a clean staged whitespace check.
- [ ] Create the approved `v0.1.0` tag after the initial commit exists.
- [ ] Create the GitHub repository and push only after KB1JDX approves the
  final staged contents.
