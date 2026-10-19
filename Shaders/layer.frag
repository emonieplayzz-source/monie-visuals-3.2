#version 120
// monie visuals v3 - layer pass. Draws ONE layer (image / video frame / rasterised text) with its transform and per-layer look
// into a transparent, premultiplied-alpha buffer. Blending onto the composition happens in composite.frag.
uniform sampler2D u_tex, u_tex2, u_fill, u_grad;
uniform int   u_mode;                 // 0 = media, 1 = text/caption
uniform vec2  u_res;
uniform float u_time;
uniform vec2  u_ctr, u_half, u_skew, u_tilt;     // aspect-space centre / half size, skew, perspective
uniform float u_rot;
uniform vec4  u_crop;                 // l r t b (fractions)
uniform float u_bright, u_contrast, u_sat, u_gamma, u_hue, u_cmap, u_blur, u_sharp, u_dist, u_mscale, u_mspeed, u_disp, u_noise, u_rgb, u_aud;
// text
uniform vec3  u_fillC, u_fill2C, u_o1C, u_o2C, u_shC, u_glowC, u_bgC, u_hiC;
uniform float u_gradOn, u_gradAngle, u_gradAnim, u_cycle, u_useFillTex, u_shOp, u_glow, u_bgOp;
uniform vec2  u_shOff;
// animation state (see TextAnim.h)
uniform float u_reveal, u_waveAmp, u_waveFreq, u_wavePhase, u_shake, u_glitch, u_aBlur, u_aRgb, u_alpha, u_hueAnim, u_glowBoost;
varying vec2 vUV;
#define PI 3.14159265

float hash(vec2 p) { p = fract(p * vec2(443.897, 441.423)); p += dot(p, p + 19.19); return fract(p.x * p.y); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y); }
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 3; i++) { s += a * noise(p); p = p * 2.03 + 3.7; a *= 0.5; } return s; }
vec2 rotv(vec2 v, float a) { float c = cos(a), s = sin(a); return vec2(c * v.x - s * v.y, s * v.x + c * v.y); }
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 hueShift(vec3 c, float h) { float a = h * 6.2831853, ca = cos(a), sa = sin(a); vec3 k = vec3(0.57735); return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca); }
vec3 gradMap(float t) { t = 1.0 - abs(1.0 - mod(t, 2.0)); return texture2D(u_grad, vec2(clamp(t, 0.002, 0.998), 0.5)).rgb; }
vec4 over(vec4 top, vec4 bot) { return top + bot * (1.0 - top.a); }

vec4 sampleMedia(vec2 uv) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec4(0.0);
    vec2 cuv = vec2(mix(u_crop.x, 1.0 - u_crop.y, uv.x), mix(u_crop.w, 1.0 - u_crop.z, uv.y));
    return texture2D(u_tex, vec2(cuv.x, 1.0 - cuv.y));
}

void main() {
    float asp = u_res.x / u_res.y;
    vec2 p = (vUV * 2.0 - 1.0) * vec2(asp, 1.0);
    vec2 l = rotv(p - u_ctr, -u_rot);
    l.x -= u_skew.x * l.y; l.y -= u_skew.y * l.x;
    l /= max(1.0 + dot(u_tilt, l), 0.25);
    vec2 uv = l / u_half * 0.5 + 0.5;
    vec4 outc = vec4(0.0);

    if (u_mode == 0) {
        float t = u_time * u_mspeed;
        vec2 w = vec2(0.0);
        if (u_dist > 0.001) { vec2 q = uv * u_mscale * 2.0; w += (vec2(fbm(q + t * 0.3), fbm(q + 7.0 - t * 0.3)) - 0.5) * u_dist * (0.15 + 0.5 * u_aud); }
        if (u_noise > 0.001) w += (vec2(noise(uv * 40.0 + t * 3.0), noise(uv * 40.0 + 9.0 - t * 3.0)) - 0.5) * u_noise * 0.06;
        if (u_disp > 0.001) w.x += u_disp * u_aud * 0.06 * sin(uv.y * 22.0 + t * 2.0);
        vec2 q = uv + w;
        vec4 c;
        if (u_rgb > 0.001) { float s = u_rgb * 0.04 * (0.3 + u_aud); vec4 a = sampleMedia(q + vec2(s, 0.0)), b = sampleMedia(q), d = sampleMedia(q - vec2(s, 0.0)); c = vec4(a.r, b.g, d.b, max(a.a, max(b.a, d.a))); }
        else c = sampleMedia(q);
        if (u_blur > 0.001 || u_sharp > 0.001) {
            vec4 b = vec4(0.0); float r = (max(u_blur, 0.02)) * 0.03;
            for (int i = 0; i < 8; i++) { float a = float(i) * 0.785398; b += sampleMedia(q + vec2(cos(a), sin(a)) * r * vec2(1.0 / asp, 1.0)); }
            b = (b + c) / 9.0;
            c = u_blur > 0.001 ? mix(c, b, clamp(u_blur * 3.0, 0.0, 1.0)) : c;
            if (u_sharp > 0.001) c.rgb += (c.rgb - b.rgb) * u_sharp * 2.0;
        }
        vec3 col = c.rgb;
        col = hueShift(col, u_hue + u_hueAnim);
        col = mix(vec3(lum(col)), col, u_sat);
        col = (col - 0.5) * u_contrast + 0.5;
        col = pow(max(col * u_bright, 0.0), vec3(1.0 / u_gamma));
        if (u_cmap > 0.001) col = mix(col, gradMap(lum(col)), u_cmap);
        outc = vec4(clamp(col, 0.0, 1.5) * c.a, c.a);
    } else {
        // ---- text: masks come from the CPU rasteriser (TextRaster): tex.r fill, g outline1, b outline2, a box; tex2.r shadow, g glow, b active word ----
        uv.y = 1.0 - uv.y;                                 // text bitmaps are stored top row first
        vec2 q = uv;
        float t = u_time;
        if (u_waveAmp > 0.0001) q.y += sin(uv.x * u_waveFreq + u_wavePhase) * u_waveAmp;
        if (u_shake > 0.0001) q += (vec2(noise(vec2(t * 30.0, 1.0)), noise(vec2(t * 30.0, 9.0))) - 0.5) * u_shake;
        if (u_dist > 0.001) { vec2 n = vec2(fbm(uv * 6.0 + t * 0.8), fbm(uv * 6.0 + 5.0 - t * 0.8)) - 0.5; q += n * u_dist * 0.12; }
        if (u_noise > 0.001) q += (vec2(noise(uv * 90.0 + t * 8.0), noise(uv * 90.0 + 3.0 - t * 8.0)) - 0.5) * u_noise * 0.02;
        if (u_glitch > 0.001) {
            float seed = floor(t * 18.0), row = floor(uv.y * 14.0);
            if (hash(vec2(row, seed)) > 1.0 - 0.5 * u_glitch) q.x += (hash(vec2(seed, row)) - 0.5) * 0.25 * u_glitch;
        }
        float blurR = (u_blur + u_aBlur) * 0.03, split = (u_rgb + u_aRgb) * 0.02;
        vec4 m = vec4(0.0), m2 = vec4(0.0);
        // base sample (with optional blur)
        if (blurR > 0.0005) {
            for (int i = 0; i < 9; i++) {
                float a = float(i) * 0.785398; vec2 o = i == 8 ? vec2(0.0) : vec2(cos(a), sin(a)) * blurR * vec2(0.5, 1.0);
                vec2 s = q + o; vec4 a1 = texture2D(u_tex, s), a2 = texture2D(u_tex2, s);
                m += a1; m2 += a2;
            }
            m /= 9.0; m2 /= 9.0;
        } else { m = texture2D(u_tex, q); m2 = texture2D(u_tex2, q); }
        vec3 rgbMask = vec3(m.r);
        if (split > 0.0005) rgbMask = vec3(texture2D(u_tex, q + vec2(split, 0.0)).r, m.r, texture2D(u_tex, q - vec2(split, 0.0)).r);
        // reveal (typewriter / wipe): soft edge sweeping left to right across the text bounds
        float rv = u_reveal >= 1.0 ? 1.0 : smoothstep(0.0, 0.06, u_reveal * 1.06 - uv.x);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { m = vec4(0.0); m2 = vec4(0.0); rgbMask = vec3(0.0); }
        // fill colour
        vec3 fc = u_fillC;
        float gt = dot(uv - 0.5, vec2(cos(u_gradAngle * PI / 180.0), sin(u_gradAngle * PI / 180.0))) + 0.5 + u_gradAnim * t * 0.2;
        if (u_gradOn > 0.5) fc = u_gradOn > 1.5 ? gradMap(gt) : mix(u_fillC, u_fill2C, 1.0 - abs(1.0 - mod(gt, 2.0)));
        if (u_useFillTex > 0.5) fc = texture2D(u_fill, vec2(uv.x, 1.0 - uv.y)).rgb;
        fc = mix(fc, u_hiC, clamp(m2.b, 0.0, 1.0));
        fc = hueShift(fc, u_cycle * t * 0.15 + u_hueAnim);
        vec4 acc = vec4(u_bgC * u_bgOp * m.a, u_bgOp * m.a);
        float sh = texture2D(u_tex2, q - u_shOff).r * u_shOp;
        acc = over(vec4(u_shC * sh, sh), acc);
        float gl = clamp(m2.g * u_glow * (1.0 + u_glowBoost), 0.0, 1.0);
        acc = over(vec4(u_glowC * gl, gl * 0.85), acc);
        acc = over(vec4(u_o2C * m.b, m.b), acc);
        acc = over(vec4(u_o1C * m.g, m.g), acc);
        acc = over(vec4(fc * rgbMask, max(rgbMask.r, max(rgbMask.g, rgbMask.b))), acc);
        outc = acc;
        float boxKeep = 1.0;
        outc *= mix(1.0, rv, 1.0);
    }
    gl_FragColor = outc * u_alpha;
}
