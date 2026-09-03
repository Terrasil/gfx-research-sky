from pathlib import Path
import math
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:
    raise SystemExit("Install numpy and pillow: python -m pip install numpy pillow")

def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0

a = load(sys.argv[1])
b = load(sys.argv[2])
if a.shape != b.shape:
    raise SystemExit(f"Image size mismatch: {a.shape} vs {b.shape}")
mse = float(np.mean((a - b) ** 2))
rmse = math.sqrt(mse)
psnr = float("inf") if mse == 0.0 else 10.0 * math.log10(1.0 / mse)
print(f"RMSE,{rmse:.9f}")
print(f"PSNR_dB,{psnr:.6f}")
