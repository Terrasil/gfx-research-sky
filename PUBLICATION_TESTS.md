# Publication test protocol

The application contains a deterministic publication harness for the local screen-space sky experiment. Keep `Animate sky` disabled, use a Release build, disable VSync/external frame limiters and record the exact source revision before reporting numbers.

## Automated suites

From the UI use **Run quick publication suite** or **Run full publication suite**.

The same runs can be started without touching the UI:

```bash
./gfx-research-sky --publication-quick
./gfx-research-sky --publication-suite
```

A different output directory can be selected with:

```bash
./gfx-research-sky --publication-suite --publication-output=results/run-001
```

The full suite performs:

1. **Timing baseline** - direction-only and local-ray, all three sky modes, 1920x1080 and 2560x1440, 120 warm-up frames and 600 measured frames per case.
2. **Camera translation** - fixed view orientation translated from X=-1.5 to X=+1.5 in 21 positions. Both methods and all sky modes are tested. Endpoints and center are captured.
3. **Reflective-object translation** - reflective sphere translated from X=-1.5 to X=+1.5 in 21 positions. Both methods and all sky modes are tested. Endpoints and center are captured.
4. **Domain-radius sweep** - radii 75, 150, 300, 600 and 1200 world units for both methods and all sky modes.
5. **Spatially invariant null test** - local positional influence is forced to zero. Direction-only and local-ray images should converge within floating-point/rendering error at both publication resolutions.

The quick suite reduces warm-up/sample counts, translation samples and the radius sweep so code or shader changes can be checked quickly.

## Recorded data

`raw.csv` stores every timed GPU sample. `summary.csv` stores mean, median, standard deviation, p95, min/max and CPU-reference domain-hit errors. `captures.csv` maps deterministic case IDs to screenshots. `manifest.txt` records fixed procedural parameters and asset availability.

Key captures contain:

- an 8-bit PNG of the displayed result;
- a linear HDR PFM exported from the pre-tone-map attachment.

The shader also writes the world-space procedural-domain sample point to an RGBA32F validation attachment. The harness reads selected background and reflective-sphere samples and compares them with an independent CPU double-precision ray-sphere intersection. This produces `background_hit_error` and `reflection_hit_error` in `summary.csv`.

## Analysis

After a run:

```bash
python tools/analyze_publication.py results/publication
```

The script creates `analysis.csv`, pairs direction-only/local-ray cases, computes absolute timing overhead in microseconds, carries through the domain-hit errors and computes HDR RMSE/PSNR/max-absolute differences for paired captures. For the null test the paired HDR RMSE should be approximately zero.

## Claims supported by each test

- **H1 positional/reflection consistency:** object translation + `reflection_hit_error`.
- **H2 camera-translation consistency:** camera translation + `background_hit_error`.
- **H3 controlled overhead:** timing cases at both resolutions; report absolute microseconds as well as percentages.
- **H4 null case:** null-test PFM RMSE/PSNR and matched screenshots.

Do not report expected numbers. Use only values regenerated from a Release build and a fixed commit. For broad hardware claims repeat the full suite on at least two GPU architectures.
