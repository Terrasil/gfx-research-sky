#version 460 core

in vec2 vUv;
layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oDomainHit;
layout(location = 2) out vec4 oLinearColor;

uniform sampler2D uSceneColor;
uniform sampler2D uNormalRoughness;
uniform sampler2D uDepth;
uniform sampler2D uWorldPosition;
uniform mat4 uInvViewProjection;
uniform vec3 uEye;
uniform vec3 uSkyCenter;
uniform float uSkyRadius;
uniform float uSunElevation;
uniform float uCloudCoverage;
uniform float uCloudScale;
uniform float uCloudDensity;
uniform float uStarIntensity;
uniform float uAuroraIntensity;
uniform float uLocalInfluence;
uniform float uTime;
uniform int uSkyMode;
uniform int uMethod;
uniform int uObjectReflections;

const float PI = 3.14159265358979323846;

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;
    mat3 rotation = mat3(
        0.00, 0.80, 0.60,
       -0.80, 0.36,-0.48,
       -0.60,-0.48, 0.64
    );

    for (int octave = 0; octave < 5; ++octave) {
        value += amplitude * valueNoise(p);
        p = rotation * p * 2.03 + vec3(17.1, 9.2, 13.7);
        amplitude *= 0.5;
    }
    return value;
}

vec3 sunDirection() {
    return normalize(vec3(cos(uSunElevation), sin(uSunElevation), 0.24));
}

vec3 moonDirection() {
    float elevation = max(uSunElevation, radians(12.0));
    return normalize(vec3(-0.52 * cos(elevation), sin(elevation), -0.73 * cos(elevation)));
}

vec3 queryPosition(vec3 rayDir, vec3 samplePosition) {
    vec3 baselinePosition = uSkyCenter + normalize(rayDir) * uSkyRadius;
    return mix(baselinePosition, samplePosition, clamp(uLocalInfluence, 0.0, 1.0));
}

vec3 queryDirection(vec3 rayDir, vec3 samplePosition) {
    vec3 local = queryPosition(rayDir, samplePosition) - uSkyCenter;
    float lengthSquared = dot(local, local);
    return lengthSquared > 1e-8 ? local * inversesqrt(lengthSquared) : normalize(rayDir);
}

float cloudField(vec3 rayDir, vec3 samplePosition) {
    if (rayDir.y < -0.08) return 0.0;

    vec3 query = queryPosition(rayDir, samplePosition);
    float scale = max(uCloudScale, 0.05);
    vec3 wind = vec3(uTime * 0.018, uTime * 0.006, -uTime * 0.012);
    vec3 p = ((query - uSkyCenter) / max(uSkyRadius, 1e-4)) * (4.7 * scale) + wind;

    float broad = fbm(p);
    float erosion = fbm(p * 2.35 + vec3(23.4, -8.1, 41.7));
    float density = broad * 0.78 + erosion * 0.22;
    float threshold = mix(0.76, 0.40, clamp(uCloudCoverage, 0.0, 1.0));
    float cloud = smoothstep(threshold, threshold + 0.12, density);
    cloud *= smoothstep(-0.04, 0.16, rayDir.y);
    return clamp(cloud * uCloudDensity, 0.0, 1.0);
}

float cloudEdge(vec3 rayDir, vec3 samplePosition) {
    vec3 query = queryPosition(rayDir, samplePosition);
    float scale = max(uCloudScale, 0.05);
    vec3 p = (query - uSkyCenter) / max(uSkyRadius, 1e-4);
    float coarse = fbm(p * (4.5 * scale));
    float fine = fbm(p * (8.2 * scale) + vec3(8.0, 31.0, -15.0));
    return clamp((coarse - fine * 0.34) * 1.15, 0.0, 1.0);
}

vec3 daySky(vec3 rayDir, vec3 samplePosition) {
    float y = clamp(rayDir.y, 0.0, 1.0);
    float horizonFactor = exp(-max(rayDir.y, 0.0) * 4.0);
    vec3 horizon = vec3(0.53, 0.70, 0.91);
    vec3 upper = vec3(0.16, 0.39, 0.72);
    vec3 zenith = vec3(0.035, 0.16, 0.43);
    vec3 color = mix(horizon, upper, smoothstep(0.0, 0.48, y));
    color = mix(color, zenith, smoothstep(0.38, 1.0, pow(y, 0.72)));
    color += vec3(0.18, 0.11, 0.055) * horizonFactor;

    vec3 sunDir = sunDirection();
    float sunCos = max(dot(rayDir, sunDir), 0.0);
    float sunDisc = smoothstep(cos(radians(0.52)), cos(radians(0.18)), sunCos);
    float aureole = pow(sunCos, 18.0);
    color += vec3(1.0, 0.66, 0.28) * aureole * 0.42;
    color += vec3(1.0, 0.88, 0.62) * sunDisc * 2.4;

    float cloud = cloudField(rayDir, samplePosition);
    if (cloud > 0.0001) {
        float edge = cloudEdge(rayDir, samplePosition);
        float towardSun = clamp(dot(rayDir, sunDir) * 0.5 + 0.5, 0.0, 1.0);
        float altitudeLight = smoothstep(0.0, 0.45, rayDir.y);
        vec3 cloudShadow = vec3(0.43, 0.48, 0.55);
        vec3 cloudLight = vec3(1.08, 1.05, 0.99);
        vec3 cloudColor = mix(cloudShadow, cloudLight, 0.48 + 0.30 * towardSun + 0.22 * altitudeLight);
        cloudColor += vec3(1.0, 0.75, 0.42) * aureole * edge * 0.18;
        color = mix(color, cloudColor, cloud * (0.72 + 0.22 * edge));
    }
    return max(color, vec3(0.0));
}

float starLayer(vec3 rayDir, float scale, float threshold) {
    vec3 p = rayDir * scale;
    vec3 cell = floor(p);
    vec3 local = fract(p) - 0.5;
    float seed = hash13(cell);
    float star = smoothstep(threshold, 1.0, seed);
    float point = exp(-dot(local, local) * 42.0);
    float twinkle = 0.82 + 0.18 * sin(seed * 93.0 + uTime * (1.2 + seed * 2.0));
    return star * point * twinkle;
}

vec3 nightBase(vec3 rayDir, vec3 samplePosition, bool withClouds) {
    float y = clamp(rayDir.y, 0.0, 1.0);
    vec3 horizon = vec3(0.025, 0.045, 0.085);
    vec3 zenith = vec3(0.0025, 0.008, 0.028);
    vec3 color = mix(horizon, zenith, pow(y, 0.52));

    float stars = starLayer(rayDir, 310.0, 0.9955) + starLayer(rayDir, 520.0, 0.9982) * 1.7;
    stars *= smoothstep(-0.01, 0.16, rayDir.y) * uStarIntensity;
    color += vec3(0.78, 0.86, 1.0) * stars;

    vec3 moonDir = moonDirection();
    float moonCos = max(dot(rayDir, moonDir), 0.0);
    float moonDisc = smoothstep(cos(radians(1.15)), cos(radians(0.76)), moonCos);
    float moonGlow = pow(moonCos, 42.0);
    color += vec3(0.45, 0.56, 0.82) * moonGlow * 0.12;
    color += vec3(0.92, 0.94, 0.87) * moonDisc * 1.35;

    if (withClouds) {
        float cloud = cloudField(rayDir, samplePosition) * 0.78;
        vec3 cloudColor = mix(vec3(0.045, 0.055, 0.082), vec3(0.15, 0.17, 0.22), moonGlow);
        color = mix(color, cloudColor, cloud);
    }
    return max(color, vec3(0.0));
}

vec3 aurora(vec3 rayDir, vec3 samplePosition) {
    vec3 color = nightBase(rayDir, samplePosition, false);
    if (rayDir.y < 0.01) return color;

    vec3 domainDir = queryDirection(rayDir, samplePosition);
    float azimuth = atan(domainDir.z, domainDir.x);
    float elevation = asin(clamp(rayDir.y, -1.0, 1.0));

    float slowNoise = fbm(vec3(azimuth * 1.35, domainDir.y * 1.6, 2.7 + uTime * 0.035));
    float arcCenter = 0.30 + 0.075 * sin(azimuth * 2.1 + slowNoise * 4.0 + uTime * 0.06);
    float arc = exp(-abs(elevation - arcCenter) * 7.5);

    float folds = 0.5 + 0.5 * sin(
        azimuth * 31.0
        + fbm(vec3(azimuth * 4.2, domainDir.xz * 2.2)) * 8.0
        + uTime * 0.10
    );
    folds = pow(folds, 2.2);

    float verticalFade = smoothstep(0.02, 0.16, rayDir.y) * (1.0 - smoothstep(0.83, 0.98, rayDir.y));
    float curtain = arc * (0.38 + 0.85 * folds) * verticalFade;

    float secondaryCenter = 0.48 + 0.055 * sin(azimuth * 1.4 - slowNoise * 3.0);
    float secondary = exp(-abs(elevation - secondaryCenter) * 11.0) * (0.25 + 0.75 * (1.0 - folds));
    curtain += secondary * 0.42 * verticalFade;

    float fine = fbm(domainDir * 14.0 + vec3(0.0, uTime * 0.045, 0.0));
    curtain *= mix(0.65, 1.18, fine);
    curtain *= uAuroraIntensity;

    vec3 green = vec3(0.07, 1.00, 0.48);
    vec3 cyan = vec3(0.12, 0.72, 1.00);
    vec3 violet = vec3(0.55, 0.20, 0.95);
    float high = smoothstep(0.28, 0.62, rayDir.y);
    vec3 auroraColor = mix(green, cyan, high * 0.58);
    auroraColor = mix(auroraColor, violet, high * high * 0.34 + secondary * 0.20);

    color += auroraColor * curtain * 0.95;
    color += green * curtain * curtain * 0.22;
    return max(color, vec3(0.0));
}

vec3 syntheticWorldField(vec3 rayDir, vec3 samplePosition) {
    vec3 q = (queryPosition(rayDir, samplePosition) - uSkyCenter) / max(uSkyRadius, 1e-4);
    vec3 bands = 0.5 + 0.5 * sin(vec3(11.0 * q.x + 3.0 * q.z, 9.0 * q.y - 4.0 * q.x, 13.0 * q.z + 2.0 * q.y));
    float cells = mod(floor((q.x + 2.0) * 6.0) + floor((q.z + 2.0) * 6.0), 2.0);
    vec3 checker = mix(vec3(0.10, 0.18, 0.72), vec3(0.95, 0.38, 0.08), cells);
    return mix(checker, bands, 0.55);
}

vec3 proceduralSky(vec3 rayDir, vec3 samplePosition) {
    rayDir = normalize(rayDir);
    if (uSkyMode == 1) return nightBase(rayDir, samplePosition, true);
    if (uSkyMode == 2) return aurora(rayDir, samplePosition);
    if (uSkyMode == 3) return syntheticWorldField(rayDir, samplePosition);
    return daySky(rayDir, samplePosition);
}

bool sphereIntersection(vec3 origin, vec3 direction, out vec3 hit) {
    direction = normalize(direction);
    vec3 o = origin - uSkyCenter;
    float b = dot(o, direction);
    float c = dot(o, o) - uSkyRadius * uSkyRadius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return false;

    float root = sqrt(discriminant);
    float nearT = -b - root;
    float farT = -b + root;
    float t = nearT > 1e-4 ? nearT : farT;
    if (t <= 1e-4) return false;

    hit = origin + direction * t;
    return true;
}

vec3 reconstructWorld(float depth) {
    vec4 clip = vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProjection * clip;
    return world.xyz / world.w;
}

vec3 skyForRay(vec3 origin, vec3 direction, out vec3 samplePosition) {
    direction = normalize(direction);

    // 0: direction-only baseline. The synthesized coordinate is independent of ray origin.
    samplePosition = uSkyCenter + direction * uSkyRadius;

    // 1: legacy fixed world-space spherical domain, retained only for comparison.
    if (uMethod == 1) {
        vec3 localHit;
        if (sphereIntersection(origin, direction, localHit)) samplePosition = localHit;
    }

    // 2/3: proposed per-origin virtual sphere. The virtual sampling shell is centered
    // at the ray origin, so for normalized d the query is exactly q = o + R d.
    // No quadratic equation, square root, or root selection is required.
    if (uMethod >= 2) samplePosition = origin + direction * uSkyRadius;

    return proceduralSky(direction, samplePosition);
}

vec3 displayMap(vec3 color) {
    // Mild filmic compression keeps the sun and aurora readable on the default 8-bit framebuffer.
    color = max(color, vec3(0.0));
    return color / (vec3(1.0) + color);
}

void main() {
    float depth = texture(uDepth, vUv).r;
    vec3 farPoint = reconstructWorld(1.0);
    vec3 viewRay = normalize(farPoint - uEye);

    if (depth >= 0.999999) {
        vec3 samplePosition;
        vec3 linearColor = skyForRay(uEye, viewRay, samplePosition);
        oLinearColor = vec4(linearColor, 1.0);
        oColor = vec4(displayMap(linearColor), 1.0);
        oDomainHit = vec4(samplePosition, 1.0);
        return;
    }

    vec3 base = texture(uSceneColor, vUv).rgb;
    if (uObjectReflections == 0) {
        oDomainHit = vec4(0.0);
        oLinearColor = vec4(base, 1.0);
        oColor = vec4(displayMap(base), 1.0);
        return;
    }

    vec4 normalRoughness = texture(uNormalRoughness, vUv);
    vec3 normal = normalize(normalRoughness.xyz * 2.0 - 1.0);
    float roughness = clamp(normalRoughness.w, 0.0, 1.0);
    vec3 world = uMethod == 3 ? texture(uWorldPosition, vUv).xyz : reconstructWorld(depth);
    vec3 incident = normalize(world - uEye);
    vec3 reflected = reflect(incident, normal);
    vec3 samplePosition;
    vec3 reflectedSky = skyForRay(world + normal * 0.002, reflected, samplePosition);
    oDomainHit = vec4(samplePosition, 1.0);

    float fresnel = pow(1.0 - max(dot(normalize(uEye - world), normal), 0.0), 5.0);
    float reflectionWeight = mix(0.76, 0.07, roughness);
    reflectionWeight = mix(reflectionWeight, 1.0, fresnel * (1.0 - roughness) * 0.45);
    vec3 shaded = mix(base, reflectedSky, clamp(reflectionWeight, 0.0, 0.92));
    oLinearColor = vec4(shaded, 1.0);
    oColor = vec4(displayMap(shaded), 1.0);
}
