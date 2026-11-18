# Implementation notes

## Relationship to LOM

The cloud path was adapted after inspecting `sky_common.glsl`, `sky_clouds.glsl`, `Sky.hpp`, and `SkySettings.hpp` in the supplied LOM archives. The later settings archive and earlier complete shader archive agree on the central cloud parameters.

| Component | This project |
|---|---|
| Query | Shared `q = o + R d` interface, plus two comparison representations |
| Cloud column | LOM's world-anchored plane projection at `0.35 R` |
| Height profile | Type-dependent lower/upper profile |
| Coverage | Weather-modulated base-field threshold |
| Erosion | LOM remapping with pixel/interval footprint filtering |
| Integration | Front-to-back Beer-Lambert accumulation and opacity termination |
| Lighting | Quadratic cone samples, anisotropy, multiple-scattering and powder approximations |
| Noise storage | Two native 3D RG8 textures replacing packed 2D slice atlases |
| Noise contents | New deterministic value-noise/Worley fields; original source image assets were absent |
| Empty-space stepping | Coarse search, backtracking at entry, coarse search after empty samples |
| Sampling near sun | Fixed selected budget; extra celestial refinement is omitted |
| Temporal paths | No history cache or temporal accumulation |
| Jitter | Stable world-column hash replacing the blue-noise image |
| Atmosphere | Same Preetham zenith and Perez equations, simplified day/night composition |
| Night | Separate moonlight/ambient constants with the existing research star/moon background |
| Water | Research reflected-ray path; LOM water/SSR/cache code is not ported |

This is a source-based adaptation of LOM's rendering workload, not a pixel-identical LOM build. Texture contents, celestial details, exposure integration, and scene lighting differ. The cheap skies remain independent controls.

## Coordinates and valid domain

The planar cloud path follows LOM's below-cloud use case. It fades out as `direction.y` approaches `0.10`; it is not a general renderer for flying above clouds and looking down through them. The sphere selects the query coordinate rather than bounding atmospheric scattering.

Projecting any point along the same ray onto the cloud base plane gives the same horizontal coordinate. Fixed-sphere and fixed-distance queries can agree below the layer. A complete aurora slab integral can likewise agree when both effective starting points lie before the emitting layer. This is an expected mathematical property.

The interactive and main volumetric timing distance is `R = 6000`. Translation/reconstruction cases also use `R = 30` or `300`, exposing local spatial effects without claiming a one-unit object translation strongly shifts a distant atmosphere.

## Aurora and reference

Lawlor and Genetti's journal paper was published in 2011 despite the author's `2010/aurora` PDF path. The implemented connection is a footprint multiplied by altitude-dependent emission, integrated along a three-dimensional ray.

Electron-energy/atmosphere tables, fluid-generated spline footprints, global planetary geometry, and distance-field acceleration are not reproduced. Folded footprints are procedural and advected with time. Color profiles are analytic and artistic. Pixel-footprint filtering broadens subpixel curtains with an amplitude correction. An error-function approximation integrates each locally linear Gaussian curtain cross-section over a full march interval. Height-dependent emission still uses midpoint quadrature. This reduces coherent slice bands without merely increasing the sample count.

The default layer spans `[1.2 R, 3.6 R]`. These are scaled scene units, not measured terrestrial aurora altitudes. Aurora absorption is omitted under an optically thin emissive approximation. Foreground clouds attenuate the summed background and aurora through their integrated transmission.

## Scientific interpretation

- Origins and representation formulas are independent variables in the representation comparison.
- Integration budgets are separate workload variables.
- Paired methods use the same field, optics, time, and budget.
- Explicit-position references isolate reconstruction error, not radiometric integration error.
- Differences to an origin-aware reference are representation diagnostics, not an unconditional quality ranking.
- A costly evaluator permits overhead-scaling measurements; it does not establish a performance improvement by itself.
- This source revision supplies no new target-GPU publication measurements.

## Display and HDR

The linear HDR attachment is unchanged by presentation. Display output now applies the sRGB transfer after the existing Reinhard compression, matching a conventional LDR display instead of writing linear values directly to an unencoded framebuffer. The manifest records `display_transfer=srgb`; the analyzer preserves the older linear presentation when that key is absent. Consequently old and new LDR images must not be compared as if they used the same transfer function.

## Resources and timing

The two noise textures contain `128 × 128 × 128 × 2` bytes each, totalling `8,388,608` bytes without mipmaps. They are periodic and trilinearly filtered. Compute generation finishes before benchmark timestamps begin.

All methods access the same field textures. GPU timestamps surround the fullscreen sky/reflection draw, excluding scene rasterization, initialization, GUI, and validation/readback. Validation attachments remain common to all methods.

The project-local timer tracks submitted slots, consumes results once, and resets pending samples at case boundaries. Controls cannot change a running benchmark. Stopped runs write `completed=0`; finished runs write `completed=1`.

## Reproducibility boundaries

The full profile is substantial, especially at 4K. Use smoke and quick profiles first. Collect publication measurements with a Release executable, the recorded driver, no VSync, and a quiet machine. Software-rendered validation demonstrates correctness only.

The manuscript and its old numerical results have not been modified.
