# Windows PowerSDR USB capture checklist

The public source has reduced this from open-ended reverse engineering to a
short validation capture. A Windows boot is useful but not yet mandatory.

## Preparation

1. Install Wireshark using its official Windows installer and include the
   optional USBPcap component.
2. Confirm that the known-working PowerSDR installation still recognizes the
   FLEX-1500.
3. Disconnect or disable anything that can assert PTT: microphone PTT, foot
   switch, keyer, CAT applications, VOX, TUN, and MOX.
4. Disable or disconnect external amplifiers and transmit-control lines.
5. Do not accept a firmware-update prompt. Stop and record what appeared instead.
6. Create a folder that can later be copied into this project's `captures/`
   directory.

USBPcap is the Windows USB capture mechanism recommended by Wireshark. Capturing
the full USB root hub is intentional; filtering can be done safely afterward.

## Capture A: discovery and idle initialization

Suggested filename: `flex1500-a-discovery-start-stop.pcapng`

1. Leave PowerSDR closed.
2. Connect and power the FLEX-1500 normally.
3. Start Wireshark as needed for USBPcap access.
4. Select the USBPcap interface/root hub containing the FLEX-1500. If uncertain,
   observe which USBPcap interface changes when the radio USB cable is briefly
   disconnected and reconnected before PowerSDR is opened.
5. Start capture without a display or capture filter.
6. Wait two seconds.
7. Launch PowerSDR and let it recognize the radio.
8. Do not press Start yet; wait five seconds.
9. Press PowerSDR's Start button once.
10. Leave it in receive for ten seconds without changing controls.
11. Press Stop once and wait three seconds.
12. Close PowerSDR, stop capture, and save the file.

## Capture B: clean Start/Stop boundary

Suggested filename: `flex1500-b-start-stop-only.pcapng`

1. Launch PowerSDR and allow it to recognize the radio, but leave it stopped.
2. Start USBPcap capture on the already identified root hub.
3. Wait two seconds, press Start, receive for five seconds, press Stop, and wait
   two seconds.
4. Stop and save the capture.

Capture B makes stream startup ordering easier to analyze because it excludes
most application discovery traffic.

## Do not capture yet

Do not key PTT, use TUN, enable VOX, change firmware, run calibration or
production tests, or perform EEPROM operations. Frequency and individual-setting
captures can be planned later after the initialization capture is understood.

## Files to bring back to Linux

Copy the `.pcapng` files into `captures/`. If convenient, also copy these files
from the working PowerSDR directory into `research/windows-binaries/`:

- `PowerSDR.exe`
- `Flex1500USB.dll`
- Any Jungo or Flex1500-specific DLLs alongside it

Do not copy personal database/configuration files unless specifically needed.

## Validation on Linux

After installing the Ubuntu `tshark` package, run:

```sh
python3 tools/analyze_usb_capture.py captures/FILENAME.pcapng --list-devices
python3 tools/analyze_usb_capture.py captures/FILENAME.pcapng --device-address N
```

The first command helps identify the FLEX-1500 device address. The second
summarizes endpoints and decodes 20-byte command requests on endpoint `0x04`.
