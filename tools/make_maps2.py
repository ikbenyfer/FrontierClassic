# Extra, more detailed maps from Natural Earth 1:10m data (public domain).
#
#   python -I tools/make_maps2.py <dir with ne_10m_admin_0_countries.geojson,
#                                   ne_10m_lakes.geojson, ne_10m_populated_places_simple.geojson>
#                                  <out dir (Resources/maps)>
#
# Same file format as tools/make_maps.py, plus terrain 7 = "blocked": land of
# countries that are not part of the map (shown grey, nobody can take it).
# Maps of one or a few countries use cities as the AI nations.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

DEEP, SHALLOW, PLAINS, HILLS, MOUNTAINS, DESERT, TUNDRA, BLOCKED = range(8)

# id, name, lon0, lon1, lat0, lat1, W, H, playable countries (ADM0_A3, None = all),
# nations ("cities" or "countries"), relief (0 flat .. 1 mountainous)
MAPS = [
    ("netherlands", "Nederland", 3.2, 7.3, 50.7, 53.65, 630, 740, ["NLD"], "cities", 0.0),
    ("britishisles", "British Isles", -10.8, 2.2, 49.8, 60.95, 500, 755, ["GBR", "IRL", "IMN", "GGY", "JEY"], "cities", 0.6),
    ("scandinavia", "Scandinavia", 4.0, 32.0, 54.5, 71.3, 560, 740, ["NOR", "SWE", "FIN", "DNK", "ALD"], "cities", 0.8),
    ("japan", "Japan", 128.5, 146.0, 30.5, 45.8, 660, 730, ["JPN"], "cities", 1.0),
    ("middleeast", "Middle East", 25.0, 63.0, 12.0, 42.0, 860, 760, None, "countries", 0.8),
    ("oceania", "Oceania", 110.0, 180.0, -48.0, -9.0, 1000, 634, None, "countries", 0.7),
]

# Natural Earth has only 14 Dutch cities; these are added (approximate centres).
NL_CITIES = [
    ("Amsterdam", 52.37, 4.90), ("Rotterdam", 51.92, 4.48), ("Den Haag", 52.08, 4.30), ("Utrecht", 52.09, 5.12),
    ("Eindhoven", 51.44, 5.47), ("Groningen", 53.22, 6.57), ("Tilburg", 51.56, 5.09), ("Almere", 52.37, 5.21),
    ("Breda", 51.59, 4.78), ("Nijmegen", 51.84, 5.86), ("Enschede", 52.22, 6.89), ("Haarlem", 52.38, 4.64),
    ("Arnhem", 51.98, 5.91), ("Zaanstad", 52.44, 4.83), ("Amersfoort", 52.16, 5.39), ("Apeldoorn", 52.21, 5.97),
    ("'s-Hertogenbosch", 51.69, 5.30), ("Maastricht", 50.85, 5.69), ("Leiden", 52.16, 4.49), ("Dordrecht", 51.81, 4.67),
    ("Zoetermeer", 52.06, 4.49), ("Zwolle", 52.52, 6.08), ("Deventer", 52.26, 6.16), ("Delft", 52.01, 4.36),
    ("Alkmaar", 52.63, 4.75), ("Heerlen", 50.89, 5.98), ("Venlo", 51.37, 6.17), ("Leeuwarden", 53.20, 5.80),
    ("Hilversum", 52.22, 5.18), ("Hengelo", 52.27, 6.79), ("Roosendaal", 51.53, 4.46), ("Purmerend", 52.50, 4.95),
    ("Oss", 51.77, 5.52), ("Helmond", 51.48, 5.66), ("Almelo", 52.36, 6.66), ("Gouda", 52.01, 4.71),
    ("Lelystad", 52.52, 5.47), ("Hoorn", 52.64, 5.06), ("Ede", 52.04, 5.67), ("Bergen op Zoom", 51.49, 4.29),
    ("Assen", 52.99, 6.56), ("Emmen", 52.79, 6.90), ("Den Helder", 52.96, 4.76), ("Middelburg", 51.50, 3.61),
    ("Vlissingen", 51.44, 3.57), ("Terneuzen", 51.34, 3.83), ("Harderwijk", 52.34, 5.62), ("Kampen", 52.56, 5.91),
    ("Sneek", 53.03, 5.66), ("Drachten", 53.11, 6.10), ("Heerenveen", 52.96, 5.92), ("Doetinchem", 51.97, 6.29),
    ("Winterswijk", 51.97, 6.72), ("Roermond", 51.19, 5.99), ("Sittard", 51.00, 5.87), ("Weert", 51.25, 5.71),
    ("Goes", 51.50, 3.89), ("Den Burg", 53.05, 4.80), ("Meppel", 52.70, 6.19), ("Hardenberg", 52.58, 6.62),
    ("Tiel", 51.89, 5.43), ("Zutphen", 52.14, 6.20), ("Veenendaal", 52.03, 5.56), ("Gorinchem", 51.83, 4.97),
    ("Hoogeveen", 52.72, 6.48), ("Stadskanaal", 52.99, 6.95), ("Delfzijl", 53.33, 6.92), ("Harlingen", 53.17, 5.42),
    ("Enkhuizen", 52.70, 5.29), ("Schagen", 52.79, 4.80), ("Emmeloord", 52.71, 5.75), ("Zeist", 52.09, 5.23),
]
DESERT_MAPS = {"middleeast", "oceania"}
RENAME = {"The Hague": "Den Haag"}


def value_noise(w, h, seed, octaves=5, base=24.0):
    rng = np.random.RandomState(seed)
    total = np.zeros((h, w), np.float32)
    amp, norm, scale = 1.0, 0.0, base
    for _ in range(octaves):
        gw, gh = int(w / scale) + 2, int(h / scale) + 2
        grid = rng.rand(gh, gw).astype(np.float32)
        img = Image.fromarray((grid * 255).astype(np.uint8), "L")
        img = img.resize((int(gw * scale), int(gh * scale)), Image.BICUBIC)
        total += np.asarray(img, np.float32)[:h, :w] / 255.0 * amp
        norm += amp
        amp *= 0.5
        scale /= 2.0
        if scale < 1.5:
            break
    return total / norm


def polygons(geom):
    if geom["type"] == "Polygon":
        return [geom["coordinates"]]
    if geom["type"] == "MultiPolygon":
        return geom["coordinates"]
    return []


def ascii_name(s):
    s = RENAME.get(s, s)
    return "".join(c for c in s if 32 <= ord(c) < 127).strip()


def make_map(countries, lakes, places, spec, out_dir, seed, prev_dir):
    mid, name, lon0, lon1, lat0, lat1, W, H, allowed, nation_mode, relief = spec

    def proj(lon, lat):
        return ((lon - lon0) / (lon1 - lon0) * W, (lat1 - lat) / (lat1 - lat0) * H)

    def inside(pts):
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        return not (max(xs) < 0 or min(xs) > W or max(ys) < 0 or min(ys) > H)

    # country ids; 65535 = land of a country that is not on this map
    ids = Image.new("I", (W, H), 0)
    draw = ImageDraw.Draw(ids)
    BLOCK = 65535
    country_info = []
    for idx, f in enumerate(countries):
        props = f["properties"]
        a3 = props.get("ADM0_A3") or props.get("ISO_A3")
        playable = allowed is None or a3 in allowed
        fill = idx + 1 if playable else BLOCK
        best = None
        for poly in polygons(f["geometry"]):
            pts = [proj(lon, lat) for lon, lat in poly[0]]
            if not inside(pts):
                continue
            draw.polygon(pts, fill=fill)
            for hole in poly[1:]:
                draw.polygon([proj(lon, lat) for lon, lat in hole], fill=0)
            a = cx = cy = 0.0
            for i in range(len(pts) - 1):
                x0, y0 = pts[i]
                x1, y1 = pts[i + 1]
                c = x0 * y1 - x1 * y0
                a += c
                cx += (x0 + x1) * c
                cy += (y0 + y1) * c
            if abs(a) < 1e-9:
                continue
            cx /= 3 * a
            cy /= 3 * a
            area = abs(a) / 2
            if 2 <= cx < W - 2 and 2 <= cy < H - 2 and (best is None or area > best[0]):
                best = (area, cx, cy)
        if playable and best and best[0] >= 40:
            country_info.append((best[0], ascii_name(props.get("NAME") or props.get("ADMIN") or "Nation"),
                                 int(best[1]), int(best[2]), idx + 1))
    # lakes become water (IJsselmeer, Markermeer, ...)
    for f in lakes:
        for poly in polygons(f["geometry"]):
            pts = [proj(lon, lat) for lon, lat in poly[0]]
            if inside(pts) and len(pts) > 2:
                draw.polygon(pts, fill=0)

    cid = np.asarray(ids, np.int64)
    blocked = cid == BLOCK
    land = (cid > 0) & ~blocked
    anyland = cid > 0

    near = anyland.copy()
    for _ in range(2):
        n = near.copy()
        n[1:, :] |= near[:-1, :]
        n[:-1, :] |= near[1:, :]
        n[:, 1:] |= near[:, :-1]
        n[:, :-1] |= near[:, 1:]
        near = n

    base = max(W, H) / 14.0
    elev = value_noise(W, H, seed, base=base)
    moist = value_noise(W, H, seed + 99, base=max(W, H) / 10.0)
    jit = (value_noise(W, H, seed + 7, base=max(W, H) / 30.0) - 0.5) * 14.0
    lat = lat1 - (np.arange(H, dtype=np.float32) + 0.5) / H * (lat1 - lat0)
    nlat = np.abs(lat)[:, None] * np.ones((1, W), np.float32) + jit

    terr = np.full((H, W), DEEP, np.uint8)
    terr[near & ~anyland] = SHALLOW
    terr[land] = PLAINS
    hills_t = 0.58 + (1 - relief) * 0.2      # flat maps get (almost) no hills
    mount_t = 0.68 + (1 - relief) * 0.4
    terr[land & (elev > hills_t)] = HILLS
    terr[land & (elev > mount_t)] = MOUNTAINS
    if mid in DESERT_MAPS:  # no deserts in Japan or north-west Europe
        terr[land & (nlat > 14) & (nlat < 34) & (moist < 0.5) & (terr == PLAINS)] = DESERT
    terr[land & (nlat > 63) & (terr == PLAINS)] = TUNDRA
    terr[blocked] = BLOCKED

    border = np.zeros((H, W), bool)
    border[:, 1:] |= (cid[:, 1:] != cid[:, :-1]) & land[:, 1:] & land[:, :-1]
    border[1:, :] |= (cid[1:, :] != cid[:-1, :]) & land[1:, :] & land[:-1, :]
    terr[border] |= 0x10

    # ---- nations
    cand = []
    if nation_mode == "countries":
        for area, cname, x, y, ci in sorted(country_info, key=lambda n: -n[0]):
            cand.append((cname, x, y, ci))
    city_list = []
    if mid == "netherlands":
        city_list += [(n, la, lo) for n, la, lo in NL_CITIES]
    for f in sorted(places, key=lambda f: -(f["properties"].get("pop_max") or 0)):
        p = f["properties"]
        if allowed is not None and p.get("adm0_a3") not in allowed:
            continue
        city_list.append((ascii_name(p.get("nameascii") or p.get("name") or ""), p["latitude"], p["longitude"]))
    for cname, la, lo in city_list:
        x, y = proj(lo, la)
        if 1 <= x < W - 1 and 1 <= y < H - 1 and cname:
            cand.append((cname, int(x), int(y), None))

    nations, used, names = [], [], set()
    ys_land, xs_land = np.nonzero(land)
    for cname, x, y, ci in cand:
        if cname in names:
            continue
        if not land[y, x]:
            d = (xs_land - x) ** 2 + (ys_land - y) ** 2
            k = int(np.argmin(d))
            if d[k] > 36:
                continue
            x, y = int(xs_land[k]), int(ys_land[k])
        if any((x - ux) ** 2 + (y - uy) ** 2 < 49 for ux, uy in used):
            continue
        used.append((x, y))
        names.add(cname)
        nations.append({"name": cname, "x": x, "y": y})
        if len(nations) >= 70:
            break

    terr.tofile(os.path.join(out_dir, mid + ".bin"))
    meta = {"id": mid, "name": name, "width": W, "height": H, "file": mid + ".bin",
            "landTiles": int(land.sum()), "nations": nations}
    with open(os.path.join(out_dir, mid + ".json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=True, indent=1)

    pal = np.array([[30, 60, 110], [52, 96, 150], [120, 160, 90], [140, 140, 90],
                    [130, 120, 110], [210, 190, 130], [200, 210, 210], [74, 78, 84]], np.uint8)
    rgb = pal[terr & 0x0F]
    rgb[border] = (rgb[border] * 0.7).astype(np.uint8)
    for n in nations:
        rgb[max(0, n["y"] - 1):n["y"] + 2, max(0, n["x"] - 1):n["x"] + 2] = (255, 0, 0)
    Image.fromarray(rgb, "RGB").save(os.path.join(prev_dir, mid + ".png"))
    print("%-13s %4dx%-4d land %6d  nations %d" % (mid, W, H, int(land.sum()), len(nations)))
    return meta


def main():
    src, out_dir = sys.argv[1], sys.argv[2]
    prev_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "previews")
    os.makedirs(prev_dir, exist_ok=True)
    load = lambda n: json.load(open(os.path.join(src, n), encoding="utf-8"))["features"]
    countries = load("ne_10m_admin_0_countries.geojson")
    lakes = load("ne_10m_lakes.geojson")
    places = load("ne_10m_populated_places_simple.geojson")
    with open(os.path.join(out_dir, "index.json"), encoding="utf-8") as f:
        index = json.load(f)
    keep = [m for m in index["maps"] if m["id"] not in {s[0] for s in MAPS}]
    new = []
    for i, spec in enumerate(MAPS):
        meta = make_map(countries, lakes, places, spec, out_dir, 2000 + i * 31, prev_dir)
        new.append({"id": meta["id"], "name": meta["name"], "file": meta["id"] + ".json"})
    # Nederland right after the world map, the rest at the end
    maps = keep[:1] + new[:1] + keep[1:] + new[1:]
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8") as f:
        json.dump({"maps": maps}, f, indent=1)
    print("maps in index:", [m["id"] for m in maps])


if __name__ == "__main__":
    main()
