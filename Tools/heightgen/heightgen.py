"""Spec-driven heightmap generator for Unreal Landscape.

Reads a JSON terrain spec and writes:
  <out>.r16          16-bit little-endian heightmap (value 32768 = landscape zero level)
  <out>_preview.png  8-bit hillshade preview with water tinted, for quick review
  <out>_report.json  stats: height range, slope distribution, walkable share

Coordinates in the spec are Unreal world axes in meters relative to the landscape center
(+X = right and +Y = down in the editor's top view, same as the preview image).
Heights are meters relative to the zero level. Runs outside the editor.

Usage: python heightgen.py <spec.json> [--out <path-without-extension>]
"""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------- noise


def value_noise(shape: tuple[int, int], cell: float, rng: np.random.Generator) -> np.ndarray:
    """Smooth value noise in [-1, 1] with features roughly `cell` samples apart."""
    h, w = shape
    gh, gw = int(np.ceil(h / cell)) + 2, int(np.ceil(w / cell)) + 2
    grid = rng.uniform(-1.0, 1.0, (gh, gw))
    y = np.arange(h) / cell
    x = np.arange(w) / cell
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int)
    ty, tx = y - y0, x - x0
    ty = ty * ty * (3 - 2 * ty)
    tx = tx * tx * (3 - 2 * tx)
    a = grid[np.ix_(y0, x0)]
    b = grid[np.ix_(y0, x0 + 1)]
    c = grid[np.ix_(y0 + 1, x0)]
    d = grid[np.ix_(y0 + 1, x0 + 1)]
    top = a + (b - a) * tx[None, :]
    bottom = c + (d - c) * tx[None, :]
    return top + (bottom - top) * ty[:, None]


def fbm(shape, scale_m, octaves, persistence, rng, meters_per_px):
    """Fractal noise normalized to roughly [-1, 1]."""
    total = np.zeros(shape)
    amp, norm = 1.0, 0.0
    cell = scale_m / meters_per_px
    for _ in range(octaves):
        total += amp * value_noise(shape, max(cell, 2.0), rng)
        norm += amp
        amp *= persistence
        cell /= 2.0
    return total / norm


# ---------------------------------------------------------------- shapes


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def ellipse_distance(X, Y, center, radii, rotation_deg=0.0):
    """Normalized elliptical distance: 1.0 on the ellipse boundary."""
    a = np.deg2rad(rotation_deg)
    dx, dy = X - center[0], Y - center[1]
    rx = dx * np.cos(a) + dy * np.sin(a)
    ry = -dx * np.sin(a) + dy * np.cos(a)
    return np.sqrt((rx / radii[0]) ** 2 + (ry / radii[1]) ** 2)


def region_mask(X, Y, f):
    """0..1 mask: 1 inside the ellipse, smooth falloff of `falloff` meters outside."""
    radii = f.get("radii", [f.get("radius", 50), f.get("radius", 50)])
    d = ellipse_distance(X, Y, f["center"], radii, f.get("rotation", 0.0))
    falloff_norm = f.get("falloff", 30.0) / min(radii)
    return 1.0 - smoothstep(1.0, 1.0 + falloff_norm, d)


# ---------------------------------------------------------------- erosion


def thermal_erosion(H, meters_per_px, talus_deg, iterations, rate=0.4):
    """Moves material down slopes steeper than the talus angle."""
    talus = np.tan(np.deg2rad(talus_deg)) * meters_per_px
    for _ in range(iterations):
        moved = np.zeros_like(H)
        for dy, dx in ((0, 1), (1, 0), (0, -1), (-1, 0)):
            neighbor = np.roll(np.roll(H, dy, 0), dx, 1)
            diff = H - neighbor
            excess = np.where(diff > talus, (diff - talus) * rate * 0.25, 0.0)
            moved -= excess
            moved += np.roll(np.roll(excess, -dy, 0), -dx, 1)
        H = H + moved
    return H


# ---------------------------------------------------------------- generation


def generate(spec: dict) -> tuple[np.ndarray, dict]:
    res = spec["resolution"]
    mpp = spec["meters_per_vertex"]
    rng = np.random.default_rng(spec.get("seed", 1))
    half = (res - 1) * mpp / 2.0
    coords = np.linspace(-half, half, res)
    # Row i maps to landscape Y, column j to X, exactly as Unreal imports heightmaps,
    # so spec coordinates are Unreal world meters and the preview matches the editor's top view.
    X, Y = np.meshgrid(coords, coords)
    R = np.hypot(X - spec.get("origin", [0, 0])[0], Y - spec.get("origin", [0, 0])[1])

    H = np.full((res, res), float(spec.get("base_height", 0.0)))

    # Roughness grows with distance from the origin (danger rings).
    rings = spec.get("rings", [])
    rough = np.full_like(H, rings[0]["roughness"] if rings else 1.0)
    for inner, outer in zip(rings, rings[1:]):
        t = smoothstep(inner["radius"], outer["radius"], R)
        rough = rough + (outer["roughness"] - inner["roughness"]) * t * (R >= inner["radius"])

    for layer in spec.get("noise", []):
        n = fbm(H.shape, layer["scale"], layer.get("octaves", 5), layer.get("persistence", 0.5), rng, mpp)
        if layer.get("ridged"):
            n = 1.0 - 2.0 * np.abs(n)
        H += n * layer["amplitude"] * (rough if layer.get("use_rings", True) else 1.0)

    for f in spec.get("features", []):
        kind = f["type"]
        if kind == "hill":
            m = region_mask(X, Y, {**f, "falloff": f.get("falloff", f.get("radius", 50))})
            H += f["height"] * m
        elif kind == "flatten":
            m = region_mask(X, Y, f) * f.get("strength", 1.0)
            H = H * (1 - m) + f["height"] * m
        elif kind == "calm":  # scale down local noise relative to a target height
            m = region_mask(X, Y, f)
            H = H - (H - f["height"]) * m * (1.0 - f.get("keep", 0.25))
        elif kind == "water":
            m = region_mask(X, Y, f)
            H = H * (1 - m) + f["depth"] * m
        elif kind == "boundary":
            jitter = fbm(H.shape, f.get("jitter_scale", 120), 3, 0.5, rng, mpp) * f.get("jitter", 25)
            rr = R + jitter
            wall = smoothstep(f["radius"], f["radius"] + f["width"], rr)
            cliff_noise = fbm(H.shape, 40, 4, 0.55, rng, mpp) * f.get("roughness", 8)
            wall_h = (f["height"] + cliff_noise) * wall
            exclude = np.zeros_like(H)
            for ex in f.get("exclude", []):
                exclude = np.maximum(exclude, region_mask(X, Y, ex))
            H += wall_h * (1 - exclude)
        else:
            raise ValueError(f"Unknown feature type '{kind}'")

    erosion = spec.get("erosion")
    if erosion:
        H = thermal_erosion(H, mpp, erosion.get("talus_deg", 38), erosion.get("iterations", 30))
        # Re-assert water surfaces after erosion so lake beds stay flat-bottomed.
        for f in spec.get("features", []):
            if f["type"] == "water":
                m = region_mask(X, Y, f)
                H = np.where(m > 0.98, np.minimum(H, f["depth"]), H)

    return H, {"X": X, "Y": Y, "R": R}


# ---------------------------------------------------------------- output


def to_r16(H: np.ndarray, height_range_m: float) -> np.ndarray:
    scale = 65536.0 / height_range_m  # units per meter
    return np.clip(np.round(H * scale + 32768.0), 0, 65535).astype("<u2")


def write_png_gray(path: Path, img: np.ndarray) -> None:
    h, w = img.shape[:2]
    channels = 1 if img.ndim == 2 else img.shape[2]
    color_type = 0 if channels == 1 else 2
    raw = b"".join(b"\x00" + img[y].astype(np.uint8).tobytes() for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def hillshade_preview(H, mpp, focus_mask, contour_m=2.0, water_level=0.0):
    """Hillshade + elevation tint normalized over `focus_mask`, with contour lines."""
    gy, gx = np.gradient(H, mpp)
    slope = np.arctan(np.hypot(gx, gy))
    aspect = np.arctan2(-gx, gy)
    az, alt = np.deg2rad(315), np.deg2rad(45)
    shade = np.clip(np.sin(alt) * np.cos(slope) + np.cos(alt) * np.sin(slope) * np.cos(az - aspect), 0, 1)
    sample = H[focus_mask & (H > water_level)] if (focus_mask & (H > water_level)).any() else H.ravel()
    lo, hi = np.percentile(sample, 1), np.percentile(sample, 99)
    hn = np.clip((H - lo) / max(hi - lo, 1e-6), 0, 1)
    # Low = green-grey, high = warm sand.
    tint = np.stack([0.45 + 0.4 * hn, 0.5 + 0.3 * hn, 0.38 + 0.2 * hn], axis=-1)
    rgb = tint * (0.45 + 0.55 * shade[..., None])
    band = np.floor(H / contour_m)
    edge = (band != np.roll(band, 1, 0)) | (band != np.roll(band, 1, 1))
    rgb[edge] *= 0.72
    water = H <= water_level + 0.05
    rgb[water] = np.array([0.16, 0.3, 0.5]) * (0.8 + 0.2 * shade[water][..., None])
    return (np.clip(rgb, 0, 1) * 255).astype(np.uint8), np.rad2deg(slope)


def report(H, slope_deg, spec, R):
    land = H > 0.05
    walk = spec.get("walkable_deg", 44.76)
    inside = land & (R < spec.get("report_radius", 1e9))
    return {
        "height_min_m": round(float(H.min()), 2),
        "height_max_m": round(float(H.max()), 2),
        "land_share": round(float(land.mean()), 3),
        "walkable_share_of_land_in_radius": round(float((slope_deg[inside] < walk).mean()), 3),
        "slope_percentiles_deg": {p: round(float(np.percentile(slope_deg[land], p)), 1) for p in (50, 90, 99)},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("--out", help="output path without extension (default: next to the spec)")
    args = ap.parse_args()

    spec_path = Path(args.spec)
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    # "World.spec.json" -> "World". The spec must not be named "<out>.json": Unreal reads
    # "<heightmap>.json" next to a .r16 file as raw-format metadata (width/height/bpp).
    out = Path(args.out) if args.out else spec_path.parent / spec_path.name.split(".")[0]
    if out.with_suffix(".json").resolve() == spec_path.resolve():
        raise SystemExit("Rename the spec (e.g. World.spec.json): Unreal treats <name>.json as .r16 metadata.")

    H, grid = generate(spec)
    rng_m = spec["height_range_m"]
    if H.min() < -rng_m / 2 or H.max() > rng_m / 2:
        raise SystemExit(f"Heights {H.min():.1f}..{H.max():.1f} m exceed height_range_m ±{rng_m / 2} m")

    out.parent.mkdir(parents=True, exist_ok=True)
    to_r16(H, rng_m).tofile(out.with_suffix(".r16"))
    focus = grid["R"] < spec.get("report_radius", 1e9)
    preview, slope = hillshade_preview(H, spec["meters_per_vertex"], focus, spec.get("contour_m", 2.0))
    write_png_gray(out.parent / (out.name + "_preview.png"), preview)
    stats = report(H, slope, spec, grid["R"])
    (out.parent / (out.name + "_report.json")).write_text(json.dumps(stats, indent=2), encoding="utf-8")
    print(json.dumps({"r16": str(out.with_suffix(".r16")), **stats}, indent=2))


if __name__ == "__main__":
    main()
