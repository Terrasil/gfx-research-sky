#!/usr/bin/env python3
import csv
import math
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else "results/publication")
out = root / "publication-tables.tex"


def rows(name):
    path = root / name
    if not path.exists():
        raise SystemExit(f"missing result file: {path}")
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def f(value, digits=6):
    x = float(value)
    if math.isinf(x):
        return r"\infty"
    if math.isnan(x):
        return r"\mathrm{NaN}"
    return f"{x:.{digits}f}"


validation = rows("validation.csv")[-1]
null = rows("null-test.csv")[-1]
path_summary = rows("path-summary.csv")
timing = rows("timing-comparison.csv")

with out.open("w", encoding="utf-8", newline="\n") as tex:
    tex.write("% Generated from measured CSV files by tools/publication_tables.py.\n")
    tex.write("% Do not edit numerical values manually; regenerate after the final run.\n\n")

    tex.write(f"\\newcommand{{\\ValidationWorldMeanLTwo}}{{{f(validation['world_mean_l2'])}}}\n")
    tex.write(f"\\newcommand{{\\ValidationDomainMeanLTwo}}{{{f(validation['domain_hit_mean_l2'])}}}\n")
    tex.write(f"\\newcommand{{\\ValidationHitMismatches}}{{{validation['hit_mismatches']}}}\n")
    tex.write(f"\\newcommand{{\\NullRMSE}}{{{f(null['rmse_rgb'])}}}\n")
    tex.write(f"\\newcommand{{\\NullMaxAbs}}{{{f(null['max_abs_rgb'])}}}\n")
    tex.write(f"\\newcommand{{\\NullPSNR}}{{{f(null['psnr_db'], 3)}}}\n\n")

    tex.write("% H1/H2 summary rows: path & method & mean RGB error & max RGB error & mean hit error & mean step residual \\\\\n")
    for row in path_summary:
        tex.write(
            f"{row['path']} & {row['method']} & {f(row['mean_color_l2_error'])} & "
            f"{f(row['max_color_l2_error'])} & {f(row['mean_hit_l2_error'])} & "
            f"{f(row['mean_step_residual_l2'])} \\\\\n"
        )

    tex.write("\n% H3 timing rows: family & resolution & radius & center x/y/z & baseline median & local median & delta ms & delta percent \\\\\n")
    for row in timing:
        resolution = f"{row['width']}x{row['height']}"
        center = f"({f(row['center_x'], 1)}, {f(row['center_y'], 1)}, {f(row['center_z'], 1)})"
        tex.write(
            f"{row['family']} & {resolution} & {f(row['radius'], 1)} & {center} & "
            f"{f(row['baseline_median_ms'])} & {f(row['proposed_median_ms'])} & "
            f"{f(row['delta_median_ms'])} & {f(row['delta_median_percent'], 2)}\\% \\\\\n"
        )

print(f"wrote {out}")
