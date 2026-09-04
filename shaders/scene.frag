#version 460 core

in vec3 vNormal;
in vec3 vWorld;

uniform vec3 uBaseColor;
uniform float uRoughness;
uniform vec3 uEye;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormalRoughness;
layout(location = 2) out vec4 oWorldPosition;

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(vec3(-0.35, 0.88, 0.32));
    vec3 v = normalize(uEye - vWorld);
    vec3 h = normalize(l + v);

    float ndl = max(dot(n, l), 0.0);
    float ndh = max(dot(n, h), 0.0);
    float skyAmbient = 0.14 + 0.12 * max(n.y, 0.0);
    float specularPower = mix(160.0, 12.0, clamp(uRoughness, 0.0, 1.0));
    float specular = pow(ndh, specularPower) * mix(0.45, 0.05, uRoughness);

    vec3 diffuse = uBaseColor * (skyAmbient + ndl * 0.82);
    oColor = vec4(diffuse + vec3(specular), 1.0);
    oNormalRoughness = vec4(n * 0.5 + 0.5, uRoughness);
    oWorldPosition = vec4(vWorld, 1.0);
}
