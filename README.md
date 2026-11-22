# ESP32-S3-Zero + SH1107 128x128 port

Port of https://github.com/zpukr/ArduRower (original: Heltec WiFi Kit 32 + SSD1306 128x64).

## Arduino IDE setup
- Board package: **esp32 by Espressif 3.3.x** (Boards Manager; tested with 3.3.13 and 2.0.9, **not 4.x** - it has a new BLE API), board **ESP32S3 Dev Module**
- Tools > **USB CDC On Boot: Enabled**
- Library Manager: **U8g2**
- Open `ArduRower_S3_OLED/ArduRower_S3_OLED.ino` (RunningAverage.h/.cpp stay in the same folder)

## Wiring (Waveshare ESP32-S3-Zero)
| Function | GPIO |
|---|---|
| OLED SDA / SCL (SH1107, 0x3C) | 8 / 9 (same as AirSuspension) |
| Reed switch (other side to GND) | 2 |
| Button (reset trip) | 0 (onboard BOOT) |
| Battery sense (optional, `ENABLE_BATTERY 1`) | 1, 1:1 divider from VBAT |

## Deep sleep
No reed pulse for `IDLE_SLEEP_MS` (5 min) -> display off + deep sleep. The next reed pulse
(GPIO2 LOW, internal pull-up kept in sleep) wakes and restarts the sketch (~1 s boot).
Make sure the reed switch is not parked closed, otherwise it wakes immediately.

## Options (top of the sketch)
- `MAGNETS_PER_ROUND` (default 4): magnets on the rotor = reed pulses per revolution. Meters per pulse
  and the stroke-detection thresholds scale with it (4 -> 3.156 pulses/m as in the original firmware).
  Values other than 4 are scaled from the original 4-magnet/1-magnet settings and not tested on a rower.
- Demo mode: on the "Waiting for start" screen hold BOOT for 1 s. A simulated rower (`SIM_SPM` = 30 strokes/min,
  about 3.7 m/s) feeds the normal chain, so display and BLE data to the watch/app can be checked without a rower.
  "SIM" is shown top-left; any button press ends it. Allow ~20 s for the stroke rate average to settle.

Screen rotation: `DISPLAY_ROTATION` (default U8G2_R1 like AirSuspension).

---

# ArduRower
The project was created on the basis of the ESP32 board (Heltec WiFi Kit 32 - cheap ESP32 board with OLED & LiPo battery interface) for equipping chinese clones of waterrower like Incline, XtremepowerUS, Hunter, Fit-On or DIY rowing machines with a 0.96-inch OLED screen and bluetooth interface. The device have emulates the work of the original WaterRower S4 equipped with a bluetooth comm module. Tested work with the **Coxswain** app, which allows to record and export your workouts to Garmin Connect and/or Strava, work with **Kinomap**, **HOLOFIT** and probably other applications that support the **FiTness Machine Service** protocol better known as **FTMS**. To connect to your rowing machine, you only need to attach two GND and GPIO2 wires to the RPM sensor (you can add 1kΩ resistor or similar on each wire for protection ESP32 board). You can also connect the battery to GPIO33 through a divider of two resistors: 220kΩ resistance connects to "+" of LiPo battery and GPIO33, 100kΩ resistance to GND and GPIO33

![alt tag](https://raw.githubusercontent.com/zpukr/ArduRower/main/onrower.jpg)
![alt tag](https://raw.githubusercontent.com/zpukr/ArduRower/main/heltec.jpg)
![alt tag](https://raw.githubusercontent.com/zpukr/ArduRower/main/Coxswain.jpg)
![alt tag](https://raw.githubusercontent.com/zpukr/ArduRower/main/kinomap.jpg)

It is possible to write directly to the Garmin Connect using the Connect IQ DataField **ArduRower-IQ** https://apps.garmin.com/en-US/apps/0e10bae1-9753-4915-b856-040d0cbdd82a 

![alt tag](https://raw.githubusercontent.com/zpukr/ArduRower/main/garmin.jpg)
![alt tag](https://raw.githubusercontent.com/zpukr/ArduRower/main/garmin_connect.jpg)

Rower side based on sketch **WaterRino**: https://github.com/adruino-io/waterrino                          
Bluetooth side on sketch **WaterRower S4BL3 Bluetooth BLE for S4**: https://github.com/vibr77/wr_s4bl3_samd
