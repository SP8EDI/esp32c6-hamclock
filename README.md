# HamQSL Solar Propagation Monitor

A desktop HF propagation monitor for ham radio operators, built on the
**Waveshare ESP32-C6-LCD-1.47** board. Cycles through four screens:
HF band conditions, dual-timezone clock, latest DX cluster spots, and
local weather with pressure trend.

[Polska wersja / Polish version](README.pl.md)

---

## Features

| # | Screen | Data source | Refresh |
|---|--------|-------------|---------|
| 1 | HF Propagation (bands, day/night, color-coded) | [hamqsl.com](https://www.hamqsl.com) (N0NBH) | 60 min |
| 2 | Clock — local (Poland, auto DST) + UTC | NTP | 1 s |
| 3 | DX Spots — 5 latest (band / callsign / MHz) | [dxsummit.fi](http://www.dxsummit.fi) | 5 min |
| 4 | Weather — temp, pressure + 3h trend, wind, humidity | [open-meteo.com](https://open-meteo.com) | 15 min |

Screens rotate automatically every 10 seconds (page dots at the bottom).

Additional features:

- **Captive-portal WiFi setup** — no hardcoded credentials. On first boot the
  device starts an open AP `HamQSL-Setup`; connect and open `192.168.4.1`.
- **Web panel** at `http://<device-IP>/` — live status and a form to change
  the weather location (name + lat/lon), persisted in NVS across reboots.
- **Debug log over HTTP** at `http://<device-IP>/log` (auto-refresh every 5 s)
  — no serial cable needed for troubleshooting.
- **PWM-dimmed backlight** (default 120/255) so the panel stays cool.
- DX band is derived from the spot frequency (dxsummit's API has no band field).
- 3D-printable enclosures included (OpenSCAD).

Default weather location: **Jasionka / EPRZ airport, Poland** (50.110, 22.019) —
change it from the web panel to anywhere in the world.

---

## Hardware

- Waveshare **ESP32-C6-LCD-1.47** (ESP32-C6FH8, 8 MB flash, ST7789 172×320 IPS)
- USB-C cable, WiFi 2.4 GHz

Display pins are fixed on the board (already configured in code):

| Signal | GPIO |
|--------|------|
| MOSI | 6 |
| SCLK | 7 |
| CS | 14 |
| DC | 15 |
| RST | 21 |
| BL (backlight, PWM) | 22 |

> **Panel quirk:** this ST7789 module requires `invert = true` in the
> LovyanGFX panel config — without it the display shows inverted colors
> (white background). Already handled in `src/main.cpp`.

---

## Building (PlatformIO)

1. Install [VS Code](https://code.visualstudio.com/) +
   [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)
2. Open this folder (`File → Open Folder`)
3. Click **✓ Build** — dependencies (WiFiManager, LovyanGFX) install automatically
4. Connect the board via USB-C, click **→ Upload**
5. Serial monitor: 115200 baud (optional — the HTTP log is usually enough)

The board has 8 MB flash; a custom partition table (`partitions.csv`,
app slots of 3264 KB) is required because the firmware exceeds the default
1280 KB app partition.

---

## First boot

1. The device starts an **open** WiFi AP: `HamQSL-Setup`
2. Connect to it and open `192.168.4.1`
3. Pick your home network, enter the password, Save
4. The screen shows the assigned IP and panel URL for 3 seconds

**WiFi reset:** hold the **BOOT button (GPIO9)** while powering on.

---

## Web panel

| URL | Purpose |
|-----|---------|
| `http://<IP>/` | Status + change weather location (name, latitude, longitude) |
| `http://<IP>/log` | Debug log, auto-refreshing |
| `http://<IP>/refresh` | Force-refresh all data sources |

---

## 3D-printed enclosures (`enclosure/`)

Two designs, both parametric [OpenSCAD](https://openscad.org) files —
set `PRINT_PART`, press F6, export STL:

- **`hamqsl_panel.scad`** — ICOM IC-706-inspired front panel with fold-out
  tilt legs (M3 hinges), recessed display, decorative groove, embossed label
- **`hamqsl_crt_monitor.scad`** — miniature 90s CRT monitor: head + neck +
  inverted-T base, top/rear venting, OSD buttons, power LED, ~8° tilt

Recommended: dark gray or black PETG/ABS, 0.2 mm layers, no supports.
Hardware per file header comments (M2.5/M3 screws, anti-slip pads).

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| White / washed-out background | Ensure `pcfg.invert = true` in the LGFX class |
| Weather shows "no data" | Check `http://<IP>/log` for the HTTP code / JSON |
| DX band shows `?` | Frequency outside amateur bands (band is computed from kHz) |
| Firmware doesn't fit / boot loop | Make sure `partitions.csv` sits next to `platformio.ini` |
| Clock stuck at `--:--:--` | NTP not synced yet — check internet, wait a minute |

---

## Credits & data sources

- Solar/propagation data: Paul L Herrman **N0NBH**, hamqsl.com
- DX cluster spots: dxsummit.fi
- Weather: [Open-Meteo](https://open-meteo.com) (CC BY 4.0), no API key required
- Libraries: [LovyanGFX](https://github.com/lovyan03/LovyanGFX),
  [WiFiManager](https://github.com/tzapu/WiFiManager)

## License

MIT — do whatever you like, attribution appreciated. 73!
