#!/usr/bin/env python3
from __future__ import annotations

import csv
import math
import sys
from collections import defaultdict
from pathlib import Path


def number(value: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return float("nan")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def read_pfm(path: Path):
    try:
        import numpy as np
    except ImportError as error:
        raise SystemExit("PFM analysis requires numpy: python -m pip install numpy") from error

    with path.open("rb") as stream:
        header = stream.readline().decode("ascii").strip()
        if header not in {"PF", "Pf"}:
            raise ValueError(f"{path}: unsupported PFM header {header!r}")
        dimensions = stream.readline().decode("ascii").strip().split()
        width, height = map(int, dimensions)
        scale = float(stream.readline().decode("ascii").strip())
        endian = "<" if scale < 0 else ">"
        channels = 3 if header == "PF" else 1
        data = np.fromfile(stream, dtype=endian + "f4")
        expected = width * height * channels
        if data.size != expected:
            raise ValueError(f"{path}: expected {expected} floats, got {data.size}")
        image = data.reshape((height, width, channels))
        return np.flipud(image)


def image_metrics(a_path: Path, b_path: Path) -> tuple[float, float, float]:
    import numpy as np

    a = read_pfm(a_path).astype(np.float64)
    b = read_pfm(b_path).astype(np.float64)
    if a.shape != b.shape:
        return float("nan"), float("nan"), float("nan")
    difference = a - b
    mse = float(np.mean(difference * difference))
    rmse = math.sqrt(mse)
    peak = max(float(np.max(np.abs(a))), float(np.max(np.abs(b))), 1.0)
    psnr = float("inf") if mse == 0.0 else 10.0 * math.log10((peak * peak) / mse)
    max_abs = float(np.max(np.abs(difference)))
    return rmse, psnr, max_abs


def finite_mean(values: list[float]) -> float:
    values = [value for value in values if math.isfinite(value)]
    return sum(values) / len(values) if values else float("nan")


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/publication")
    summary_path = root / "summary.csv"
    captures_path = root / "captures.csv"
    if not summary_path.is_file():
        raise SystemExit(f"Missing {summary_path}. Run the publication suite first.")

    summary = read_csv(summary_path)
    pairs: dict[str, dict[str, dict[str, str]]] = defaultdict(dict)
    for row in summary:
        pairs[row["pair_id"]][row["method"]] = row

    capture_pairs: dict[str, dict[str, dict[str, str]]] = defaultdict(dict)
    if captures_path.is_file():
        for row in read_csv(captures_path):
            capture_pairs[row["pair_id"]][row["method"]] = row

    output_rows: list[dict[str, object]] = []
    for pair_id, methods in pairs.items():
        baseline = methods.get("direction-only")
        local = methods.get("local-ray")
        if not baseline or not local:
            continue

        baseline_ms = number(baseline["median_ms"])
        local_ms = number(local["median_ms"])
        row: dict[str, object] = {
            "pair_id": pair_id,
            "sweep": baseline["sweep"],
            "step": baseline["step"],
            "sky_mode": baseline["sky_mode"],
            "width": baseline["width"],
            "height": baseline["height"],
            "sky_radius": baseline["sky_radius"],
            "camera_offset_x": baseline["camera_offset_x"],
            "sphere_x": baseline["sphere_x"],
            "null_test": baseline["null_test"],
            "baseline_median_ms": baseline_ms,
            "local_median_ms": local_ms,
            "delta_us": (local_ms - baseline_ms) * 1000.0,
            "baseline_background_hit_error": number(baseline["background_hit_error"]),
            "local_background_hit_error": number(local["background_hit_error"]),
            "baseline_reflection_hit_error": number(baseline["reflection_hit_error"]),
            "local_reflection_hit_error": number(local["reflection_hit_error"]),
            "pair_hdr_rmse": float("nan"),
            "pair_hdr_psnr_db": float("nan"),
            "pair_hdr_max_abs": float("nan"),
        }

        captures = capture_pairs.get(pair_id, {})
        a = captures.get("direction-only", {}).get("pfm", "")
        b = captures.get("local-ray", {}).get("pfm", "")
        if a and b:
            a_path = Path(a)
            b_path = Path(b)
            if not a_path.is_absolute():
                a_path = root / a_path
            if not b_path.is_absolute():
                b_path = root / b_path
            if a_path.is_file() and b_path.is_file():
                rmse, psnr, maximum = image_metrics(a_path, b_path)
                row["pair_hdr_rmse"] = rmse
                row["pair_hdr_psnr_db"] = psnr
                row["pair_hdr_max_abs"] = maximum

        output_rows.append(row)

    analysis_path = root / "analysis.csv"
    fieldnames = list(output_rows[0].keys()) if output_rows else []
    if fieldnames:
        with analysis_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(output_rows)

    timing_deltas = [float(row["delta_us"]) for row in output_rows if row["sweep"] == "timing"]
    local_background = [float(row["local_background_hit_error"]) for row in output_rows]
    baseline_background = [float(row["baseline_background_hit_error"]) for row in output_rows]
    local_reflection = [float(row["local_reflection_hit_error"]) for row in output_rows]
    baseline_reflection = [float(row["baseline_reflection_hit_error"]) for row in output_rows]
    null_rmse = [float(row["pair_hdr_rmse"]) for row in output_rows if row["sweep"] == "null"]

    print(f"paired_cases,{len(output_rows)}")
    print(f"mean_timing_delta_us,{finite_mean(timing_deltas):.9g}")
    print(f"mean_background_hit_error_direction_only,{finite_mean(baseline_background):.9g}")
    print(f"mean_background_hit_error_local_ray,{finite_mean(local_background):.9g}")
    print(f"mean_reflection_hit_error_direction_only,{finite_mean(baseline_reflection):.9g}")
    print(f"mean_reflection_hit_error_local_ray,{finite_mean(local_reflection):.9g}")
    print(f"mean_null_hdr_rmse,{finite_mean(null_rmse):.9g}")
    print(f"analysis_csv,{analysis_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
