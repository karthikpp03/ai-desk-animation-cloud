# AI Desk Companion + Animation Cloud

Please refer the setup_guide folder for the commands if you dont have patience to read the readme file

A small desk pet built on an **ESP32**. It has a living face on a 128x64 OLED screen, 3 LEDs, 2 servo ears, a buzzer, a motion sensor and one button.

You can also **upload your own animations from a website** and play them on the OLED, or **draw on the OLED from your phone or laptop** (Draw Pad).

This file explains everything: how it works, how to set it up from zero, what every pin and button does, and how to change things. Everything here was checked against the real code. 

> **Before you share this folder with anyone:** delete the secret files listed in [Section 17](#17-before-you-share-this-folder). They contain real passwords and tokens.

---

## Table of contents

1. [What this project does](#1-what-this-project-does)
2. [How everything is connected](#2-how-everything-is-connected)
3. [Folder map](#3-folder-map)
4. [What you need](#4-what-you-need)
5. [Hardware wiring and GPIO pins](#5-hardware-wiring-and-gpio-pins)
6. [The button: what every press does](#6-the-button-what-every-press-does)
7. [Screens and features on the device](#7-screens-and-features-on-the-device)
8. [Setup from zero (step by step)](#8-setup-from-zero-step-by-step)
9. [WiFi setup and how to change WiFi](#9-wifi-setup-and-how-to-change-wifi)
10. [GitHub setup and login](#10-github-setup-and-login)
11. [Cloudflare / Wrangler setup](#11-cloudflare--wrangler-setup)
12. [The website](#12-the-website)
13. [Secrets and settings reference](#13-secrets-and-settings-reference)
14. [How the whole system works, step by step](#14-how-the-whole-system-works-step-by-step)
15. [Where things are in the code](#15-where-things-are-in-the-code)
16. [How to change common things](#16-how-to-change-common-things)
17. [Before you share this folder](#17-before-you-share-this-folder)
18. [Common problems and fixes](#18-common-problems-and-fixes)
19. [First-time setup checklist](#19-first-time-setup-checklist)
20. [Notes about the older documents in this folder](#20-notes-about-the-older-documents-in-this-folder)

---

## 1. What this project does

**On the ESP32 (the device):**

- Shows a cartoon face that blinks, looks around, gets sleepy, and reacts to things.
- Cycles through screens: face, clock, weather, temperature, quote, reminders, AI message, system status.
- Moves two servo "ears" and lights 3 LEDs to match the mood.
- Beeps with a buzzer.
- Feels movement with a motion sensor (MPU6050): tap, shake, pick up, tilt, spin.
- Has **Animation Display Mode**: plays animations you uploaded on the website.
- Has a **Draw Pad**: you draw in a web page and it appears on the OLED.

**On the website:**

- Upload an Arduino `.ino` animation file. It is converted automatically.
- See animation previews, press **Play**, start a **Slideshow**, or **Stop**.
- Switch the ESP32 between normal mode and Animation Display Mode (**Mode Change**).
- See if the ESP32 is **Online / Offline**.
- Open the **Draw Pad**.
- See a **Logs** section (stored only in your browser).

> The website has **no password**. Anyone who knows the link can upload animations and control the display. This is on purpose.

---

## 2. How everything is connected

```
  You (browser)
      │  open the website
      ▼
  GitHub Pages  ───────────────  the website files (folder: cloud/)
      │  the website calls the API
      ▼
  Cloudflare Worker  ──────────  the backend (cloud/worker/)
      │  saves and reads files          ▲
      ▼                                 │  the ESP32 asks the Worker
  GitHub repository  ──────────         │  "what should I do?" every second
  (animations + device state)           │
                                        │
                                  ESP32  ───►  OLED screen
```

In simple words:

- **GitHub Pages** hosts the website (just normal web files).
- **The Cloudflare Worker** is the backend. It converts uploaded `.ino` files, checks tokens, and talks to GitHub. It is a small program that runs on Cloudflare's servers, so your PC does not need to stay on.
- **GitHub** is the storage. There is no database. Animations are saved as files inside your GitHub repository. The device's last state is also saved there (`device/state.json`).
- **The ESP32** connects to WiFi and keeps asking the Worker for commands (play, stop, slideshow, change mode). It downloads the chosen animation and plays it from its own memory.
- **Draw Pad** works through the same Worker: the ESP32 opens an outgoing connection to the Worker, and your browser connects to the Worker too. So you do not need to be on the same WiFi as the ESP32.

**Two tokens keep this safe:**

- `DEVICE_TOKEN`: the ESP32 sends it with every request. The Worker only trusts the ESP32 if it matches.
- `ADMIN_TOKEN`: only needed to **delete** an animation using the API. The website never asks for it.

---

## 3. Folder map

```
ai-desk-animation-cloud/
├── README.md                    ← this file
├── SETUP.md                     older setup guide (see Section 20)
├── .github/workflows/pages.yml  publishes the website (cloud/ folder) to GitHub Pages
├── animations/                  animations saved by the Worker (one folder each)
├── animations.json              list of all animations (the Worker keeps this updated)
├── device/state.json            last known device state (the Worker keeps this updated)
│
├── cloud/                       WEBSITE + BACKEND
│   ├── index.html, app.js, style.css   the website
│   ├── drawpad.html                    the Draw Pad page
│   ├── wrangler.toml                   Cloudflare Worker settings
│   ├── package.json                    npm commands
│   ├── .env.example                    template for your secrets
│   ├── worker/                         the backend code
│   │   ├── index.js                    all API routes + Draw Pad relay
│   │   ├── parser/inoParser.js         turns .ino files into frames
│   │   ├── ai/analyze.js               OpenAI fallback (optional)
│   │   ├── storage/github.js           reads/writes files in GitHub
│   │   └── validator/animation.js      checks converted animations
│   ├── scripts/                        helper scripts (npm run ...)
│   ├── sample-eye.ino                  a small test animation to upload
│   └── esp32/animation_client/         OLD standalone ESP32 player (not needed)
│
└── firmware/AIDeskCompanion/    ESP32 PROGRAM (open the .ino in Arduino IDE)
    ├── AIDeskCompanion.ino             main file
    ├── Config.h                        all pins and settings
    ├── secrets.h.example               template (the real secrets.h is generated)
    └── *.h / *.cpp                     one pair of files per feature
```

The **whole project folder is one GitHub repository** (the remote is `ai-desk-animation-cloud`). The Worker saves animations into the `animations/` folder at the top of that repo, and `pages.yml` publishes the `cloud/` folder as the website.

---

## 4. What you need

### Accounts (free plans are enough)

| Account | Used for |
|---|---|
| GitHub | stores the code, the animations, and hosts the website |
| Cloudflare | runs the Worker (backend) |
| OpenAI (optional) | only used if the built-in parser cannot read an `.ino` file |
| OpenWeatherMap (optional) | weather screen on the device |
| Anthropic API key (optional) | the "AI message" screen (see Section 7) |

### Software on your computer

- **Node.js 20 or newer** and npm (check with `node -v`)
- **git**
- **Arduino IDE 2.x**

### Hardware

| Part | How many |
|---|---|
| ESP32 Dev Module (ESP32-WROOM) | 1 |
| SSD1306 OLED, 128x64, I2C | 1 |
| MPU6050 (GY-521 board) | 1 |
| Push button | 1 |
| Active buzzer | 1 |
| LEDs (red, white, green) | 3 |
| 220 ohm resistors (one per LED) | 3 |
| SG90 micro servos (ears) | 2 |
| USB cable, jumper wires | as needed |

### Arduino IDE board package

1. Arduino IDE → Settings → **Additional boards manager URLs**, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Tools → Board → **Boards Manager** → install **esp32 by Espressif Systems**.

### Arduino libraries (Tools → Manage Libraries)

Install all of these before compiling:

| Library | Note |
|---|---|
| Adafruit GFX Library | |
| Adafruit SSD1306 | |
| ArduinoJson | **version 7** (the code uses `JsonDocument`) |
| ESP32Servo | |
| ESP Async WebServer | |
| Async TCP | the original author's notes list version 3.5.0 |
| WebSockets (by Markus Sattler) | |

These come with the ESP32 package, so you do not install them: WiFi, HTTPClient, LittleFS, Preferences, DNSServer, Wire, WiFiClientSecure.

---

## 5. Hardware wiring and GPIO pins

All pins are set in `firmware/AIDeskCompanion/Config.h`.

### Pin table

| ESP32 GPIO | Connected to | What it is used for |
|---:|---|---|
| **21** | OLED **SDA** and MPU6050 **SDA** | I2C data (shared by both) |
| **22** | OLED **SCL** and MPU6050 **SCL** | I2C clock (shared by both) |
| **19** | Push button | Button input. Other leg of the button goes to GND |
| **18** | Active buzzer | Buzzer output |
| **25** | Left LED (red) | LED output (through 220 ohm resistor) |
| **26** | Center LED (white) | LED output (through 220 ohm resistor) |
| **27** | Right LED (green) | LED output (through 220 ohm resistor) |
| **32** | Left servo signal | Left ear |
| **33** | Right servo signal | Right ear |
| **23** | MPU6050 **INT** | Motion sensor interrupt pin (set as input) |

I2C addresses: OLED = `0x3C`, MPU6050 = `0x68`.

### Wiring, part by part

**OLED (SSD1306)**

| OLED pin | Connect to |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

**MPU6050 (GY-521)**

| MPU pin | Connect to |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO21 (same wire as the OLED SDA) |
| SCL | GPIO22 (same wire as the OLED SCL) |
| AD0 | GND (this makes the address `0x68`) |
| INT | GPIO23 |
| XDA, XCL | not connected |

**Button**

```
GPIO19 ───── BUTTON ───── GND
```

No outside resistor is needed. The code turns on the ESP32's built-in pull-up.

**Buzzer (active type)**

```
GPIO18 ───── Buzzer signal
GND    ───── Buzzer GND
```

**LEDs** (each LED needs its own resistor)

```
GPIO25 ── 220 ohm ──► Left LED  (+)      LED (-) ──► GND
GPIO26 ── 220 ohm ──► Center LED (+)     LED (-) ──► GND
GPIO27 ── 220 ohm ──► Right LED (+)      LED (-) ──► GND
```

**Servos (ears)**

| Servo | Signal wire | Power |
|---|---|---|
| Left ear | GPIO32 | servo supply |
| Right ear | GPIO33 | servo supply |

### Power rules

- Power the ESP32 from USB for setup. A regulated 5V source can go to the board's 5V/VIN pin.
- **Never** put 5V into the 3.3V pin.
- Servos can pull sudden current. If the ESP32 keeps restarting when the ears move, give the servos a stronger, stable supply.
- **All grounds must be joined together** (ESP32, servos, sensor, etc.).

---

## 6. The button: what every press does

There is **one** button on GPIO19. The device counts quick presses.

| What you do | What happens |
|---|---|
| **1 short press** | Go to the **next screen**. (The device waits about 0.3 s to make sure you are not pressing again.) |
| **2 quick presses** | Turn **LED mode** on or off (the 3 LEDs follow the face's mood). |
| **3 quick presses** | Turn **ear mode** on or off (the servo ears move). |
| **4 quick presses** | Turn **Animation Display Mode** on or off. |
| **Long press** (hold about 0.6 s) | **Lock / unlock** the current screen. |

**Good to know:**

- **Lock:** while locked, the screens stop changing by themselves. Short, double and triple presses only give a small beep. Only a long press unlocks. Also, you cannot **enter** Animation Display Mode while locked (you can still leave it).
- **Automatic screen change:** unlocked screens change by themselves every few seconds (3 to 8 seconds depending on the screen).
- **Sleep:** after about 1.5 minutes with no button press, the face falls asleep (not while locked or while an animation is playing). Press the button or move the device to wake it.
- **Inside Animation Display Mode:** short and long presses do nothing (there is no screen to change). Double (LEDs) and triple (ears) still work. Four quick presses exit.
- **While the Draw Pad is open:** short press, long press and the 4-press shortcut are ignored. Double and triple presses still work.
- **Buzzer sounds:** short tick for a short press, a two-beep pattern for lock, a different pattern for unlock, a playful beep pattern for mode changes, and a happy chirp at boot.

**Website buttons** (see Section 12) can also switch the mode, so you do not always need the 4-press shortcut.

---

## 7. Screens and features on the device

**Boot:** a dot grows, the eyes open, they look left and right, the face says "Hi!", then normal life starts.

**Screens** (in this order):

| Screen | Needs WiFi? |
|---|---|
| Face | no |
| Clock (time from internet) | yes |
| Weather | yes, plus a weather key |
| Temperature | yes, plus a weather key |
| Quote (a random short quote) | no |
| Reminders (uses the clock time from the internet) | yes |
| AI message (one short upbeat line, refreshed hourly) | yes, plus an AI key |
| System status (mode, WiFi, LED, ears, MPU, heap, uptime) | no |

**Motion sensor reactions:** tap, shock, shake (3 or more shakes), sudden spin, dizzy (strong shaking), pick-up, landing, tilt front/back/left/right, too much tilt, and waking up from sleep when moved. The "idiot", "stupid" and "dizzy" bitmap animations are stored in `MotionAnimations.h`.

**Reminders:** two are built in: 17:30 "Meeting 5:30" and 20:00 "Stretch break" (see Section 16 to change them).

**Draw Pad:** a drawing page for the OLED. Open it with the website's **Draw Pad** button (works from any internet connection), or by opening the ESP32's IP address in a browser on the same WiFi. The page has a pen-size slider (1 to 8), **Clear panel** and **Exit Draw Pad**. Closing the page returns the screen to normal.

**AI message screen (important):** this screen calls the Anthropic API directly from the ESP32. The key is **not** generated from `.env`. It is a placeholder in `Config.h` (`AI_API_KEY`). If you leave the placeholder, the AI screen simply shows its error reaction. Everything else still works. If you do add a key there, **never commit or share that file with the key inside**.

**Demo mode:** `DEMO_MODE` in `Config.h` is `false` (real WiFi and live data). Set it to `true` to run with no WiFi at all (fake clock, weather, quotes and AI messages), which is handy for testing only the hardware.

---

## 8. Setup from zero (step by step)

Total time: about 30 to 40 minutes. You type each secret **once**, into `cloud/.env`.

### Step 1: Get the project and install tools

1. Install Node.js 20+, git and Arduino IDE 2.x.
2. Put this project folder on your computer.
3. Install the ESP32 board package and the Arduino libraries from [Section 4](#4-what-you-need).

### Step 2: Create your GitHub repository and token

Follow [Section 10](#10-github-setup-and-login). You will end up with a repo and a token.

### Step 3: Fill in `cloud/.env`

```bash
cd cloud
npm install
cp .env.example .env
```

Make two random tokens (run the command twice):

```bash
openssl rand -hex 32
```

Open `.env` in a text editor and fill it in. Use [Section 13](#13-secrets-and-settings-reference) to see what each line means. Leave `API_BASE` for step 5.

### Step 4: Push the project to GitHub and turn on the website

From the **project root folder** (the one that contains `cloud/` and `firmware/`):

```bash
git add .
git commit -m "Initial commit"
git push -u origin main
```

Then run the safety check inside `cloud/`:

```bash
cd cloud
npm run check:secrets
```

It must say **No secrets found in tracked files**. Then turn on GitHub Pages as shown in [Section 10](#10-github-setup-and-login).

### Step 5: Deploy the Worker (backend)

Inside `cloud/`:

```bash
npx wrangler login        # opens the browser, approve it
npm run deploy            # may ask you to pick a workers.dev name the first time
```

`deploy` prints your Worker address, like `https://ai-desk-animation-api.<your-name>.workers.dev`.

1. Put that address in `.env` as `API_BASE`.
2. Upload the secrets to Cloudflare:

```bash
npm run sync:secrets -- --dry-run     # only lists the names
npm run sync:secrets                  # uploads them
npm run test:api                      # live check
```

3. Open `cloud/app.js` and `cloud/drawpad.html` and replace the Worker address in both files (see Section 16, "Change the API address"). Then commit and push again.

### Step 6: Generate the ESP32 secrets file

Inside `cloud/`:

```bash
npm run gen:firmware
```

This writes `firmware/AIDeskCompanion/secrets.h` (WiFi, Worker address, device token, weather key). Run it again every time you change `.env`, then upload the firmware again.

### Step 7: Upload the firmware to the ESP32

1. Open `firmware/AIDeskCompanion/AIDeskCompanion.ino` in Arduino IDE.
2. **Tools → Board → ESP32 Dev Module.**
3. **Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS).** (This is from the project author's own notes: "before compiling".)
4. **Tools → Port →** pick your ESP32's port.
5. Click **Upload**.
6. Open **Tools → Serial Monitor** and set **115200 baud** to see messages.

If the upload does not start, hold the **BOOT** button on the ESP32 while it says "Connecting...".

### Step 8: Test it

1. The OLED should show the boot animation, then the face.
2. Open your website. The top right should say **ESP32 Online** within about 10 to 20 seconds.
3. Upload `cloud/sample-eye.ino`. It should appear in the list with a preview.
4. Press the button **4 times** (or click **Mode Change** on the website), then click **Play** on the animation.

---

## 9. WiFi setup and how to change WiFi

### Where the ESP32 gets its WiFi

It tries these networks **in this order**:

1. **Saved network**: one you entered on the setup page (stored inside the ESP32).
2. **Home WiFi**: `HOME_WIFI_SSID` / `HOME_WIFI_PASSWORD` from `.env` (baked into `secrets.h`).
3. **Hotspot**: `HOTSPOT_WIFI_SSID` / `HOTSPOT_WIFI_PASSWORD` from `.env` (for example, your phone hotspot).

Use **2.4 GHz** WiFi. The ESP32 cannot see 5 GHz networks. For a phone hotspot, turn on the 2.4 GHz / "maximize compatibility" option and keep the hotspot on.

### The setup page (no computer needed)

If the ESP32 cannot connect to any network for about **45 seconds**, it creates its own WiFi called **`AIDeskCompanion-Setup`** (no password).

1. On your phone, connect to `AIDeskCompanion-Setup`.
2. A setup page should open by itself. If not, open a browser and go to the address printed in the Serial Monitor (normally `192.168.4.1`).
3. Type the WiFi name and password, or tap **Scan Wi-Fi Networks** and pick one.
4. Tap **Save & Connect**. The page shows **Connected!** and the new IP address.

The new network is remembered, even after a restart. On the same page, **Forget saved Wi-Fi** clears it (Home and Hotspot from `secrets.h` are not touched).

### How to change WiFi

**Option A: change it in the project (needs re-upload)**

1. Edit `HOME_WIFI_SSID` and `HOME_WIFI_PASSWORD` in `cloud/.env`.
2. Run `npm run gen:firmware` inside `cloud/`.
3. Upload the firmware again.

**Option B: use the setup page (no re-upload)**

The setup page only appears when the ESP32 cannot connect. So switch off the old WiFi (or move the device away from it) and wait about 45 seconds. Then follow "The setup page" above.

**Important:** a network saved from the setup page is tried **before** Home WiFi. If you changed Home WiFi in `.env` but the device still joins an old network, it has an old saved network. Use Option B and "Forget saved Wi-Fi", or in Arduino IDE turn on **Tools → Erase All Flash Before Sketch Upload** for one upload.

---

## 10. GitHub setup and login

### Create the repository

1. On github.com, click **New repository**. Name it, for example, `ai-desk-animation-cloud`.
2. Make it **public** (free GitHub Pages needs public).
3. Do not add any files on GitHub. Leave it empty.

### Connect your folder to it

From the project root folder:

```bash
git remote -v                      # shows where it points now
git remote set-url origin https://github.com/YOUR_USER/YOUR_REPO.git
```

(If the folder has no git yet: `git init`, `git branch -M main`, then `git remote add origin ...`.)

When you `git push`, GitHub asks you to sign in. Use your GitHub login in the browser window, or a personal access token as the password.

### Create the token the Worker uses

1. GitHub → **Settings → Developer settings → Personal access tokens → Fine-grained tokens → Generate new token**.
2. **Repository access:** only select your repo.
3. **Permissions → Repository permissions → Contents: Read and write.**
4. Click Generate and copy the token **now** (GitHub shows it once).
5. Paste it into `cloud/.env` as `GITHUB_TOKEN`.

This token lets the Worker save animations and device state into your repo.

### Turn on the website (GitHub Pages)

The project uses a **GitHub Actions workflow** (`.github/workflows/pages.yml`). It copies the `cloud/` folder and publishes it every time you push to `main`.

1. Repo → **Settings → Pages**.
2. Under **Source**, choose **GitHub Actions**.
3. Push to `main`. Watch the **Actions** tab until it turns green.
4. Your website is at `https://YOUR_USER.github.io/YOUR_REPO/`.

Note: the Worker also saves files into `main` (animations and device state), and each save can start this workflow. Many green runs in the Actions tab are normal.

---

## 11. Cloudflare / Wrangler setup

Wrangler is Cloudflare's command-line tool. It is installed by `npm install` inside `cloud/`.

**Always run these inside the `cloud/` folder.**

| What | Command |
|---|---|
| Log in (opens browser) | `npx wrangler login` |
| Deploy the Worker | `npm run deploy` |
| See which secrets would be uploaded (names only) | `npm run sync:secrets -- --dry-run` |
| Upload secrets to Cloudflare | `npm run sync:secrets` |
| Live test of the deployed Worker | `npm run test:api` |
| Run the Worker on your computer | `npm run gen:devvars` then `npm run dev` |

**Worker settings** are in `cloud/wrangler.toml`:

- Worker name: `ai-desk-animation-api`
- Entry file: `worker/index.js`
- Non-secret values: `OPENAI_MODEL` and `STORE_ORIGINAL_INO` (`"false"` means the original `.ino` file is not kept)
- A **Durable Object** called `DRAW_PAD_RELAY` runs the Draw Pad connection. It is created automatically on the first deploy.

**Secrets uploaded by `npm run sync:secrets`:** `ADMIN_TOKEN`, `DEVICE_TOKEN`, `GITHUB_TOKEN`, `OPENAI_API_KEY` (only if set), `GITHUB_OWNER`, `GITHUB_REPO`, `GITHUB_BRANCH`.

The script sends values securely and never prints them.

**After you change a secret in `.env`:** run `npm run sync:secrets` again. If you changed `DEVICE_TOKEN`, also run `npm run gen:firmware` and upload the firmware again, because the ESP32 and the Worker must have the **same** device token.

**Test commands (no deploy needed):** `npm run test:parser` (checks the `.ino` reader) and `npm run test:worker` (checks the Worker locally).

---

## 12. The website

The website is plain files in `cloud/` (`index.html`, `app.js`, `style.css`, `drawpad.html`). There is **nothing to build**.

### What each button does

| Button | What it does |
|---|---|
| **Upload .ino** | Pick an Arduino animation file (max 2 MB). The Worker converts it and saves it to GitHub. |
| **Play** (on each animation card) | Tells the ESP32 to play that animation. |
| **Slideshow** | Plays all animations one after another, in a loop. |
| **Stop** | Stops playing. |
| **Mode Change** | Switches the ESP32 between **Normal** and **Animation Display** mode. Disabled while the ESP32 is offline or in Draw Pad. |
| **Draw Pad** | Opens the drawing page. Disabled while the ESP32 is offline. |
| **Refresh** | Reloads animations, failed imports and status. |
| **Clear** (in Logs) | Clears the logs saved in your browser. |

The top-right pill shows **ESP32 Online / Offline** and the current mode. The status is checked every 10 seconds.

The page also lists **Failed imports** (the latest 50) so you can see which `.ino` files could not be converted.

### Animation file rules

- 128x64 pixels only. Other sizes are rejected.
- Up to 600 frames.
- The frames must be byte arrays (`uint8_t ... PROGMEM = { ... }`), as used with `drawBitmap`.
- Speed is read from the file in this order: a `millis()` interval, a `delay()` in `loop()`, an `FPS` constant, a frame-delay variable. If nothing is found, a default is used.
- If the built-in reader fails, the Worker asks OpenAI to read the file (only if `OPENAI_API_KEY` is set).

### Run it on your computer

```bash
cd cloud
python3 -m http.server 8000      # or any simple web server
```

Open `http://localhost:8000`. The page still talks to the Worker address written in `app.js`, so it works with your deployed backend.

### Deploy it

Push to `main`. GitHub Pages publishes it (see Section 10). Open the **Actions** tab to watch.

---

## 13. Secrets and settings reference

**Rule:** type each value **once**, in `cloud/.env`. Never type a real secret into any other file, and never show real values in chat or email.

### `cloud/.env`

| Name | What to put | Secret? |
|---|---|:-:|
| `ADMIN_TOKEN` | random string from `openssl rand -hex 32`. Only guards deleting animations through the API. | yes |
| `DEVICE_TOKEN` | a **different** random string. The ESP32 and Worker must share it. | yes |
| `GITHUB_TOKEN` | the fine-grained token from Section 10 | yes |
| `OPENAI_API_KEY` | optional; leave empty to skip the AI fallback | yes |
| `GITHUB_OWNER` | your GitHub username | no |
| `GITHUB_REPO` | your repository name | no |
| `GITHUB_BRANCH` | `main` | no |
| `API_BASE` | your Worker address (printed by `npm run deploy`) | no |
| `HOME_WIFI_SSID` / `HOME_WIFI_PASSWORD` | your home WiFi (2.4 GHz) | password: yes |
| `HOTSPOT_WIFI_SSID` / `HOTSPOT_WIFI_PASSWORD` | backup WiFi, e.g. phone hotspot (optional) | password: yes |
| `WEATHER_API_KEY` | optional, free key from openweathermap.org | yes |
| `WEATHER_LOCATION` | for example `Chennai,IN` | no |

Values that start with `your`, `replace` or `changeme` are treated as empty.

### Files made from `.env` (all are git-ignored, never commit them)

| File | Made by | Used by |
|---|---|---|
| `firmware/AIDeskCompanion/secrets.h` | `npm run gen:firmware` | the ESP32 firmware |
| `cloud/esp32/animation_client/secrets.h` | `npm run gen:firmware` | the old standalone client |
| `cloud/.dev.vars` | `npm run gen:devvars` | `npm run dev` |
| Cloudflare Worker secrets | `npm run sync:secrets` | the deployed Worker |

---

## 14. How the whole system works, step by step

**Uploading an animation**

1. You click **Upload .ino** and pick a file.
2. The website sends it to the Worker (`POST /api/animations/upload`).
3. The Worker reads the frames and timing (`parser/inoParser.js`). If that fails and an OpenAI key exists, it asks OpenAI.
4. The Worker checks the result (128x64, 1 bit per pixel, at most 600 frames).
5. The Worker saves `metadata.json` and `frames.bin` into `animations/<name>-<id>/` in your GitHub repo and updates `animations.json`.
6. The website reloads the list and draws the preview from the saved frames.

**Playing an animation**

1. You press **Play**. The website tells the Worker (`POST /api/animations/<id>/play`).
2. The Worker writes the command into `device/state.json` in GitHub.
3. The ESP32 (in Animation Display Mode) asks `GET /api/device/command` every second, with the `X-Device-Token` header.
4. It sees the new command, downloads the frames into its own storage (LittleFS), and plays them on the OLED in a loop. It tells the Worker it has started (`/api/device/ack`).
5. If it already has that animation saved, it plays it at once.

**Slideshow:** the ESP32 gets the animation list, plays each one for one full loop, then moves to the next.

**Online / Offline:** the ESP32 sends a "heartbeat" to the Worker (every 5 seconds in normal mode, every 15 seconds in animation mode). If no heartbeat arrives for about 100 seconds, the website shows **Offline**.

**Mode Change:** the website saves a mode request. The ESP32 sees it in its next heartbeat or command check and switches, the same as 4 quick presses.

**Draw Pad:** the website asks the Worker for a short session. The ESP32 keeps an outgoing connection to the Worker (`/api/drawpad/device`). Your browser connects to `/api/drawpad/browser`. Strokes go browser → Worker → ESP32, which draws them into a 128x64 picture and shows it on the OLED.

**Why two modes use the same screen:** the OLED has one owner at a time (normal screens, Animation Display Mode, or Draw Pad). WiFi, LEDs, ears, buzzer and motion keep running in all of them.

---

## 15. Where things are in the code

### ESP32 firmware (`firmware/AIDeskCompanion/`)

| I want to find... | File |
|---|---|
| All pins, timing, feature switches | `Config.h` |
| Startup, boot animation, main loop, mode switching | `AIDeskCompanion.ino` |
| Button press counting (1/2/3/4 presses, long press) | `Buttons.cpp` |
| What each press does (next screen, lock, LED mode, ear mode) | `ScreenManager.cpp` (`handleShortPress`, `handleLongPress`, `handleDoublePress`, `handleTriplePress`) |
| Animation Display Mode on/off | `AIDeskCompanion.ino` (`toggleAnimationMode`) |
| List and order of screens, quotes | `ScreenManager.cpp` (`SCREEN_TABLE`, `QUOTES` near the top) |
| WiFi order, setup page, saved network | `WiFiManager.cpp` |
| Downloading and playing website animations | `CloudAnimation.cpp` |
| Draw Pad (local page, relay to Worker) | `DrawPadManager.cpp`, `DrawPadPage.h` |
| Face drawing and expressions | `Character.cpp` |
| Idle, sleep, reactions | `Animation.cpp` |
| LED patterns | `LEDManager.cpp` |
| Ear (servo) poses and movement | `EarManager.cpp` |
| Motion sensor (tap, shake, tilt...) | `MotionManager.cpp` |
| Built-in motion bitmap animations | `MotionAnimations.h` |
| Buzzer sounds | `Buzzer.cpp` |
| Clock, weather, AI message, reminders | `ClockManager.cpp`, `Weather.cpp`, `AI.cpp`, `Reminders.cpp` |

### Backend and website (`cloud/`)

| I want to find... | File |
|---|---|
| All API routes | `worker/index.js` |
| Token checks (admin and device) | `worker/index.js` (`requireAdmin`, `requireDevice`) |
| `.ino` reading and speed detection | `worker/parser/inoParser.js` |
| Saving to GitHub | `worker/storage/github.js` |
| OpenAI fallback | `worker/ai/analyze.js` |
| Draw Pad relay | `worker/index.js` (`DrawPadRelay`) |
| Worker name and settings | `wrangler.toml` |
| Website buttons and logic | `app.js` |
| Website look | `style.css` |
| Draw Pad page | `drawpad.html` |
| `.env` to firmware / Cloudflare scripts | `scripts/` |
| Website publishing | `../.github/workflows/pages.yml` |

---

## 16. How to change common things

After any change in `firmware/`, upload the firmware again from Arduino IDE.

**Change a pin** (`Config.h`)

| Pin | Line to edit |
|---|---|
| Button | `BUTTON_PIN` |
| Buzzer | `BUZZER_PIN` |
| OLED / MPU6050 I2C | `OLED_SDA_PIN`, `OLED_SCL_PIN` |
| LEDs | `LED_LEFT_PIN`, `LED_CENTER_PIN`, `LED_RIGHT_PIN` |
| Servos | `EAR_LEFT_SERVO_PIN`, `EAR_RIGHT_SERVO_PIN` |
| MPU6050 INT | `MPU6050_INT_PIN` |

Then rewire to match. Keep every pin different from the others.

**Turn a feature off** (`Config.h`, set to `false`): `ENABLE_LED_MODE`, `ENABLE_EAR_MODE`, `ENABLE_ANIMATION_MODE`, `MOTION_ENABLED`.

**Change button timing** (`Config.h`): `LONG_PRESS_MS` (how long to hold), `DOUBLE_PRESS_WINDOW_MS` (time allowed between quick presses).

**Change sleep time** (`Config.h`): `SLEEP_TIMEOUT_MS`.

**Change WiFi:** see [Section 9](#9-wifi-setup-and-how-to-change-wifi).

**Change servo behaviour** (`Config.h`)

- Ear angles: `EAR_LEFT_UPRIGHT_ANGLE`, `EAR_RIGHT_UPRIGHT_ANGLE` and the other `EAR_LEFT_*` / `EAR_RIGHT_*` angles (0 to 180).
- Pulse range: `EAR_SERVO_MIN_US`, `EAR_SERVO_MAX_US`.
- If a servo hits its end stop, lower the angle instead of forcing it.

**Change screens or their time** (`ScreenManager.cpp`, `SCREEN_TABLE`): each row is `{ screen, min time ms, max time ms, enabled }`. Set `enabled` to `false` to hide a screen, or reorder the rows.

**Change quotes** (`ScreenManager.cpp`, `QUOTES` list).

**Change reminders** (`Reminders.cpp`, in `begin()`): `addReminder(hour, minute, "text");` (24-hour time).

**Change time zone** (`Config.h`): `GMT_OFFSET_SEC` is in seconds. It is set to 19800 (India, UTC+5:30).

**Change weather city:** set `WEATHER_LOCATION` in `cloud/.env`, then `npm run gen:firmware` and re-upload.

**Add the AI-message key** (`Config.h`, `AI_API_KEY`): replace the placeholder. Do not commit or share the file after that.

**Change the API address** (new Worker URL)

1. Set `API_BASE` in `cloud/.env`, then `npm run gen:firmware` and re-upload the firmware.
2. In `cloud/app.js`, change the `API_BASE` line near the top (line 4).
3. In `cloud/drawpad.html`, change the `API_BASE` line (around line 203).
4. Commit and push so the website updates.

**Change the status check speed on the website** (`cloud/app.js`): `POLL_MS` (default 10000).

**Change how often the ESP32 asks for commands** (`CloudAnimation.cpp`): `COMMAND_POLL_MS`, `HEARTBEAT_MS`, `IDLE_HEARTBEAT_MS`.

**Change the OpenAI model** (`cloud/wrangler.toml`): `OPENAI_MODEL`. Then `npm run deploy`.

**Add or change animations:** upload an `.ino` on the website. Do not edit `animations/` by hand.

**Change the Draw Pad / setup WiFi name** (`WiFiManager.cpp`): `WIFI_SETUP_AP_SSID`, `WIFI_SETUP_AP_PASSWORD` (empty means open), `WIFI_SETUP_AP_AFTER_MS`.

---

## 17. Before you share this folder

This copy of the project may contain **real secrets**. Before giving it to another person, delete these files (they are git-ignored, but they are still inside the folder):

- `cloud/.env`
- `cloud/.dev.vars` (if it exists)
- `firmware/AIDeskCompanion/secrets.h`
- `cloud/esp32/animation_client/secrets.h`
- `firmware/AIDeskCompanion.zip` (this packed copy of the firmware **also contains a `secrets.h` file**; delete the zip, or remove `secrets.h` from inside it)
- `cloud/.wrangler/` and any `node_modules/` folders (not secret, just large)

Also check that `AI_API_KEY` in `Config.h` is still the placeholder.

Two more things:

- `animations/`, `animations.json` and `device/state.json` are the data of the **original owner's** website. A new person can keep them, but they will be replaced by their own data once their Worker uses their repo.
- If a token was ever shared by mistake, **make a new one** and replace it in `.env`, then run `npm run sync:secrets` (and `npm run gen:firmware` for the device token).

---

## 18. Common problems and fixes

| Problem | Fix |
|---|---|
| `npm ERR! package.json` | You are in the wrong folder. Run npm commands inside `cloud/`. |
| `Missing in .env: ...` | Fill that line in `cloud/.env`. |
| Compile error about `JsonDocument` | ArduinoJson is version 6. Install **version 7**. |
| Compile error: a library file is missing | Install every library in Section 4. |
| "Sketch too big" | Tools → Partition Scheme → **Huge APP (3MB No OTA/1MB SPIFFS)**. |
| Upload fails / "Connecting..." forever | Hold the BOOT button while uploading. Try another USB cable or port. |
| OLED stays black, Serial says "SSD1306 allocation failed" | Check SDA (21), SCL (22), 3.3V, GND. The address must be `0x3C`. |
| Status screen shows `MPU: ERROR` | Check the MPU6050 wires. AD0 must go to GND. SDA/SCL are shared with the OLED. |
| Ears twitch, shake or the ESP32 restarts | Servo power is too weak. Use a stronger supply and join the grounds. |
| WiFi never connects | Wrong name or password, or 5 GHz only. Use 2.4 GHz. Watch the Serial Monitor (115200): it prints why. |
| A phone hotspot does not connect | Turn the hotspot on, keep it awake nearby, use 2.4 GHz, check the exact spelling. |
| `AIDeskCompanion-Setup` WiFi never appears | It only starts after about 45 seconds with no connection. Switch off the old WiFi first. |
| Device joins an old network | A saved network wins over Home. Use "Forget saved Wi-Fi" or erase the flash (Section 9). |
| Website says **ESP32 Offline** but it is on | Check WiFi, then check that `secrets.h` was generated after `API_BASE` and `DEVICE_TOKEN` were set. Re-upload the firmware. |
| Website cannot load animations / Logs show network errors | `API_BASE` in `cloud/app.js` is not your Worker address. |
| Draw Pad button does nothing or fails | `API_BASE` in `cloud/drawpad.html` is not your Worker address, or the ESP32 is offline. |
| OLED says **No API config** | `secrets.h` is missing or has no API address. Run `npm run gen:firmware` and re-upload. |
| OLED says **Server unreachable** | Wrong `API_BASE`, wrong `DEVICE_TOKEN`, or no internet. The device keeps retrying. |
| OLED says **Pick an animation on the website** | Normal. Click **Play** or **Slideshow** on the website. |
| Mode Change button is greyed out | The ESP32 is offline, or the Draw Pad is open. Close the Draw Pad first. |
| `401 Unauthorized` from the Worker | `DEVICE_TOKEN` in the firmware does not match the one on Cloudflare. Run `npm run sync:secrets`, `npm run gen:firmware`, then re-upload. |
| Upload fails: "GitHub storage is not configured" or GitHub errors (401/403/404) | Check `GITHUB_TOKEN`, `GITHUB_OWNER`, `GITHUB_REPO`, `GITHUB_BRANCH`. The token needs **Contents: Read and write** on that repo. Then run `npm run sync:secrets`. |
| Upload fails: "Unsupported dimensions" | Only 128x64 animations work. |
| Upload fails: file too large | Max 2 MB per `.ino`. |
| Animation plays at the wrong speed | The speed could not be read from the file. Add `#define FPS 15` (or a `delay(67)` in `loop()`) to the `.ino`. |
| Weather or clock screens show errors | Check WiFi. For weather, check `WEATHER_API_KEY` and `WEATHER_LOCATION`. |
| AI message screen shows an error face | Normal until a real key is put in `AI_API_KEY` in `Config.h`. |
| Website changes do not show | Wait for the green run in the GitHub **Actions** tab, then refresh. Confirm Pages **Source** is **GitHub Actions**. |
| `npm run check:secrets` reports a problem | Do not push. Remove the secret from the file it names. If it was already pushed, replace the token. |

---

## 19. First-time setup checklist

**Accounts and tools**
- [ ] GitHub account, Cloudflare account
- [ ] Node.js 20+ installed (`node -v`)
- [ ] git installed
- [ ] Arduino IDE 2.x installed

**GitHub**
- [ ] New public repository created
- [ ] `origin` points to your repository
- [ ] Fine-grained token created (Contents: Read and write, only that repo)
- [ ] Pages Source set to **GitHub Actions**

**Secrets**
- [ ] `cd cloud && npm install`
- [ ] `.env` created from `.env.example`
- [ ] `ADMIN_TOKEN` and `DEVICE_TOKEN` generated (two different values)
- [ ] `GITHUB_TOKEN`, `GITHUB_OWNER`, `GITHUB_REPO`, `GITHUB_BRANCH` filled in
- [ ] WiFi name and password filled in (2.4 GHz)
- [ ] Optional keys filled in (OpenAI, weather) or left empty

**Cloudflare**
- [ ] `npx wrangler login`
- [ ] `npm run deploy` and Worker address copied
- [ ] `API_BASE` set in `.env`
- [ ] `npm run sync:secrets` done
- [ ] `npm run test:api` passes

**Website**
- [ ] Worker address updated in `cloud/app.js` **and** `cloud/drawpad.html`
- [ ] `npm run check:secrets` says no secrets found
- [ ] Changes committed and pushed; Actions run is green
- [ ] Website opens at `https://YOUR_USER.github.io/YOUR_REPO/`

**Hardware**
- [ ] OLED, MPU6050, button, buzzer, 3 LEDs with resistors, 2 servos wired as in Section 5
- [ ] All grounds joined; servos have stable power

**Firmware**
- [ ] ESP32 board package and all 7 libraries installed (ArduinoJson **v7**)
- [ ] `npm run gen:firmware` run
- [ ] Board: ESP32 Dev Module, Partition: Huge APP, correct port
- [ ] Firmware uploaded; Serial Monitor open at 115200

**Final test**
- [ ] Boot animation and face appear
- [ ] Short press changes the screen
- [ ] Website shows **ESP32 Online**
- [ ] `sample-eye.ino` uploaded and visible with a preview
- [ ] 4 presses (or **Mode Change**) enters Animation Display Mode
- [ ] **Play** shows the animation on the OLED
- [ ] **Draw Pad** opens and drawing appears on the OLED

---

## 20. Notes about the older documents in this folder

This README was written by reading the actual code. A few older files say slightly different things. If they disagree with this README, follow this README:

- **`SETUP.md` says `cloud/` is its own GitHub repo** and that Pages should use "Deploy from a branch". The real project uses the whole folder as one repo, and `pages.yml` publishes `cloud/`, so Pages **Source** must be **GitHub Actions**.
- **`SETUP.md` says to keep the default partition scheme.** `arudio_library.txt` (the author's notes) says to use **Huge APP (3MB No OTA/1MB SPIFFS)**. Use Huge APP.
- **`SETUP.md` only mentions `app.js` for the Worker address.** `drawpad.html` also has one. Change both.
- **`cloud/animations.json` and `cloud/device/state.json`** are not the live data. The Worker uses `animations.json` and `device/state.json` at the **top** of the repo.
- **`cloud/esp32/animation_client/`** is an older standalone animation player. The main firmware already includes this feature, so you do not need it.
- **`firmware/AIDeskCompanion/README.md`** has much more detail about the character, motion thresholds and enclosure.
- **`cloud/README.md`** has more detail about the API and the animation file format.
- **`command.txt`** is just the author's personal notes (it is not needed for setup).
- **`firmware/AIDeskCompanion.zip`** is a packed copy of the firmware folder. It is not needed for setup, and it contains a `secrets.h` (see Section 17).