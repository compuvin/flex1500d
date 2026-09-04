# FLEX-1500 transmit telemetry research

This note records whether the FLEX-1500 can report forward power, reflected
power, SWR, or PA temperature to the host. The conclusion is that none of these
quantities is available as measured radio telemetry through the protocol paths
used by PowerSDR or observed in the project's captures.

## Results

| Quantity | Result | Consequence for `flex1500d` |
| --- | --- | --- |
| Forward power | No measured forward-power telemetry was found. | Drive percentage or calibrated expected output may be reported only as an estimate, never as measured watts. |
| Reflected power | The FLEX-1500 has no RF power/SWR bridge from which to obtain it. | Do not expose a fabricated reflected-power value. An external directional wattmeter would be required. |
| SWR | Cannot be derived without measured forward and reflected power. | The daemon cannot implement internal high-SWR detection or foldback from radio telemetry. Use an external SWR meter or protective device. |
| PA temperature | No PA temperature sensor or host-readable temperature path was found. | The daemon cannot implement temperature protection from radio telemetry. Conservative transmit time limits remain important. |

An unknown or unavailable reading must remain explicitly unavailable. Zero is
not a safe substitute: zero reflected power could be misinterpreted as a
perfect load, and zero temperature or power could be mistaken for a valid
measurement.

## Evidence

The official FLEX-1500 service manual describes the PA-board SPI bus as
outbound control only. Its PA schematic, theory of operation, and parts list do
not identify a directional coupler, forward/reflected detector, or PA
temperature sensor. The production PA calibration procedure instead requires
an external PowerMaster RF wattmeter. See the
[FLEX-1500 Service Manual](https://edge.flexradio.com/www/uploads/20200818185036/FLEX-1500-Service-Manual.pdf),
especially pages 7 and 11-13.

The examined PowerSDR source defines the generic HID opcode
`USB_OP_READ_PA_ADC` (1274), but does not provide or call a FLEX-1500 wrapper for
it. `PollFWCPAPWR()` accepts only the FLEX-5000 and FLEX-3000 and reads their
model-specific PA ADC channels. The startup model switch explicitly does
nothing for the FLEX-1500 because there is "no bridge to read." The SWR scanner
also rejects models without an SWR circuit. These observations were made in
[PowerSDR-KE9NS v2.8.0](https://github.com/ke9ns/PowerSDR-KE9NS-v2.8.0),
revision `12cdc2bb3b2a777cf4dfb5b5a0a6a78242eef275`, in
`Console/hid/usbhid.cs`, `Console/console.cs`, and `Console/scan.cs`.

All eight USB captures currently retained for local research were checked,
including the complete startup/transmit/unkey/shutdown sequence and the Tune
capture. None contains opcode 1274 or another observed periodic query that
could carry these measurements.

The owner-facing TX meter labels in PowerSDR do not establish that the
FLEX-1500 contains the measurement hardware used by other FLEX models. The
model-specific source and service documentation take precedence.

## Implementation boundary

No live radio query was sent during this research. Sending opcode 1274 merely
because it exists in a shared enumeration would be an experimental USB write,
not a read-only host operation, and requires KB1JDX's separate approval. There
is currently no evidence that such a probe would return useful FLEX-1500 data.

Future telemetry support should use an explicitly identified external sensor,
meter, or station controller. If an estimated-power field is added before
then, its name and API documentation must clearly say `estimated`, and it must
not drive SWR or thermal safety decisions.
