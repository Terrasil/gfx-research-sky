# gfx-research-sky

OpenGL 4.6 research prototype for origin-aware, fixed-distance sampling of position-dependent procedural skies and reflections. Shared rendering infrastructure and classic research models come from `gfx-research-base`; this repository contains experiment-specific code, shaders, CPU reference tests, and publication tooling.

## Method

For a normalized environment-ray direction `d`, ray origin `o`, and selected sampling distance `R`, the proposed path evaluates the procedural environment at the standard ray point

```text
q = o + R d
```

The ray equation itself is not claimed as novel. The studied design choice is to use that point as one procedural-environment interface for both background and rasterized reflected rays, while retaining the actual origin of each ray and avoiding captured environment storage or scene-ray traversal.

The renderer exposes three sampling paths:

- **Direction-only control**: `q = s0 + R d`. It intentionally discards ray-origin translation and is used as a capability/control condition rather than as a competitive local-reflection method.
- **Fixed world sphere (legacy comparator)**: intersects the current ray with the earlier global spherical proxy. This is the closer origin-aware comparator and is now included in publication-suite measurements.
- **Origin-aware fixed-distance**: `q = o + R d`, with no proxy intersection.

For background pixels `o` is the camera position. For reflected sky `o` is exactly the reconstructed surface hit. No normal offset is used because this path does not re-intersect the reflected environment ray with scene geometry.

For two queries at the same `R`,

```text
Delta q = Delta o + R Delta d
```

so pure equal-direction origin translation is independent of `R`. In the supplied procedural evaluators, which normalize the field coordinate by `R`, a fixed translation contributes `Delta o / R`.

## Shared models

`Stanford Dragon`, `Suzanne`, and `Utah Teapot` are loaded through `gfx-research-base` via `gfx::research::model_path(...)`. The base repository owns the bundled model files and their shared asset API, so this experiment does not duplicate them.

## Build

```bash
cmake -S . -B out/build -G Ninja
cmake --build out/build
```

For local development against a base checkout:

```bash
cmake -S . -B out/build -G Ninja -DGFX_RESEARCH_BASE_LOCAL_PATH=../gfx-research-base
cmake --build out/build
```

## Publication suite

```bash
./gfx-research-sky --publication-quick --publication-output=results/quick
./gfx-research-sky --publication-suite --publication-output=results/release
python tools/analyze_publication.py results/release
```

The revised suite covers exact 1080p, 1440p and 4K targets, rotating-order timing of direction-only / fixed-world-sphere / origin-aware fixed-distance paths, object reflections on/off, camera and reflective-object translation, sampling-distance sweeps, the invariant null test, explicit-position reconstruction ablations, HDR comparisons, and the deterministic synthetic world-space field.

The quality suite now separates two questions that should not be conflated:

- **Representation difference**: direction-only and fixed-domain renders are compared with the origin-aware explicit-position path. This is a diagnostic of how representations differ, not an absolute image-quality ranking.
- **Reconstruction fidelity**: each of the three representations is compared with its own explicit-position counterpart, isolating depth/world-position reconstruction error from the representation choice.

The analyzer emits fixed-domain metrics, per-representation reconstruction metrics, and origin-aware-versus-fixed-domain timing deltas with bootstrap confidence intervals. It also remains backward-compatible with the earlier `per-origin-sphere` method identifier and older quality CSV fields.

## Important revision note

Results produced by an older publication run should not be presented as measurements of the revised fixed-world-sphere comparison. The closer comparator was not part of the old timing/quality sequence, and the old reflected-ray implementation also used a small `0.002` normal offset while the paper defined the origin as the surface hit itself. Run the revised publication suite before adding fixed-domain numerical comparisons to a manuscript.

`tools/compare_images.py` provides image comparison support and `tests/reference_math.cpp` contains CPU-side reference checks for the fixed-distance identity and the legacy fixed-sphere intersection.


### Null-only publication validation

```bash
./gfx-research-sky --publication-null-only --publication-output=results/null-rerun
```

This mode runs only the exact direction-only/origin-aware null comparisons.
