#!/usr/bin/env python3
"""Optional OpenGL 4.6 regression checks against the actual project shaders.

Requires numpy, moderngl, glcontext and Pillow. Uses EGL by default; these are
correctness checks, not publication performance measurements.
"""
from __future__ import annotations
import argparse
import json
import math
import re
from pathlib import Path
import numpy as np
import moderngl as gl

ROOT = Path(__file__).resolve().parents[1]
NAMES = ["Cheap day", "Cheap night", "Cheap aurora", "Synthetic field",
         "LOM volumetric day", "LOM volumetric night", "LOM clouds + 3D aurora"]


def source(path: Path) -> str:
    return re.sub(r'#include "([^"]+)"', lambda m: source(path.parent / m[1]), path.read_text())


def normalized(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


def camera(width, height, offset=0.0):
    eye = np.array([3.55 + offset, 3.01, 5.38])
    target = np.array([offset, 0.95, -0.55])
    f = normalized(target-eye); right = normalized(np.cross(f, [0, 1, 0])); up = np.cross(right, f)
    view = np.eye(4); view[:3, :3] = [right, up, -f]; view[:3, 3] = -view[:3, :3] @ eye
    near, far = 0.02, 6000.0; cot = 1/math.tan(math.radians(54)/2)
    projection = np.zeros((4, 4)); projection[0, 0] = cot/(width/height); projection[1, 1] = cot
    projection[2, 2] = (far+near)/(near-far); projection[2, 3] = 2*far*near/(near-far); projection[3, 2] = -1
    return eye, projection @ view


def atmosphere():
    t = 2.5; theta = math.acos(normalized([math.cos(0.58), math.sin(0.58), 0.24])[1])
    th2, th3 = theta**2, theta**3
    x=(0.00166*th3-0.00375*th2+0.00209*theta)*t*t+(-0.02903*th3+0.06377*th2-0.03202*theta+0.00394)*t+(0.11693*th3-0.21196*th2+0.06052*theta+0.25886)
    y=(0.00275*th3-0.00610*th2+0.00317*theta)*t*t+(-0.04214*th3+0.08970*th2-0.04153*theta+0.00516)*t+(0.15346*th3-0.26756*th2+0.06670*theta+0.26688)
    luminance=(4.0453*t-4.9710)*math.tan((4/9-t/120)*(math.pi-2*theta))-0.2155*t+2.4192
    def norm(a,b,c,d,e):
        return 1/((1+a*math.exp(b))*(1+c*math.exp(d*theta)+e*math.cos(theta)**2))
    return (t,luminance,x,y), (norm(0.1787*t-1.4630,-0.3554*t+0.4275,-0.0227*t+5.3251,0.1206*t-2.5771,-0.0670*t+0.3703),
        norm(-0.0193*t-0.2592,-0.0665*t+0.0008,-0.0004*t+0.2125,-0.0641*t-0.8989,-0.0033*t+0.0452),
        norm(-0.0167*t-0.2608,-0.0950*t+0.0092,-0.0079*t+0.2102,-0.0441*t-1.6537,-0.0109*t+0.0529),0.035)


class Renderer:
    def __init__(self, backend, width=320, height=180):
        self.ctx=gl.create_standalone_context(require=460, **({"backend":backend} if backend else {}))
        self.width, self.height=width,height
        self.sky=self.ctx.program(vertex_shader=source(ROOT/"shaders/fullscreen.vert"),fragment_shader=source(ROOT/"shaders/sky.frag"))
        self.scene=self.ctx.program(vertex_shader=source(ROOT/"shaders/scene.vert"),fragment_shader=source(ROOT/"shaders/scene.frag"))
        self.fullscreen=self.ctx.vertex_array(self.sky,[])
        generator=self.ctx.compute_shader(source(ROOT/"shaders/cloud_noise.comp"))
        self.noise=[self.ctx.texture3d((128,128,128),2,dtype="f1") for _ in range(2)]
        for i,tex in enumerate(self.noise):
            tex.filter=(gl.LINEAR,gl.LINEAR); tex.bind_to_image(i,read=False,write=True)
        generator.run(16,16,16); self.ctx.memory_barrier(); self.ctx.finish()
        self.gbuffer=[self.ctx.texture((width,height),4,dtype=d) for d in ("f2","f2","f4")]
        for tex in self.gbuffer: tex.filter=(gl.NEAREST,gl.NEAREST)
        self.depth=self.ctx.depth_texture((width,height)); self.depth.compare_func=""
        self.scene_fbo=self.ctx.framebuffer(self.gbuffer,self.depth)
        self.post=[self.ctx.texture((width,height),4,dtype=d) for d in ("f2","f4","f2")]
        self.post_fbo=self.ctx.framebuffer(self.post)
        vertices=[]
        for x,z in [(-12,-12),(12,-12),(12,12),(-12,-12),(12,12),(-12,12)]: vertices.append([x,0,z,0,1,0])
        self.plane=self.mesh(vertices)
        vertices=[]
        def point(a,b):
            n=[math.sin(a)*math.cos(b),math.cos(a),math.sin(a)*math.sin(b)]
            return [n[0]*0.78,0.95+n[1]*0.78,0.15+n[2]*0.78]+n
        for y in range(24):
            for x in range(48):
                a,b=y*math.pi/24,x*2*math.pi/48; da,db=math.pi/24,2*math.pi/48
                vertices.extend([point(a,b),point(a+da,b),point(a+da,b+db),point(a,b),point(a+da,b+db),point(a,b+db)])
        self.sphere=self.mesh(vertices)
        for name,value in {"uSceneColor":0,"uNormalRoughness":1,"uDepth":2,"uWorldPosition":3,"uCloudShape":4,"uCloudDetail":5}.items(): self.sky[name]=value

    def mesh(self, vertices):
        data=self.ctx.buffer(np.array(vertices,dtype="f4").tobytes())
        return self.ctx.vertex_array(self.scene,[(data,"3f 3f","aPosition","aNormal")])

    def render(self, mode, method=2, influence=1.0, coverage=0.30, aurora=1.15, offset=0.0, radius=30.0, budget=1):
        eye,vp=camera(self.width,self.height,offset)
        # Use the same float matrix for rasterization and inversion as the C++ path.
        vp=vp.astype("f4"); inverse=np.linalg.inv(vp).astype("f4")
        self.scene_fbo.use(); self.ctx.viewport=(0,0,self.width,self.height)
        self.scene_fbo.clear(0,0,0,0,depth=1); self.ctx.enable(gl.DEPTH_TEST)
        for mesh,roughness,color in [(self.plane,0.38,(0.115,0.13,0.15)),(self.sphere,0.05,(0.055,0.065,0.085))]:
            self.scene["uModel"].write(np.eye(4,dtype="f4").T.tobytes())
            self.scene["uMvp"].write(vp.T.tobytes()); self.scene["uEye"].value=tuple(eye)
            self.scene["uRoughness"]=roughness; self.scene["uBaseColor"]=color; mesh.render()
        self.ctx.disable(gl.DEPTH_TEST); self.post_fbo.use()
        for unit,tex in enumerate([self.gbuffer[0],self.gbuffer[1],self.depth,self.gbuffer[2],*self.noise]): tex.use(unit)
        a,n=atmosphere()
        uniforms={"uEye":tuple(eye),"uSkyCenter":(0,0,0),"uSkyRadius":radius,"uSunElevation":0.58,
            "uCloudCoverage":0.52,"uCloudScale":1.0,"uCloudDensity":1.0,"uStarIntensity":1.0,"uAuroraIntensity":1.15,
            "uLocalInfluence":influence,"uTime":12.0,"uSkyMode":mode,"uMethod":method,"uObjectReflections":1,
            "uVolumeBudget":[(32,3,2,64),(64,6,2,128),(128,12,2,256)][budget],"uVolumeField":(1.25,coverage,2.0,0.28),
            "uVolumeShape":(0.25,0.02,1.3,0.8),"uVolumeOptics":(0.65,0.35,0.8,2.07),
            "uVolumeMotion":(12,4,0.8,0.2),"uVolumeSampling":(0.12,0.15),"uAtmosphere":a,"uAtmosphereNorm":n,
            "uAuroraVolume":(1.2,2.4,0.035,0.08),"uVolumeAuroraIntensity":aurora}
        for name,value in uniforms.items(): self.sky[name].value=value
        self.sky["uInvViewProjection"].write(inverse.T.tobytes())
        self.fullscreen.render(vertices=3)
        return np.frombuffer(self.post[2].read(),dtype="f2").astype("f4").reshape(self.height,self.width,4)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend",default="egl"); parser.add_argument("--output",type=Path,default=Path("results/gpu-validation"))
    args=parser.parse_args(); renderer=Renderer(args.backend)
    report={"renderer":renderer.ctx.info["GL_RENDERER"],"kind":"shader correctness, not performance", "checks":[]}
    previews=[]
    def check(name, passed, **data):
        row={"name":name,"passed":bool(passed),**data}; report["checks"].append(row); print(json.dumps(row),flush=True)
        if not passed: raise AssertionError(name)
    for mode,name in enumerate(NAMES):
        image=renderer.render(mode,radius=6000.0 if mode>=4 else 300.0)
        check(name+" finite HDR",np.isfinite(image).all(),maximum=float(image[:,:,:3].max()))
        baseline=renderer.render(mode,method=0,influence=0.0)
        for method in (1,2):
            tested=renderer.render(mode,method=method,influence=0.0)
            check(name+f" null method {method}",np.array_equal(baseline,tested),rmse=float(np.sqrt(np.mean((baseline-tested)**2))))
        previews.append(image)
    for mode in (4,5,6):
        image=renderer.render(mode,radius=6000.0)
        repeat=renderer.render(mode,radius=6000.0)
        check(NAMES[mode]+" deterministic",np.array_equal(image,repeat))
        clear=renderer.render(mode,coverage=0.0,radius=6000.0)
        difference=np.abs(image-clear)[:,:,:3]
        check(NAMES[mode]+" occupied clouds",float(difference.max())>0.01,changed_pixels=int(np.any(difference>0.005,axis=2).sum()))
    with_aurora=renderer.render(6); without_aurora=renderer.render(6,aurora=0.0)
    check("Spatial aurora emits through cloud gaps",float(np.abs(with_aurora-without_aurora).max())>0.01,
          maximum_difference=float(np.abs(with_aurora-without_aurora).max()))
    moved=renderer.render(3,offset=1.0); static=renderer.render(3,offset=0.0)
    check("World field responds to translation",float(np.abs(moved-static).max())>0.1)
    for budget in (0,2):
        image=renderer.render(6,budget=budget)
        check(f"Volume budget {budget} finite",np.isfinite(image).all())
    # Exercise the actual GLSL interval integrator against an independent
    # double-precision erf reference over distances and integration lengths.
    fragment=source(ROOT/"shaders/sky.frag").replace("void main() {","void unusedSceneMain() {")
    fragment+='''
void main() {
    float distance=(vUv.x-0.5)*1.6;
    float extent=0.02+0.5*vUv.y;
    oDomainHit=vec4(curtainInterval(distance,extent,0.04),0,0,1);
    oColor=vec4(0); oLinearColor=vec4(0);
}
'''
    probe=renderer.ctx.program(vertex_shader=source(ROOT/"shaders/fullscreen.vert"),fragment_shader=fragment)
    renderer.post_fbo.use(); renderer.ctx.vertex_array(probe,[]).render(vertices=3)
    values=np.frombuffer(renderer.post[1].read(),dtype="f4").reshape(renderer.height,renderer.width,4)[:,:,0]
    distance=((np.arange(renderer.width)+0.5)/renderer.width-0.5)*1.6
    extent=0.02+0.5*(np.arange(renderer.height)+0.5)/renderer.height
    reference=np.array([[math.sqrt(math.pi/2)*0.04/e*(math.erf((d+e/2)/(math.sqrt(2)*0.04))
        -math.erf((d-e/2)/(math.sqrt(2)*0.04))) for d in distance] for e in extent])
    maximum_error=float(np.max(np.abs(values-reference)))
    check("Curtain interval vs analytic Gaussian integral",maximum_error<1e-5,maximum_error=maximum_error)
    check("OpenGL error state",renderer.ctx.error=="GL_NO_ERROR")
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/"validation.json").write_text(json.dumps(report,indent=2)+"\n")
    from PIL import Image,ImageDraw
    sheet=Image.new("RGB",(renderer.width*3,(renderer.height+28)*2),(12,17,25)); draw=ImageDraw.Draw(sheet)
    for index,mode in enumerate((0,1,2,4,5,6)):
        color=np.maximum(previews[mode][:,:,:3],0); color=color/(1+color)
        color=np.where(color<=0.0031308,12.92*color,1.055*color**(1/2.4)-0.055)
        image=Image.fromarray(np.flipud(np.round(color*255).astype("u1")))
        x,y=(index%3)*renderer.width,(index//3)*(renderer.height+28)
        sheet.paste(image,(x,y+28)); draw.text((x+8,y+7),NAMES[mode],fill=(206,225,245))
    sheet.save(args.output/"sky_modes.png")
    print(f"Passed {len(report['checks'])} GPU checks; {args.output}")


if __name__=="__main__": main()
