# gfx-research-sky

OpenGL 4.6 research prototype for **local screen-space ray evaluation of position-dependent procedural skies and reflections**. The project imports the reusable [`gfx-research-base`](https://github.com/Terrasil/gfx-research-base) library and keeps the experiment-specific code small enough to audit.

## Experiment

The renderer compares two paths that share the same scene, procedural radiance evaluator and post-process shader:

- **Direction-only baseline** - the procedural environment lookup is parameterized by ray direction and therefore does not change when the ray origin translates.
- **Local-ray method** - a per-pixel camera or reflection ray is intersected with a bounded analytic sky sphere and the same procedural radiance function is evaluated at the local hit.

The current visual presets are intentionally different so the method can be inspected under several kinds of procedural structure:

1. **Day clouds** - atmospheric gradient, sun disc/aureole and 3D FBM clouds.
2. **Night clouds** - night gradient, moon, stars and low-contrast procedural clouds.
3. **Aurora** - night base with procedural auroral arcs/curtains driven by the local domain.

The cloud/aurora structure is generated from 3D directional/domain coordinates. There is no sky texture or 2D planar projection to stretch across the dome.

## Classic test models

The scene supports:

- Stanford Dragon
- Suzanne
- Utah Teapot
- reflective sphere
- reflective/rough ground plane

Classic mesh files live in `assets/models/`. With your current layout the expected files are:

```text
assets/models/dragon.obj
assets/models/suzanne.obj
assets/models/teapot.obj
```

If they are missing, fetch them once:

```bash
python tools/fetch_assets.py
```

After CMake has found Python you can also use:

```bash
cmake --build out/build --target fetch-assets
```

Exact repository/direct-download links and attribution notes are in [`assets/SOURCES.md`](assets/SOURCES.md). Missing models do not prevent the experiment from launching; the UI reports their status.

## Build

For the normal private-repository path:

```bash
cmake -S . -B out/build -G Ninja
cmake --build out/build
```

For local development against a checked-out base project:

```bash
cmake -S . -B out/build -G Ninja -DGFX_RESEARCH_BASE_LOCAL_PATH=../gfx-research-base
cmake --build out/build
```

The project requires a compiler with C++20 support and OpenGL 4.6. `gfx-research-base` owns GLFW, GLAD, GLM, ImGui and model loading.

## Controls

The ImGui panel exposes:

- direction-only vs local-ray method,
- spatially invariant null test,
- sky radius and center,
- local-position influence,
- day/night/aurora preset,
- cloud coverage/scale/density,
- star and aurora intensity,
- deterministic frozen time or optional animation,
- reflective sphere translation and roughness,
- floor roughness,
- Stanford Dragon / Suzanne / Utah Teapot visibility,
- camera orbit controls,
- shader/model reload,
- PNG capture and CSV timing capture.

For reproducible comparisons keep **Animate sky** disabled. The default `Frozen time` gives deterministic procedural structure between runs.

## Output

Screenshots are saved as, for example:

```text
results/sky-day-clouds-local-ray.png
results/sky-night-clouds-direction-only.png
results/sky-aurora-local-ray.png
```

GPU post-process timings are appended to:

```text
results/sky-timing.csv
```

The CSV includes the method, sky preset, resolution, radius and the relevant procedural parameters so a run can be reconstructed later.

## Publication test suite

The publication checks are automated. Use the UI buttons **Run quick publication suite** / **Run full publication suite**, or launch:

```bash
./gfx-research-sky --publication-quick
./gfx-research-sky --publication-suite
```

The full suite covers baseline/local-ray timing, 21-step camera translation, 21-step reflective-sphere translation, domain-radius sweeps, all three sky modes, the spatially invariant null test, and 1920x1080 + 2560x1440 timing/null cases. It records raw GPU samples, mean/median/stddev/p95, CPU double-precision domain-hit validation, PNG screenshots and linear HDR PFM captures.

After the run:

```bash
python tools/analyze_publication.py results/publication
```

See [`PUBLICATION_TESTS.md`](PUBLICATION_TESTS.md) for the exact protocol and the mapping from tests to paper hypotheses. Use a Release build, fixed source revision, fixed frozen time, VSync off and external frame limiters disabled before reporting final numbers.
