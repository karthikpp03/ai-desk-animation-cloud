# AI Desk Companion — full project

A desk companion on an ESP32: a living face on a 128x64 OLED, LEDs, servo ears, motion reactions, clock/weather/AI screens — plus an **Animation Display Mode** that plays animations you upload from a website.

```
Website (GitHub Pages) → Cloudflare Worker → GitHub storage → ESP32 → SSD1306 OLED
```

- `cloud/` — website, Worker, animation parser, scripts (own GitHub repo)
- `firmware/AIDeskCompanion/` — the ESP32 firmware
- **`SETUP.md` — start here.**

Button: 1 press next screen · 2 LED mode · 3 ear mode · long lock/unlock · **4 Animation Display Mode on/off**.
