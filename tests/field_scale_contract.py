"""Field-space scale may be varied independently of ray sampling distance."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'shaders/sky.frag').read_text();v=(r/'shaders/volume.glsl').read_text()
assert 'uniform float uFieldScaleRadius;' in s
assert 'uSkyCenter + normalize(rayDir) * uSkyRadius' in s
assert '(query - uSkyCenter) / max(uFieldScaleRadius' in s
assert '/max(uFieldScaleRadius' in v
assert 'uniform float uSkyRadius' in s
print('field-scale split contract: PASS')
