# ArduRower S3 – Connect IQ data field

Data field for the Garmin fenix 7X (product id `fenix7x`) that connects to the ArduRower ESP32
over BLE (FTMS Rower Data) and shows and records the rower values.

## What it does
- Connects automatically: scans for FTMS service 0x1826 / name `WR S4BL3` (also parsed from the raw
  advertisement), pairs, enables notifications on Rower Data (0x2AD1) and Battery (0x2A19).
  Reconnects on its own; if that fails for 20 s it scans again.
- Full-screen field: rowing time, meters, pace /500 m, strokes/min, watts, rower battery.
- Half-screen fields (2/3-field layouts): 2x2 grid. Top half: time, meters, pace, spm;
  bottom half: pace, spm, watts, meters.
- Small fields: one value, chosen by position: top = rowing time, middle = pace, bottom = stroke rate;
  quadrants: top-left time, top-right meters, bottom-left spm, bottom-right watts.
  The label line shows the connection status while not receiving.
- FIT recording (developer fields): stroke rate, power and pace as charts; average stroke rate,
  average power, average pace, distance and strokes in the activity summary on Garmin Connect.

## Build
1. Connect IQ SDK + VS Code "Monkey C" extension, developer key generated.
2. Open this `ConnectIQ` folder, "Monkey C: Build for Device" -> `fenix7x`.
3. Copy the `.prg` to `GARMIN/APPS/` on the watch (USB).
4. Start **Indoor Row** (or any activity), edit a data screen, add Connect IQ field **ArduRower S3**
   (a 1-field screen gives the full layout).

The simulator only supports BLE with a Nordic nRF52 dongle, so test on the watch.
Debug output (`System.println`) ends up in `GARMIN/APPS/LOGS/<name>.TXT` if you create an empty
file with that name first.

## Notes
- Other watches: add their product id to `manifest.xml` (e.g. `fenix7xpro`, `fenix8...`).
- The rower's elapsed time is moving time (pauses when you stop); the watch's activity timer is separate.
- Pace is recorded in seconds per 500 m (Garmin Connect shows it as a number, e.g. 135 = 2:15).
