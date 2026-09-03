# Classic research scene assets

The application primarily loads models from `assets/models/` using these filenames:

- `dragon.obj` - Stanford Dragon
- `suzanne.obj` - Blender Suzanne
- `teapot.obj` - Utah/Newell Teapot

The loader also accepts the older names `stanford_dragon.obj` and `utah_teapot.obj`, both in `assets/models/` and directly in `assets/`, for compatibility with previous project revisions.

The OBJ files are not committed automatically because classic research meshes can carry source-specific attribution/redistribution terms. The repository includes a fetcher:

```bash
python tools/fetch_assets.py
```

It creates `assets/models/` automatically. If Python was found by CMake, the same operation is available as a build target:

```bash
cmake --build out/build --target fetch-assets
```

## Exact download sources

### Stanford Dragon

Repository/mirror:

`https://github.com/Domodhoro/Stanford-Dragon`

Direct OBJ:

`https://raw.githubusercontent.com/Domodhoro/Stanford-Dragon/main/dragon.obj`

Original model family: Stanford 3D Scanning Repository, Stanford Computer Graphics Laboratory. Keep Stanford attribution in publication/reproducibility material.

### Suzanne

Repository:

`https://github.com/alecjacobson/common-3d-test-models`

Direct OBJ:

`https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/suzanne.obj`

Suzanne is Blender's classic monkey-head test mesh.

### Utah Teapot

Repository:

`https://github.com/jaz303/utah-teapot`

Direct OBJ:

`https://raw.githubusercontent.com/jaz303/utah-teapot/master/teapot.obj`

The model is the classic Utah/Newell teapot. Keep the original source attribution with published material.
