#version 330 core

in vec2 vWorldXY;

uniform float uGridSize;
uniform vec2  uCamPos;
uniform vec3  uLineColor;
uniform float uMaxDist;

out vec4 fragColor;

void main() {
    if (uGridSize < 0.01) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Grid line detection: find distance to nearest grid edge within cell
    vec2 cell = mod(vWorldXY, uGridSize) / uGridSize;
    vec2 f = abs(cell - 0.5);
    const float lineW = 0.04;
    float line = max(
        smoothstep(0.5 - lineW, 0.5, f.x),
        smoothstep(0.5 - lineW, 0.5, f.y)
    );

    // Quadratic distance fade from camera
    float dist = length(vWorldXY - uCamPos);
    float fade = 1.0 - clamp(dist / uMaxDist, 0.0, 1.0);
    fade *= fade;

    fragColor = vec4(uLineColor * line * fade, 1.0);
}
