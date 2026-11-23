<div align="center">

  <img src="https://raw.githubusercontent.com/arslansadiq87/circuit-studio/main/techpanda%20logo.png" alt="TechPanda Logo" width="150">

  <h1>Tech Panda Labs</h1>

  <p>
    Create 3D electronics wiring diagrams, connect components,<br>
    and generate animated wiring tutorials.
  </p>

  <p>
    <a href="https://circuit.techpandalabs.com">
      <img src="https://img.shields.io/badge/Launch-Circuit_Studio-2563EB?style=for-the-badge" alt="Launch Circuit Studio">
    </a>
    <a href="https://techpandalabs.com">
      <img src="https://img.shields.io/badge/Explore-TechPanda_Labs-374151?style=for-the-badge" alt="Explore TechPanda Labs">
    </a>
  </p>

</div>

# ESP32-C6 Super Mini GC9A01 Analog Clock

PlatformIO Arduino project for a 240x240 round GC9A01 TFT on an ESP32-C6 Super Mini.

## Wiring

| TFT Display Pin | ESP32-C6 Super Mini Pin |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCL | GPIO4 |
| SDA | GPIO6 |
| DC | GPIO2 |
| CS | GPIO7 |
| RST | GPIO1 |
| BL / LED | 3V3, or a GPIO driven HIGH |

`SCL` and `SDA` are used as the SPI clock and MOSI pins for this display.
If your module has a `BL` or `LED` backlight pin and it is left floating, the screen can stay visibly blank even when SPI wiring is correct. Tie it to `3V3` for this first test, or set `TFT_BL` in `src/main.cpp` to the GPIO you wired.

On startup the firmware now shows a wiring test before the clock:

1. Full-screen red, green, blue, and white flashes.
2. A four-quadrant color pattern labeled `GC9A01 wiring test`.
3. The analog clock face.

If the serial monitor prints `Display init OK` but the TFT never lights, check `VCC`, `GND`, and `BL` first. If the backlight is on but the pattern is missing or corrupted, check `SCL`, `SDA`, `DC`, `CS`, and `RST`.

## Build and upload

```bash
pio run
pio run -t upload
pio device monitor
```

The project uses `esp32-c6-devkitm-1` as the closest stock PlatformIO board definition for the ESP32-C6 Super Mini.

## Time source

By default the clock starts from the firmware build time and advances with `millis()`.
For NTP time sync, edit `include/config.h` and set `WIFI_SSID`, `WIFI_PASSWORD`, and `CLOCK_TZ`.
