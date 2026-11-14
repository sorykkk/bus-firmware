# Server: integrating the bus devices

## Where the server fits

```
Bus beacon (Node A)  --BLE broadcast-->  Phone app
        |                                    |
        | HTTP: GPS every 2 s                | HTTPS: "I heard bus 101" under the user's login
        v                                    v
     +-----------------------------------------+
     |                 Server                  |
     +-----------------------------------------+
        ^
        | HTTP: cabin photo every few seconds
        |
Bus camera (Node B)
```

Every bus carries two ESP32 devices:

| Device | What it does | What it sends to the server |
| --- | --- | --- |
| Node A, the beacon | Broadcasts the bus id over BLE, reports position | GPS telemetry every 2 s |
| Node B, the camera | Photographs the inside of the bus | One JPEG every 1–60 s, at the rate you choose |

The devices never send anything to the phone except the BLE broadcast, and they never receive anything from it. They know nothing about users. All decisions (who is on board, who pays, how many people are inside) are made on the server.

The server implements four endpoints:

| Endpoint | Called by | Purpose |
| --- | --- | --- |
| `POST /api/v1/buses/{bus_id}/telemetry` | Node A | Bus position |
| `POST /api/v1/buses/{bus_id}/frames` | Node B | Cabin photo |
| `GET /api/v1/routes/{route_id}` | Node A | The route this bus is configured to run |
| `POST /api/v1/sightings` | Phone app | "This user heard this bus" |

## What you need per bus

Before a bus can talk to the server, the firmware team and the server team agree on these values. They are set on the devices when they are flashed:

| Value | Example | Notes |
| --- | --- | --- |
| `bus_id` | `101` | Number from 0 to 65535 |
| `route_id` | `7` | The line the bus runs. Number from 0 to 65535. |
| Device secret | 64 random hex characters | One per device, not per bus. Never sent over the network. |
| Device ids | `a4cf12e08b10` | One per device, 12 hex characters. Printed by each device on its serial console at boot. This is how you find that device's secret. |

Node A keeps its secret in `env/node_a/.env` and uses it both to sign telemetry and to build the BLE token. Node B keeps a different secret in `env/node_b/.env` and uses it only to sign photo uploads. Store each secret on the server next to that device's id. Use the string as is (its ASCII characters) as the HMAC key. Do not hex-decode it.

Each device also has its own `SERVER_BASE_URL` in that same file. Node A and Node B do not have to call the same host. Node A calls `POST /api/v1/buses/{bus_id}/telemetry` and `GET /api/v1/routes/{route_id}` on its URL. Node B calls `POST /api/v1/buses/{bus_id}/frames` on its URL. The phone still calls `POST /api/v1/sightings` on the app's server, which may be a third address. If the URLs differ, each host implements only the endpoints that device uses, and both must share the device table (ids, secrets, bus and route) so a sighting for bus 101 can be checked against that bus's beacon secret and GPS.

Route names such as `"E2"` and stop names exist only on the server. The devices send numbers only.

## Checking that a request really comes from the bus

Every request from Node A and Node B carries three headers:

| Header | Value |
| --- | --- |
| `X-Device-Id` | The device id, for example `a4cf12e08b10` |
| `X-Timestamp` | Unix time in seconds. `0` if the device has not got the time from the internet yet (the first seconds after power-up). |
| `X-Signature` | 64 lowercase hex characters |

Compute the signature yourself and compare:

```python
import hashlib, hmac

def check(secret: str, headers, body: bytes) -> bool:
    message = f"{headers['X-Timestamp']}.{headers['X-Device-Id']}.{hashlib.sha256(body).hexdigest()}"
    expected = hmac.new(secret.encode(), message.encode(), hashlib.sha256).hexdigest()
    return hmac.compare_digest(expected, headers["X-Signature"])
```

`secret` is the secret stored for `X-Device-Id`, not a secret shared by the whole bus. `body` is the raw request body, which is empty for a GET.

If the signature matches, the request was made by that device, and the body was not changed. Also check that this device belongs to the `bus_id` in the URL. If `X-Timestamp` is not `0`, you can reject it when it is more than a few minutes away from your clock.

The devices may use plain `http://`. When they use `https://`, they do not check the server certificate yet. The signature is the protection.

## Telemetry from Node A

```
POST /api/v1/buses/101/telemetry
Content-Type: application/json
```

About every 2 seconds:

```json
{
  "bus_id": 101,
  "route_id": 7,
  "device_id": "a4cf12e08b10",
  "boot_id": "6b2fa839",
  "seq": 1532,
  "ts": 1761523200,
  "uptime_ms": 3064120,
  "sim": 0,
  "lat": 45.753012,
  "lon": 21.229004,
  "speed_kmh": 28.4,
  "heading": 90,
  "fix_ok": true,
  "sats": 8,
  "stop_index": -1,
  "last_stop_index": -1,
  "wifi_rssi": -52,
  "scenario": "real"
}
```

| Field | Meaning |
| --- | --- |
| `bus_id`, `route_id` | Which bus and which line, as set on the device |
| `device_id` | Same as `X-Device-Id` |
| `boot_id` | 8 lowercase hex characters, chosen at random every time Node A boots. Constant until the next reboot. |
| `seq` | Message counter for this boot. Starts at 0 and increases by 1. The same number appears again after a reboot, under a new `boot_id`. |
| `ts` | Unix time when the position was taken. `0` if the device has no time yet. |
| `uptime_ms` | Milliseconds since the device started |
| `sim` | `1` = fake position from the built-in simulator, `0` = real GPS. Never charge anyone based on `sim: 1`. |
| `lat`, `lon` | Position, 6 decimals |
| `speed_kmh`, `heading` | Speed and direction (degrees, 0 = north) |
| `fix_ok` | `false` = GPS has no position right now (tunnel, indoors). Ignore `lat`/`lon` then. |
| `sats` | Number of GPS satellites, `0` on the simulator |
| `stop_index`, `last_stop_index` | Simulator only: the stop the bus is at and the last stop visited, counted from 0 along the route. `-1` with real GPS. |
| `wifi_rssi` | The bus's own Wi-Fi signal. Says nothing about passengers. |
| `scenario` | Simulator scenario name, or `"real"` |
| `sim_state` | Simulator only: `idle`, `at_stop`, `driving`, `paused` or `finished` |

If the bus loses Wi-Fi, the device keeps up to the last 60 messages (about 2 minutes) and sends them in order when it reconnects. Expect bursts of older positions; use `ts` for ordering.

Response: the device reads only the status code.

| You return | The device |
| --- | --- |
| `2xx` | Considers it delivered. Also return `2xx` for a duplicate you already have. |
| `4xx` | Throws the message away for good |
| `5xx` or no answer | Keeps it and retries a few seconds later |

### Clock not set yet (hackathon)

Until NTP answers, every signed request has `X-Timestamp: 0`. Node A deletes a queued telemetry message on any `4xx`, including `401`, and does not send it again. The hackathon server therefore accepts timestamp `0` when the signature matches:

| Situation | Answer |
| --- | --- |
| Signature matches and `X-Timestamp` is `0` | `200`. Store the position. `ts` in the body may also be `0`. Do not use it for a payment decision. |
| Signature matches and the timestamp is a real time within a few minutes | `200` |
| Signature does not match | `401`. The device drops that telemetry message. This means the secret or the body is wrong, not that the clock is unset. |
| Body is malformed, or `boot_id` is missing | `400` or `422`. The device drops it. |

The mock server already does this: timestamp `0` skips the skew check, and a bad signature is the only `401`. Do not answer `401` merely because the timestamp is `0`. That is what loses the first positions after boot.

### Deduplication

Wi-Fi can drop the HTTP response after you have already stored the message. Node A then sends that same JSON again. A retry is the same `device_id`, the same `boot_id` and the same `seq`. Store a message once per that triple and answer `200` both times.

```text
unique (device_id, boot_id, seq)
```

Do not dedupe on `(bus_id, seq)` or on `seq` alone. `seq` goes back to 0 on every boot, and the new boot carries a different `boot_id`, so those messages are new. A missing or empty `boot_id` is a bad request: answer `400`, which makes the device drop that message.

`ts` is not part of the key. It is `0` until the clock syncs, so several real messages can share it. Use `ts` only to order positions, and `uptime_ms` to order the ones taken before the clock was set.

## Photos from Node B

```
POST /api/v1/buses/101/frames
Content-Type: image/jpeg
```

The body is the JPEG. Extra headers:

| Header | Example | Meaning |
| --- | --- | --- |
| `X-Bus-Id` | `101` | Bus id |
| `X-Route-Id` | `7` | Route id |
| `X-Frame-Seq` | `42` | Photo counter, restarts from 0 when the camera restarts |
| `X-Capture-Ts` | `1761523200` | Unix time of the photo, `0` if the camera has no time yet |
| `X-Frame-Size` | `800x600` | Resolution |

Response: you control how often the camera shoots. Reply with JSON:

```json
{ "next_capture_ms": 3000 }
```

A value from 1000 to 60000 sets the delay before the next photo. Anything else, or no body, keeps the current delay (10 s at start-up). A failed upload is not retried; the camera just takes the next photo.

Use the photos for counting people. Compare the count with the number of users the sightings say are on board, to spot people riding without the app.

## The route this bus runs

```
GET /api/v1/routes/7
```

Node A calls this at start-up with the `route_id` configured on that bus. It is the line the bus is assigned to, not a simulator-only request. Reply with that route:

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

Points are in driving order. A point with a `name` is a stop; a point without one is just a bend in the road. For a circular line, repeat the first point at the end. Node A stores the body and, when GPS is simulated, drives this polyline. A real GPS receiver does not follow it. If you return an error, the device keeps the route it already has stored.

## Sightings from the phone app

The app calls this, not the bus.

```
POST /api/v1/sightings
Authorization: <user login>
Content-Type: application/json

{ "bus_id": 101, "slot": 58734123, "token": "a9f3c2104e7b19d05c8ae13f7726b4d1", "rssi": -68, "clock_synced": true }
```

The beacon on bus 101 broadcasts a new token every 30 seconds:

```
slot  = unix_time // 30
token = first 16 bytes of HMAC-SHA256(Node A's secret, "<bus_id>.<slot>")
```

for example `HMAC-SHA256(node_a_secret, "101.58734123")`. Use the beacon's secret, not the camera's. Only Node A and the server know it, so a valid token proves the phone heard bus 101 during that slot.

```python
def check_sighting(secret: str, bus_id: int, slot: int, token_hex: str, now: float) -> bool:
    current = int(now) // 30
    if not current - 4 <= slot <= current + 1:        # accept up to ~2 minutes late
        return False
    expected = hmac.new(secret.encode(), f"{bus_id}.{slot}".encode(), hashlib.sha256).digest()[:16]
    return hmac.compare_digest(expected, bytes.fromhex(token_hex))
```

Reply `200` if valid, `422` if not, `400` for a malformed body, and `401` if the user is not logged in. Also reject `clock_synced: false`.

The user is whoever is logged in. The bus never knows.

## Deciding who is on board

A single sighting only means "this phone was near bus 101 for a moment"; for example, it may be at the bus stop while the bus passes. A token can also be copied and relayed by someone near the bus. So decide from a pattern:

- Valid sightings from the same user for several consecutive slots (a few minutes) **while** the bus telemetry shows it moving (for example 300 m or more): the user is on board.
- Sightings stop while the bus keeps moving: the user got off.
- `rssi` is a hint only. A steady signal supports "on board"; a signal that fades and disappears supports "got off". Phones differ a lot, so compare a user with their own earlier values, not with a fixed number.
- Charge each user once per trip, and notify them through the app (for example a push notification), not through the bus device.

## Data to store

A starting point; adapt to your stack.

```text
routes      route_id (pk), name, points
buses       bus_id (pk), route_id
devices     device_id (pk), bus_id, role ("beacon" or "camera"), secret
users       user_id (pk), ...                                       -- your app accounts

telemetry   bus_id, device_id, boot_id, seq, ts, lat, lon, speed_kmh, fix_ok, sim, received_at
            unique (device_id, boot_id, seq)
frames      bus_id, frame_seq, capture_ts, image, received_at
sightings   user_id, bus_id, slot, rssi, received_at
            unique (user_id, bus_id, slot)
trips       user_id, bus_id, started_at, ended_at, charged
```

`buses.route_id` is the planned assignment. The device also reports its own `route_id` in telemetry. If they differ, someone reconfigured the bus; flag it.

## Testing without hardware

The firmware team has a test server implementing all four endpoints, with a small web page showing the buses, their GPS track and the latest photo. It accepts the user for sightings from an `X-User-Id` header instead of a login. Use it as a reference for the signature and token checks.
