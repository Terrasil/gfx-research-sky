#!/usr/bin/env python3
"""Score the actual GLSL slab and representation paths against a closed-form CPU integral."""
import argparse
import csv
from pathlib import Path
import sys
import numpy as np
import moderngl as gl
from validate_gpu import ROOT, source


def analytic(origin, direction, scale):
    """Integrate uniform extinction and an oscillatory source with a complex exponential."""
    origin = origin / scale
    a, b = (0.2-origin[...,1])/direction[...,1], (0.8-origin[...,1])/direction[...,1]
    entry = np.maximum(0, np.minimum(a,b))
    length = np.maximum(0, np.minimum(4,np.maximum(a,b)) - entry)
    begin = origin + entry[...,None]*direction
    phase = 7*begin[...,0] + 2*begin[...,2]
    omega = 7*direction[...,0] + 2*direction[...,2]
    z = -0.8 + 1j*omega
    transmittance = np.exp(-0.8*length)
    oscillation = np.real(np.exp(1j*phase)*np.expm1(z*length)/z)
    light = 0.65*(1-transmittance) + 0.24*oscillation
    return transmittance[...,None]*np.array([.06,.12,.24]) + light[...,None]*np.array([1.,.64,.35])


def check(backend, output):
    width, height = 160, 90
    ctx = gl.create_standalone_context(require=460, **({"backend":backend} if backend else {}))
    fragment = source(ROOT/"shaders/sky.frag").replace("void main() {","void unusedSceneMain() {")
    fragment += """
uniform float uOffset;
void main() {
    vec3 d=normalize(vec3(2.0*vUv.x-1.0,0.07+0.65*vUv.y,0.48-vUv.x));
    vec3 o=vec3(uOffset+4.0*vUv.x,1.0+5.0*vUv.y,-1.0+2.0*vUv.x);
    vec3 samplePosition;
    oLinearColor=vec4(skyForRay(o,d,0.001,samplePosition),1);
    oColor=vec4(0); oDomainHit=vec4(0);
}
"""
    program = ctx.program(vertex_shader=source(ROOT/"shaders/fullscreen.vert"),fragment_shader=fragment)
    textures = [ctx.texture((width,height),4,dtype="f4") for _ in range(3)]
    fbo = ctx.framebuffer(textures)
    fbo.use()
    vao = ctx.vertex_array(program,[])
    x,y = np.meshgrid((np.arange(width)+.5)/width,(np.arange(height)+.5)/height)
    d = np.stack([2*x-1,.07+.65*y,.48-x],axis=-1)
    d /= np.linalg.norm(d,axis=-1,keepdims=True)
    program["uSkyCenter"] = (0,0,0)
    program["uLocalInfluence"] = 1.
    program["uSkyMode"] = 7
    program["uFieldScaleRadius"] = 30.
    rows = []
    for offset in (0.,4.,-4.):
        origin = np.stack([offset+4*x,1+5*y,-1+2*x],axis=-1)
        reference = analytic(origin,d,30.)
        program["uOffset"] = offset
        for radius in (15.,30.,150.):
            program["uSkyRadius"] = radius
            for method in (0,1,2):
                program["uMethod"] = method
                for steps in (32,64,128):
                    program["uVolumeBudget"] = (steps,6,2,128)
                    vao.render(vertices=3)
                    values = np.frombuffer(textures[2].read(),dtype=np.float32).reshape(height,width,4)[...,:3]
                    error = np.abs(values-reference)
                    rows.append(dict(offset=offset,radius=radius,field_scale=30,method=method,samples=steps,
                                     rmse=float(np.sqrt(np.mean(error**2))),abs_p95=float(np.quantile(error,.95)),
                                     max_abs=float(error.max())))
    for r in rows:
        if not np.isfinite(r["rmse"]):
            raise ValueError("Nonfinite shader radiance")
        if r["method"] == 2 and r["samples"] == 128 and r["max_abs"] > 3e-4:
            raise ValueError(f"Fixed-distance slab disagrees with the analytic integral: {r}")
    output.mkdir(parents=True,exist_ok=True)
    with (output/"analytic_slab.csv").open("w",newline="",encoding="utf-8") as file:
        writer=csv.DictWriter(file,fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    info=ctx.info
    (output/"manifest.txt").write_text(f"renderer={info['GL_RENDERER']}\nversion={info['GL_VERSION']}\n"
                                       f"cases={len(rows)}\nwidth={width}\nheight={height}\ncompleted=1\n")
    maximum=max(r["max_abs"] for r in rows if r["method"]==2 and r["samples"]==128)
    print(f"{len(rows)} analytic slab comparisons; maximum origin-aware error at 128 steps = {maximum:.7g}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend",default="egl" if sys.platform.startswith("linux") else "")
    parser.add_argument("--output",type=Path,default=Path("results/analytic-slab"))
    args = parser.parse_args()
    check(args.backend,args.output)
