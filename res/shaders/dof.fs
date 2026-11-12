#version 330 core
//https://www.youtube.com/watch?v=v9x_50czf-4
// Depth of field, in four runs of this layer (main.cpp's LAYERS, repeats 4):
//   uPass 0: the inverse distance of every pixel (1/d: the sky goes to 0,
//            not to a huge number), into red
//   uPass 1: that blurred along x, BLUR_RADIUS px
//   uPass 2: and along y
//   uPass 3: the image (uLayerInput) blurred by a disc kernel: every pixel
//            gathers the points of RINGS rings around it, out to MAX_RADIUS
//            px, ring k with 6k points evenly round it (the same every
//            pixel and frame)
// How much a pixel is blurred, its circle of confusion (coc), comes from the
// blurred distances: so a blurred foreground softens over the edges of
// what's behind it instead of stopping hard at them. Gathered as if
// scattered: a point adds to a pixel only if its own circle reaches it.
//
// Two ways to set the coc, blended by uTiltShift (the demo's clock, see
// render::Dof): 0 a lens focused at uFocus metres (render::Dof eases it
// toward what's at its focus point), 1 tilt-shift, the timelapse's
// miniature look: a sharp band across the screen, more blur toward its top
// and bottom, whatever the distance.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScreen;     // the previous run's result
uniform sampler2D uLayerInput; // the image before this layer
uniform sampler2D uDepth;
uniform int uPass;
uniform mat4 uInvProjection; // view space from clip space
uniform float uFocus;        // m, the lens's focus distance
uniform float uTiltShift;    // 0 the lens, 1 tilt-shift

const float MAX_RADIUS = 15.0; // px, the most a pixel is blurred
// Rings of the disc kernel: 6 gives 1 + 6 + 12 + ... + 36 = 127 points,
// enough that a full-size blur shows no pattern
const int RINGS = 6;
// How fast the blur grows away from the focus: 1 reaches MAX_RADIUS where
// the distance is twice (or half) the focus distance
const float APERTURE = 0.15;
// How far the distances are blurred, px each side (sigma a third of it)
const int BLUR_RADIUS = 8;
// Tilt-shift: the most it blurs, px; the sharp band's middle (texture
// coordinates count up from the bottom), its half height, and how far beyond
// it the blur takes to grow to TILT_RADIUS
const float TILT_RADIUS = 6.0;
const float TILT_CENTER = 0.45, TILT_SHARP = 0.12, TILT_FALLOFF = 0.4;

// Inverse distance from the camera, 1/m; the sky (depth 1) about 0
float inverseDistanceAt(vec2 uv) {
    float depth = texture(uDepth, uv).r;
    vec4 p = uInvProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
    return 1.0 / max(length(p.xyz / p.w), 0.01);
}

// Circle of confusion at uv, px: 0 in focus. From the blurred inverse
// distance (run 1 and 2's result, in uScreen during run 3).
float coc(vec2 uv) {
    float lens = clamp(APERTURE * abs(1.0 - uFocus * texture(uScreen, uv).r),
                       0.0, 1.0);
    float tilt = smoothstep(TILT_SHARP, TILT_SHARP + TILT_FALLOFF,
                            abs(uv.y - TILT_CENTER));
    return mix(lens * MAX_RADIUS, tilt * TILT_RADIUS, uTiltShift);
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScreen, 0));
    if (uPass == 0) {
        FragColor = vec4(inverseDistanceAt(vUV), 0.0, 0.0, 1.0);
        return;
    }
    if (uPass == 1 || uPass == 2) {
        // Gaussian along x, then y
        vec2 step = uPass == 1 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
        const float SIGMA = float(BLUR_RADIUS) / 3.0;
        float sum = 0.0, total = 0.0;
        for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++) {
            float weight = exp(-0.5 * float(i * i) / (SIGMA * SIGMA));
            sum += texture(uScreen, vUV + float(i) * step).r * weight;
            total += weight;
        }
        FragColor = vec4(sum / total, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = texture(uLayerInput, vUV).rgb;
    float total = 1.0;
    for (int ring = 1; ring <= RINGS; ring++) {
        float r = float(ring) / float(RINGS) * MAX_RADIUS;
        // 6 points per ring step: evenly spaced, about as far apart along
        // every ring as the rings are from each other
        int points = 6 * ring;
        for (int k = 0; k < points; k++) {
            float a = 6.2831853 * float(k) / float(points);
            vec2 uv = vUV + vec2(cos(a), sin(a)) * r * texel;
            // Reaches this pixel if its circle is at least this far: soft
            // over the last pixel, so the blur grows smoothly
            float weight = clamp(coc(uv) - r + 1.0, 0.0, 1.0);
            color += texture(uLayerInput, uv).rgb * weight;
            total += weight;
        }
    }
    FragColor = vec4(color / total, 1.0);
}
