#!/usr/bin/env python3
from __future__ import annotations

import csv
import math
import random
import sys
from collections import defaultdict
from pathlib import Path

DISPLAY_TRANSFER = "linear"


def number(value: str | None) -> float:
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
        raise SystemExit("PFM analysis requires numpy: python -m pip install numpy pillow") from error

    with path.open("rb") as stream:
        header = stream.readline().decode("ascii").strip()
        if header not in {"PF", "Pf"}:
            raise ValueError(f"{path}: unsupported PFM header {header!r}")
        width, height = map(int, stream.readline().decode("ascii").strip().split())
        scale = float(stream.readline().decode("ascii").strip())
        endian = "<" if scale < 0 else ">"
        channels = 3 if header == "PF" else 1
        data = np.fromfile(stream, dtype=endian + "f4")
        expected = width * height * channels
        if data.size != expected:
            raise ValueError(f"{path}: expected {expected} floats, got {data.size}")
        return np.flipud(data.reshape((height, width, channels)))


def hdr_metrics(image_path: Path, reference_path: Path) -> dict[str, float]:
    import numpy as np

    image = read_pfm(image_path).astype(np.float64)
    reference = read_pfm(reference_path).astype(np.float64)
    if image.shape != reference.shape:
        return {key: float("nan") for key in ("rmse", "relative_rmse", "psnr_db", "max_abs")}
    difference = image - reference
    mse = float(np.mean(difference * difference))
    rmse = math.sqrt(mse)
    reference_rms = math.sqrt(float(np.mean(reference * reference)))
    relative = rmse / max(reference_rms, 1e-12)
    peak = max(float(np.max(np.abs(reference))), 1.0)
    psnr = float("inf") if mse == 0.0 else 10.0 * math.log10((peak * peak) / mse)
    return {
        "rmse": rmse,
        "relative_rmse": relative,
        "psnr_db": psnr,
        "max_abs": float(np.max(np.abs(difference))),
    }


def display_map(image):
    import numpy as np
    image = np.maximum(image, 0.0)
    mapped = np.clip(image / (1.0 + image), 0.0, 1.0)
    if DISPLAY_TRANSFER == "srgb":
        mapped = np.where(mapped <= 0.0031308, 12.92*mapped,
                          1.055*np.power(mapped, 1.0/2.4)-0.055)
    return mapped


def save_preview(pfm_path: Path, png_path: Path) -> bool:
    try:
        from PIL import Image
    except ImportError:
        return False
    import numpy as np

    image = display_map(read_pfm(pfm_path))
    png_path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.round(image * 255.0).astype(np.uint8), mode="RGB").save(png_path)
    return True


def ssim_if_available(image_path: Path, reference_path: Path) -> float:
    try:
        from skimage.metrics import structural_similarity
    except ImportError:
        return float("nan")
    import numpy as np

    a = display_map(read_pfm(image_path)).astype(np.float64)
    b = display_map(read_pfm(reference_path)).astype(np.float64)
    if a.shape != b.shape:
        return float("nan")
    minimum_extent = min(a.shape[0], a.shape[1])
    if minimum_extent < 3:
        return float("nan")
    win_size = min(7, minimum_extent if minimum_extent % 2 == 1 else minimum_extent - 1)
    return float(structural_similarity(a, b, channel_axis=2, data_range=1.0, win_size=win_size))


def percentile(values: list[float], q: float) -> float:
    values = sorted(value for value in values if math.isfinite(value))
    if not values:
        return float("nan")
    position = (len(values) - 1) * q
    lo = int(math.floor(position))
    hi = int(math.ceil(position))
    if lo == hi:
        return values[lo]
    fraction = position - lo
    return values[lo] * (1.0 - fraction) + values[hi] * fraction


def bootstrap_ci(values: list[float], iterations: int = 20000) -> tuple[float, float]:
    values = [value for value in values if math.isfinite(value)]
    if len(values) < 2:
        value = values[0] if values else float("nan")
        return value, value
    rng = random.Random(0x475846)
    medians: list[float] = []
    for _ in range(iterations):
        sample = [values[rng.randrange(len(values))] for _ in values]
        medians.append(percentile(sample, 0.5))
    return percentile(medians, 0.025), percentile(medians, 0.975)


def resolve(root: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def write_rows(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def field_scale_analysis(root, summary):
    captures = read_csv(root/"captures.csv")
    paths = {r["case_id"]: resolve(root, r["pfm"]) for r in captures if r["pfm"]}
    groups = defaultdict(dict)
    for row in summary:
        sweep = row["sweep"]
        if sweep not in ("radius_fixed_scale","radius_coupled_scale") or row["case_id"] not in paths:
            continue
        key = row["sky_mode"], row["method"], row["width"], row["height"]
        groups[key][float(row["sky_radius"]),sweep] = row
    output = []
    for (mode,method,width,height), cases in sorted(groups.items()):
        anchor = cases.get((30.,"radius_fixed_scale"))
        if not anchor:
            raise ValueError(f"No fixed-field anchor for {mode}/{method}")
        for radius, sweep in sorted(cases):
            row = cases[radius,sweep]
            m = hdr_metrics(paths[row["case_id"]], paths[anchor["case_id"]])
            output.append(dict(sky_mode=mode,method=method,width=width,height=height,sky_radius=radius,
                               field_scale=row["field_scale"],sweep=sweep,anchor_radius=30,
                               rmse_vs_anchor=m["rmse"],maximum_absolute=m["max_abs"]))
    if output:
        write_rows(root/"field_scale_comparison.csv",output)


def main() -> int:
    global DISPLAY_TRANSFER
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/publication")
    manifest_path = root / "manifest.txt"
    if manifest_path.is_file():
        metadata = dict(line.split("=", 1) for line in manifest_path.read_text().splitlines() if "=" in line)
        DISPLAY_TRANSFER = metadata.get("display_transfer", "linear")
        if metadata.get("schema_version")=="3" and metadata.get("completed")!="1":
            raise ValueError("The sky suite is incomplete")
    summary_path = root / "summary.csv"
    if not summary_path.is_file():
        raise SystemExit(f"Missing {summary_path}. Run the publication suite first.")

    summary = read_csv(summary_path)
    methods_by_pair: dict[str, dict[str, dict[str, str]]] = defaultdict(dict)
    for row in summary:
        methods_by_pair[row["pair_id"]][row["method"]] = row

    paired_rows: list[dict[str, object]] = []
    timing_groups: dict[str, list[dict[str, object]]] = defaultdict(list)
    for pair_id, methods in methods_by_pair.items():
        baseline = methods.get("direction-only")
        fixed_domain = methods.get("fixed-domain-local-ray")
        proposed = methods.get("origin-aware-fixed-distance") or methods.get("per-origin-sphere") or methods.get("per-origin-dome") or methods.get("local-ray")
        if not baseline or not proposed:
            continue
        baseline_ms = number(baseline.get("median_ms"))
        fixed_domain_ms = number(fixed_domain.get("median_ms")) if fixed_domain else float("nan")
        proposed_ms = number(proposed.get("median_ms"))
        delta_us = (proposed_ms - baseline_ms) * 1000.0
        fixed_delta_us = (fixed_domain_ms - baseline_ms) * 1000.0 if math.isfinite(fixed_domain_ms) else float("nan")
        proposed_vs_fixed_us = (proposed_ms - fixed_domain_ms) * 1000.0 if math.isfinite(fixed_domain_ms) else float("nan")
        row: dict[str, object] = {
            "pair_id": pair_id,
            "group_id": baseline.get("group_id", pair_id),
            "sweep": baseline["sweep"],
            "step": baseline["step"],
            "repeat": baseline.get("repeat", "0"),
            "sky_mode": baseline["sky_mode"],
            "workload_family": baseline.get("workload_family", "legacy"),
            "volume_budget": baseline.get("volume_budget", "none"),
            "cloud_samples": baseline.get("cloud_samples", "0"),
            "cloud_light_samples": baseline.get("cloud_light_samples", "0"),
            "aurora_samples": baseline.get("aurora_samples", "0"),
            "width": baseline["width"],
            "height": baseline["height"],
            "sky_radius": baseline["sky_radius"],
            "camera_offset_x": baseline["camera_offset_x"],
            "sphere_x": baseline["sphere_x"],
            "null_test": baseline["null_test"],
            "reflections_enabled": baseline.get("reflections_enabled", "1"),
            "baseline_median_ms": baseline_ms,
            "fixed_domain_median_ms": fixed_domain_ms,
            "proposed_median_ms": proposed_ms,
            "fixed_domain_delta_us": fixed_delta_us,
            "delta_us": delta_us,
            "proposed_vs_fixed_delta_us": proposed_vs_fixed_us,
            "baseline_world_position_rmse": number(baseline.get("world_position_rmse")),
            "fixed_domain_world_position_rmse": number(fixed_domain.get("world_position_rmse")) if fixed_domain else float("nan"),
            "proposed_world_position_rmse": number(proposed.get("world_position_rmse")),
            "baseline_background_query_error": number(baseline.get("background_hit_error")),
            "fixed_domain_background_query_error": number(fixed_domain.get("background_hit_error")) if fixed_domain else float("nan"),
            "proposed_background_query_error": number(proposed.get("background_hit_error")),
            "baseline_reflection_query_error": number(baseline.get("reflection_hit_error")),
            "fixed_domain_reflection_query_error": number(fixed_domain.get("reflection_hit_error")) if fixed_domain else float("nan"),
            "proposed_reflection_query_error": number(proposed.get("reflection_hit_error")),
        }
        paired_rows.append(row)
        if baseline["sweep"] in {"timing", "volume_budget"}:
            timing_groups[str(row["group_id"])].append(row)

    write_rows(root / "analysis.csv", paired_rows)

    timing_aggregate: list[dict[str, object]] = []
    for group_id, rows in sorted(timing_groups.items()):
        deltas = [float(row["delta_us"]) for row in rows]
        fixed_deltas = [float(row["fixed_domain_delta_us"]) for row in rows if math.isfinite(float(row["fixed_domain_delta_us"]))]
        proposed_vs_fixed = [float(row["proposed_vs_fixed_delta_us"]) for row in rows if math.isfinite(float(row["proposed_vs_fixed_delta_us"]))]
        baseline_values = [float(row["baseline_median_ms"]) for row in rows]
        fixed_values = [float(row["fixed_domain_median_ms"]) for row in rows if math.isfinite(float(row["fixed_domain_median_ms"]))]
        proposed_values = [float(row["proposed_median_ms"]) for row in rows]
        ci_low, ci_high = bootstrap_ci(deltas)
        fixed_ci_low, fixed_ci_high = bootstrap_ci(fixed_deltas)
        proposed_fixed_ci_low, proposed_fixed_ci_high = bootstrap_ci(proposed_vs_fixed)
        first = rows[0]
        mean_delta = sum(deltas) / len(deltas)
        variance = sum((value - mean_delta) ** 2 for value in deltas) / max(len(deltas) - 1, 1)
        timing_aggregate.append({
            "group_id": group_id,
            "sky_mode": first["sky_mode"],
            "workload_family": first["workload_family"],
            "volume_budget": first["volume_budget"],
            "cloud_samples": first["cloud_samples"],
            "cloud_light_samples": first["cloud_light_samples"],
            "aurora_samples": first["aurora_samples"],
            "width": first["width"],
            "height": first["height"],
            "reflections_enabled": first.get("reflections_enabled", "1"),
            "repeats": len(deltas),
            "baseline_median_of_medians_ms": percentile(baseline_values, 0.5),
            "fixed_domain_median_of_medians_ms": percentile(fixed_values, 0.5),
            "proposed_median_of_medians_ms": percentile(proposed_values, 0.5),
            "fixed_domain_delta_median_us": percentile(fixed_deltas, 0.5),
            "fixed_domain_delta_bootstrap_ci95_low_us": fixed_ci_low,
            "fixed_domain_delta_bootstrap_ci95_high_us": fixed_ci_high,
            "paired_delta_median_us": percentile(deltas, 0.5),
            "paired_delta_mean_us": mean_delta,
            "paired_delta_stddev_us": math.sqrt(variance),
            "paired_delta_p05_us": percentile(deltas, 0.05),
            "paired_delta_p95_us": percentile(deltas, 0.95),
            "paired_delta_bootstrap_ci95_low_us": ci_low,
            "paired_delta_bootstrap_ci95_high_us": ci_high,
            "proposed_vs_fixed_delta_median_us": percentile(proposed_vs_fixed, 0.5),
            "proposed_vs_fixed_delta_bootstrap_ci95_low_us": proposed_fixed_ci_low,
            "proposed_vs_fixed_delta_bootstrap_ci95_high_us": proposed_fixed_ci_high,
        })
    write_rows(root / "timing_aggregate.csv", timing_aggregate)

    # Compare the same sky/resolution with object reflections disabled/enabled.
    reflection_groups: dict[tuple[str, str, str, str], dict[str, dict[str, object]]] = defaultdict(dict)
    for row in timing_aggregate:
        group_id = str(row["group_id"])
        normalized = group_id.replace("-reflections-on-", "-reflections-").replace("-reflections-off-", "-reflections-")
        key = (str(row["sky_mode"]), str(row["width"]), str(row["height"]))
        reflection_groups[(key[0], key[1], key[2], normalized)][str(row.get("reflections_enabled", "1"))] = row

    reflection_toggle_rows: list[dict[str, object]] = []
    for (_, _, _, normalized), states in sorted(reflection_groups.items()):
        off = states.get("0")
        on = states.get("1")
        if not off or not on:
            continue
        fixed_on = float(on.get("fixed_domain_median_of_medians_ms", float("nan")))
        fixed_off = float(off.get("fixed_domain_median_of_medians_ms", float("nan")))
        reflection_toggle_rows.append({
            "group_id": normalized,
            "sky_mode": on["sky_mode"],
            "volume_budget": on["volume_budget"],
            "width": on["width"],
            "height": on["height"],
            "direction_only_reflection_cost_us": (float(on["baseline_median_of_medians_ms"]) - float(off["baseline_median_of_medians_ms"])) * 1000.0,
            "fixed_domain_reflection_cost_us": (fixed_on - fixed_off) * 1000.0 if math.isfinite(fixed_on) and math.isfinite(fixed_off) else float("nan"),
            "proposed_reflection_cost_us": (float(on["proposed_median_of_medians_ms"]) - float(off["proposed_median_of_medians_ms"])) * 1000.0,
            "fixed_domain_overhead_reflections_off_us": float(off.get("fixed_domain_delta_median_us", float("nan"))),
            "fixed_domain_overhead_reflections_on_us": float(on.get("fixed_domain_delta_median_us", float("nan"))),
            "proposed_overhead_reflections_off_us": float(off["paired_delta_median_us"]),
            "proposed_overhead_reflections_on_us": float(on["paired_delta_median_us"]),
            "proposed_vs_fixed_reflections_off_us": float(off.get("proposed_vs_fixed_delta_median_us", float("nan"))),
            "proposed_vs_fixed_reflections_on_us": float(on.get("proposed_vs_fixed_delta_median_us", float("nan"))),
        })
    write_rows(root / "reflection_toggle_aggregate.csv", reflection_toggle_rows)

    quality_path = root / "quality.csv"
    quality_rows = read_csv(quality_path) if quality_path.is_file() else []
    quality_aggregate: list[dict[str, object]] = []
    quality_groups: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in quality_rows:
        key = (row["sky_mode"], row.get("metric_region", "full-frame"), row.get("reflections_enabled", "1"))
        quality_groups[key].append(row)
    for (sky_mode, metric_region, reflections_enabled), rows in sorted(quality_groups.items()):
        # New schema separates representation difference from reconstruction fidelity.
        direction_to_origin = [number(row.get("direction_to_origin_ref_relative_rmse") or row.get("baseline_relative_rmse")) for row in rows]
        fixed_to_origin = [number(row.get("fixed_domain_to_origin_ref_relative_rmse") or row.get("fixed_domain_relative_rmse")) for row in rows]
        origin_to_origin = [number(row.get("origin_aware_to_origin_ref_relative_rmse") or row.get("proposed_relative_rmse") or row.get("local_relative_rmse")) for row in rows]
        direction_reconstruction = [number(row.get("direction_reconstruction_relative_rmse")) for row in rows]
        fixed_reconstruction = [number(row.get("fixed_domain_reconstruction_relative_rmse")) for row in rows]
        origin_reconstruction = [number(row.get("origin_aware_reconstruction_relative_rmse") or row.get("proposed_relative_rmse") or row.get("local_relative_rmse")) for row in rows]
        fixed_to_origin = [value for value in fixed_to_origin if math.isfinite(value)]
        direction_reconstruction = [value for value in direction_reconstruction if math.isfinite(value)]
        fixed_reconstruction = [value for value in fixed_reconstruction if math.isfinite(value)]
        origin_reconstruction = [value for value in origin_reconstruction if math.isfinite(value)]
        quality_aggregate.append({
            "sky_mode": sky_mode,
            "metric_region": metric_region,
            "reflections_enabled": reflections_enabled,
            "cases": len(rows),
            "direction_to_origin_ref_relative_rmse_mean": sum(direction_to_origin) / len(direction_to_origin) if direction_to_origin else float("nan"),
            "fixed_domain_to_origin_ref_relative_rmse_mean": sum(fixed_to_origin) / len(fixed_to_origin) if fixed_to_origin else float("nan"),
            "origin_aware_to_origin_ref_relative_rmse_mean": sum(origin_to_origin) / len(origin_to_origin) if origin_to_origin else float("nan"),
            "direction_to_origin_ref_relative_rmse_median": percentile(direction_to_origin, 0.5),
            "fixed_domain_to_origin_ref_relative_rmse_median": percentile(fixed_to_origin, 0.5),
            "origin_aware_to_origin_ref_relative_rmse_median": percentile(origin_to_origin, 0.5),
            "direction_reconstruction_relative_rmse_mean": sum(direction_reconstruction) / len(direction_reconstruction) if direction_reconstruction else float("nan"),
            "fixed_domain_reconstruction_relative_rmse_mean": sum(fixed_reconstruction) / len(fixed_reconstruction) if fixed_reconstruction else float("nan"),
            "origin_aware_reconstruction_relative_rmse_mean": sum(origin_reconstruction) / len(origin_reconstruction) if origin_reconstruction else float("nan"),
            "direction_reconstruction_relative_rmse_median": percentile(direction_reconstruction, 0.5),
            "fixed_domain_reconstruction_relative_rmse_median": percentile(fixed_reconstruction, 0.5),
            "origin_aware_reconstruction_relative_rmse_median": percentile(origin_reconstruction, 0.5),
        })
    write_rows(root / "quality_aggregate.csv", quality_aggregate)

    capture_path = root / "captures.csv"
    capture_quality: list[dict[str, object]] = []
    null_source_path = root / "null.csv"
    null_rows: list[dict[str, object]] = read_csv(null_source_path) if null_source_path.is_file() else []
    if capture_path.is_file():
        captures = read_csv(capture_path)
        capture_methods: dict[str, dict[str, dict[str, str]]] = defaultdict(dict)
        preview_rows: list[dict[str, object]] = []
        for row in captures:
            capture_methods[row["pair_id"]][row["method"]] = row
            pfm_value = row.get("pfm", "")
            preview = ""
            if pfm_value:
                pfm_path = resolve(root, pfm_value)
                preview_path = root / "previews" / f"{row['case_id']}.png"
                if pfm_path.is_file() and save_preview(pfm_path, preview_path):
                    preview = preview_path.relative_to(root).as_posix()
            preview_rows.append({**row, "generated_preview_png": preview})
        write_rows(root / "captures_analyzed.csv", preview_rows)

        for pair_id, methods in capture_methods.items():
            baseline = methods.get("direction-only")
            fixed_domain = methods.get("fixed-domain-local-ray")
            proposed = methods.get("origin-aware-fixed-distance") or methods.get("per-origin-sphere") or methods.get("per-origin-dome") or methods.get("local-ray")
            reference = methods.get("origin-aware-explicit-position-reference") or methods.get("explicit-position-reference")
            direction_reference = methods.get("direction-only-explicit-position-reference")
            fixed_reference = methods.get("fixed-domain-explicit-position-reference")
            if baseline and proposed:
                a = resolve(root, baseline.get("pfm", "")) if baseline.get("pfm") else None
                b = resolve(root, proposed.get("pfm", "")) if proposed.get("pfm") else None
                if not null_source_path.is_file() and a and b and a.is_file() and b.is_file() and baseline.get("sweep") == "null":
                    metrics = hdr_metrics(a, b)
                    null_rows.append({
                        "pair_id": pair_id,
                        "sky_mode": baseline["sky_mode"],
                        "width": baseline.get("width", ""),
                        "height": baseline.get("height", ""),
                        "reflections_enabled": baseline.get("reflections_enabled", "1"),
                        "byte_identical": int(a.read_bytes() == b.read_bytes()),
                        **{f"pair_{key}": value for key, value in metrics.items()},
                    })
            if not (baseline and proposed and reference):
                continue
            paths = {
                "baseline": resolve(root, baseline.get("pfm", "")) if baseline.get("pfm") else None,
                "proposed": resolve(root, proposed.get("pfm", "")) if proposed.get("pfm") else None,
                "reference": resolve(root, reference.get("pfm", "")) if reference.get("pfm") else None,
            }
            if not all(path and path.is_file() for path in paths.values()):
                continue
            baseline_metrics = hdr_metrics(paths["baseline"], paths["reference"])
            proposed_metrics = hdr_metrics(paths["proposed"], paths["reference"])
            direction_reconstruction_metrics = None
            if direction_reference and direction_reference.get("pfm"):
                direction_reference_path = resolve(root, direction_reference["pfm"])
                if direction_reference_path.is_file():
                    direction_reconstruction_metrics = hdr_metrics(paths["baseline"], direction_reference_path)
            fixed_reconstruction_metrics = None
            fixed_metrics = None
            fixed_path = resolve(root, fixed_domain.get("pfm", "")) if fixed_domain and fixed_domain.get("pfm") else None
            if fixed_path and fixed_path.is_file():
                fixed_metrics = hdr_metrics(fixed_path, paths["reference"])
                if fixed_reference and fixed_reference.get("pfm"):
                    fixed_reference_path = resolve(root, fixed_reference["pfm"])
                    if fixed_reference_path.is_file():
                        fixed_reconstruction_metrics = hdr_metrics(fixed_path, fixed_reference_path)
            capture_quality.append({
                "pair_id": pair_id,
                "sky_mode": baseline["sky_mode"],
                "sweep": baseline["sweep"],
                "reflections_enabled": baseline.get("reflections_enabled", "1"),
                "baseline_hdr_rmse": baseline_metrics["rmse"],
                "fixed_domain_hdr_rmse": fixed_metrics["rmse"] if fixed_metrics else float("nan"),
                "proposed_hdr_rmse": proposed_metrics["rmse"],
                "baseline_relative_rmse": baseline_metrics["relative_rmse"],
                "fixed_domain_relative_rmse": fixed_metrics["relative_rmse"] if fixed_metrics else float("nan"),
                "proposed_relative_rmse": proposed_metrics["relative_rmse"],
                "baseline_psnr_db": baseline_metrics["psnr_db"],
                "fixed_domain_psnr_db": fixed_metrics["psnr_db"] if fixed_metrics else float("nan"),
                "proposed_psnr_db": proposed_metrics["psnr_db"],
                "baseline_ssim_display": ssim_if_available(paths["baseline"], paths["reference"]),
                "fixed_domain_ssim_display": ssim_if_available(fixed_path, paths["reference"]) if fixed_path and fixed_path.is_file() else float("nan"),
                "proposed_ssim_display": ssim_if_available(paths["proposed"], paths["reference"]),
                "direction_reconstruction_relative_rmse": direction_reconstruction_metrics["relative_rmse"] if direction_reconstruction_metrics else float("nan"),
                "fixed_domain_reconstruction_relative_rmse": fixed_reconstruction_metrics["relative_rmse"] if fixed_reconstruction_metrics else float("nan"),
                "origin_aware_reconstruction_relative_rmse": proposed_metrics["relative_rmse"],
                "reference_kind": "origin-aware-explicit-position plus per-representation explicit-position references when available",
            })
    write_rows(root / "capture_quality.csv", capture_quality)
    write_rows(root / "null_results.csv", null_rows)
    if (root/"captures.csv").is_file():
        field_scale_analysis(root, summary)

    print(f"paired_cases,{len(paired_rows)}")
    print(f"timing_groups,{len(timing_aggregate)}")
    print(f"reflection_toggle_groups,{len(reflection_toggle_rows)}")
    print(f"quality_cases,{len(quality_rows)}")
    print(f"captured_reference_comparisons,{len(capture_quality)}")
    print(f"null_capture_pairs,{len(null_rows)}")
    print(f"analysis_csv,{root / 'analysis.csv'}")
    print(f"timing_aggregate_csv,{root / 'timing_aggregate.csv'}")
    print(f"reflection_toggle_csv,{root / 'reflection_toggle_aggregate.csv'}")
    print(f"quality_csv,{quality_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
