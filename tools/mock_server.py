"""
Mock backend for the ESP32 nodes. Helps in develop/demo the firmware before the real server exists.

  pip install -r requirements.txt
  python mock_server.py                      # listens on 0.0.0.0:8000
                                              # reads secrets from env/node_a/.env and env/node_b/.env
  VERIFY_SIG=0 python mock_server.py         # skip signature checks

Endpoints:
  POST /api/v1/buses/<id>/telemetry          JSON from Node A
  POST /api/v1/buses/<id>/frames             image/jpeg from Node B -> replies {"next_capture_ms": ...}
  GET  /api/v1/buses                         latest state of every bus
  GET  /api/v1/buses/<id>/track.geojson      breadcrumb trail (paste into https://geojson.io)
  GET  /api/v1/buses/<id>/frames/latest.jpg
  GET  /api/v1/routes/<id>                   env/node_a/route.json, downloaded by Node A at boot
  POST /api/v1/sightings                     from the phone app: verifies the beacon token
  GET  /                                     tiny live dashboard
"""
import hashlib, hmac, os, time, json
from collections import deque
from flask import Flask, request, jsonify, Response, abort

VERIFY = os.environ.get("VERIFY_SIG", "1") == "1"
MAX_SKEW_S = 300
TOKEN_SLOT_S = 30                # must match token_slot_s in env/config.ini
ENV_DIR = os.path.join(os.path.dirname(__file__), "..", "env")
ROUTE_FILE = os.path.join(ENV_DIR, "node_a", "route.json")


def read_dotenv(path):
    vals = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                vals[key.strip()] = value.strip().strip('"').strip("'")
    except OSError:
        return {}
    return vals


def device_secret(filename, override):
    """Env var wins, then the flashed .env, then empty."""
    if os.environ.get(override):
        return os.environ[override]
    return read_dotenv(os.path.join(ENV_DIR, filename)).get("DEVICE_HMAC_SECRET", "")


BEACON_SECRET = device_secret(os.path.join("node_a", ".env"), "DEVICE_HMAC_SECRET") or "change-me-per-bus"
CAM_SECRET = device_secret(os.path.join("node_b", ".env"), "DEVICE_HMAC_SECRET_CAM") or BEACON_SECRET
HTTP_SECRETS = list(dict.fromkeys([BEACON_SECRET, CAM_SECRET]))

app = Flask(__name__)
latest = {}                      # bus_id -> last telemetry dict (+ server_rx)
trail = {}                       # bus_id -> deque of [lon, lat]
seen_msg = set()                 # (device_id, boot_id, seq); seq restarts every boot
frames = {}                      # bus_id -> (bytes, time)
sightings = {}                   # user_id -> deque of (time, bus_id, rssi)


def verify_signature(body: bytes):
    dev = request.headers.get("X-Device-Id", "")
    ts = request.headers.get("X-Timestamp", "0")
    sig = request.headers.get("X-Signature", "")
    if not (dev and sig):
        abort(401, "missing auth headers")
    if ts != "0" and abs(time.time() - int(ts)) > MAX_SKEW_S:
        abort(401, "timestamp skew")
    msg = f"{ts}.{dev}.{hashlib.sha256(body).hexdigest()}".encode()
    # Each device signs with its own secret. The mock has no device table, so either matches.
    if not any(hmac.compare_digest(hmac.new(s.encode(), msg, hashlib.sha256).hexdigest(), sig)
               for s in HTTP_SECRETS):
        abort(401, "bad signature")


@app.post("/api/v1/buses/<int:bus_id>/telemetry")
def telemetry(bus_id):
    body = request.get_data()
    if VERIFY:
        verify_signature(body)
    d = request.get_json(force=True)
    if d.get("bus_id") != bus_id:
        abort(400, "bus_id mismatch")
    boot_id = d.get("boot_id")
    if not isinstance(boot_id, str) or not boot_id:
        abort(400, "boot_id required")
    try:
        seq = int(d["seq"])
    except (KeyError, TypeError, ValueError):
        abort(400, "seq required")
    key = (d.get("device_id"), boot_id, seq)
    if key in seen_msg:
        return jsonify(ok=True, duplicate=True)
    seen_msg.add(key)
    d["server_rx"] = time.time()
    latest[bus_id] = d
    if d.get("fix_ok"):
        trail.setdefault(bus_id, deque(maxlen=2000)).append([d["lon"], d["lat"]])
    return jsonify(ok=True)


@app.post("/api/v1/buses/<int:bus_id>/frames")
def frame(bus_id):
    body = request.get_data()
    if VERIFY:
        verify_signature(body)
    frames[bus_id] = (body, time.time())
    moving = latest.get(bus_id, {}).get("speed_kmh", 0) > 3
    return jsonify(ok=True, bytes=len(body), next_capture_ms=10000 if moving else 3000)


TOKEN_LEN = 16
SLOT_PAST, SLOT_FUTURE = 4, 1    # accepted slot window around the server clock


def beacon_token(bus_id: int, slot: int) -> bytes:
    """Same as crypto::presenceToken() on the ESP32."""
    # The token is minted by Node A, so only the beacon secret can verify it.
    return hmac.new(BEACON_SECRET.encode(), f"{bus_id}.{slot}".encode(), hashlib.sha256).digest()[:TOKEN_LEN]


def verify_beacon_token(bus_id: int, slot: int, token: bytes, now: float) -> bool:
    """The slot must be recent (clock drift, a phone that reports late) and the token must match it."""
    current = int(now) // TOKEN_SLOT_S
    if not current - SLOT_PAST <= slot <= current + SLOT_FUTURE:
        return False
    return len(token) == TOKEN_LEN and hmac.compare_digest(beacon_token(bus_id, slot), token)


@app.post("/api/v1/sightings")
def sighting():
    # A real server takes the user from the app's login session (e.g. a JWT); the mock trusts a header.
    user = request.headers.get("X-User-Id")
    if not user:
        abort(401, "missing X-User-Id")
    d = request.get_json(force=True)
    try:
        bus_id, slot, token = int(d["bus_id"]), int(d["slot"]), bytes.fromhex(d["token"])
    except (KeyError, TypeError, ValueError):
        abort(400, "need bus_id, slot, token (32 hex chars)")
    if d.get("clock_synced") is False:
        return jsonify(ok=False, reason="bus clock was not synced"), 422
    now = time.time()
    if not verify_beacon_token(bus_id, slot, token, now):
        return jsonify(ok=False, reason="token not issued by this bus recently"), 422
    sightings.setdefault(user, deque(maxlen=200)).append((now, bus_id, d.get("rssi")))
    recent = sum(1 for t, b, _ in sightings[user] if b == bus_id and now - t < 300)
    return jsonify(ok=True, bus_id=bus_id, sightings_last_5min=recent)


@app.get("/api/v1/routes/<int:route_id>")
def route(route_id):
    with open(ROUTE_FILE, encoding="utf-8") as f:
        r = json.load(f)
    if r.get("route_id") != route_id:
        abort(404)
    return jsonify(r)


@app.get("/api/v1/buses")
def buses():
    now = time.time()
    return jsonify([{**d, "age_s": round(now - d["server_rx"], 1), "online": now - d["server_rx"] < 10}
                    for d in latest.values()])


@app.get("/api/v1/buses/<int:bus_id>/track.geojson")
def track(bus_id):
    coords = list(trail.get(bus_id, []))
    return jsonify(type="Feature", properties={"bus_id": bus_id},
                   geometry={"type": "LineString", "coordinates": coords})


@app.get("/api/v1/buses/<int:bus_id>/frames/latest.jpg")
def latest_frame(bus_id):
    if bus_id not in frames:
        abort(404)
    return Response(frames[bus_id][0], mimetype="image/jpeg")


@app.get("/")
def dash():
    rows = ""
    now = time.time()
    for b in sorted(set(latest) | set(frames)):
        img = (f"<td><img src='/api/v1/buses/{b}/frames/latest.jpg?t={now:.0f}' height=90 "
               "onerror=\"this.style.display='none'\"></td></tr>")
        d = latest.get(b)
        if d is None:       # Node B is uploading but Node A has not sent telemetry
            age = now - frames[b][1]
            rows += (f"<tr><td>{b}</td><td colspan=7>no telemetry yet (camera only, last photo {age:.0f}s ago)</td>"
                     + img)
            continue 

        age = now - d["server_rx"]
        rows += (f"<tr><td>{b}</td><td>{d['route_id']}</td><td>{d['lat']:.5f}, {d['lon']:.5f}</td>"
                 f"<td>{d['speed_kmh']}</td><td>{'yes' if d['fix_ok'] else '<b>NO</b>'}</td>"
                 f"<td>{d.get('scenario','')}</td><td>{d.get('stop_index')}</td>"
                 f"<td>{'online' if age < 10 else 'OFFLINE'} ({age:.0f}s)</td>" + img)
    return (f"<meta http-equiv=refresh content=2><title>mock server</title>"
            "<style>body{font-family:sans-serif}td,th{padding:4px 10px;border-bottom:1px solid #ccc}</style>"
            "<h3>Mock bus server</h3><table><tr><th>bus<th>route<th>position<th>km/h<th>fix<th>scenario<th>stop#<th>status<th>frame</tr>"
            f"{rows}</table>")


if __name__ == "__main__":
    same = CAM_SECRET == BEACON_SECRET
    print("beacon secret from DEVICE_HMAC_SECRET or env/node_a/.env")
    print("camera secret " + ("same as beacon" if same else "from DEVICE_HMAC_SECRET_CAM or env/node_b/.env"))
    app.run(host="0.0.0.0", port=8000)
