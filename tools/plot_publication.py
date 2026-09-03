#!/usr/bin/env python3
import csv
import math
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ImportError:
    raise SystemExit("matplotlib is required: python -m pip install matplotlib")

root = Path(sys.argv[1] if len(sys.argv) > 1 else "results/publication")
out = root / "plots"
out.mkdir(parents=True, exist_ok=True)


def rows(name):
    with (root / name).open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def save_path_plot(filename, parameter, ylabel, values, title):
    data = rows(filename)
    for method in ("direction-only", "local-ray"):
        selected = [r for r in data if r["method"] == method]
        x = [float(r[parameter]) for r in selected]
        y = [float(r[values]) for r in selected]
        plt.plot(x, y, marker="o", label=method)
    plt.xlabel(parameter.replace("_", " "))
    plt.ylabel(ylabel)
    plt.title(title)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out / f"{Path(filename).stem}-{values}.png", dpi=200)
    plt.close()


save_path_plot("camera-path.csv", "camera_offset_x", "L2 error vs local reference", "color_l2_error", "Camera translation consistency")
save_path_plot("object-path.csv", "object_offset_x", "L2 error vs local reference", "color_l2_error", "Reflective object translation consistency")

summary = rows("timing-summary.csv")
resolution_rows = [r for r in summary if r["case"].startswith("timing-resolution-")]
for method in ("direction-only", "local-ray"):
    selected = [r for r in resolution_rows if r["method"] == method]
    x = [int(r["width"]) * int(r["height"]) / 1_000_000.0 for r in selected]
    y = [float(r["median_ms"]) for r in selected]
    plt.plot(x, y, marker="o", label=method)
plt.xlabel("resolution [megapixels]")
plt.ylabel("median fullscreen GPU time [ms]")
plt.title("Postprocess scaling")
plt.legend()
plt.tight_layout()
plt.savefig(out / "timing-resolution.png", dpi=200)
plt.close()

radius_rows = [r for r in summary if r["case"].startswith("timing-radius-") and float(r["center_x"]) == 0.0]
for method in ("direction-only", "local-ray"):
    selected = sorted((r for r in radius_rows if r["method"] == method), key=lambda r: float(r["radius"]))
    plt.plot([float(r["radius"]) for r in selected], [float(r["median_ms"]) for r in selected], marker="o", label=method)
plt.xscale("log")
plt.xlabel("local domain radius")
plt.ylabel("median fullscreen GPU time [ms]")
plt.title("Radius sweep")
plt.legend()
plt.tight_layout()
plt.savefig(out / "timing-radius.png", dpi=200)
plt.close()

print(f"plots written to {out}")
