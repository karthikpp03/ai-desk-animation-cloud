# AI Desk Companion — GitHub Animation Cloud V2

Standalone cloud animation system for an ESP32 + SSD1306 128x64 OLED.

This version replaces the previous Cloudflare R2 dependency with a GitHub repository. The browser talks only to the Cloudflare Worker. GitHub tokens and the OpenAI key stay inside Worker secrets.

## Architecture

```text
Browser
   ↓
GitHub Pages (this repository)
   ↓
Cloudflare Worker
   ├── GitHub Contents API
   └── OpenAI Responses API (fallback only)
   ↓
GitHub repository
   ↓
ESP32 client
   ↓
128x64 SSD1306 OLED
```

The PC does not need to remain powered on after deployment.

### What changed from V1

- Removed Cloudflare R2 as animation storage.
- Removed the R2 device-state store.
- GitHub is now the source of truth for `animations.json`, normalized animation files, device command state, and optional import-failure history.
- The Worker remains the backend/API bridge.
- The existing deterministic parser and validator were reused.
- OpenAI is still a fallback only when deterministic parsing fails.
- The standalone ESP32 client is kept separate from the existing AIDeskCompanion V5 firmware.

GitHub's repository Contents API supports creating/updating repository files with Base64 content and requires the file SHA when replacing an existing file. GitHub also recommends serializing content-file writes because concurrent updates can conflict. This project therefore writes animation files first and updates `animations.json` last.

## Repository structure

```text
animation-project/
├── index.html
├── app.js
├── style.css
├── animations.json
├── failed.json
├── device/
│   └── state.json
├── animations/
│   └── <animation-id>/
│       ├── metadata.json
│       ├── frames.bin
│       └── original.ino       # optional, disabled by default
├── worker/
│   ├── index.js
│   ├── ai/analyze.js
│   ├── parser/inoParser.js
│   ├── storage/github.js
│   └── validator/animation.js
├── esp32/
│   └── animation_client/animation_client.ino
├── sample-eye.ino
├── sample-normalized/
└── README.md
```

The GitHub Pages site is the repository root, so no separate web hosting folder is needed.

## Normalized animation format

Every imported animation becomes:

- width: `128`
- height: `64`
- monochrome: `1-bit`
- bytes per frame: `1024`
- frame count
- FPS
- frame duration
- loop flag

Example:

```json
{
  "id": "cute_cat-7a2f5f7a1c2d",
  "name": "cute_cat",
  "width": 128,
  "height": 64,
  "frames": 120,
  "fps": 8,
  "loop": true,
  "format": "raw-ssd1306-row-major-1bpp-v1"
}
```

`frames.bin` is sequential raw frame data:

```text
frame 0 = 1024 bytes
frame 1 = 1024 bytes
frame 2 = 1024 bytes
...
```

No executable Arduino code is used as the playback format.

## Import flow

1. Browser uploads an `.ino` to `POST /api/animations/upload`.
2. Worker validates extension and size.
3. Worker hashes the source with SHA-256.
4. If the same source was already imported, the existing animation is returned instead of creating a duplicate.
5. Worker tries deterministic parsing first.
6. It extracts actual bitmap byte arrays, dimensions, timing, and frame count.
7. It validates every frame as exactly 1024 bytes.
8. If deterministic parsing fails, the Worker calls OpenAI.
9. OpenAI returns structured hints only: frame-array names, dimensions, timing, loop information, and convertibility.
10. The Worker extracts the actual bytes itself and runs the same validator. AI output never supplies fabricated frame bytes.
11. Worker writes `metadata.json` and `frames.bin` to GitHub.
12. Optionally it stores the original `.ino` when `STORE_ORIGINAL_INO=true`.
13. Worker updates `animations.json` last.
14. Browser refreshes the list automatically.

If conversion fails, nothing is added to `animations.json` and the failure is recorded in `failed.json` when possible.

The OpenAI fallback uses the Responses API. OpenAI's current documentation recommends Responses API for new integrations.

## GitHub setup

Create a GitHub repository and keep it as the source of truth for this project.

For a fine-grained GitHub token, grant the repository **Contents: Read and write** permission. GitHub documents that the Contents permission is sufficient for the create/update file endpoint.

Then push this project to the repository.

Example:

```bash
git init
git branch -M main
git add .
git commit -m "Initial GitHub animation cloud"
git remote add origin https://github.com/YOUR_USER/YOUR_REPO.git
git push -u origin main
```

## GitHub Pages

Enable GitHub Pages for the repository and select:

```text
Source: Deploy from a branch
Branch: main
Folder: / (root)
```

The site will serve `index.html` from the repository root. The Worker URL is the `API_BASE` constant at the top of `app.js` (edit that one line to point at a different Worker). The page has no settings panel and no admin code.

## Cloudflare Worker setup

Install Wrangler if necessary:

```bash
npm install
```

For local development, copy `.dev.vars.example`:

```bash
cp .dev.vars.example .dev.vars
```

Fill in the GitHub repository values and local secrets.

Run locally:

```bash
npm run dev
```

Deploy:

```bash
npm run deploy
```

## Configuration and secrets (.env)

Every token is entered **once**, in a local `.env` file that is git-ignored:

```bash
cp .env.example .env     # then fill in the values
```

| Variable | Used by | Where it ends up |
| --- | --- | --- |
| `ADMIN_TOKEN` | Worker, `scripts/api-test.mjs` (only guards `DELETE /api/animations/:id`; the website does not use it) | Cloudflare secret |
| `DEVICE_TOKEN` | Worker, ESP32 | Cloudflare secret, firmware build |
| `GITHUB_TOKEN` | Worker only | Cloudflare secret |
| `OPENAI_API_KEY` | Worker only | Cloudflare secret |
| `GITHUB_OWNER` / `GITHUB_REPO` / `GITHUB_BRANCH` | Worker | Cloudflare variables |
| `API_BASE` | scripts, ESP32 | firmware build |
| `WIFI_SSID` / `WIFI_PASSWORD` | ESP32 | firmware build |

```text
.env  ->  npm run sync:secrets   ->  Cloudflare Worker secrets
.env  ->  npm run gen:devvars    ->  .dev.vars   (for `wrangler dev`)
.env  ->  npm run gen:firmware   ->  esp32/animation_client/secrets.h  ->  firmware you flash
```

`npm run sync:secrets -- --dry-run` lists what would be uploaded. Values are sent to Wrangler over stdin and are never printed.
`npm run check:secrets` confirms `.env`/`secrets.h`/`.dev.vars` are ignored and no secret value appears in any committable file.

`GITHUB_TOKEN` and `OPENAI_API_KEY` exist only in `.env` (on your PC) and as Cloudflare secrets. They are never sent to the browser or the ESP32.
The website is a static site and holds no tokens. Upload, play, stop, slideshow and mode change are intentionally public (no authentication).

`OPENAI_MODEL`, `STORE_ORIGINAL_INO`, and the other non-secret values can be configured in `wrangler.toml` or the Worker environment. The provided config defaults to `gpt-5-mini` and does not enable original `.ino` storage.

**Never put `GITHUB_TOKEN` or `OPENAI_API_KEY` in `app.js`.**

## API

```text
GET  /api/animations
POST /api/animations/upload
GET  /api/animations/:id
GET  /api/animations/:id/metadata
GET  /api/animations/:id/frames
POST /api/animations/:id/play
POST /api/animations/stop
POST /api/slideshow/start
POST /api/slideshow/stop
GET  /api/import-failures
GET  /api/device/status
GET  /api/device/command
POST /api/device/ack
POST /api/device/heartbeat   (ESP32, any mode: liveness + pending mode request)
POST /api/device/mode        (website Mode Change button)
GET  /api/animations/:id/preview   (website thumbnails; same frames.bin the ESP32 plays)
DELETE /api/animations/:id
```

The exact API can be extended later, but V2 keeps the control surface intentionally small.

## Device command flow

A browser cannot normally open an inbound connection to an ESP32 behind a home router/NAT. The device therefore polls the Worker.

```text
Browser
  ↓ POST /api/animations/:id/play
Worker
  ↓ writes device/state.json in GitHub
ESP32
  ↓ GET /api/device/command
Worker
  ↓ selected animation ID
ESP32
  ↓ GET /api/animations/:id/metadata
Worker → GitHub
  ↓
ESP32
  ↓ GET /api/animations/:id/frames
Worker → GitHub
  ↓
LittleFS cache
  ↓
SSD1306 OLED
```

The Worker proxies animation bytes from GitHub so the ESP32 never receives the GitHub token.

The ESP32 only downloads the selected animation to LittleFS. It does not permanently store the complete animation library.

## Device state and GitHub commit rate

The old R2 implementation wrote device state during every command poll. That would be a poor fit for GitHub because every write creates repository history.

V2 avoids this:

- `/api/device/command` is read-only.
- Browser commands write `device/state.json` only when a command changes.
- `/api/device/ack` and `/api/device/heartbeat` persist `lastSeenAt` only when it is older than 40 s or the mode/animation changed (each persist is one GitHub commit). The ESP32 counts as online while `lastSeenAt` is less than 100 s old.
- The ESP32 can poll every second without generating a Git commit every second.

## Security

Implemented protections include:

- `.ino` extension validation
- upload size limit
- converted animation size limit
- filename sanitization
- stable source-content IDs
- duplicate detection
- 128x64 validation
- 1024-byte frame validation
- frame-count limit
- FPS validation
- structured AI response validation
- GitHub token isolation
- OpenAI key isolation
- device/admin token checks
- no execution of uploaded Arduino code

The website and its write routes are intentionally open: anyone who can reach the URL can upload animations (which can spend OpenAI credits), start/stop playback and switch the ESP32's mode. This is not authentication. If that becomes a problem, put the site and Worker behind an access layer such as Cloudflare Access.

## GitHub storage limits for this project

The expected individual `frames.bin` files are small enough for the intended library. The project also enforces its own converted-animation limit before writing to GitHub.

Avoid duplicate uploads because GitHub stores each committed file in repository history. Stable IDs and source hashing prevent the application from creating duplicate animation entries.

## ESP32 setup

Open:

```text
esp32/animation_client/animation_client.ino
```

Configure (no secrets in source): fill in `.env`, then generate the git-ignored header the sketch includes:

```bash
npm run gen:firmware      # writes esp32/animation_client/secrets.h from .env
```

Re-run it whenever a value in `.env` changes, then re-flash. `secrets.h.example` shows the format.

Wiring: SDA = GPIO21, SCL = GPIO22, I2C at 400 kHz.

Install:

- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoJson

Flash this standalone firmware to the test ESP32 first. It does **not** modify or depend on the existing AIDeskCompanion V5 firmware.

## Playback behavior

Normal play:

```text
frame 0 → frame 1 → ... → final frame → frame 0 → ...
```

Selecting another animation causes the current playback to stop and the new animation to download/load.

Slideshow:

```text
Animation A complete loop
        ↓
Animation B complete loop
        ↓
Animation C complete loop
        ↓
Animation A ...
```

Frame timing is driven by `millis()` in the standalone client so OLED playback is not implemented as a long blocking delay loop.
All networking (polling, heartbeat, downloads) runs in a separate FreeRTOS task on core 0; `loop()` on core 1 only draws frames, so a slow HTTPS request can never freeze the animation. A new animation downloads while the old one keeps playing and is swapped in between two frames.

### Timing detection (`worker/parser/inoParser.js`)

The parser reads the frame interval from the sketch, in this order, and records which rule matched in `metadata.timingSource`:

1. `millis()` interval: `if (millis() - lastMs >= 67)`, `now - last >= INTERVAL`, `next = millis() + 67`
2. `delay(...)` in `loop()` or helpers (never in `setup()`): literals, constants, `1000 / FPS`
3. FPS constants (`#define FPS 15`)
4. Frame-duration variables (`frameDelay`, `FRAME_INTERVAL`, ...)
5. Only if none match: 100 ms (10 FPS), flagged as `default (...)`.

Frame order follows the sketch's pointer table (`const uint8_t* frames[] = {...}`) when present, otherwise `frame0, frame1, ...`.
Already-imported animations keep their stored metadata (uploads are de-duplicated by hash); delete and re-upload one to re-detect its timing.

## Test checklist

### Local parser test

The included `sample-eye.ino` contains 14 frames of exactly 1024 bytes each and uses a 100 ms frame interval (10 FPS). It should convert deterministically without OpenAI.

### End-to-end

1. Push the project to GitHub.
2. Enable GitHub Pages.
3. Deploy the Worker.
4. Configure Worker secrets.
5. Put the Worker URL into the website settings.
6. Put the device token and Worker URL into the standalone ESP32 firmware.
7. Start the ESP32.
8. Open the Pages website.
9. Upload `sample-eye.ino`.
10. Confirm the animation appears automatically.
11. Click **Play**.
12. Confirm the ESP32 downloads `frames.bin` and displays it.
13. Upload the same `.ino` again and confirm it is detected as a duplicate.
14. Test Stop.
15. Test Slideshow.
16. Test a deliberately unsupported `.ino` and confirm a clear conversion failure.

## Scope boundary

This repository is intentionally a standalone animation system.

Do **not** merge it into the existing AIDeskCompanion V5 firmware yet. MPU6050, servos, LEDs, buzzer, Telegram, weather, reminders, and existing screens remain untouched. Integration can happen after this standalone cloud animation pipeline is proven.


## Smoke test after deployment

From the project directory:

```bash
npx wrangler deploy
curl https://ai-desk-animation-api.YOUR-SUBDOMAIN.workers.dev/api/animations
```

A fresh repository should return:

```json
{"animations":[]}
```

Then open the GitHub Pages site, and upload `sample-eye.ino`. A successful import should create `animations/<id>/metadata.json`, `animations/<id>/frames.bin`, and update `animations.json`.

## Tests

```bash
npm run test:parser     # parses sample-eye.ino and oled_display.ino, checks frames/size/order/timing, plus timing patterns
npm run test:worker     # runs the real Worker in-process against a fake GitHub: auth, upload, play, stop, no secret leaks
npm run check:secrets   # .env ignored, no secrets in committable files
npm run test:api        # live: checks the deployed Worker's 401/200 behaviour using tokens from .env
node scripts/api-test.mjs upload animations/oled_display.ino   # also: list | status | command | play <id> | stop
```
