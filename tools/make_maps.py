# Generates the offline map files in Resources/maps from Natural Earth
# (public domain) country shapes.
#
#   python -I tools/make_maps.py <ne_50m_admin_0_countries.geojson> <out dir>
#
# Each map is a raw grid of 1 byte per tile:
#   bits 0-3: terrain (0 deep water, 1 shallow water, 2 plains, 3 hills,
#             4 mountains, 5 desert, 6 tundra)
#   bit 4   : a real country border runs through this tile (drawn faintly)
# plus a small JSON file with size and nation start points.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

MAPS = [
    # id, name, lon0, lon1, lat0, lat1, width, height
    ("world", "World Map", -180.0, 180.0, -58.0, 80.0, 1024, 400),
    ("europe", "Europe", -25.0, 45.0, 34.0, 72.0, 760, 700),
    ("asia", "Asia", 25.0, 150.0, -10.0, 78.0, 880, 740),
    ("northamerica", "North America", -170.0, -50.0, 7.0, 75.0, 880, 680),
    ("southamerica", "South America", -92.0, -30.0, -57.0, 14.0, 620, 760),
    ("africa", "Africa", -20.0, 55.0, -36.0, 38.0, 740, 740),
]

DEEP, SHALLOW, PLAINS, HILLS, MOUNTAINS, DESERT, TUNDRA = range(7)


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


def make_map(features, spec, out_dir, seed):
    mid, name, lon0, lon1, lat0, lat1, W, H = spec

    def proj(lon, lat):
        return ((lon - lon0) / (lon1 - lon0) * W, (lat1 - lat) / (lat1 - lat0) * H)

    ids = Image.new("I", (W, H), 0)
    draw = ImageDraw.Draw(ids)
    nations = []
    for idx, f in enumerate(features):
        props = f["properties"]
        cname = props.get("NAME") or props.get("ADMIN") or "Nation"
        best = None
        for poly in polygons(f["geometry"]):
            outer = poly[0]
            pts = [proj(lon, lat) for lon, lat in outer]
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            if max(xs) < 0 or min(xs) > W or max(ys) < 0 or min(ys) > H:
                continue
            draw.polygon(pts, fill=idx + 1)
            for hole in poly[1:]:
                draw.polygon([proj(lon, lat) for lon, lat in hole], fill=0)
            # area + centroid of this ring (shoelace), clipped to the map
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
        if best and best[0] >= 12:
            nations.append((best[0], cname, int(best[1]), int(best[2]), idx + 1))

    cid = np.asarray(ids, np.int32)
    land = cid > 0

    # distance-to-land for shallow water (2 tiles)
    near = land.copy()
    for _ in range(2):
        n = near.copy()
        n[1:, :] |= near[:-1, :]
        n[:-1, :] |= near[1:, :]
        n[:, 1:] |= near[:, :-1]
        n[:, :-1] |= near[:, 1:]
        near = n

    elev = value_noise(W, H, seed, base=max(W, H) / 14.0)
    moist = value_noise(W, H, seed + 99, base=max(W, H) / 10.0)
    lat = lat1 - (np.arange(H, dtype=np.float32) + 0.5) / H * (lat1 - lat0)
    alat = np.abs(lat)[:, None] * np.ones((1, W), np.float32)

    terr = np.full((H, W), DEEP, np.uint8)
    terr[near & ~land] = SHALLOW
    terr[land] = PLAINS
    terr[land & (elev > 0.58)] = HILLS
    terr[land & (elev > 0.68)] = MOUNTAINS
    jit = (value_noise(W, H, seed + 7, base=max(W, H) / 30.0) - 0.5) * 14.0
    nlat = alat + jit
    desert = land & (nlat > 14) & (nlat < 34) & (moist < 0.5) & (terr == PLAINS)
    terr[desert] = DESERT
    terr[land & (nlat > 63) & (terr == PLAINS)] = TUNDRA

    # real country borders (only between two different countries on land)
    border = np.zeros((H, W), bool)
    diff_r = (cid[:, 1:] != cid[:, :-1]) & land[:, 1:] & land[:, :-1]
    diff_d = (cid[1:, :] != cid[:-1, :]) & land[1:, :] & land[:-1, :]
    border[:, 1:] |= diff_r
    border[1:, :] |= diff_d
    terr[border] |= 0x10

    # nation start points must be on land of their own country
    out_nations = []
    nations.sort(key=lambda n: -n[0])
    used = []
    for area, cname, x, y, ci in nations:
        if cid[y, x] != ci:
            ys, xs = np.nonzero(cid == ci)
            if len(xs) == 0:
                continue
            d = (xs - x) ** 2 + (ys - y) ** 2
            k = int(np.argmin(d))
            x, y = int(xs[k]), int(ys[k])
        if any((x - ux) ** 2 + (y - uy) ** 2 < 36 for ux, uy in used):
            continue
        used.append((x, y))
        out_nations.append({"name": cname, "x": x, "y": y})
        if len(out_nations) >= 80:
            break

    terr.tofile(os.path.join(out_dir, mid + ".bin"))
    meta = {
        "id": mid, "name": name, "width": W, "height": H, "file": mid + ".bin",
        "landTiles": int(land.sum()), "nations": out_nations,
    }
    with open(os.path.join(out_dir, mid + ".json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=True, indent=1)

    # preview for checking by eye (not shipped)
    pal = np.array([[30, 60, 110], [52, 96, 150], [120, 160, 90], [140, 140, 90],
                    [130, 120, 110], [210, 190, 130], [200, 210, 210]], np.uint8)
    rgb = pal[terr & 0x0F]
    rgb[border] = (rgb[border] * 0.7).astype(np.uint8)
    for n in out_nations:
        rgb[max(0, n["y"] - 1):n["y"] + 2, max(0, n["x"] - 1):n["x"] + 2] = (255, 0, 0)
    prev_dir = os.path.join(out_dir, "..", "..", "tools", "previews")
    os.makedirs(prev_dir, exist_ok=True)
    Image.fromarray(rgb, "RGB").save(os.path.join(prev_dir, mid + ".png"))
    print("%-13s %4dx%-4d land %6d  nations %d" % (mid, W, H, int(land.sum()), len(out_nations)))
    return meta


def main():
    src, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    with open(src, encoding="utf-8") as f:
        features = json.load(f)["features"]
    index = []
    for i, spec in enumerate(MAPS):
        meta = make_map(features, spec, out_dir, 1000 + i * 17)
        index.append({"id": meta["id"], "name": meta["name"], "file": meta["id"] + ".json"})
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8") as f:
        json.dump({"maps": index}, f, indent=1)


if __name__ == "__main__":
    main()
