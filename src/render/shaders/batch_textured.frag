#version 330 core

in vec4 vColor;
in vec4 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    // Project texcoords (q-division), matching GL_MODULATE fixed-function behavior.
    // For non-projective coords q=1, so the division is a no-op.
    vec2 tc = vTexCoord.xy / max(vTexCoord.w, 0.0001);
    fragColor = texture(uTexture, tc) * vColor;
}
