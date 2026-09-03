#version 460 core

in vec2 vUv;

layout(location = 0) out vec4 oReconstructedWorld;
layout(location = 1) out vec4 oDomainHit;

uniform sampler2D uNormalRoughness;
uniform sampler2D uDepth;
uniform mat4 uInvViewProjection;
uniform vec3 uEye;
uniform vec3 uSkyCenter;
uniform float uSkyRadius;

vec3 reconstructWorld(float depth) {
    vec4 clip = vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProjection * clip;
    return world.xyz / world.w;
}

bool sphereIntersection(vec3 origin, vec3 direction, out vec3 hit) {
    direction = normalize(direction);
    vec3 offset = origin - uSkyCenter;
    float b = dot(offset, direction);
    float c = dot(offset, offset) - uSkyRadius * uSkyRadius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return false;
    float root = sqrt(discriminant);
    float t0 = -b - root;
    float t1 = -b + root;
    float t = t0 > 1e-6 ? t0 : (t1 > 1e-6 ? t1 : -1.0);
    if (t <= 0.0) return false;
    hit = origin + direction * t;
    return true;
}

void main() {
    float depth = texture(uDepth, vUv).r;
    vec3 farPoint = reconstructWorld(1.0);

    if (depth >= 0.999999) {
        vec3 direction = normalize(farPoint - uEye);
        vec3 hit = vec3(0.0);
        bool valid = sphereIntersection(uEye, direction, hit);
        oReconstructedWorld = vec4(farPoint, 0.0);
        oDomainHit = vec4(hit, valid ? 1.0 : 0.0);
        return;
    }

    vec3 world = reconstructWorld(depth);
    vec3 normal = normalize(texture(uNormalRoughness, vUv).xyz * 2.0 - 1.0);
    vec3 incident = normalize(world - uEye);
    vec3 reflected = reflect(incident, normal);
    vec3 hit = vec3(0.0);
    bool valid = sphereIntersection(world + normal * 0.002, reflected, hit);
    oReconstructedWorld = vec4(world, 1.0);
    oDomainHit = vec4(hit, valid ? 1.0 : 0.0);
}
