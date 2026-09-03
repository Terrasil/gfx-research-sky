# Publication test protocol

This protocol maps executable outputs to the hypotheses and tables in the companion paper. The executable never inserts synthetic result values.

## Before a final run

1. Build `Release`.
2. Pin `GFX_RESEARCH_BASE_GIT_TAG` to a fixed commit/tag or use a clean local base checkout.
3. Disable external FPS limiters, capture overlays and GPU tuning that is not part of the documented configuration.
4. Keep VSync disabled.
5. Run `ctest` first.
6. Run the full publication suite at least three independent times per GPU configuration, using separate output folders.
7. Preserve raw CSV files, images and `metadata.txt`.

## H1: reflective-object positional consistency

Source: `object-path.csv` and `path-summary.csv`.

The reflective sphere follows a deterministic X translation while camera, lighting and domain parameters remain fixed. The image sample at the projected sphere center is compared with a CPU reference that uses explicit rasterized world position, the G-buffer normal, double-precision ray/sphere intersection and the same procedural field.

Primary quantities:

- `color_l2_error`: rendered RGB error against the local CPU reference.
- `hit_l2_error`: GPU local-domain hit error against double-precision CPU intersection.
- `mean_step_residual_l2`: error in frame-to-frame RGB change relative to the expected reference change.

Use matched screenshots from `images/object-*` as qualitative evidence, not as the primary measurement.

## H2: camera-translation consistency

Source: `camera-path.csv` and `path-summary.csv`.

The camera target is translated in X while distance/yaw/pitch are unchanged, giving a fixed-orientation translation path. A deterministic background pixel is compared against a double-precision local-domain CPU reference.

Primary quantities are the same as H1. A direction-only method can remain nearly invariant under translation, so interpret temporal variation together with the reference variation rather than rewarding a constant signal.

## H3: GPU overhead

Sources: `timing-raw.csv`, `timing-summary.csv`, `timing-comparison.csv`.

Only the fullscreen sky draw is enclosed by OpenGL timestamp queries. Scene rendering, GUI, readback, screenshots and CSV writes are outside the timed interval. Every condition receives warm-up frames before samples are collected.

Report:

- median GPU time as the primary central value,
- mean and sample standard deviation,
- p95,
- absolute baseline-to-local delta in milliseconds,
- relative delta in percent.

The suite includes 1920x1080 and 2560x1440 resolution tests plus four domain radii and two domain-center placements at 1920x1080.

## H4: spatially invariant null control

Source: `null-test.csv`.

Spatial variation is disabled while all other rendering code remains active. Direction-only and local-ray output should converge apart from implementation/floating-point error. Report `rmse_rgb`, `max_abs_rgb` and `psnr_db`. A failed null case is a correctness problem and should be fixed before using H1-H3 results.

## Reconstruction and domain-hit validation

Source: `validation.csv`.

The scene writes explicit world position to RGBA32F. A separate fullscreen validation shader reconstructs world position from depth and emits the local-domain hit. CPU code then compares:

- reconstructed world position against the explicit world-position MRT,
- GPU domain hit against a double-precision CPU ray/sphere intersection.

The paper's domain-hit metric E_q maps directly to `domain_hit_mean_l2`. Also retain RMSE, maximum error and hit-mismatch count.

## Statistical reporting

Do not combine samples from different GPUs, drivers, resolutions or build revisions as if they were one population. Report each hardware/software configuration separately. If three complete repeated runs are performed, report run-level medians and variability across runs in addition to per-run timestamp distributions.

Avoid a percentage-only performance claim. For example, report `0.082 ms -> 0.097 ms (+0.015 ms, +18.3%)`, not only `18.3% slower`.

## Figures

Use identical exposure, camera matrices, resolution and procedural parameters for baseline/proposed screenshots. Recommended final figures:

1. method diagram,
2. baseline/proposed/reference object-translation frames,
3. baseline/proposed/reference camera-translation frames,
4. RGB or luminance difference image,
5. H1/H2 error curves,
6. resolution timing curve,
7. radius timing curve.

`tools/plot_publication.py` generates the quantitative curves. `tools/publication_tables.py` writes LaTeX fragments from the actual CSV outputs.
