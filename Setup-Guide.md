# Setup Guide

## 1. Arduino IDE

1. Install the Arduino IDE.
2. Add ESP32 board support (Boards Manager → "esp32" by Espressif).
3. Install the **TFT_eSPI** library (Library Manager).
4. Board: your ESP32 dev board (for example "ESP32 Dev Module").

The sketch has compile-time paths for both the 2.x and 3.x ESP32 cores (the backlight PWM functions differ). If one of them gives you trouble, note which core and TFT_eSPI versions you use and open an issue.

## 2. Display configuration

1. Copy `User_Setup.h` from the repo over the one in `Documents/Arduino/libraries/TFT_eSPI/`.
2. If `User_Setup_Select.h` points at another file, edit that instead, or point it at this one.
3. Check that the font defines are present (see [Hardware and Wiring](Hardware-and-Wiring.md)).

## 3. Notification server

1. Upload `notifications.php` to any PHP host that serves HTTPS.
2. Open it and replace both keys with long random strings. A quick way: `openssl rand -hex 24`.
3. Test: `curl "https://YOUR_DOMAIN/notifications.php?a=poll&key=READ_KEY"` should print `0`.

## 4. Firmware

1. Put `EspDevClock_Colored.ino` in a folder called `EspDevClock_Colored`.
2. Edit the credentials block at the top:
   - `WIFI_LIST`: your networks, strongest-first order does not matter, the clock picks by signal
   - `NOTIF_URL`: your domain plus the **read** key
3. Upload. First boot shows "Searching for WiFi...", then "Syncing time...", then the clock.

## 5. Try it

```bash
curl -d "a=push&key=WRITE_KEY&type=Test&msg=Hello&id=test" https://YOUR_DOMAIN/notifications.php
```

Within about 30 seconds the bell should turn red with a badge and the bottom bar should show `Test: Hello`. Clear it with:

```bash
curl -d "a=clear&key=WRITE_KEY&id=test" https://YOUR_DOMAIN/notifications.php
```

## Before you publish your own copy

Never commit real WiFi passwords or keys. Use placeholders in anything that goes to GitHub.
