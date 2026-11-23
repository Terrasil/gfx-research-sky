# gfx-research-sky

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![OpenGL 4.6](https://img.shields.io/badge/OpenGL-4.6-5586A4.svg)](https://www.opengl.org/)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C.svg)](https://cmake.org/)

A renderer for position-dependent procedural skies and their reflections, with geometric diagnostics and an analytic light-transport control.

[Method](#method) · [Build](#build) · [Controls](#controls) · [Experiments](#experiments) · [References](#references)

![Volumetric sky and controls](docs/assets/control-panel.png)

## Method

With ray origin $\mathbf o$, unit direction $\mathbf d$, world anchor $\mathbf s_0$, query distance $R$ and field scale $S$, the common evaluator receives

```math
\mathbf q=\mathbf o+R\mathbf d,\qquad
\boldsymbol\xi=\frac{\mathbf q-\mathbf s_0}{S},\qquad
\mathbf o_{\rm eff}=\boldsymbol\xi-\frac{R}{S}\mathbf d.
```

The directional control uses $\mathbf s_0+R\mathbf d$. The finite-sphere control uses its world-space intersection. Background and reflected rays share the evaluator and retain their own origins. Explicit rasterized world position isolates depth-reconstruction error within each representation.

The analytic slab uses constant extinction $\sigma=0.8$ over a finite ray interval $[a,b]$, with a varying source $S_e(t)=0.65+0.30\cos(\phi+\omega(t-a))$. The double-precision reference evaluates

```math
L=L_{\rm bg}e^{-\sigma \ell}
 + C_e\left[0.65(1-e^{-\sigma\ell})
    + 0.30\sigma\operatorname{Re}\left(
       e^{i\phi}\frac{e^{(-\sigma+i\omega)\ell}-1}{-\sigma+i\omega}
      \right)\right],\quad \ell=b-a.
```

The GPU path integrates the same source with midpoint samples. The LOM cloud workloads use Beer-Lambert transmission and front-to-back integration. See [implementation notes](docs/IMPLEMENTATION.md) for volumetric cloud and aurora settings.

## Build

Requirements are CMake 3.25+, a C++20 compiler, Ninja, Python 3 with Jinja2, and a desktop OpenGL 4.6 Core driver. The initial CMake configuration downloads the pinned gfx-research-base revision and its dependencies. A local checkout can be supplied with `-DGFX_RESEARCH_BASE_LOCAL_PATH=../gfx-research-base`.

```powershell
python -m pip install jinja2
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

On Linux, run the same build commands and omit `.exe` when launching an executable.

## Controls

Launch `gfx-research-sky.exe` for the Sky, Scene, Camera and Tests tabs. The Sky tab exposes the query distance and optional fixed field scale. Available scenes include cheap clouds and aurora, LOM volumetric day and night, volumetric aurora, a synthetic field and the analytic slab.

**Tests** runs the smoke, quick or full benchmark, with a separate button for the invariant null checks. Choose a suite family before starting. Each GUI run writes to a new subdirectory under `results/publication`.

## Experiments

```powershell
python -m pip install numpy Pillow moderngl glcontext
.\out\build\gfx-research-sky.exe --publication-smoke --publication-output=results/sky-smoke
python tools/validate_slab.py --output results/sky-analytic
.\out\build\gfx-research-sky.exe --publication-suite --publication-output=results/sky-paper
python tools/analyze_publication.py results/sky-paper
```

The main full profile uses 1080p, 1440p and 4K. The field-scale sweep compares fixed $S=30$ with coupled $S=R$ under the same camera and scene. Use `--publication-family=volumetric`, `cheap` or `synthetic` to focus the run. The analytic slab samples three origins, three query radii and three integration budgets against the independent exact integral.

| Output | Contents |
|---|---|
| `analytic_slab.csv` | Absolute and RMS light-transport error in the controlled slab |
| `field_scale_comparison.csv` | HDR differences with fixed and coupled spatial scale |
| `quality.csv` / `null.csv` | Reconstruction error and representation equivalence |
| `raw.csv` / `hdr/` | GPU queries and linear HDR captures |

The analytic oracle applies to the synthetic slab. Numerical reconstruction comparisons within the LOM workloads use explicit world positions. The two evaluations support distinct claims about geometric queries and about reconstruction.

## References

1. Andrew Schneider and Nathan Vos. [The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn](https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn). SIGGRAPH, 2015.
2. Orion Sky Lawlor and Joe Genetti. [Interactive Volume Rendering Aurora on the GPU](https://dspace.zcu.cz/items/0bd963cb-b034-4899-8f0c-515ccd7c92e0). Journal of WSCG, 2011.
3. A. J. Preetham, Peter Shirley and Brian Smits. [A Practical Analytic Model for Daylight](https://doi.org/10.1145/311535.311545). SIGGRAPH, 1999.
