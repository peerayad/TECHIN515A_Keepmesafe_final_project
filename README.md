# KeepMeSafe — GuardianPod Room Security System

An embedded AI-powered room security system built on the Seeed XIAO ESP32-S3. It detects intruders using a PIR sensor + camera + on-device ML inference (Edge Impulse), then sends an alert with a rotated image to Firebase Realtime Database. A live web dashboard displays alerts in real time.

---

## System Architecture

```
[PIR Sensor] → [ESP32-S3] → [ML Inference (Edge Impulse)]
                   │                      │
              [OV2640 Camera]        Person detected?
                                          │
                              ┌───────────┴───────────┐
                           WiFi ON                 WiFi OFF
                              │                        │
                       [Firebase RTDB]            [SD Card]
                              │
                       [Web Dashboard]
                    keepmesafe-1e248.web.app
```

---

## Hardware Requirements

| Component | Model | Pin |
|---|---|---|
| Microcontroller | Seeed XIAO ESP32-S3 | — |
| Camera | OV2640 (built-in) | — |
| PIR Sensor | Any 3.3V PIR | D0 |
| Buzzer | Active/Passive buzzer | D3 |
| Button A | Tactile button | D6 |
| Button B | Tactile button | D7 |
| OLED Display | SSD1306 128×64 I2C | SDA=D4, SCL=D5 |
| SD Card Module | SPI | CS=D21 |

---

## Software Requirements

### Firmware (ESP32)
- [PlatformIO](https://platformio.org/) with VS Code extension
- Arduino framework for ESP32

### Libraries (auto-installed via `platformio.ini`)
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `WiFiManager @ ^2.0.17`
- `FirebaseClient @ ^1.5.0`

### Manual Library (place in `/lib`)
- `ML_Camera_inferencing` — Edge Impulse exported library (person detection model)

---

## Firebase Setup

1. Go to [Firebase Console](https://console.firebase.google.com) and create a project (or use existing `keepmesafe-1e248`)
2. Enable **Realtime Database** (start in test mode)
3. Set database rules to allow read/write:
```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```
4. Copy your Firebase config values into `src/main.cpp`:
```cpp
#define FIREBASE_DATABASE_URL "https://your-project-default-rtdb.firebaseio.com"
#define ROOM_ID               "room304"
```

---

## Project Structure

```
keepmesafe_SIT/
├── src/
│   └── main.cpp          # Main firmware
├── lib/
│   └── ML_Camera_inferencing/  # Edge Impulse model library
├── test/
│   └── index.html        # Web dashboard
├── platformio.ini         # PlatformIO config
└── README.md
```

---

## How to Run the Firmware

### 1. Clone / open the project
Open the project folder in VS Code with PlatformIO installed.

### 2. Install dependencies
PlatformIO will auto-install all libraries listed in `platformio.ini` on first build.  
Make sure the Edge Impulse `ML_Camera_inferencing` library is placed in `/lib`.

### 3. Build & Upload
```bash
# Build only
pio run

# Build and upload to device
pio run --target upload

# Open Serial Monitor
pio device monitor --baud 115200
```
Or use the PlatformIO buttons in VS Code (✓ Build / → Upload).

### 4. First Boot — Choose Mode
On startup the OLED will show:
```
Connect WiFi?
A: Connect WiFi
B: SD Card mode
Waiting...
```
- Press **A** → connects to saved WiFi (or opens WiFiManager portal `KeepMeSafe` / password `12345678`)
- Press **B** → offline SD Card mode

---

## How to Run the Web Dashboard

### Option 1 — Local (quick test)
```bash
# In the test/ folder
cd test
python3 -m http.server 8080
```
Then open `http://localhost:8080`

### Option 2 — Firebase Hosting (public URL)
```bash
# Install Firebase CLI
npm install -g firebase-tools

# Login
firebase login

# From project root
firebase init hosting
# → Select project: keepmesafe-1e248
# → Public directory: test
# → Single-page app: No
# → Overwrite index.html: No

# Deploy
firebase deploy --only hosting
```
Live at: `https://keepmesafe-1e248.web.app`

---

## Detection Pipeline

```
PIR triggered
    ↓
Confirm: ≥ 3 triggers in 3 seconds
    ↓
Capture 3 frames (100ms apart)
    ↓ per frame:
  decode JPEG → RGB888
  rotate 90° CCW (portrait)
  resize to EI input size
  auto-contrast normalization
  contrast boost (×1.3)
  Edge Impulse classifier
    ↓
Majority vote: ≥ 2/3 = PERSON
    ↓
Rotate final frame 90° CCW
    ↓
WiFi? → Firebase (base64 JPEG + metadata)
No WiFi? → SD Card (JPEG file)
    ↓
ALARM! — River Flows in You
Press B to cancel
```

---

## Button Controls

| Button | Context | Action |
|---|---|---|
| **A** | Startup | Connect WiFi |
| **B** | Startup | SD Card mode |
| **A** | Standby | Open Settings menu |
| **A** | Settings | Select / navigate |
| **B** | Settings | Next item / exit |
| **B** | Alarm | Cancel alarm immediately |

### Settings Menu
```
SETTINGS
> Mode        ← A to enter
  Sound
A:Select  B:Next/Exit
```
- **Mode** — Switch between WiFi and SD Card mode
- **Sound** — Toggle buzzer ON/OFF

---

## Web Dashboard Features

| Tab | Description |
|---|---|
| **Home** | Live status (SECURED/ALERT), latest snapshot, alert number, time |
| **History** | Last 20 alerts as gallery — click to view full image + time |

---

## Key Configuration

All in `src/main.cpp`:

```cpp
#define PERSON_THRESHOLD  0.5f    // ML confidence threshold (0.0–1.0)
#define CAPTURE_COOLDOWN  10000   // ms between detections
#define ROOM_ID           "room304"

const float alpha = 1.3f;        // Contrast boost multiplier
// personVotes >= 2               // Votes needed out of 3
```

---

## Troubleshooting

| Issue | Cause | Fix |
|---|---|---|
| OLED upside down | Enclosure orientation | `display.setRotation(2)` already applied |
| Firebase not connecting | Rules expired | Set rules `.read/.write = true` |
| Website shows no data | Opened as `file://` | Serve via localhost or Firebase Hosting |
| Image sideways on website | Rotation mismatch | 90° CCW applied automatically in firmware |
| Alert numbers jumping | PIR false triggers | Cooldown reset after each detection |
| Alarm won't stop | — | Press Button B |

---

## License

Built for SIT (School of Information Technology) project — GuardianPod v1.0
