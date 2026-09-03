#version 460 core

out vec2 vUv;

void main() {
    // Oversized fullscreen triangle:
    // (-1, -1), (3, -1), (-1, 3).
    // The interpolated UV range over the visible viewport is exactly [0, 1].
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
