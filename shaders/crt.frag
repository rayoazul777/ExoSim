#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uTime;
uniform vec2 uResolution;

void main() {
    vec2 uv = vUV;

    // Slight barrel distortion for the curved-glass feel.
    vec2 centered = uv * 2.0 - 1.0;
    float r2 = dot(centered, centered);
    centered *= 1.0 + 0.08 * r2;
    uv = centered * 0.5 + 0.5;

    // Outside the (distorted) screen bounds -> pure black bezel.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = texture(uScene, uv).rgb;

    // Horizontal scanlines.
    float scanline = sin(uv.y * uResolution.y * 1.5) * 0.5 + 0.5;
    color *= mix(0.75, 1.0, scanline);

    // Cheap glow: sample a few neighbours and add their brightness.
    vec2 texel = 1.0 / uResolution;
    vec3 glow = vec3(0.0);
    glow += texture(uScene, uv + vec2( texel.x, 0.0)).rgb;
    glow += texture(uScene, uv + vec2(-texel.x, 0.0)).rgb;
    glow += texture(uScene, uv + vec2(0.0,  texel.y)).rgb;
    glow += texture(uScene, uv + vec2(0.0, -texel.y)).rgb;
    color += glow * 0.10;

    // Vignette.
    float vignette = 1.0 - 0.55 * r2;
    color *= vignette;

    // Very subtle brightness flicker, sells the "old display" feel.
    color *= 0.97 + 0.03 * sin(uTime * 23.0);

    FragColor = vec4(color, 1.0);
}
