# ESP32 IST + UK Clock

A desk clock for the ESP32 and a 2.8" ST7789 TFT. **India (IST)** is the big hero time, **UK (GMT/BST)** sits underneath in a compact card, and a **bell lights up red** when another system pushes a notification to a tiny PHP endpoint ("Teams message not replied", "new email", and so on).

![Clock screen](docs/images/clock.jpeg)

## Features

- Two time zones from one NTP-synced UTC clock, with automatic UK daylight saving
- Big 7-segment IST time with a blinking colon and a seconds progress bar
- Notification bell with count badge, plus a bold red message bar
- Several WiFi networks: joins the strongest one in range and re-picks when the signal fades (handy when you move between floors)
- Smooth and light: one tiny update per second, partial redraws, 80 MHz CPU, network work on the second core
- Built for 24x7: night dimming, no flash writes for WiFi, low-memory self-restart
- Push, poll, clear and list notifications over a single PHP file, no database

## Hardware

Tested on an ESP32 dev board with a 2.8" 240x320 ST7789 SPI display.

| Signal | GPIO |
|--------|------|
| MISO   | 12   |
| MOSI   | 13   |
| SCLK   | 14   |
| CS     | 15   |
| DC     | 2    |
| RST    | none (-1) |
| Backlight | 21 |

Tested with: Arduino-ESP32 core `<fill in>`, TFT_eSPI `<fill in>`.

## Repo layout

```
firmware/EspDevClock_Colored/EspDevClock_Colored.ino   the sketch
config/User_Setup.h                                    TFT_eSPI setup for this display
server/notifications.php                               push/poll endpoint
docs/images/                                           screenshots
```

## Quick start

### 1. Display setup (TFT_eSPI)

1. Install **TFT_eSPI** from the Arduino Library Manager.
2. Copy `config/User_Setup.h` over the library's `User_Setup.h` (or point `User_Setup_Select.h` at it).
3. Make sure the font defines are present. Without them the screen shows only lines and no text:
   `LOAD_GLCD`, `LOAD_FONT2`, `LOAD_FONT4`, `LOAD_FONT6`, `LOAD_FONT7`, `LOAD_FONT8`.

### 2. Notification server (any PHP host, HTTPS)

1. Upload `server/notifications.php`.
2. Change `WRITE_KEY` and `READ_KEY` to long random strings (`openssl rand -hex 24`).
3. Test it: `curl "https://YOUR_DOMAIN/notifications.php?a=poll&key=READ_KEY"` should print `0`.

### 3. Firmware

1. Open the `.ino` and edit the credentials block near the top: your WiFi list and `NOTIF_URL` with the **read** key.
2. Select your ESP32 board and upload.

## Sending notifications

Use the **write** key from any system. POST is preferred so keys stay out of server logs.

```bash
# push (same id replaces the old item, so repeats don't pile up)
curl -d "a=push&key=WRITE_KEY&type=Teams&msg=Message not replied&id=teams" https://YOUR_DOMAIN/notifications.php

# clear one, by type, or everything
curl -d "a=clear&key=WRITE_KEY&id=teams" https://YOUR_DOMAIN/notifications.php
curl -d "a=clear&key=WRITE_KEY"          https://YOUR_DOMAIN/notifications.php

# inspect what is stored
curl "https://YOUR_DOMAIN/notifications.php?a=list&key=WRITE_KEY"
```

| Action | Key | Parameters |
|--------|-----|------------|
| `push` | write | `msg` (required), `type`, `id`, `ttl` seconds (default 24h, max 7 days) |
| `clear` | write | `id` or `type`, or nothing to clear all |
| `list` | write | none |
| `poll` | read or write | none. Returns the count, then the latest message on line 2 |

Text is limited to printable ASCII because the TFT fonts cannot draw anything else.

## Configuration

Everything is a constant near the top of the sketch.

| Setting | Purpose |
|---------|---------|
| `WIFI_LIST` | Known networks, tried strongest-first |
| `INVERT_DISPLAY` | Flip if colours look like a negative |
| `POLL_MS` | How often to check for notifications |
| `BRIGHT_DAY`, `BRIGHT_NIGHT`, `NIGHT_START`, `NIGHT_END` | Backlight levels and dimming hours (IST) |
| `DAILY_REBOOT` | Optional restart at `REBOOT_HOUR_IST` |
| `TZ_UK`, `TZ_IN` | POSIX time zone strings, change them for other cities |

## How it works

- **One clock, two zones.** The system clock stays in UTC. For each redraw the sketch switches `TZ`, calls `tzset()`, and converts the same timestamp twice.
- **Cheap drawing.** The static layout is drawn once. Text is drawn with a background colour and padding so it overwrites itself, and the big digits only redraw once a minute. The per-second work is two small circles and a few pixels of progress bar.
- **Two cores.** The UI runs on core 1. WiFi reconnecting and the HTTPS poll run in a FreeRTOS task on core 0, so a slow TLS handshake never freezes the clock. The task hands results to the UI through a flag.
- **Backlight.** PWM-dimmed at night, with a fade of about 12 levels per second.

## Challenges and lessons

A dev log of what went wrong and how it was fixed.

**1. The screen showed only a line and no text.**
The display was initialised and the colour fills worked, but all text was missing. TFT_eSPI silently skips any font that is not enabled in `User_Setup.h`. Fix: enable `LOAD_GLCD`, `LOAD_FONT2/4/6/7/8`.

**2. Colours looked like a negative image.**
White background, cyan text showing red, amber showing blue. The setup file defined both `TFT_INVERSION_ON` and `TFT_INVERSION_OFF`, and the sketch also called `invertDisplay(true)`. Fix: remove both defines, and set inversion explicitly in the sketch with one `INVERT_DISPLAY` constant. See `docs/images/inverted-colors-bug.jpeg`.

**3. Two time zones with daylight saving.**
Running two local zones on one chip is awkward. Fix: keep the system clock in UTC, then set a POSIX TZ string (`GMT0BST,M3.5.0/1,M10.5.0/2` for the UK, `IST-5:30` for India) around each conversion.

**4. Flicker from full-screen clears.**
`fillScreen()` on every update flashed the display. Fix: draw static parts once, then overwrite only the text that changed using a background colour and text padding.

**5. Making one clock much bigger.**
Font 7 is a 7-segment font with digits only. Fix: draw it at 2x scale, measure the digit width at startup, centre the layout from that, and draw the colon as two circles so it can blink.

**6. The UI froze during network checks.**
The HTTPS handshake can take a couple of seconds at 80 MHz. Fix: move WiFi supervision and polling into a task on core 0, and keep all display access on the main loop.

**7. Notification hub design.**
Needed push from many systems and read-only access for the clock. Fix: separate read and write keys, `id`-based de-duplication, per-item expiry, a file lock around read-modify-write, and a data file that starts with a PHP `exit` line so it cannot be downloaded.

**8. Moving between floors.**
One WiFi network was not enough. Fix: `WiFiMulti` with a list of networks, plus a re-pick when the signal stays weak for about 30 seconds.

**9. Running 24x7.**
Fixes: `WiFi.persistent(false)` to avoid flash wear, backlight dimming at night, a low-heap restart, and deliberately no reboot when WiFi drops, because the clock keeps time on its own and re-syncs when the network returns.

**10. Backlight PWM API differs between ESP32 cores.**
`ledcAttach()` on core 3.x, `ledcSetup()` plus `ledcAttachPin()` on 2.x. Fix: a compile-time `ESP_ARDUINO_VERSION_MAJOR` check.

**11. Credentials in source code.**
Fine on a bench, risky in a repo. Fix: commit placeholders only, and rotate any password or key that ever appeared in a chat, screenshot or commit.

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Only a thin line, no text | Fonts not enabled in `User_Setup.h` |
| Negative image | Flip `INVERT_DISPLAY`, remove conflicting inversion defines |
| Stuck on "Searching for WiFi..." | SSID or password wrong in the credentials block, or out of range |
| Bell never lights | Wrong key in `NOTIF_URL`, server not on HTTPS, or nothing pushed (`a=list` shows what is stored) |
| Text garbled | Non-ASCII characters in a notification |

## Security notes

- Use HTTPS and long random keys. The clock only holds the read key.
- The firmware skips TLS certificate checking (`setInsecure()`) to stay light. That is acceptable for a clock, but you can pin a CA certificate if you want stricter checks.
- Never commit real WiFi passwords or keys.

## License

MIT. See [LICENSE](LICENSE).

## Credits

Display driver library: [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer.
