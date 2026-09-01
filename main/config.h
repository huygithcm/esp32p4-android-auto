#pragma once

/* Hostname used to build the "open the web UI" URL in the QR screen
 * (components/qr_info). Was AA_MDNS_HOSTNAME on the Android Auto build.
 *
 * TODO(port): main/mdns_advertise.c was removed with the AA stack — it only
 * advertised the _aawireless._tcp service the Wireless Helper APK browses for.
 * Until an mDNS responder is registered again, `<host>.local` does not resolve
 * and the QR screen's URL only works via the raw IP it also renders. */
#define DEVICE_MDNS_HOSTNAME   "vesc-display"

/* Dashboard wall clock (cur_time_label + Time row in Settings).
 *
 * 0 on this branch. The P4 board kept time across a power cut by routing its
 * LP domain to a CR2032 through the PMU (main/vbat_routing.c, removed here).
 * Neither ESP32-S3 nor ESP32 has a VBAT pin for the RTC domain, so surviving
 * power loss needs an external RTC (DS3231 on the touch I2C bus is the obvious
 * candidate). Set to 1 only once such a source exists — otherwise the clock
 * shows garbage after every power cycle. */
#define ENABLE_WALL_CLOCK      0
