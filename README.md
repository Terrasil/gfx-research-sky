# gfx-research-sky

OpenGL 4.6 research prototype for position-dependent procedural skies and reflections using per-origin virtual-sphere sampling. Shared rendering infrastructure and classic research models come from `gfx-research-base`; this repository contains only experiment-specific code, shaders, tests, and publication tooling.

## Method

The renderer exposes three sampling paths:

- **Direction-only**: fixed-anchor baseline that discards ray-origin translation.
- **Fixed world sphere (legacy)**: previous global-sphere intersection path retained for comparison.
- **Per-origin virtual sphere**: the evaluated path, using `q = origin + radius * normalized_direction`.

For background pixels the origin is the camera position. For reflected sky the origin is the reconstructed surface hit. The procedural field stays anchored in world space.

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

The suite covers exact 1080p, 1440p and 4K targets, paired direction-only/per-origin timing, object reflections on/off, camera and reflective-object translation, virtual-sphere radius sweeps, the invariant null test, explicit-position reconstruction ablation, and the deterministic synthetic world-space field.

`tools/compare_images.py` provides image comparison support and `tests/reference_math.cpp` contains the CPU-side reference checks.
