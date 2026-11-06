"""Frame metrics and configurable black-flash detection."""
from __future__ import annotations

from pathlib import Path
import hashlib
import math

from PIL import Image, ImageChops, ImageOps, ImageStat


def frame_metrics(path: str | Path, *, black_luma: float = 12.0) -> dict:
    image = Image.open(path).convert("RGB")
    pixels = image.tobytes()
    luminance = image.convert("L")
    hist = luminance.histogram()
    black = sum(hist[:max(0, min(256, int(black_luma) + 1))])
    return {"path": str(path), "width": image.width, "height": image.height,
            "sha256_rgba": hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest(),
            "mean_luma": ImageStat.Stat(luminance).mean[0],
            "near_black_ratio": black / (image.width * image.height)}


def compare_frames(left: str | Path, right: str | Path,
                   *, heatmap: bool = False, heatmap_path: str | Path | None = None) -> dict:
    a = Image.open(left).convert("RGB")
    b = Image.open(right).convert("RGB")
    original_size = {"left": a.size, "right": b.size}
    if a.size != b.size:
        b = b.resize(a.size, Image.Resampling.LANCZOS)
    diff = ImageChops.difference(a, b)
    red, green, blue = diff.split()
    changed_mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    changed = a.width * a.height - changed_mask.histogram()[0]
    gray = ImageChops.difference(a.convert("L"), b.convert("L"))
    histogram = gray.histogram()
    mean_absolute = sum(i * n for i, n in enumerate(histogram)) / (a.width * a.height)
    # Global structural similarity: luminance/contrast/covariance terms.
    ya, yb = a.convert("L"), b.convert("L")
    sa, sb = ImageStat.Stat(ya), ImageStat.Stat(yb)
    ma, mb = sa.mean[0], sb.mean[0]
    va, vb = sa.var[0], sb.var[0]
    covariance = sum((x - ma) * (y - mb) for x, y in zip(ya.getdata(), yb.getdata())) / (a.width * a.height)
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    ssim = ((2 * ma * mb + c1) * (2 * covariance + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))
    result = {"sizes": original_size, "exact_changed_pixels": changed,
              "exact_changed_ratio": changed / (a.width * a.height),
              "mean_absolute_luma_diff": mean_absolute,
              "global_ssim": max(-1.0, min(1.0, ssim)),
              "difference_bbox": changed_mask.getbbox(),
              "left": frame_metrics(left), "right": frame_metrics(right)}
    if heatmap:
        target = Path(heatmap_path) if heatmap_path else Path(left).with_name(Path(left).stem + "-heatmap.png")
        target.parent.mkdir(parents=True, exist_ok=True)
        ImageOps.colorize(gray, black="black", white="red").save(target)
        result["heatmap"] = str(target)
    return result


def temporal_delta(left: str | Path, right: str | Path) -> float:
    """Mean absolute luminance change between two frames of one game."""
    a = Image.open(left).convert("L")
    b = Image.open(right).convert("L")
    if a.size != b.size:
        b = b.resize(a.size, Image.Resampling.BILINEAR)
    hist = ImageChops.difference(a, b).histogram()
    return sum(value * count for value, count in enumerate(hist)) / (a.width * a.height)


def detect_black_window(pairs: list[dict], *, black_ratio: float = 0.95,
                        normal_ratio: float = 0.7, normal_window: int = 4,
                        sync_tolerance_ms: float = 50.0,
                        max_gap_ms: float | None = None) -> list[dict]:
    """Find isolated Kotonoha black runs; classify only synchronized pairs."""
    results = []
    for index, pair in enumerate(pairs):
        k = pair["kotonoha"]["near_black_ratio"]
        if k < black_ratio:
            continue
        before = next((n for n in range(index - 1, max(-1, index - normal_window - 1), -1)
                       if pairs[n]["kotonoha"]["near_black_ratio"] < normal_ratio), None)
        after = next((n for n in range(index + 1, min(len(pairs), index + normal_window + 1))
                      if pairs[n]["kotonoha"]["near_black_ratio"] < normal_ratio), None)
        if before is None or after is None:
            continue
        original_black = any(pairs[n]["original"]["near_black_ratio"] >= black_ratio
                             for n in range(before, after + 1))
        synchronized = all(pairs[n].get("syncUpperBoundMs", pairs[n]["captureDeltaMs"]) <= sync_tolerance_ms
                           for n in range(before, after + 1))
        sufficiently_sampled = (max_gap_ms is None or all(
            (pairs[n][side]["capturedMonotonicNs"] -
             pairs[n - 1][side]["capturedMonotonicNs"]) / 1e6 <= max_gap_ms
            for n in range(before + 1, after + 1)
            for side in ("original", "kotonoha")))
        if original_black:
            continue
        results.append({"index": index, "before": before, "after": after,
                        "classification": ("BLACK_FLASH_DIVERGENCE" if synchronized and sufficiently_sampled else
                                           "BLACK_FLASH_CANDIDATE"),
                        "synchronized": synchronized,
                        "sufficientlySampled": sufficiently_sampled,
                        "kotonohaBlackRatio": k,
                        "originalBlackRatio": pair["original"]["near_black_ratio"],
                        "deltaFromPrevious": pair.get("kotonohaDeltaFromPrevious"),
                        "deltaToNext": pair.get("kotonohaDeltaToNext")})
    return results


def unexpected_black_flash(history: list[dict], *, ratio: float = 0.95,
                           normal_ratio: float = 0.7,
                           original_tolerance: float = 0.2) -> dict:
    """Detect an isolated black Kotonoha frame absent in the original."""
    if len(history) < 3:
        return {"detected": False, "reason": "need three paired captures"}
    previous, current, following = history[-3:]
    k = [item["kotonoha"]["near_black_ratio"] for item in (previous, current, following)]
    o = current["original"]["near_black_ratio"]
    detected = k[0] < normal_ratio and k[1] >= ratio and k[2] < normal_ratio and o + original_tolerance < k[1]
    return {"detected": detected,
            "classification": "BLACK_FLASH_DIVERGENCE" if detected else None,
            "kotonoha_ratios": k, "original_middle_ratio": o,
            "thresholds": {"black_ratio": ratio, "normal_ratio": normal_ratio,
                           "original_tolerance": original_tolerance}}
