# ESP32 bus nodes

Firmware for the two ESP32 devices that ride on every bus. The server and the phone app are separate projects; this repo only talks to them.

- [docs/phone.md](docs/phone.md): what the phone app receives over BLE and what it sends to the server.
- [docs/server.md](docs/server.md): the HTTP endpoints the server implements for these devices.

Both are written for teams who do not have this code. This README is for whoever works on the firmware.

## Contents

1. [What the system does](#what-the-system-does)
2. [Hardware](#hardware)
3. [One-time setup on your PC](#one-time-setup-on-your-pc)
4. [Configure a bus](#configure-a-bus)
5. [Build and flash](#build-and-flash)
6. [First boot: what you should see](#first-boot-what-you-should-see)
7. [Run against the mock server](#run-against-the-mock-server)
8. [The GPS simulator](#the-gps-simulator)
9. [Serial console](#serial-console)
10. [Tests](#tests)
11. [Configuration reference](#configuration-reference)
12. [How it works](#how-it-works)
13. [Common tasks](#common-tasks)
14. [Troubleshooting](#troubleshooting)
15. [Project layout](#project-layout)
16. [Known limitations](#known-limitations)

## What the system does

```
                    BLE broadcast
  Node A (beacon) ----------------> Phone app
        |                               |
        | Wi-Fi: GPS every 2 s          | internet: "user X heard bus 420"
        v                               v
     +-----------------------------------------+
     |                 Server                  |  decides who rides and who pays
     +-----------------------------------------+
        ^
        | Wi-Fi: cabin photo every 1-60 s
        |
  Node B (camera)
```

**Node A** (a plain ESP32 dev board) does two jobs:

- Broadcasts a BLE iBeacon carrying the route id and bus id, plus a token that changes every 30 seconds. A phone near the bus forwards that token to the server, which proves the phone was near this bus at that moment. Phones never connect to the beacon.
- Sends its GPS position to the server every 2 seconds. The position comes either from a real GPS module or from a built-in simulator that drives a virtual bus along a route.

**Node B** (an ESP32-CAM-MB) takes a JPEG of the inside of the bus and uploads it. The server counts the people in it and sets how often the camera shoots.

Neither device knows anything about users. All decisions are made on the server.

### Build targets

| Target | Board | What it is |
| --- | --- | --- |
| `node_a_sim` | ESP32 dev board | Beacon + **simulated** GPS. Default target; no GPS module needed. |
| `node_a_gps` | ESP32 dev board + GPS module | Beacon + **real** GPS |
| `node_b_cam` | ESP32-CAM-MB (AI-Thinker) | Camera |
| `native` | your PC | Logic unit tests only, no board |

## Hardware

### Node A

- Any ESP32 dev board with the classic ESP32 chip (ESP32-WROOM-32 / "ESP32 DevKit V1"). The target uses board `esp32dev`.
- A USB data cable (charge-only cables are a common cause of "no port found").
- For `node_a_gps` only: a NEO-6M or NEO-M8N GPS module.

GPS wiring (UART2, 9600 baud by default):

| GPS module | ESP32 |
| --- | --- |
| TX | GPIO 16 (ESP32 RX) |
| RX | GPIO 17 (ESP32 TX) |
| VCC | 3.3 V or 5 V, as printed on the module |
| GND | GND |

TX goes to RX and RX to TX. The pins can be changed in `env/config.ini` under `[pins]`. A GPS module needs a clear view of the sky; the first fix can take several minutes. Indoors it may never get one; use `node_a_sim` there.

The BOOT button (GPIO 0) on the dev board controls the simulator: a short press starts or pauses it, a press longer than 1 second switches to the next scenario.

### Node B

- An AI-Thinker ESP32-CAM with its ESP32-CAM-MB USB adapter board, and the OV2640 camera that comes with it.
- The board needs PSRAM for resolutions above QVGA (320×240). Without PSRAM it falls back to QVGA automatically.
- Power it from a good USB port or a 5 V supply. Weak power causes brownout resets as soon as the camera or Wi-Fi starts.
- The bright white LED is the flash on GPIO 4. It is off unless `flash = true` in `[camera]`.

## One-time setup on your PC

1. Install [PlatformIO](https://platformio.org/): either the PlatformIO extension in VS Code or Cursor, or the `pio` command line (`pip install platformio`).
2. Open this folder (the one containing `platformio.ini`) as the project.
3. The first build downloads the ESP32 toolchain and libraries, a few hundred MB, into `%USERPROFILE%\.platformio`. This happens once.
4. For the PC unit tests you also need `g++` on `PATH` (MinGW-w64 or MSYS2 on Windows).
5. For the mock server you need Python 3 with Flask: `pip install -r tools\requirements.txt`.

## Configure a bus

Each bus has one Node A and one Node B. Both read their settings from files you flash with them. Settings live in four files under `env/`:

| File | Contents | In git? |
| --- | --- | --- |
| `env/node_a/.env` | Node A's Wi-Fi, server address and secret | **No**, it holds secrets |
| `env/node_b/.env` | Node B's Wi-Fi, server address and secret | **No**, it holds secrets |
| `env/config.ini` | Bus id, route id, beacon settings, pins, camera settings. Shared by both nodes. | Yes |
| `env/node_a/route.json` | Cached route for this bus. Both Node A builds refresh it from the server at boot. The simulator drives it. | Yes |
| `env/node_a/scenarios.json` | How the simulated bus behaves (Node A only) | Yes |

### 1. Create one `.env` per device

```powershell
copy env\.env.example env\node_a\.env
copy env\.env.example env\node_b\.env
```

Edit each file:

```ini
WIFI_SSID="my-hotspot"
WIFI_PASS="my-password"
SERVER_BASE_URL="http://192.168.1.50:8000"
DEVICE_HMAC_SECRET="<64 hex characters>"
```

- **Wi-Fi** must be 2.4 GHz. The ESP32 cannot see 5 GHz networks. A phone hotspot works; set it to 2.4 GHz or "maximize compatibility". The two devices can use different networks.
- **`SERVER_BASE_URL`** is the address of the server **as seen from that ESP32**. `localhost` or `127.0.0.1` never works, because that would be the ESP32 itself. Use the PC's LAN IP (`ipconfig` → IPv4 address) when running the mock server, or the real server's URL. No trailing slash needed.
- **`DEVICE_HMAC_SECRET`** is this device's own secret. Generate a different one for each device:

  ```powershell
  python -c "import secrets; print(secrets.token_hex(32))"
  ```

  Node A signs its telemetry with its secret and also builds the BLE token from it. Node B only signs its photo uploads. Give both secrets to the server team, each tied to that device's id. A secret is never sent over the network; both sides must already have it.

### 2. Set the bus and route in `env/config.ini`

```ini
[identity]
route_id = 7
bus_id   = 420
```

Both are numbers from 0 to 65535. The server owns their meaning (route 7 is called "E2", bus 420 is a given vehicle). Use the same values on Node A and Node B of a bus. The other sections can stay as they are; see the [configuration reference](#configuration-reference).

## Build and flash

Plug in the board, then from this folder. Replace `node_a_sim` with `node_a_gps` or `node_b_cam` for the other targets.

```powershell
pio run -e node_a_sim                  # 1. compile (no board needed)
pio run -e node_a_sim -t upload        # 2. flash the firmware
pio run -e node_a_sim -t uploadfs      # 3. flash the files from env/
pio device monitor -e node_a_sim       # 4. watch the serial output (Ctrl+C to quit)
```

The firmware (step 2) and the files (step 3) are separate. If you change anything under `env/`, run only `uploadfs` again. If you change code, run only `upload`. A board that never got `uploadfs` boots with built-in defaults and no Wi-Fi.

Before `uploadfs`, a script (`tools/prepare_fs.py`) copies the right files for the target into `.pio/fsdata/<target>/`. It also stops the build if a JSON file has a syntax error, and warns if that target's `.env` is missing. Node A (`node_a_sim` and `node_a_gps`) gets `config.ini`, `env/node_a/.env`, `env/node_a/route.json` and `env/node_a/scenarios.json`. Node B gets `config.ini` and `env/node_b/.env`. On the device the secrets file is stored as `/secrets.env`, and the two JSON files as `/route.json` and `/scenarios.json`.

If several boards are plugged in, add `--upload-port COM5` (and `--port COM5` for the monitor). `pio device list` shows the ports.

On the ESP32-CAM-MB, uploading is usually automatic. If it stops at `Connecting....` and fails, hold the IO0 button on the MB board, start the upload, and release it when the percentage starts. After flashing, press RST.

## First boot: what you should see

Open the monitor (`pio device monitor -e node_a_sim`) and press the board's EN/RST button to see the start.

Healthy Node A (simulator):

```text
[boot] node A device=a4cf12e08b10 boot=6b2fa839 bus=420 route=7
[cfg] files: fs=1 ini=1 env=1
[cfg] route=7 bus=420 server=http://192.168.1.50:8000 wifi="my-hotspot" secret=set
[cfg] ble uuid=d22b117c-2f6e-4d74-9e9f-6f09213c3dd0 tx1m=-59 pwr=3dBm adv=150ms slot=30s
[ble] iBeacon on: major(route)=7 minor(bus)=420 non-connectable
[route] 'E2' (8 points) saved
[sim] route 'E2' ... m, ... stops, 7 scenarios, active 'normal'
commands: status | config | help | set bus N | set route N | clear id | reboot
[ble] clock synced, token on (slot 58734123)
```

What each line tells you:

- `device=` is this chip's id. `boot=` is the id of this power cycle, sent with every telemetry message. The server team needs both.
- `fs=1 ini=1 env=1` means the files were flashed. A `0` means `uploadfs` is missing or that target's `.env` did not exist.
- `secret=set` means a secret was loaded. `EMPTY` means it was not.
- `[route] ... saved` means the server answered the route request. `download failed` or `offline at boot` means the copy already on the board is used, which is fine.
- `clock synced, token on` appears once the board has internet time. Until then the beacon broadcasts only its route and bus, without a token. If this line never appears, the board has no internet access.

Type `status` and press Enter for a live summary.

Healthy Node B:

```text
[boot] node B cam device=b8d61a4f02c4 bus=420 route=7 psram=1
[cfg] files: fs=1 ini=1 env=1
...
[cam] 48213 bytes -> HTTP 200 in 412 ms (next in 10000 ms)
```

`psram=0` means photos are limited to 320×240. `HTTP -1` means the server was not reachable.

## Run against the mock server

`tools/mock_server.py` is a small test server that implements everything the real server must. Use it until the real one exists, or to check that a board works.

```powershell
pip install -r tools\requirements.txt
python tools\mock_server.py
```

It reads Node A's secret from `env/node_a/.env` and Node B's from `env/node_b/.env`. BLE tokens are checked with Node A's secret. Either secret is accepted on HTTP requests. To override the files, set `DEVICE_HMAC_SECRET` and `DEVICE_HMAC_SECRET_CAM` before starting it.

It listens on port 8000 on all interfaces. Then:

1. Set `SERVER_BASE_URL="http://<your PC's IP>:8000"` in both `.env` files and run `uploadfs` for each target.
2. Allow Python through Windows Firewall when asked (private network). Without that the board gets `HTTP -1`.
3. Open `http://localhost:8000/` on the PC: one row per bus with position, speed, scenario, online status and the latest photo, refreshing every 2 seconds.

Other mock pages:

| URL | Shows |
| --- | --- |
| `/api/v1/buses` | Latest telemetry of every bus as JSON |
| `/api/v1/buses/420/track.geojson` | The path driven; paste it into [geojson.io](https://geojson.io) to see it on a map |
| `/api/v1/buses/420/frames/latest.jpg` | Latest photo |

The mock rejects a signature that matches neither device secret (`401`). To accept anything while debugging, start it with `$env:VERIFY_SIG = "0"`. A real server keeps one secret per device and checks it against `X-Device-Id`.

It also serves `GET /api/v1/routes/7` from `env/node_a/route.json` and accepts `POST /api/v1/sightings` as the phone app would send it (user taken from an `X-User-Id` header).

## The GPS simulator

`node_a_sim` replaces the GPS module with a virtual bus. It drives along `env/node_a/route.json`, accelerates, brakes into stops, waits, and continues. Its telemetry is real-looking but marked `"sim": 1`, so the server never confuses it with a real bus.

### The route: `env/node_a/route.json`

```json
{
  "route_id": 7,
  "name": "E2",
  "points": [
    { "lat": 45.7500, "lon": 21.2200, "name": "Central Station" },
    { "lat": 45.7500, "lon": 21.2260 },
    { "lat": 45.7530, "lon": 21.2290, "name": "Market Square" }
  ]
}
```

- Points are in driving order. The bus drives straight lines between them, so add points without a `name` to follow a curved street.
- A point with a `name` is a stop.
- If the last point equals the first, the route is a loop.
- At boot Node A asks the server for `GET /api/v1/routes/<route_id>`, the line this bus is configured to run. A valid answer for that same `route_id` replaces this file on the board. The simulator then drives that polyline. A real GPS receiver does not.
- If the file is broken or missing, a small built-in route is used and the log says so.

### The scenarios: `env/node_a/scenarios.json`

A scenario describes how the bus behaves. The file has three parts:

```json
{
  "default": "normal",
  "defaults": { "cruise_kmh": 30, "dwell_s": 15, "time_scale": 1 },
  "scenarios": [
    { "name": "normal" },
    { "name": "demo_fast", "time_scale": 10, "dwell_s": 20 }
  ]
}
```

- `default`: the scenario used at boot, unless `[sim] scenario` in `config.ini` names another.
- `defaults`: values every scenario starts from.
- `scenarios`: the list. Each one only lists what differs from `defaults`.

All keys:

| Key | Default | Meaning |
| --- | --- | --- |
| `name` | (required) | Unique name, used by `config.ini` and the console |
| `description` | | Free text |
| `cruise_kmh` | `30` | Normal driving speed |
| `accel_mps2` / `decel_mps2` | `1.0` / `1.3` | How fast it speeds up and brakes, in m/s² |
| `dwell_s` | `15` | Seconds waiting at each stop |
| `time_scale` | `1` | `10` makes everything run 10× faster; good for demos |
| `gps_jitter_m` | `0` | Random position noise in metres, like a GPS in a city |
| `loop` | `true` | `false` stops after one lap |
| `autostart` | `true` | `false` waits for `start` or the BOOT button |
| `start_stop` | `0` | Stop to begin at, counted from 0 |
| `slow_zones` | none | `[{"from_m": 900, "to_m": 1500, "kmh": 6}]`: traffic jam between 900 m and 1500 m along the route |
| `gps_dropouts` | none | `[{"from_m": 1100, "to_m": 1500}]`: GPS lost there (`fix_ok: false`), like a tunnel |
| `skip_stops` | none | `["Market Square"]`: stop names the bus drives through without stopping |

Distances (`from_m`, `to_m`) are metres along the route from its first point. `status` on the console shows the current position as `pos=812/3050m`, which helps you pick them.

The included scenarios:

| Name | What it shows |
| --- | --- |
| `normal` | Regular service, stops everywhere |
| `demo_fast` | 10× speed: a full lap in about a minute |
| `rush_hour` | Long stops and a traffic jam |
| `express` | Faster, skips two stops |
| `tunnel` | GPS lost for 400 m |
| `noisy_gps` | ±12 m position noise |
| `one_lap` | Starts at University, waits for a start command, does one lap and stops |

Simulator states, as reported in telemetry `sim_state` and by `status`: `idle` (not started), `driving`, `at_stop`, `paused`, `finished` (one-lap scenario done).

## Serial console

Open `pio device monitor -e node_a_sim`, type a command and press Enter. Node A only; Node B has no commands.

| Command | Effect |
| --- | --- |
| `help` | List commands and scenarios |
| `status` | Bus, route, Wi-Fi, clock, queue length, simulator state, position |
| `config` | Print the loaded configuration |
| `set bus 102` | Change the bus id. Saved on the board and survives reflashing `config.ini`. Reboots. |
| `set route 9` | Change the route id. Same as above. |
| `clear id` | Forget `set bus` / `set route` and use `config.ini` again. Reboots. |
| `reboot` | Restart |
| `start` / `pause` | Simulator only: start or pause the virtual bus |
| `reset` | Simulator only: back to the scenario's starting stop |
| `scn 3` / `next` | Simulator only: switch scenario by number, or to the next one |
| `speed 45` | Simulator only: cruise speed in km/h, until the scenario changes |
| `scale 10` | Simulator only: time speed-up, until the scenario changes |
| `route sync` | Download the route for this bus from the server again. The simulator reloads it; a real GPS board only stores it. |

`set bus` and `set route` store the values in NVS, a small permanent memory inside the ESP32 that survives reboots and reflashing. They win over `config.ini` until `clear id`. If the boot line ends in `(NVS override)`, that is why `config.ini` seems to be ignored.

## Tests

| Command | Runs on | Needs |
| --- | --- | --- |
| `pio test -e native` | Your PC | `g++` on `PATH`. No board. |
| `pio test -e node_a_sim` | ESP32 dev board | Board plugged in, `uploadfs` done, Wi-Fi reachable |
| `pio test -e node_b_cam` | ESP32-CAM-MB | Board plugged in, `uploadfs` done |

Test suites under `test/`:

| Suite | Where | Checks |
| --- | --- | --- |
| `logic_parsers` | PC | Reading `config.ini` and `.env`, route and scenario JSON, the exact beacon bytes |
| `logic_sim` | PC | The simulator: stops, slow zones, skipped stops, GPS dropouts, noise limits, pause, time scale, one-lap finish, and that the real files in `env/` load (run from the project folder) |
| `wifi` | Board | Files present, Wi-Fi connects within 20 s, internet time within 15 s, the server answers a plain and a signed request |
| `ble` | Node A | Beacon starts and keeps running after a token change |
| `camera` | Node B | Camera starts, photos are valid JPEGs, 5 photos in a row work, one upload (skipped without Wi-Fi) |

Run the mock server while running the board tests to also check that the server accepts the signatures. Any HTTP server still validates that the network works.

`pio test` flashes test firmware over your normal firmware. Run `pio run -e <target> -t upload` afterwards.

## Configuration reference

### `env/node_a/.env` and `env/node_b/.env`

Same keys in both files. The values can differ, and `DEVICE_HMAC_SECRET` should.

| Key | Example | Meaning |
| --- | --- | --- |
| `WIFI_SSID` | `"my-hotspot"` | 2.4 GHz network name |
| `WIFI_PASS` | `"my-password"` | Its password |
| `SERVER_BASE_URL` | `"http://192.168.1.50:8000"` | Server address reachable from this ESP32 |
| `DEVICE_HMAC_SECRET` | 64 hex characters | This device's secret. Node A's also builds the BLE token. |

Keys are case-sensitive. Quotes are optional but needed if a value contains `#`.

### `env/config.ini`

Section and key names are not case-sensitive. A comment starts with `;` or `#`; after a value it needs a space before it (`bus_id = 420 ; comment`).

| Section | Key | Default | Meaning |
| --- | --- | --- | --- |
| `[identity]` | `route_id` | `7` | Route the bus runs |
| | `bus_id` | `420` | This bus |
| `[ble]` | `system_uuid` | `d22b117c-...` | Shared by all buses. The phone app filters on it, so changing it breaks the app. |
| | `tx_power_1m` | `-59` | Standard iBeacon field. Nothing depends on it; leave as is. |
| | `radio_power_dbm` | `3` | Beacon transmit power: `-12 -9 -6 -3 0 3 6 9`. Lower keeps the signal closer to the bus. |
| | `adv_interval_ms` | `150` | How often the beacon broadcasts |
| | `token_slot_s` | `30` | How often the token changes. The server must use the same value. |
| `[timing]` | `telemetry_interval_ms` | `2000` | Node A: how often position is sent |
| | `frame_default_interval_ms` | `10000` | Node B: photo interval until the server sets another |
| `[pins]` | `button` | `0` | Simulator button; `-1` disables it |
| | `gps_rx`, `gps_tx` | `16`, `17` | GPS UART pins |
| | `gps_baud` | `9600` | GPS module speed |
| `[sim]` | `scenario` | empty | Scenario at boot; empty = `default` from `env/node_a/scenarios.json` |
| `[camera]` | `frame_size` | `SVGA` | `QVGA` 320×240, `VGA` 640×480, `SVGA` 800×600, `XGA` 1024×768, `HD` 1280×720, `SXGA` 1280×1024, `UXGA` 1600×1200 |
| | `jpeg_quality` | `12` | 10–63; lower = better quality, bigger file |
| | `vflip`, `hmirror` | `false` | Flip or mirror the image if the camera is mounted upside down |
| | `flash` | `false` | Light the white LED for each photo |

A missing key keeps its default. Settings load in this order, later ones winning: built-in defaults, then `config.ini`, then that device's `.env`, then (for bus and route) `set bus` / `set route` values saved on the board.

## How it works

### The beacon

Node A broadcasts two BLE packets:

- **iBeacon advert**: UUID `system_uuid` (identifies our system), major = route id, minor = bus id. Always on.
- **Scan response**: a token that changes every `token_slot_s` seconds. Sent only once the board has internet time.

```text
slot  = unix_time / 30
token = first 16 bytes of HMAC-SHA256(DEVICE_HMAC_SECRET, "<bus_id>.<slot>")
bytes : FF FF | 02 (version) | flags (bit 0 = clock synced) | slot (4 bytes, big-endian) | token (16 bytes)
```

The phone copies the bus id, slot and token to the server under the user's login. The server computes the same HMAC with Node A's secret; if it matches, the phone was near that bus during that slot. The phone cannot fake it without the secret, and an old token expires within minutes. The beacon is non-connectable: phones only listen.

The firmware does not estimate distance. BLE signal strength (RSSI) is far too noisy for that, and timing-based ranging needs Bluetooth 6.0 hardware the ESP32 does not have. The phone sends the raw RSSI and the server only looks at whether it stays steady or fades out while the bus moves.

### Telemetry and the queue

Every 2 seconds Node A puts a JSON position report in a queue (the fields are listed in [docs/server.md](docs/server.md)). Each boot picks a random `boot_id` (8 hex characters, printed on the boot line) and counts `seq` from 0 underneath it. A separate task sends the queue to `POST /api/v1/buses/<bus_id>/telemetry`. If Wi-Fi is down, up to 60 messages (2 minutes) wait and are sent in order when it returns; older ones are dropped. A `2xx` or `4xx` answer removes a message from the queue; anything else is retried with a backoff of up to 5 seconds. The server tells a retry from a new boot by `(device_id, boot_id, seq)`, as described in [docs/server.md](docs/server.md).

### Camera

Node B takes a photo, posts it to `POST /api/v1/buses/<bus_id>/frames`, and waits. If the server answers `{"next_capture_ms": N}` with N between 1000 and 60000, that becomes the next interval. Failed uploads are not retried.

### Signed requests

Every request from both nodes carries `X-Device-Id` (the chip id), `X-Timestamp` (unix time, `0` before internet time) and `X-Signature = hex(HMAC-SHA256(secret, "<timestamp>.<device_id>.<sha256 hex of body>"))`. The server looks up the secret for `X-Device-Id` and recomputes it, so it knows which device sent the request and that the body was not altered. The BLE token uses Node A's secret only.

### Watchdog and recovery

Both nodes restart themselves if the main loop hangs for 30 seconds. Wi-Fi reconnects automatically and is reset after 15 seconds without a connection. Node B restarts after 10 failed photos in a row.

## Common tasks

**Set up a new bus**

1. Generate one secret per device and choose a `bus_id` (and `route_id`) with the server team.
2. Edit `env/node_a/.env` and `env/node_b/.env`, and `env/config.ini` (`[identity]`).
3. Node A: `pio run -e node_a_sim -t upload`, then `-t uploadfs`.
4. Node B: `pio run -e node_b_cam -t upload`, then `-t uploadfs`.
5. Send the server team the bus id, route id, and each `device=` id from the boot logs together with that device's secret.

`config.ini` is shared, so a second bus needs its own copy of these files (or you edit them before that bus's `uploadfs`). The two `.env` files already keep the two devices apart.

**Change a bus id without reflashing**: on Node A, `set bus 102` in the console. Node B has no console; change `config.ini` and run `uploadfs`.

**Show a fast demo**: `scn 1` (or `next` until `demo_fast`) on the console, or set `[sim] scenario = demo_fast` and `uploadfs`.

**Add a scenario**: add an object to `scenarios` in `env/node_a/scenarios.json` with a unique `name` and only the keys that differ, then `uploadfs`. `pio test -e native` checks that the real file still loads.

**Change the route**: edit `env/node_a/route.json` and `uploadfs`, or have the server return the new route. Both Node A builds download it at boot. `route sync` downloads it again without a reboot.

**Switch to a real GPS**: wire the module, then `pio run -e node_a_gps -t upload` and `-t uploadfs`. Telemetry then has `"sim": 0` and `"scenario": "real"`.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Upload: `No serial port found` / `Failed to connect` | Charge-only cable, missing USB driver (CP210x or CH340), or the board is not in download mode | Try another cable; install the driver; on the CAM-MB hold IO0 during upload |
| `fs=0` or `ini=0` in the boot log | `uploadfs` never ran for this target | `pio run -e <target> -t uploadfs` |
| `env=0`, `secret=EMPTY` | That target's `.env` did not exist when `uploadfs` ran | Create `env/node_a/.env` or `env/node_b/.env`, then `uploadfs` again |
| Wi-Fi never connects | 5 GHz network, wrong password, or hotspot asleep | Use 2.4 GHz; check `config` output; keep the hotspot screen on |
| `HTTP -1` | Server not reachable: `localhost` in the URL, wrong IP, firewall, different network | Use the PC's LAN IP; allow Python through the firewall; same Wi-Fi |
| Mock answers `401` | The board was flashed with a different secret than the file the mock read, or its clock is off | `uploadfs` again after editing the `.env`; or `VERIFY_SIG=0` to debug |
| `clock synced, token on` never appears | No internet access from that Wi-Fi (time servers unreachable) | Use a network with internet; the token cannot work without real time |
| Phone sees the iBeacon but no token | Clock not synced yet, or the phone uses a passive scan | Wait for the log line; the app must use an active scan |
| `config.ini` changes seem ignored | A `set bus` / `set route` value is saved on the board | `clear id` |
| `[sim] ... unusable, using built-in route` | `route.json` or `scenarios.json` invalid or not flashed | Fix the file; `uploadfs` (the build reports JSON syntax errors) |
| Node B resets right after start (brownout) | Weak USB power | Another port or cable, or an external 5 V supply |
| Photos only 320×240 | No PSRAM detected (`psram=0`) | Use a board with PSRAM |
| Real GPS: `fix_ok` stays `false` | No sky view, or RX/TX swapped | Go outside and wait; swap the two wires |

## Project layout

```text
env/
  .env.example    template for the two .env files below (those files are not in git)
  config.ini      bus identity, beacon, pins, camera (shared by both nodes)
  node_a/.env     Node A secrets, flashed by node_a_sim and node_a_gps
  node_a/route.json       route this bus is configured to run, stored as /route.json
  node_a/scenarios.json   simulated behavior, stored on the board as /scenarios.json
  node_b/.env     Node B secrets, flashed by node_b_cam
src/node_a/     Node A firmware: beacon, telemetry, console, simulator controls
src/node_b/     Node B firmware: capture and upload loop
lib/core/       pure C++, also built on the PC for tests:
                  .ini/.env readers, beacon byte layout, GPS geometry, simulator engine
lib/common/     configuration loading, HMAC and SHA-256, device identity and NVS,
                  Wi-Fi + time + signed HTTP client, telemetry queue
lib/ble/        iBeacon advertiser and token updates
lib/gps/        SimGps and RealGps behind one interface, so the rest of the code
                  does not care which one is used
lib/camera/     AI-Thinker ESP32-CAM driver setup and capture
test/           unit tests (see Tests)
tools/          prepare_fs.py (build step), mock_server.py (test server)
docs/           phone.md, server.md: contracts for the other teams
```

The default `bus_id` 420 and `route_id` 7 are also compiled in (`platformio.ini`, `DEFAULT_BUS_ID` / `DEFAULT_ROUTE_ID`) and used only when `config.ini` is missing.

## Known limitations

Current behavior the other teams work around (as described in `docs/`), with where to change it:

1. **Unsent telemetry is lost on reboot.**  The 60-message `StoreForward` queue is still only in RAM, so a reboot drops anything not yet posted. Fix, if those gaps matter: persist the queue on LittleFS and replay it with the same `boot_id` and `seq`.
2. **Any HTTP 4xx deletes a queued telemetry message.** `uplinkTask()` in `src/node_a/main.cpp` pops on 2xx and 4xx. Before NTP, `X-Timestamp` is `0`, so a server that answers `401` for that drops the first positions. Accepted for the hackathon: the server, including `tools/mock_server.py`, accepts timestamp `0` when the signature matches and reserves `401` for a bad signature. The rule is in [docs/server.md](docs/server.md). A later firmware change can hold the queue until the clock is set, or drop only `400` and `422`.
3. **Frames are not retried.** `captureAndUpload()` in `src/node_b/main.cpp` posts once and keeps no copy; `X-Frame-Seq` is a RAM counter. Fix: keep the last JPEG or a short queue and replay it with the same sequence number.