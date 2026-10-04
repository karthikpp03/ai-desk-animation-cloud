# Setup from scratch (new person / new laptop)

Follow top to bottom. Total time: about 30–40 minutes. Every secret is typed **once**, into `cloud/.env`.

```
AIDeskCompanion-Full/
├── cloud/       website + Cloudflare Worker + GitHub animation storage (its own GitHub repo)
└── firmware/    AIDeskCompanion ESP32 sketch (flashed with Arduino IDE)
```

## 0. What you need

**Accounts (all free tiers work):**
- GitHub
- Cloudflare (for the Worker)
- OpenAI API key — *optional*, only used when the built-in parser cannot read an `.ino` file

**Software on the laptop:**
- Node.js 20 or newer (`node -v`) and npm
- git
- Arduino IDE 2.x

**Hardware:** ESP32 Dev Module, SSD1306 128x64 OLED (I2C), push button, active buzzer, 3 LEDs, 2 SG90 servos, MPU6050.
Pins are in `firmware/AIDeskCompanion/Config.h` (OLED SDA 21 / SCL 22, button 19, buzzer 18, LEDs 25/26/27, servos 32/33, MPU6050 INT 23).

## 1. Create the GitHub repo for the cloud part

1. On github.com create a new repo, e.g. `ai-desk-animation-cloud` (public is simplest: free GitHub Pages needs it).
2. Create a **fine-grained token**: GitHub → Settings → Developer settings → Fine-grained tokens → *Generate new token*.
   - Repository access: only that repo
   - Permissions: **Contents → Read and write**
   - Copy the token (you will paste it into `.env` in step 2).

## 2. Fill in `cloud/.env`

```bash
cd cloud
npm install
cp .env.example .env
openssl rand -hex 32     # run twice: one value for ADMIN_TOKEN, one for DEVICE_TOKEN
nano .env                # fill in everything except API_BASE for now
```

| Variable | What to put |
|---|---|
| `ADMIN_TOKEN` | random string you generated (you type it into the website) |
| `DEVICE_TOKEN` | a different random string (baked into the ESP32) |
| `GITHUB_TOKEN` | the fine-grained token from step 1 |
| `GITHUB_OWNER` / `GITHUB_REPO` / `GITHUB_BRANCH` | your GitHub username, the repo name, `main` |
| `OPENAI_API_KEY` | optional; leave empty to skip |
| `WIFI_SSID` / `WIFI_PASSWORD` | the Wi-Fi the ESP32 will join (2.4 GHz) |
| `WEATHER_API_KEY` / `WEATHER_LOCATION` | optional, free key from openweathermap.org |
| `API_BASE` | fill in after step 4 |

## 3. Push `cloud/` to GitHub and turn on the website

Run inside `cloud/`:

```bash
git init
git branch -M main
git add .
git commit -m "Initial animation cloud"
git remote add origin https://github.com/karthikpp03/ai-desk-animation-cloud.git
git push -u origin main
npm run check:secrets      # must say "No secrets found in tracked files"
```

Then on GitHub: repo → Settings → Pages → *Deploy from a branch* → `main` / `/ (root)`.
Your website will be at `https://YOUR_USER.github.io/YOUR_REPO/`.

## 4. Deploy the Worker

```bash
npx wrangler login         # opens the browser; approve
npm run deploy             # first run may ask you to register a workers.dev subdomain
```

`deploy` prints the Worker URL: `https://ai-desk-animation-api.<your-subdomain>.workers.dev`.

1. Put that URL in `.env` as `API_BASE`.
2. Upload the secrets to Cloudflare:

```bash
npm run sync:secrets -- --dry-run     # lists names only
npm run sync:secrets
npm run test:api                      # live check: 401 without a token, 200 with it
```

## 5. Add your first animation

1. Open the website, expand **Connection settings**, enter the Worker URL and your `ADMIN_TOKEN`.
2. Upload `cloud/sample-eye.ino` (14-frame test animation). It should appear in the list.

## 6. Generate the firmware secrets

```bash
npm run gen:firmware
```

This writes `firmware/AIDeskCompanion/secrets.h` (Wi-Fi, Worker URL, device token, optional weather key).
Re-run it whenever `.env` changes, then re-flash.

## 7. Flash the ESP32

1. Arduino IDE → Settings → *Additional boards manager URLs*:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   then Boards Manager → install **esp32 by Espressif**.
2. Library Manager → install: **Adafruit GFX Library**, **Adafruit SSD1306**, **ESP32Servo**, **ArduinoJson (v7)**.
3. Open `firmware/AIDeskCompanion/AIDeskCompanion.ino`.
4. Tools → Board: **ESP32 Dev Module**; pick the USB port; keep the default partition scheme.
5. Upload. Open Serial Monitor at **115200**.

## 8. Use it

| Press | Function |
|---|---|
| 1 | next screen |
| 2 | LED mode |
| 3 | ear mode |
| long | lock / unlock |
| **4** | **enter / exit Animation Display Mode** |

In Animation Display Mode, choose **Play** or **Slideshow** on the website. The ESP32 polls the Worker, downloads the animation and loops it on the OLED. Press 4 times again to return to the normal screens.

## Handing this to someone else

- Give them this whole folder **without** `cloud/.env`, `secrets.h` or any tokens. They repeat steps 1–7 with their own accounts.
- Or to move to a new laptop with the same accounts: copy the folder, run `npm install` in `cloud/`, and bring `cloud/.env` over securely (never by email or chat). Then run `npm run gen:firmware` and flash.
- The `.env` file, `secrets.h` and `.dev.vars` are git-ignored on purpose. Never commit them.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `npm ERR! package.json` | you are in the wrong folder; run npm commands inside `cloud/` |
| `Missing in .env: ...` | fill that variable; values starting with `your`/`replace` are treated as empty |
| Website says "Set the Worker API URL first" | enter the Worker URL in Connection settings |
| Upload on the website returns 401 | `ADMIN_TOKEN` in the page differs from the one uploaded with `sync:secrets` |
| OLED says "No API config" | `secrets.h` is missing; run `npm run gen:firmware` and re-flash |
| OLED says "Server unreachable" | wrong `API_BASE`, wrong `DEVICE_TOKEN`, or ESP32 not on Wi-Fi |
| OLED says "Pick an animation on the website" | click Play or Slideshow on the website |
| Compile error about `JsonDocument` | ArduinoJson is v6; install v7 |
| Wi-Fi never connects | SSID/password typo, or the network is 5 GHz only |
