#!/usr/bin/env python3
"""Convert a GTFS feed (STPT Timisoara) into route.json files (+ scenarios for one route).

usage: python3 gtfs_to_routes.py <gtfs_dir> <out_dir> [scenario_route_short_name]
"""
import sys, os, json, math
import pandas as pd

GTFS = sys.argv[1] if len(sys.argv) > 1 else "."
OUT = sys.argv[2] if len(sys.argv) > 2 else "out"
SCEN_ROUTE = sys.argv[3] if len(sys.argv) > 3 else "E2"
PINNED_IDS = {"E2": 7}          # keep the id used in your mock route.json

R_EARTH = 6371000.0

def hav(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    d = math.sin((la2-la1)/2)**2 + math.cos(la1)*math.cos(la2)*math.sin((lo2-lo1)/2)**2
    return 2*R_EARTH*math.asin(math.sqrt(d))

def load():
    rd = lambda n: pd.read_csv(os.path.join(GTFS, n + ".txt"), dtype=str, keep_default_na=False)
    return rd("routes"), rd("trips"), rd("stop_times"), rd("stops"), rd("shapes")

def shape_pts(shapes, sid):
    g = shapes[shapes.shape_id == sid].copy()
    g["seq"] = g.shape_pt_sequence.astype(int)
    g = g.sort_values("seq")
    pts = [(float(a), float(b)) for a, b in zip(g.shape_pt_lat, g.shape_pt_lon)]
    out = [pts[0]]
    for p in pts[1:]:                       # drop exact duplicates
        if p != out[-1]:
            out.append(p)
    return out

def project_stops(poly, stops):
    """Monotonic (along the line) snap of ordered stops onto polyline via DP.
    Returns [(seg_index, t, lat, lon, dist_m)] per stop."""
    lat0 = math.radians(poly[0][0]); kx = R_EARTH*math.cos(lat0)*math.pi/180; ky = R_EARTH*math.pi/180
    xy = [(p[1]*kx, p[0]*ky) for p in poly]
    seglen = [math.dist(xy[i], xy[i+1]) for i in range(len(xy)-1)]
    cum = [0.0]
    for l in seglen: cum.append(cum[-1]+l)
    cands = []
    for s in stops:
        sx, sy = s[1]*kx, s[0]*ky
        row = []
        for i in range(len(xy)-1):
            (x1, y1), (x2, y2) = xy[i], xy[i+1]
            dx, dy = x2-x1, y2-y1
            L2 = dx*dx+dy*dy
            t = 0 if L2 == 0 else max(0, min(1, ((sx-x1)*dx+(sy-y1)*dy)/L2))
            px, py = x1+t*dx, y1+t*dy
            row.append((math.hypot(sx-px, sy-py), cum[i]+t*seglen[i], i, t))
        cands.append(row)
    n = len(xy)-1
    INF = 1e18
    best = [[INF]*n for _ in stops]; back = [[0]*n for _ in stops]
    for j in range(n): best[0][j] = cands[0][j][0]
    for s in range(1, len(stops)):
        pm, pj = INF, 0
        for j in range(n):
            if best[s-1][j] < pm: pm, pj = best[s-1][j], j
            best[s][j] = pm + cands[s][j][0]; back[s][j] = pj
    j = min(range(n), key=lambda k: best[-1][k]); path = [j]
    for s in range(len(stops)-1, 0, -1):
        j = back[s][j]; path.append(j)
    path.reverse()
    res = []
    for s, j in enumerate(path):
        d, pos, i, t = cands[s][j]
        a, b = poly[i], poly[i+1]
        res.append((i, t, a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1]), d, pos))
    return res

def build_direction(poly, stops):
    """-> list of points [{lat,lon,(name)}], list of snap distances, list of stop names in order"""
    proj = project_stops(poly, stops)
    ins = {}                                  # seg -> list of (t, lat, lon, name)
    named_vertex = {}
    for (i, t, la, lo, d, pos), st in zip(proj, stops):
        if t < 1e-6: named_vertex[i] = st[2]
        elif t > 1-1e-6: named_vertex[i+1] = st[2]
        else:
            ins.setdefault(i, []).append((t, la, lo, st[2]))
    pts = []
    for i, p in enumerate(poly):
        pt = {"lat": round(p[0], 6), "lon": round(p[1], 6)}
        if i in named_vertex: pt["name"] = named_vertex[i]
        pts.append(pt)
        for t, la, lo, nm in sorted(ins.get(i, [])):
            pts.append({"lat": round(la, 6), "lon": round(lo, 6), "name": nm})
    return pts, [p[4] for p in proj], [p[5] for p in proj], proj

def main():
    routes, trips, st, stops, shapes = load()
    stops_by_id = {r.stop_id: (float(r.stop_lat), float(r.stop_lon), r.stop_name) for r in stops.itertuples()}
    # trips with weekday-school service first, otherwise any
    os.makedirs(os.path.join(OUT, "routes"), exist_ok=True)
    index, report = [], []
    next_id = 1
    used = set(PINNED_IDS.values())
    scen_info = None
    for r in routes.itertuples():
        rt = trips[trips.route_id == r.route_id]
        dirs = {}
        for d in ("0", "1"):
            td = rt[rt.direction_id == d]
            if td.empty: continue
            shape = td.shape_id.value_counts().index[0]     # dominant shape
            ts = td[td.shape_id == shape].trip_id
            sub = st[st.trip_id.isin(set(ts))].copy()
            sub["seq"] = sub.stop_sequence.astype(int)
            pat = sub.sort_values("seq").groupby("trip_id").stop_id.apply(tuple)
            dom = pat.value_counts().index[0]               # dominant stop pattern
            trip_id = pat[pat == dom].index[0]
            tt = sub[sub.trip_id == trip_id].sort_values("seq")
            seq_stops = [stops_by_id[s] for s in tt.stop_id]
            poly = shape_pts(shapes, shape)
            pts, snaps, along, proj = build_direction(poly, seq_stops)
            dirs[d] = dict(shape=shape, pts=pts, snaps=snaps, trip=tt, poly=poly)
        # ---- stitch directions into a closed loop
        loop = []
        for d in ("0", "1"):
            if d in dirs:
                pts = [dict(p) for p in dirs[d]["pts"]]
                if loop and (loop[-1]["lat"], loop[-1]["lon"]) == (pts[0]["lat"], pts[0]["lon"]): pts = pts[1:]
                loop += pts
        # the lap starts on the first stop and ends on the last one (drop the shape lead-in / tail)
        while "name" not in loop[0]: loop.pop(0)
        while "name" not in loop[-1]: loop.pop()
        loop.append(dict(loop[0]))                 # close the loop on the first point (like the mock)
        # a terminus appears as two stops (arrival side / departure side) with the same name:
        # keep a single dwell point (the closing copy wins over the last stop of the lap)
        last = len(loop) - 1
        prev = None                                   # index of last kept named point
        for i, p in enumerate(loop):
            if "name" not in p: continue
            if prev is not None and loop[prev]["name"] == p["name"] and \
               hav((loop[prev]["lat"], loop[prev]["lon"]), (p["lat"], p["lon"])) < 150:
                if i == last:
                    loop[prev].pop("name"); prev = i      # closing copy wins
                else:
                    p.pop("name")                         # keep the first, drop the second
                    continue
            else:
                prev = i
        rid = PINNED_IDS.get(r.route_short_name)
        if rid is None:
            while next_id in used: next_id += 1
            rid = next_id; used.add(rid)
        doc = {"route_id": rid, "name": r.route_short_name, "points": loop}
        fname = f"route_{r.route_short_name}.json"
        with open(os.path.join(OUT, "routes", fname), "w", encoding="utf-8") as f:
            f.write("{\n    \"route_id\": %d,\n    \"name\": %s,\n    \"points\": [\n" % (rid, json.dumps(r.route_short_name)))
            rows = []
            for p in loop:
                s = '{ "lat": %.6f, "lon": %.6f' % (p["lat"], p["lon"])
                if "name" in p: s += ', "name": ' + json.dumps(p["name"], ensure_ascii=False)
                rows.append("        " + s + " }")
            f.write(",\n".join(rows) + "\n    ]\n}\n")
        length = sum(hav((a["lat"], a["lon"]), (b["lat"], b["lon"])) for a, b in zip(loop, loop[1:]))
        nstops = sum(1 for p in loop if "name" in p)
        mx = max(max(x["snaps"]) for x in dirs.values())
        index.append({"route_id": rid, "name": r.route_short_name, "gtfs_route_id": r.route_id,
                      "long_name": r.route_long_name, "file": "routes/" + fname,
                      "points": len(loop), "named_stops": nstops, "lap_km": round(length/1000, 2),
                      "max_stop_snap_m": round(mx, 1), "shapes": [x["shape"] for x in dirs.values()]})
        if r.route_short_name == SCEN_ROUTE:
            scen_info = (rid, loop, dirs, trips, st, r.route_id)
    with open(os.path.join(OUT, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, ensure_ascii=False, indent=2)
    print(f"{len(index)} routes written")
    if scen_info: make_scenarios(*scen_info)

# ---------------------------------------------------------------- scenarios
def to_s(t):
    h, m, s = map(int, t.split(":")); return h*3600+m*60+s

def sim_time(dist_list, dwell, v_kmh, a, dcl):
    """seconds to drive consecutive stop-to-stop distances stopping at each"""
    v = v_kmh/3.6; tot = 0.0
    for i, d in enumerate(dist_list):
        dacc, ddec = v*v/(2*a), v*v/(2*dcl)
        if d >= dacc+ddec: tot += v/a + v/dcl + (d-dacc-ddec)/v
        else:
            vp = math.sqrt(2*d*a*dcl/(a+dcl)); tot += vp/a + vp/dcl
        if i < len(dist_list)-1: tot += dwell
    return tot

def make_scenarios(rid, loop, dirs, trips, st, gtfs_rid):
    # cumulative metres along the loop + stop positions
    cum = [0.0]
    for a, b in zip(loop, loop[1:]): cum.append(cum[-1]+hav((a["lat"], a["lon"]), (b["lat"], b["lon"])))
    named = [(i, p["name"], cum[i]) for i, p in enumerate(loop) if "name" in p]
    # calibrate cruise speed on a real weekday trip (scheduled run time, direction 0)
    a, dcl, dwell = 1.0, 1.3, 15
    rt = trips[(trips.route_id == gtfs_rid) & (trips.service_id.str.endswith("WDSC"))]
    cal = []
    for d in ("0", "1"):
        td = rt[rt.direction_id == d]
        if td.empty or d not in dirs: continue
        tt = st[st.trip_id == td.trip_id.iloc[len(td)//2]].copy()
        tt["seq"] = tt.stop_sequence.astype(int); tt = tt.sort_values("seq")
        sched = to_s(tt.arrival_time.iloc[-1]) - to_s(tt.departure_time.iloc[0])
        # distances between stops of that direction along the shape polyline
        pl = dirs[d]["poly"]; c = [0.0]
        for p, q in zip(pl, pl[1:]): c.append(c[-1]+hav(p, q))
        proj = project_stops(pl, [(float(s.stop_lat), float(s.stop_lon), s.stop_name) for s in
                                  pd.read_csv(os.path.join(GTFS, "stops.txt"), dtype=str).set_index("stop_id").loc[tt.stop_id].reset_index().itertuples()])
        pos = [p[5] for p in proj]
        dl = [y-x for x, y in zip(pos, pos[1:])]
        lo, hi = 5.0, 90.0
        for _ in range(40):
            mid = (lo+hi)/2
            if sim_time(dl, dwell, mid, a, dcl) > sched: lo = mid
            else: hi = mid
        cal.append((d, sched, round(sum(dl)), round((lo+hi)/2, 1)))
    cruise = round(sum(c[3] for c in cal)/len(cal)) if cal else 30
    print("calibration (dir, scheduled s, metres, cruise km/h):", cal, "->", cruise)
    names = [n for _, n, _ in named]
    L = cum[-1]
    # slow zone: stretch between 2 consecutive stops in the first direction (~city centre part of the loop)
    k = max(1, len(named)//4)
    z_from, z_to = round(named[k][2]), round(named[min(k+2, len(named)-1)][2])
    # tunnel: 400 m centred between two stops two-thirds into the lap (away from the terminals)
    k2 = (len(named)*2)//3
    t_from = round((named[k2][2]+named[k2+1][2])/2 - 200); t_to = t_from+400
    # express: skip 2 intermediate stops of the first direction (avoid terminals and duplicates)
    first_dir = [n for _, n, _ in named[:len(named)//2]]
    uniq = [n for n in first_dir[1:-1] if names.count(n) == 1] or first_dir[1:-1]
    skip = [uniq[len(uniq)//3], uniq[(2*len(uniq))//3]]
    start = len(named)//2 if len(named) > 2 else 0
    scen = {
        "default": "normal",
        "defaults": {"cruise_kmh": cruise, "accel_mps2": 1.0, "decel_mps2": 1.3, "dwell_s": 15,
                     "time_scale": 1, "gps_jitter_m": 0, "loop": True, "autostart": True, "start_stop": 0},
        "scenarios": [
            {"name": "normal", "description": "Regular service, stops everywhere (cruise speed calibrated on the real weekday timetable)"},
            {"name": "demo_fast", "description": "10x time for live demos: a full lap in a few minutes", "time_scale": 10, "dwell_s": 20},
            {"name": "rush_hour", "description": f"Traffic jam between {named[k][1]} and {named[min(k+2,len(named)-1)][1]}, long boarding times",
             "dwell_s": 40, "slow_zones": [{"from_m": z_from, "to_m": z_to, "kmh": 6}]},
            {"name": "express", "description": "Skips " + " and ".join(skip), "cruise_kmh": min(45, cruise+12), "skip_stops": skip},
            {"name": "tunnel", "description": "GPS fix lost for 400 m: the server must cope with fix_ok=false",
             "gps_dropouts": [{"from_m": t_from, "to_m": t_to}]},
            {"name": "noisy_gps", "description": "Urban canyon: +-12 m position noise", "gps_jitter_m": 12},
            {"name": "one_lap", "description": f"Starts at {named[start][1]}, does one lap and parks there (end of shift)",
             "start_stop": start, "loop": False, "autostart": False},
        ]}
    def fmt(o):  # compact like the mock: arrays of zones / stop names on one line
        import re
        t = json.dumps(o, ensure_ascii=False, indent=4)
        def one(m):
            inner = re.sub(r"\s*\n\s*", " ", m.group(2)).strip()
            inner = re.sub(r"\{ ", "{ ", inner)
            return m.group(1) + "[ " + inner + " ]"
        return re.sub(r'("(?:slow_zones|gps_dropouts|skip_stops)": )\[(.*?)\n\s*\]', one, t, flags=re.S)
    with open(os.path.join(OUT, f"scenarios_{SCEN_ROUTE}.json"), "w", encoding="utf-8") as f:
        f.write(fmt(scen) + "\n")
    print("lap length m:", round(L), "named stops:", len(named))

if __name__ == "__main__":
    main()
