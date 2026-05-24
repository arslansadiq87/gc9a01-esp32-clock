#pragma once

// Optional Wi-Fi/NTP clock sync.
// Leave WIFI_SSID empty to run from the firmware build time plus millis().
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Set this to your local timezone string if you enable Wi-Fi/NTP.
// Examples:
//   Coordinated Universal Time (UTC): "UTC0"
//   India Standard Time: "IST-5:30"
//   Pakistan Standard Time: "PKT-5"
//   UK (with daylight saving): "GMT0BST,M3.5.0/1,M10.5.0"
//   US Eastern Time: "EST5EDT,M3.2.0,M11.1.0"
//   Japan Standard Time: "JST-9"
//   Australia Sydney: "AEST-10AEDT,M10.1.0,M4.1.0/3"
//   Coordinated Universal Time (UTC): "UTC0"
#define CLOCK_TZ "UTC0"

