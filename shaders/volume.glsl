// LOM cloud evaluator adapted from sky_common.glsl / sky_clouds.glsl.
// Reference: Schneider and Vos, SIGGRAPH 2015. See docs/IMPLEMENTATION.md.
uniform sampler3D uCloudShape;
uniform sampler3D uCloudDetail;
uniform ivec4 uVolumeBudget;  // view, light, empty stride, aurora samples
uniform vec4 uVolumeField;   // scale, coverage, density, depth / R
uniform vec4 uVolumeShape;   // detail, softness, self shadow, type
uniform vec4 uVolumeOptics;  // anisotropy, multiple scatter, powder, detail scale
uniform vec4 uVolumeMotion;  // wind X, wind Z, evolution, weather
uniform vec2 uVolumeSampling; // light cone, deterministic jitter
uniform vec4 uAtmosphere;    // turbidity, zenith luminance, x, y
uniform vec4 uAtmosphereNorm;
uniform vec4 uAuroraVolume;  // base / R, height / R, curtain width / R, speed
uniform float uVolumeAuroraIntensity;

float perez(float ct,float gamma,float cg,vec4 abcd,float e) {
    return (1.0+abcd.x*exp(abcd.y/max(ct,0.03)))
        *(1.0+abcd.z*exp(abcd.w*gamma)+e*cg*cg);
}
vec3 lomAtmosphere(vec3 direction) {
    float cg=clamp(dot(direction,sunDirection()),-1.0,1.0), gamma=acos(cg), t=uAtmosphere.x;
    float Y=uAtmosphere.y*uAtmosphereNorm.x*perez(direction.y,gamma,cg,
        vec4(0.1787*t-1.4630,-0.3554*t+0.4275,-0.0227*t+5.3251,0.1206*t-2.5771),-0.0670*t+0.3703);
    float x=uAtmosphere.z*uAtmosphereNorm.y*perez(direction.y,gamma,cg,
        vec4(-0.0193*t-0.2592,-0.0665*t+0.0008,-0.0004*t+0.2125,-0.0641*t-0.8989),-0.0033*t+0.0452);
    float y=uAtmosphere.w*uAtmosphereNorm.z*perez(direction.y,gamma,cg,
        vec4(-0.0167*t-0.2608,-0.0950*t+0.0092,-0.0079*t+0.2102,-0.0441*t-1.6537),-0.0109*t+0.0529);
    vec3 xyz=vec3(x,max(y,0.001),max(1.0-x-y,0.0))*(max(Y,0.0)/max(y,0.001));
    vec3 rgb=max(vec3(dot(vec3(3.2406,-1.5372,-0.4986),xyz),dot(vec3(-0.9689,1.8758,0.0415),xyz),
        dot(vec3(0.0557,-0.2040,1.0570),xyz)),vec3(0))*uAtmosphereNorm.w;
    rgb/=1.0+dot(rgb,vec3(0.2126,0.7152,0.0722));
    float disc=smoothstep(cos(radians(0.55)),cos(radians(0.42)),cg);
    rgb+=vec3(1.0,0.86,0.62)*(disc*5.0+pow(max(cg,0.0),32.0)*0.12);
    return mix(vec3(0.09,0.08,0.07),rgb,smoothstep(-0.25,0.0,direction.y));
}

float lomShapeSignal(vec3 p,float height) {
    if(height<=0.0 || height>=1.0) return 0.0;
    float bottom=mix(0.06,0.14,uVolumeShape.w), top=mix(0.52,0.76,uVolumeShape.w);
    float profile=smoothstep(0.0,bottom,height)*(1.0-smoothstep(top,1.0,height));
    return textureLod(uCloudShape,p,0.0).r*profile;
}
float lomRefine(vec3 p,float height,float density,float footprint,float intervalFootprint) {
    if(density<=0.0) return 0.0;
    float detailFootprint=max(footprint,intervalFootprint)*(uVolumeOptics.w/2.07);
    float detailWeight=1.0-smoothstep(0.004,0.025,detailFootprint);
    if(detailWeight>0.0 && uVolumeShape.x>0.0) {
        vec2 fine=textureLod(uCloudDetail,p*uVolumeOptics.w+vec3(0.31,0.57,0.11),0.0).rg;
        float erosion=dot(fine,vec2(0.7,0.3));
        erosion=mix(1.0-erosion,erosion,smoothstep(0.0,0.25,height));
        erosion*=uVolumeShape.x*detailWeight*(1.0-density);
        density=max((density-erosion)/max(1.0-erosion,0.01),0.0);
    }
    return density*smoothstep(0.0,max(uVolumeShape.y,0.001),density);
}
float lomDensity(vec3 p,float height,float coverage,bool detail,float footprint,float interval) {
    float density=clamp((lomShapeSignal(p,height)-(1.0-coverage))/max(coverage,0.001),0.0,1.0);
    return detail ? lomRefine(p,height,density,footprint,interval) : density;
}
float lomLightDepth(vec3 p,float height,float coverage,float thickness,float footprint,
                    vec3 light,vec3 right,vec3 up) {
    float extent=min((1.0-height)*thickness/max(light.y,0.08),thickness*4.0);
    int samples=clamp(uVolumeBudget.y,1,16);
    float sum=0.0;
    vec2 coneDirection=vec2(1,0);
    for(int i=0;i<samples;++i) {
        float a=float(i)/float(samples),b=float(i+1)/float(samples);
        float first=a*a*extent,last=b*b*extent,offset=(first+last)*0.5;
        vec3 cone=(right*coneDirection.x+up*coneDirection.y)*(offset*uVolumeSampling.x);
        sum+=lomDensity(p+light*offset+cone,height+(light.y*offset+cone.y)/thickness,
            coverage,i<2,footprint,0.5*(last-first))*(last-first);
        coneDirection=vec2(-0.737368878*coneDirection.x-0.675490294*coneDirection.y,
            0.675490294*coneDirection.x-0.737368878*coneDirection.y);
    }
    return sum*(18.0*uVolumeField.z/thickness)*uVolumeShape.z;
}
float lomPhase(float cosine,float g) {
    float denominator=max(1.0+g*g-2.0*g*cosine,0.01);
    return (1.0-g*g)/(denominator*sqrt(denominator));
}
vec3 lomClouds(vec3 background,vec3 direction,vec3 samplePosition,float angularFootprint,bool night) {
    if(direction.y<=0.10 || uVolumeField.y<=0.0 || uVolumeField.z<=0.0) return background;
    vec3 xi=(queryPosition(direction,samplePosition)-uSkyCenter)/max(uSkyRadius,1e-4);
    vec2 slope=direction.xz/max(direction.y,0.10);
    vec2 projected=(xi.xz+(vec2(0.35)-xi.yy)*slope)*uVolumeField.x;
    float originHeight=xi.y-direction.y;
    float firstHeight=clamp((originHeight-0.35)/uVolumeField.w,0.0,1.0);
    if(firstHeight>=1.0) return background;
    // Wind is in world units per second, matching the LOM coordinate convention.
    vec3 motion=vec3(uVolumeMotion.x,-uVolumeMotion.z,uVolumeMotion.y)
        *(uTime*uVolumeField.x/max(uSkyRadius,1e-4));
    vec3 start=vec3(projected.x,0,projected.y)-motion;
    float thickness=uVolumeField.w*uVolumeField.x;
    vec3 segment=vec3(slope.x,1.0,slope.y)*thickness;
    float footprint=angularFootprint*uVolumeField.x*0.35/(direction.y*direction.y);
    float weather=clamp((textureLod(uCloudShape,vec3(start.x*0.17,0.37,start.z*0.17),0.0).r-0.45)*2.5,0.0,1.0);
    float coverage=clamp(uVolumeField.y+(weather-0.5)*uVolumeMotion.w,0.001,0.995);
    int samples=clamp(uVolumeBudget.x,8,256);
    float stepLength=length(segment)*(1.0-firstHeight)/float(samples);
    float extinction=18.0*uVolumeField.z/thickness;
    vec3 light=night ? moonDirection() : sunDirection();
    vec3 right=normalize(cross(light,abs(light.y)>0.98 ? vec3(0,0,1) : vec3(0,1,0)));
    vec3 up=cross(right,light);
    float cosine=dot(direction,light);
    float phase=0.55+0.45*mix(lomPhase(cosine,uVolumeOptics.x),lomPhase(cosine,-0.25),0.18);
    float powderView=1.0-smoothstep(-0.1,0.7,cosine);
    vec3 sunlight=night ? vec3(0.13,0.17,0.28) : vec3(1.0,0.93,0.79);
    vec3 ambient=night ? vec3(0.018,0.027,0.052) : vec3(0.26,0.32,0.42);
    float jitter=0.5+(hash13(vec3(floor(projected*2048.0),5.0))-0.5)*uVolumeSampling.y;
    float transmittance=1.0;
    vec3 radiance=vec3(0);
    int emptyStep=max(uVolumeBudget.z,1), i=0,emptyCount=0;
    bool fine=emptyStep==1;
    while(i<samples) {
        float height=mix(firstHeight,1.0,(float(i)+jitter)/float(samples));
        vec3 p=start+segment*height;
        float signal=lomShapeSignal(p,height);
        if(!fine) {
            if(signal>max(1.0-coverage-0.03,0.0)) {
                fine=true;i=max(i-emptyStep+1,0);emptyCount=0;continue;
            }
            i+=emptyStep;continue;
        }
        float base=clamp((signal-(1.0-coverage))/max(coverage,0.001),0.0,1.0);
        float density=lomRefine(p,height,base,footprint,0.5*stepLength);
        ++i;
        if(density<=0.0001) { if(++emptyCount>=4 && emptyStep>1) fine=false;continue; }
        emptyCount=0;
        float attenuation=exp(-density*extinction*stepLength);
        float lightDepth=uVolumeShape.z>0.0
            ? lomLightDepth(p,height,coverage,thickness,footprint,light,right,up) : 0.0;
        float single=exp(-lightDepth);
        float multiple=uVolumeOptics.y*(0.45*exp(-lightDepth*0.25)+0.15*exp(-lightDepth*0.0625));
        float powder=mix(1.0,0.5+0.5*(1.0-exp(-density*6.0)),uVolumeOptics.z*powderView);
        vec3 source=ambient*mix(0.42,1.2,height)+sunlight*(single+multiple)*phase*powder*1.8;
        radiance+=transmittance*(1.0-attenuation)*source;
        transmittance*=attenuation;
        float brightest=max(max(background.r,background.g),max(background.b,0.0625));
        if(transmittance<min(0.008,0.0005/brightest)) {transmittance=0.0;break;}
    }
    float opacity=1.0-transmittance;
    if(opacity<=0.0001) return background;
    vec3 cloudColor=radiance/opacity;
    cloudColor=min(cloudColor,vec3(0.5))+(1.0-exp(-max(cloudColor-0.5,vec3(0))*2.0))*0.5;
    float haze=(1.0-smoothstep(0.08,0.24,direction.y))*0.35;
    vec3 fog=night ? vec3(0.025,0.045,0.085) : lomAtmosphere(vec3(direction.x,0,direction.z));
    cloudColor=mix(cloudColor,fog,haze);
    float fade=smoothstep(0.10,0.18,direction.y);
    return background*mix(1.0,transmittance,fade)+cloudColor*opacity*fade;
}

// Lawlor and Genetti (2011): emissive footprint multiplied by altitude profiles,
// integrated along a spatial ray. Procedural advection replaces their fluid/spline data.
// These are scaled research-scene heights, not a magnetospheric particle simulation.
bool auroraInterval(vec3 origin,vec3 direction,out float begin,out float end) {
    if(abs(direction.y)<1e-6) return false;
    float a=(uAuroraVolume.x-origin.y)/direction.y;
    float b=(uAuroraVolume.x+uAuroraVolume.y-origin.y)/direction.y;
    begin=max(min(a,b),0.0); end=max(a,b);
    return end>begin;
}
float auroraCoordinate(vec2 p,vec2 drift) {
    vec2 flow=textureLod(uCloudDetail,vec3(p*0.12+drift,0.37),0.0).rg-0.5;
    float warp=0.80*sin(p.x*0.8+uTime*uAuroraVolume.w)+0.35*sin(p.x*2.3+flow.x*3.0);
    return p.y+warp+flow.y*0.45;
}
float auroraErf(float value) {
    // Abramowitz-Stegun 7.1.26. Used to integrate a thin Gaussian curtain
    // over the whole march interval instead of occasionally missing it.
    float x=abs(value),t=1.0/(1.0+0.3275911*x);
    float polynomial=((((1.061405429*t-1.453152027)*t+1.421413741)*t-0.284496736)*t+0.254829592)*t;
    return sign(value)*(1.0-polynomial*exp(-x*x));
}
float curtainInterval(float distance,float interval,float width) {
    float extent=abs(interval);
    if(extent<width*0.1) return exp(-0.5*distance*distance/(width*width));
    float scale=0.70710678118/width;
    float first=(distance-0.5*extent)*scale,last=(distance+0.5*extent)*scale;
    return max(1.2533141373*width/extent*(auroraErf(last)-auroraErf(first)),0.0);
}
float auroraFootprint(vec2 horizontal,vec2 interval,float pixelRadius) {
    vec2 p=horizontal*0.65,halfStep=interval*0.325;
    vec2 drift=vec2(uTime*uAuroraVolume.w*0.18,-uTime*uAuroraVolume.w*0.07);
    float coordinate=auroraCoordinate(p,drift);
    float firstCoordinate=auroraCoordinate(p-halfStep,drift);
    float lastCoordinate=auroraCoordinate(p+halfStep,drift);
    float originalWidth=max(uAuroraVolume.z*0.65,0.002);
    float width=sqrt(originalWidth*originalWidth+pixelRadius*pixelRadius/12.0);
    float footprint=0.0;
    for(int curtain=0;curtain<3;++curtain) {
        float offset=-4.0-float(curtain)*4.5,phase=float(curtain)*2.3;
        float center=offset+1.10*sin(p.x*0.44+phase);
        float first=firstCoordinate-(offset+1.10*sin((p.x-halfStep.x)*0.44+phase));
        float last=lastCoordinate-(offset+1.10*sin((p.x+halfStep.x)*0.44+phase));
        float streak=textureLod(uCloudShape,vec3(p.x*0.70+drift.x,0.19+float(curtain)*0.11,0.51),0.0).r;
        float rays=0.28+1.2*streak*streak;
        footprint+=curtainInterval(coordinate-center,last-first,width)*rays*(originalWidth/width);
    }
    return footprint;
}
vec3 volumetricAurora(vec3 direction,vec3 samplePosition,float angularFootprint) {
    vec3 q=(queryPosition(direction,samplePosition)-uSkyCenter)/max(uSkyRadius,1e-4);
    vec3 origin=q-direction; // Equals (o-s0)/R for the origin-aware representation.
    float begin,end;
    if(!auroraInterval(origin,direction,begin,end) || uVolumeAuroraIntensity<=0.0) return vec3(0);
    int samples=clamp(uVolumeBudget.w,16,512);
    float stepLength=(end-begin)/float(samples);
    vec3 radiance=vec3(0);
    // Midpoint height profiles with segment-integrated curtain footprints.
    // Sampling is deterministic and independent of screen coordinates.
    for(int i=0;i<samples;++i) {
        float distance=begin+(float(i)+0.5)*stepLength;
        vec3 p=origin+direction*distance;
        float h=clamp((p.y-uAuroraVolume.x)/uAuroraVolume.y,0.0,1.0);
        float pixelRadius=angularFootprint*distance*0.65;
        float flux=auroraFootprint(p.xz,direction.xz*stepLength,pixelRadius);
        float green=exp(-pow((h-0.25)/0.19,2.0));
        float violet=exp(-pow((h-0.075)/0.055,2.0));
        float red=exp(-pow((h-0.74)/0.25,2.0))*0.24;
        vec3 emission=vec3(0.08,1.0,0.32)*green+vec3(0.43,0.10,0.90)*violet+vec3(1.0,0.08,0.12)*red;
        float envelope=smoothstep(0.0,0.035,h)*(1.0-smoothstep(0.90,1.0,h));
        radiance+=emission*(flux*envelope*stepLength);
    }
    return radiance*(uVolumeAuroraIntensity*2.2/uAuroraVolume.y)*smoothstep(0.0,0.08,direction.y);
}
