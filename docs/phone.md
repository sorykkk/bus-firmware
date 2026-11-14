# Phone app: integrating the bus beacon

## Where the app fits

```
Bus beacon (ESP32)  --BLE broadcast-->  Phone app  --HTTPS, user login-->  Server
```

Every bus has a small BLE beacon. It only broadcasts: the app listens to it and never connects to it, never writes to it, and never sends it anything.

What the app does with the beacon:

1. Detect that the phone is near one of our buses.
2. Read which bus it is, plus a short-lived proof code (the token).
3. Send that proof to the server, under the logged-in user.

The server checks the proof and decides whether the user is riding and must pay. The app does not decide either.

## What the beacon broadcasts

Each beacon sends two BLE packets.

### 1. The iBeacon advert: "which bus is this"

A standard iBeacon:

| Field | Value |
| --- | --- |
| UUID | `d22b117c-2f6e-4d74-9e9f-6f09213c3dd0`. The same on every bus. Filter your scan on it. |
| major | Route id, a number from 0 to 65535 (for example `7`) |
| minor | Bus id, a number from 0 to 65535 (for example `101`) |
| measured power | `-59`. Standard iBeacon field. You can ignore it. |

The beacon repeats it about every 150–200 ms. It is on as soon as the beacon powers up.

The UUID is not a secret. It only tells your scanner which beacons belong to us.

### 2. The scan response: "proof that you are near it right now"

This packet is sent only to an active scan. A passive scan, or an iBeacon region callback, gives you only the advert, without the token.

It is manufacturer-specific data with company id `0xFFFF`. On Android, `scanRecord.getManufacturerSpecificData(0xFFFF)` returns 22 bytes:

| Bytes (index) | Content |
| --- | --- |
| 0 | Format version. It is `0x02`. Ignore the packet if it is anything else. |
| 1 | Flags. Bit 0 = bus clock is synced. Always set in practice. |
| 2–5 | Slot: a 32-bit unsigned number, big-endian |
| 6–21 | Token: 16 bytes |

The slot is the current 30-second time window (`unix time / 30`). The token changes with every slot, so about every 30 seconds. The app only copies the slot and token; it cannot compute or check the token, and does not need to.

Right after power-up the bus has no internet time yet. During that time the advert is on and the scan response is missing. Wait for it. Do not send a sighting without a token.

### Parsing example (Kotlin)

```kotlin
val record = result.scanRecord ?: return

// iBeacon: Android strips the company id, so these 23 bytes start with 02 15
val ib = record.getManufacturerSpecificData(0x004C) ?: return
if (ib.size < 23 || ib[0] != 0x02.toByte() || ib[1] != 0x15.toByte()) return
val uuid = ib.copyOfRange(2, 18)                       // compare with our UUID
val routeId = ((ib[18].toInt() and 0xFF) shl 8) or (ib[19].toInt() and 0xFF)
val busId = ((ib[20].toInt() and 0xFF) shl 8) or (ib[21].toInt() and 0xFF)

// Token packet
val p = record.getManufacturerSpecificData(0xFFFF) ?: return   // no token yet
if (p.size < 22 || p[0] != 0x02.toByte()) return
val clockSynced = (p[1].toInt() and 0x01) != 0
val slot = ByteBuffer.wrap(p, 2, 4).order(ByteOrder.BIG_ENDIAN).int.toLong() and 0xFFFFFFFFL
val token = p.copyOfRange(6, 22).joinToString("") { "%02x".format(it) }
val rssi = result.rssi
```

Android may deliver the advert and the scan response as one result or as two. If you see the token packet without the iBeacon in the same result, take the bus id from the last iBeacon you saw from the same device address.

## What the app sends to the server

One request per bus per slot. When the slot number changes, send again.

```
POST /api/v1/sightings
Authorization: <the user's login token>
Content-Type: application/json

{
  "bus_id": 101,
  "slot": 58734123,
  "token": "a9f3c2104e7b19d05c8ae13f7726b4d1",
  "rssi": -68,
  "clock_synced": true
}
```

| Field | Type | Value |
| --- | --- | --- |
| `bus_id` | number | iBeacon minor |
| `slot` | number | Slot from the token packet |
| `token` | string | The 16 token bytes as 32 lowercase hex characters |
| `rssi` | number | Signal strength in dBm as Android reports it, for example `-68`. Send it raw. Do not convert it to a distance. |
| `clock_synced` | bool | Flags bit 0 |

The user is identified only by the `Authorization` header. Nothing about the user is ever sent over BLE.

Responses from the server (the test server behaves this way):

| Status | Meaning | What the app does |
| --- | --- | --- |
| 200 | Valid: the phone heard that bus during that slot | Nothing more |
| 422 | Token wrong or too old | Drop it. Do not retry the same sighting. |
| 400 | Malformed body | Bug in the app |
| 401 | Not logged in | Ask the user to log in |

If the phone is offline, keep a sighting for at most about 2 minutes. After that the server rejects it as too old.

## What happens after the app sends

The server collects sightings per user and per bus and compares them with the bus GPS position, which the bus sends to the server on its own. Several valid sightings in a row while the bus is moving means the user is on that bus. When sightings stop while the bus keeps moving, the user got off.

Any notification to the user, such as "you are being charged for bus 101", comes from the server, for example as a push notification. It never comes from the beacon.

## Testing without a bus

A test server is available from the firmware team. It accepts `POST /api/v1/sightings` with the body above, and takes the user from an `X-User-Id` header instead of a real login. A phone app such as nRF Connect shows the beacon's raw packets if you have a powered bus beacon nearby.
