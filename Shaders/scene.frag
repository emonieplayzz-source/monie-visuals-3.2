#version 120
// monie visuals v3 - scene pass. 30 original procedural scenes. Each scene reads its own 8 knobs (u_k[0..7], native units,
// see Source/Core/SceneRegistry.h for labels/ranges). Audio inputs a_* are already sensitivity-scaled and smoothed on the CPU.
// Colours always come from the user's gradient (u_grad) via pal(); nothing here hard-codes a neon palette.
uniform int   u_scene;
uniform float u_time;
uniform vec2  u_res;
uniform float u_k[8];
uniform float a_bass, a_lmid, a_mid, a_hmid, a_treb, a_beat, a_rms, a_flux, a_trans;
uniform float u_beatAge, u_beatId;
uniform sampler2D u_prev, u_imgA, u_imgB, u_imgC, u_imgD, u_spec, u_grad, u_rd;
uniform float u_hasImg, u_blend, u_aspA, u_aspB, u_aspC, u_aspD, u_hasC, u_hasD;
uniform float u_palShift;
uniform float u_zoom, u_rot;   // global scene zoom / rotation
varying vec2 vUV;

#define PI 3.14159265
#define K(i) u_k[i]

float hash21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
vec2  hash22(vec2 p) { float n = hash21(p); return vec2(n, hash21(p + n + 17.0)); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x), mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbmN(vec2 p, float oct) {
    float s = 0.0, a = 0.5, n = 0.0;
    for (int i = 0; i < 6; i++) { if (float(i) >= oct) break; s += a * noise(p); n += a; p = p * 2.03 + vec2(7.1, 3.7); a *= 0.5; }
    return s / max(n, 0.001);
}
float fbm(vec2 p) { return fbmN(p, 5.0); }
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
vec3 hsv2rgb(vec3 c) { vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0); return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y); }
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 hueShift(vec3 c, float h) { float a = h * 6.2831853, ca = cos(a), sa = sin(a); vec3 k = vec3(0.57735); return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca); }
vec2 dirOf(float deg) { float a = deg * PI / 180.0; return vec2(cos(a), sin(a)); }

vec3 pal(float t) {
    t = t + u_palShift;
    t = 1.0 - abs(1.0 - mod(t, 2.0));                 // triangle wrap: shifting the palette never makes a seam
    return texture2D(u_grad, vec2(clamp(t, 0.002, 0.998), 0.5)).rgb;
}

// ---------- image access (cover-fit, mirrored edges) ----------
vec2 mirrorT(vec2 t) { return 1.0 - abs(1.0 - mod(t, 2.0)); }
vec3 demoImg(vec2 p) {                                   // shown when no image is loaded, so every scene works out of the box
    float r = length(p), a = atan(p.y, p.x);
    float v = 0.5 + 0.5 * sin(r * 9.0 - u_time * 0.4) * cos(a * 5.0 + u_time * 0.1);
    float f = fbm(p * 3.0 + 2.0);
    float rings = smoothstep(0.02, 0.0, abs(fract(r * 3.0) - 0.5) - 0.42);
    return pal(0.15 + 0.6 * v * f + 0.25 * f) * (0.55 + 0.5 * f) + pal(0.9) * rings * 0.35;
}
vec3 fetch(sampler2D s, float asp, vec2 p) {
    float a = u_res.x / u_res.y, k = max(a / asp, 1.0);
    vec2 t = p / (2.0 * vec2(asp * k, k)) + 0.5;
    t.y = 1.0 - t.y;
    return texture2D(s, mirrorT(t)).rgb;
}
vec3 img(vec2 p) {
    if (u_hasImg < 0.5) return demoImg(p);
    vec3 A = fetch(u_imgA, u_aspA, p);
    if (u_blend > 0.002) return mix(A, fetch(u_imgB, u_aspB, p), u_blend);
    return A;
}
float specAt(float x) { return texture2D(u_spec, vec2(clamp(x, 0.0, 1.0) * 0.98 + 0.01, 0.25)).r; }
float waveAt(float x) { return texture2D(u_spec, vec2(clamp(x, 0.0, 1.0) * 0.98 + 0.01, 0.75)).r * 2.0 - 1.0; }
float sdSeg(vec2 p, vec2 a, vec2 b) { vec2 pa = p - a, ba = b - a; return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0)); }
float sdPoly(vec2 p, float n, float r) {                 // distance to the outline of a regular polygon (n sides)
    float a = atan(p.y, p.x) + PI, seg = 2.0 * PI / n;
    float d = cos(floor(0.5 + a / seg) * seg - a) * length(p);
    return abs(d - r * cos(PI / n));
}
float polyFill(vec2 p, float n, float r) { float a = atan(p.y, p.x) + PI, seg = 2.0 * PI / n; return cos(floor(0.5 + a / seg) * seg - a) * length(p) - r * cos(PI / n); }

// ===================== A: IMAGE DISTORTION =====================
vec3 scene0(vec2 p) {
    float t = u_time * K(3), sc = K(2);
    float push = K(0) * (0.05 + 0.2 * a_bass * K(4));
    vec2 q = p;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        q += push * vec2(sin(q.y * sc * (1.0 + fi * 0.6) + t * (0.6 + 0.25 * fi) + a_bass * 3.0),
                         cos(q.x * sc * (1.0 + fi * 0.5) - t * (0.7 + 0.2 * fi) + a_lmid * 2.0));
    }
    float det = 1.0 + K(7) * 5.0;
    q += K(1) * K(0) * (0.04 + 0.16 * a_mid * K(5)) * (vec2(fbmN(q * det + t * 0.3, 4.0), fbmN(q * det + 7.0 - t * 0.3, 4.0)) - 0.5) * 4.0;
    float s = K(6) * 0.03 * (1.0 + a_treb * 2.0);
    return vec3(img(q + vec2(s, 0.0)).r, img(q).g, img(q - vec2(s, 0.0)).b);
}
vec3 scene1(vec2 p) {
    vec2 dir = dirOf(K(1)), per = vec2(-dir.y, dir.x);
    float d = dot(p, dir), e = dot(p, per), t = u_time * K(4);
    float m = K(0) * (0.35 + a_bass * K(6) * 1.2);
    float drip = fbm(vec2(e * K(2) * 0.6, t * 0.25)) * (0.5 + 0.5 * sin(e * K(2) * 1.4 + t));
    vec2 q = p - dir * m * (drip * 0.5 * (d + 1.3) + 0.08 * fbm(vec2(e * K(2) * 2.0, d * 2.0 - t * 0.5)));
    q -= dir * K(3) * m * 0.25 * max(0.0, 0.5 - d * 0.5);
    q += per * K(5) * 0.06 * sin(d * 8.0 + t * 2.0 + a_mid * 4.0);
    vec3 c = img(q);
    float w = smoothstep(0.55, 1.0, fbm(q * 4.0 + t * 0.1));
    return mix(c, mix(c, pal(0.3 + 0.5 * w) * (0.4 + lum(c)), 0.6), K(7) * w);
}
vec3 scene2(vec2 p) {
    vec2 c = vec2(K(2), K(3)); vec2 d = p - c; float r = length(d);
    float fall = exp(-pow(r / K(1), K(7)));
    float ang = K(0) * (1.0 + K(5) * a_bass * 0.5) * fall * cos(r / K(1) * PI * (K(4) - 1.0)) + u_time * K(6);
    return img(c + rot(ang * fall) * d);
}
vec3 scene3(vec2 p) {
    vec2 q = (p - vec2(K(2), K(3))) / K(4);
    float n = floor(K(0) + 0.5), seg = 2.0 * PI / n, r = length(q), a = atan(q.y, q.x) + u_time * K(1) + a_bass * K(5) * 0.4;
    a = mod(a, seg);
    float folded = abs(a - seg * 0.5);
    a = mix(a, folded, K(7));
    vec2 k = r * vec2(cos(a), sin(a));
    k += K(6) * 0.1 * vec2(sin(k.y * 6.0 + u_time + a_mid * 3.0), cos(k.x * 6.0 - u_time));
    return img(k);
}
vec3 scene4(vec2 p) {
    float asp = u_res.x / u_res.y;
    float zf = 1.0 - (K(1) + 0.02 * K(6) * a_bass), rf = K(2) + 0.01 * sin(u_time * 0.4) * K(7);
    vec2 fp = rot(rf) * p * zf + K(7) * 0.01 * vec2(sin(u_time * 0.7), cos(u_time * 0.5));
    vec3 prev = texture2D(u_prev, fp / vec2(asp, 1.0) * 0.5 + 0.5).rgb;
    prev = hueShift(prev, K(5)) * K(0) - K(3);
    vec3 inj = img(p) * K(4);
    return max(prev, vec3(0.0)) + inj * (0.12 + 0.4 * a_beat + 0.25 * a_bass * K(6));
}
vec3 scene5(vec2 p) {
    float S = K(7) * (1.0 + K(6) * a_beat + a_treb * 0.5);
    return vec3(img(p + vec2(K(0), K(1)) * S).r, img(p + vec2(K(2), K(3)) * S).g, img(p + vec2(K(4), K(5)) * S).b);
}

// ===================== B: ANALOG & DIGITAL CORRUPTION =====================
vec3 scene6(vec2 p) {
    float t = u_time, A = K(5);
    float band = smoothstep(0.0, 0.1, abs(fract(p.y * 0.4 - t * 0.15 * (0.5 + K(2))) - 0.5) - 0.38 + 0.05) ;
    float track = K(2) * A * (1.0 - band) * (0.02 + 0.05 * a_flux);
    vec2 q = p;
    q.x += track * sin(p.y * 30.0 + t * 8.0) + K(4) * A * 0.012 * sin(p.y * 4.0 + t * 1.5) + K(6) * A * 0.01 * (hash21(vec2(floor(t * 24.0), floor(p.y * 80.0))) - 0.5);
    float bl = K(1) * A * 0.012;
    vec3 c = vec3(img(q + vec2(bl, 0.0)).r, img(q).g, img(q - vec2(bl, 0.0)).b);
    c = mix(c, (img(q + vec2(bl * 3.0, 0.0)) + img(q - vec2(bl * 3.0, 0.0))) * 0.5, K(1) * 0.4);
    c *= 1.0 - K(0) * 0.4 * (0.5 + 0.5 * sin(p.y * u_res.y * 1.6));
    c += (hash21(p * u_res + floor(t * 30.0)) - 0.5) * K(3) * 0.35 * A;
    float dr = step(0.995 - 0.03 * K(7), hash21(vec2(floor(t * 12.0), floor(p.y * 60.0)))) * step(0.6, hash21(vec2(floor(p.x * 12.0), floor(t * 12.0))));
    c = mix(c, vec3(0.9), dr * 0.6 * K(7));
    return c * (1.0 - 0.15 * band) + a_bass * 0.06;
}
vec3 scene7(vec2 p) {
    float seed = floor(u_time * K(7)), g = clamp(a_beat + a_flux * 0.6 + 0.15, 0.0, 1.5) * (0.3 + K(0) * 1.4);
    vec2 q = p;
    float rows = 6.0 + 30.0 * hash21(vec2(seed, 1.0)), row = floor((p.y + 1.0) * rows);
    if (hash21(vec2(row, seed)) > 1.0 - 0.45 * g * K(2)) q.x += (hash21(vec2(seed, row)) - 0.5) * 0.8 * K(2);
    vec2 bsz = vec2(K(1)) * vec2(1.6, 1.0);
    vec2 bid = floor(p / bsz);
    float bh = hash21(bid + seed);
    if (bh > 1.0 - 0.18 * g * K(4)) q += (hash22(bid + seed * 1.7) - 0.5) * 0.3;
    float s = K(3) * 0.05 * (0.4 + g);
    vec3 c = vec3(img(q + vec2(s, 0.0)).r, img(q).g, img(q - vec2(s, 0.0)).b);
    if (hash21(bid + seed * 3.3) > 1.0 - 0.06 * g * K(4)) c = c.gbr;
    if (hash21(vec2(row, seed + 3.0)) > 1.0 - 0.05 * g * K(6)) c = 1.0 - c;
    c += K(5) * a_beat * 0.25;
    return c;
}
vec3 scene8(vec2 p) {
    vec2 dir = dirOf(K(1));
    float len = K(2) * 0.9 * (1.0 + K(4) * a_bass * 0.6), thr = K(0) - K(4) * 0.1 * a_mid;
    vec3 best = img(p); float bl = lum(best);
    if (K(7) > 0.5) bl = 1.0 - bl;
    vec3 acc = best; float found = 0.0;
    for (int i = 1; i < 24; i++) {
        float f = float(i) / 24.0;
        vec2 o = p - dir * f * len + (hash21(vec2(float(i), floor(p.y * 90.0))) - 0.5) * K(6) * 0.05;
        vec3 s = img(o); float sl = lum(s); if (K(7) > 0.5) sl = 1.0 - sl;
        if (sl > thr && sl > bl - 0.05 * found && found < 1.0) { acc = mix(s, acc, 0.0); found = 1.0; }
        if (found > 0.5) { acc = mix(acc, s, 0.18); }
    }
    vec3 outc = mix(best, acc, K(3) * clamp(step(bl, thr) + 0.2, 0.0, 1.0));
    float band = floor(lum(outc) * (3.0 + 12.0 * K(5))) / (3.0 + 12.0 * K(5));
    return mix(outc, outc * (0.5 + band), K(5));
}
vec3 scene9(vec2 p) {
    float asp = u_res.x / u_res.y;
    vec2 g = vUV * u_res / K(2); vec2 bid = floor(g);
    float h = hash21(bid + floor(u_time * 0.25));
    vec2 mv = dirOf(K(3) + (h - 0.5) * 140.0) * (0.004 + 0.012 * h) * K(1) * (1.0 + a_flux);
    if (hash21(bid * 1.3 + floor(u_time * 0.5)) < K(7)) mv *= 0.0;                          // "held" blocks keep their old pixels
    vec3 prev = texture2D(u_prev, vUV - mv).rgb;
    prev = hueShift(prev, K(5) * 0.004);
    prev += (hash21(vUV * u_res + u_time) - 0.5) * K(6) * 0.03;
    float inject = 0.05 + (a_beat > 0.6 ? K(4) : 0.0) * step(hash21(bid + u_beatId), 0.75);
    vec3 c = mix(prev * K(0) * 1.01, img(p), clamp(inject + (1.0 - K(0)) * 0.3, 0.0, 1.0));
    return c;
}
vec3 scene10(vec2 p) {
    float seed = floor(u_time * 14.0);
    vec2 q = p;
    float row = floor((p.y + 1.0) * 40.0);
    if (hash21(vec2(row, seed)) > 1.0 - 0.25 * K(5)) q.x += (hash21(vec2(seed, row)) - 0.5) * 0.4 * K(5) * (0.4 + a_flux);
    float l = lum(img(q));
    l = (l - 0.5) * K(4) + 0.5;
    l += (hash21(p * u_res + seed) - 0.5) * K(1) * 0.8;
    float dots = length(fract(p * u_res.y * 0.08) - 0.5);
    l += (0.25 - dots) * K(7);
    l += K(6) * (hash21(vec2(floor(p.x * 60.0), seed)) - 0.5) * 0.4;
    float v = step(K(0) - a_bass * 0.15, l);
    v = mix(v, 1.0 - v, K(2));
    float flash = step(0.7, a_beat) * K(3) * 0.5;
    v = clamp(v + flash * (1.0 - v) * 0.5, 0.0, 1.0);
    return mix(vec3(v), mix(pal(0.05), pal(0.95), v), 0.0) * vec3(1.0);
}
vec3 scene11(vec2 p) {
    float asp = u_res.x / u_res.y;
    float res = K(0) * (1.0 - K(5) * 0.25 * a_bass * (a_beat > 0.5 ? 1.0 : 0.0));
    vec2 cells = vec2(res * asp, res);
    vec2 sp = p + K(4) * 0.05 * vec2(sin(u_time * 0.9), cos(u_time * 0.7));
    vec2 uvq = (floor((sp * 0.5 * vec2(1.0 / asp, 1.0) + 0.5) * cells) + 0.5) / cells;
    vec2 pq = (uvq - 0.5) * 2.0 * vec2(asp, 1.0);
    vec3 c = img(pq);
    float d = (mod(floor(sp.x * 0.5 * cells.x), 4.0) * 0.25 + mod(floor(sp.y * 0.5 * cells.y), 4.0)) * 0.0625 - 0.5; // 4x4 pattern dither
    float lv = K(1) - 1.0;
    vec3 post = floor(c * lv + 0.5 + d * K(3)) / lv;
    c = mix(c, post, K(2));
    c = mix(c, pal(lum(c)), K(7));
    c *= 1.0 - K(6) * 0.4 * step(0.5, fract(p.y * res * 0.5));
    return c;
}

// ===================== C: GEOMETRY & SPATIAL =====================
vec3 scene12(vec2 p) {
    float r = length(p) + 1e-3, a = atan(p.y, p.x) + u_time * 0.1 * K(5) + r * K(5) * a_mid;
    float z = 0.5 / r + u_time * K(0) * (0.5 + a_bass * K(4));
    float sides = floor(K(2) + 0.5);
    float u = a / PI * sides * 0.5;
    vec2 g = vec2(u, z * K(1) * 0.15);
    vec2 f = abs(fract(g + 0.5) - 0.5);
    float lw = K(3) * 6.0 / (1.0 + z * 0.0), line = min(f.x * 0.5, f.y);
    float fade = smoothstep(0.0, 0.5 * K(6), r) ;
    float L = smoothstep(lw, 0.0, line) * fade;
    float glow = smoothstep(lw * 6.0, 0.0, line) * fade * 0.25 * K(7);
    return pal(0.35 + 0.4 * fract(z * 0.05)) * (L * 1.4 + glow) * (0.6 + a_bass * 0.8) + pal(0.9) * a_beat * 0.15 * exp(-r * 2.0);
}
vec3 mandel(vec2 c, float zoomPhase) {
    float it = 0.0; vec2 z = vec2(0.0); float maxIt = 20.0 + K(2) * 14.0;
    float pw = K(4);
    for (int i = 0; i < 160; i++) {
        if (float(i) >= maxIt) break;
        float r = length(z), th = atan(z.y, z.x);
        z = pow(r, pw) * vec2(cos(th * pw), sin(th * pw)) + c;
        if (dot(z, z) > 256.0) break;
        it += 1.0;
    }
    float sm = it < maxIt ? it - log2(max(log2(dot(z, z)), 1e-4)) + 4.0 : 0.0;
    float t = sm / maxIt;
    vec3 col = it >= maxIt ? pal(0.0) * 0.1 : pal(fract(t * K(3) + u_time * 0.02));
    return col * (it >= maxIt ? 1.0 : 1.0) + pal(0.9) * exp(-t * 6.0) * 0.0;
}
vec3 scene13(vec2 p) {
    float pw = K(4);
    vec2 q = rot(u_time * K(1) * 0.3) * p;
    vec2 target = vec2(-0.743643887037151, 0.131825904205330) + K(5) * 0.02 * vec2(sin(u_time * 0.3), cos(u_time * 0.27));
    float spd = K(0) * (1.0 + a_bass * 0.0) * 0.08;
    float ph = fract(u_time * spd);
    float ph2 = fract(ph + 0.5);
    vec3 c;
    if (abs(pw - 2.0) < 0.1) {
        float s1 = 2.5 * exp(-ph * 9.0), s2 = 2.5 * exp(-ph2 * 9.0);
        float w = abs(ph - 0.5) * 2.0;     // crossfade two staggered zoom cycles so the loop never pops
        c = mix(mandel(target + q * s1, ph), mandel(target + q * s2, ph2), 1.0 - w);
        c = mix(mandel(target + q * s2, ph2), mandel(target + q * s1, ph), w);
    } else {
        float s = 1.6 * (0.6 + 0.4 * sin(u_time * K(0) * 0.5));
        c = mandel(q * s, 0.0);
    }
    return c * (0.8 + 0.5 * a_rms * K(6)) * (1.0 + K(7) * 0.15 * a_beat);
}
vec3 scene14(vec2 p) {
    float n = floor(K(0) + 0.5), seg = 2.0 * PI / n;
    vec2 q = rot(u_time * K(1)) * p / K(4);
    float a = atan(q.y, q.x), r = length(q);
    a = abs(mod(a, seg) - seg * 0.5);
    q = r * vec2(cos(a), sin(a));
    vec3 c = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        if (float(i) >= K(6)) break;
        float fi = float(i);
        vec2 w = q * (1.0 + fi * 0.7);
        for (int j = 0; j < 8; j++) { if (float(j) >= K(2)) break; w = abs(w) - vec2(0.35, 0.18) * (1.0 + 0.3 * a_bass * K(5)); w = rot(0.5 + 0.15 * float(j) + u_time * 0.1) * w; }
        float d = abs(length(w) - 0.2 - 0.05 * sin(u_time + fi));
        float d2 = abs(w.x) ;
        float L = min(d, d2);
        c += pal(0.3 + 0.2 * fi + length(w) * 0.5) * (smoothstep(K(3), 0.0, L) * 1.2 + smoothstep(K(3) * 6.0, 0.0, L) * 0.15);
        c += pal(0.6 + 0.1 * fi) * smoothstep(0.0, 0.5, 0.1 - length(w)) * K(7) * 0.4;
    }
    return c * (0.7 + a_rms * 0.8);
}
float terrainH(vec2 xz) {
    float h = fbmN(xz * vec2(0.6, 0.5), 4.0) * 1.4;
    h += specAt(0.5 + xz.x * 0.08) * K(5) * 0.8;
    return h * K(1) * (0.6 + 0.4 * a_bass * K(5)) * smoothstep(0.4, 2.5, abs(xz.x));
}
vec3 scene15(vec2 p) {
    float hz = K(7) - 0.2;
    p = rot(K(6) * 0.3 * sin(u_time * 0.4)) * p;
    float py = p.y - hz;
    vec3 c = mix(pal(0.05), pal(0.25), smoothstep(-0.2, 1.0, p.y)) * 0.6;
    vec2 sc = vec2(0.0, hz + 0.45);
    float sd = length((p - sc) * vec2(1.0, 1.0));
    float sun = smoothstep(0.35 * K(4) + 0.01, 0.35 * K(4), sd) * step(0.0, py);
    float stripes = step(0.5, fract((p.y - sc.y) * 14.0 + u_time * 0.5)) * smoothstep(0.1, -0.3, p.y - sc.y);
    c += pal(0.95) * sun * (1.0 - stripes * 0.9) * K(4) * 1.2 + pal(0.8) * exp(-sd * 3.0) * 0.4 * K(4);
    if (py < 0.0) {
        vec3 ro = vec3(0.0, 1.2, 0.0), rd = normalize(vec3(p.x, py - 0.0, 0.9));
        float t = 0.0, hit = 0.0, lh = 0.0;
        for (int i = 0; i < 64; i++) {
            vec3 pos = ro + rd * t;
            pos.z += u_time * K(2);
            float h = terrainH(pos.xz);
            if (pos.y - ro.y * 0.0 < h - 1.0 + 1.0 && false) break;
            float dh = ro.y + rd.y * t - h;
            if (dh < 0.01 * t) { hit = 1.0; lh = h; break; }
            t += max(0.05, dh * 0.5);
            if (t > 40.0) break;
        }
        if (hit > 0.5) {
            vec3 pos = ro + rd * t; pos.z += u_time * K(2);
            vec2 f = abs(fract(pos.xz * K(0) * 0.1 + 0.5) - 0.5);
            float w = K(3) * 3.0 * (1.0 + t * 0.12), line = smoothstep(w, 0.0, min(f.x, f.y) * 1.0);
            float fog = exp(-t * 0.07);
            c = mix(c, vec3(0.0), 0.0) ;
            c = mix(pal(0.02) * 0.4, c, 0.0) + pal(0.4 + lh * 0.25) * line * fog * (1.0 + a_treb) + pal(0.1) * 0.2 * fog;
        }
    }
    return c;
}
vec3 scene16(vec2 p) {
    vec3 c = vec3(0.0); vec2 q = p; float sc = 1.0;
    float sides = floor(K(3) + 0.5);
    for (int i = 0; i < 8; i++) {
        if (float(i) >= K(0)) break;
        float fi = float(i);
        q = rot(u_time * K(1) * 0.3 * (mod(fi, 2.0) < 0.5 ? 1.0 : -1.0) + fi * 0.2) * q;
        float r = 0.55 * (1.0 + a_bass * K(5) * 0.25 * (1.0 - fi * 0.1));
        float d = sdPoly(q, sides, r), f = polyFill(q, sides, r);
        c += pal(0.25 + 0.09 * fi) * (smoothstep(K(4) * 2.5, 0.0, d) * 1.4 + smoothstep(K(4) * 12.0, 0.0, d) * 0.18);
        c += pal(0.5 + 0.06 * fi) * smoothstep(0.01, -0.01, f) * K(7) * 0.12;
        q = (q - vec2(K(6) * 0.25 * sin(u_time * 0.3 + fi), 0.0)) / K(2); sc *= 1.0 / K(2) * 0.5;
    }
    return c;
}
float map3(vec3 p) {
    p.xz = rot(u_time * K(1)) * p.xz; p.xy = rot(u_time * K(1) * 0.6) * p.xy;
    float sz = K(5) * (1.0 + a_bass * 0.2 * K(6));
    float s = length(p) - sz;
    float tor = length(vec2(length(p.xz) - sz * 1.1, p.y)) - sz * 0.38;
    vec3 b = abs(p) - vec3(sz * 0.75); float box = length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0) - 0.05;
    float m = K(0) * 2.0;
    float d = m < 1.0 ? mix(s, tor, m) : mix(tor, box, m - 1.0);
    d += sin(p.x * 6.0 + u_time) * sin(p.y * 6.0) * sin(p.z * 5.0 - u_time) * K(2) * (0.05 + 0.1 * a_mid * K(6));
    return d;
}
vec3 scene17(vec2 p) {
    vec3 ro = vec3(0.0, 0.0, 3.2), rd = normalize(vec3(p, -1.8)); float t = 0.0;
    for (int i = 0; i < 56; i++) { float d = map3(ro + rd * t); if (d < 0.002 || t > 8.0) break; t += d * 0.9; }
    vec3 c = pal(0.05) * (1.0 - length(p) * 0.4) * 0.5;
    if (t < 8.0) {
        vec3 pos = ro + rd * t; vec2 e = vec2(0.004, 0.0);
        vec3 n = normalize(vec3(map3(pos + e.xyy) - map3(pos - e.xyy), map3(pos + e.yxy) - map3(pos - e.yxy), map3(pos + e.yyx) - map3(pos - e.yyx)));
        vec2 la = dirOf(K(3)); vec3 l = normalize(vec3(la.x, la.y, 0.8));
        float dif = max(dot(n, l), 0.0), fr = pow(1.0 - max(dot(n, -rd), 0.0), 3.0), sp = pow(max(dot(reflect(-l, n), -rd), 0.0), 8.0 + 60.0 * K(4));
        c = pal(0.25 + 0.5 * dif) * (0.15 + dif) + pal(0.95) * fr * K(7) * (0.5 + a_treb) + sp * K(4) * 0.9;
    }
    return c;
}

// ===================== D: ORGANIC & ATMOSPHERIC =====================
vec3 scene18(vec2 p) {
    float t = u_time * K(1);
    vec2 q = p * K(2);
    q += K(5) * 0.6 * vec2(fbm(q * 0.5 + t * 0.2), fbm(q * 0.5 + 9.0 - t * 0.2));
    float v = sin(q.x + t) + sin(q.y * 1.3 + t * 1.2) + sin((q.x + q.y) * 0.8 + t * 0.7) + sin(length(q) * 2.0 - t * 1.5 + a_bass * 6.0 * K(4));
    v += K(0) * 2.0 * sin(q.x * q.y * 0.5 + a_mid * 2.0);
    float x = v * 0.125 + 0.5;
    x = (x - 0.5) * K(6) + 0.5;
    x = mix(x, floor(x * 8.0) / 8.0, K(7));
    return pal(x * K(3) + 0.1);
}
vec3 particleLayer(vec2 p, float scale, float seed, float dens, float size, float twinkle) {
    vec2 g = p * scale + seed * 13.0; vec2 id = floor(g), f = fract(g) - 0.5;
    float h = hash21(id + seed);
    vec2 off = (hash22(id + seed * 3.0) - 0.5) * 0.7;
    float tw = 0.6 + 0.4 * sin(u_time * (2.0 + 6.0 * h) + h * 40.0);
    float s = smoothstep(size * (0.04 + 0.08 * h), 0.0, length(f - off)) * step(h, dens);
    return pal(0.6 + h * 0.4) * s * (0.5 + 1.2 * mix(1.0, tw, twinkle) + a_treb * twinkle);
}
vec3 scene19(vec2 p) {
    float t = u_time * 0.1 * K(3);
    vec2 q = rot(K(7) * t * 2.0) * p;
    float n = pow(clamp(fbm(q * 2.4 + t) * fbm(q * 3.6 - t * 1.3 + 4.0) * 2.0, 0.0, 1.0), 1.4);
    vec3 c = pal(n * 0.9) * (0.35 + n) * K(4) * (0.6 + a_bass * 0.6);
    for (int i = 0; i < 5; i++) {
        if (float(i) >= K(6)) break;
        float fi = float(i);
        vec2 pp = p * (1.0 + fi * 0.35) + vec2(t * (1.0 + fi) * 1.5, 0.0);
        c += particleLayer(pp, 7.0 + fi * 5.0, fi, K(0) * 0.55, K(1), K(5)) * K(2) * (0.6 + 0.2 * fi);
    }
    return c;
}
vec3 scene20(vec2 p) {
    float t = u_time * K(3); vec2 dir = dirOf(K(1));
    vec2 q = p * K(0) + dir * t * 0.5;
    for (int i = 0; i < 4; i++) {
        q += K(2) * 0.5 * (vec2(fbmN(q + vec2(t * 0.3, 1.7 * float(i)) + a_bass * K(4), K(6)), fbmN(q + vec2(5.2, 1.3 * float(i)) - t * 0.3, K(6))) - 0.45);
        q = rot(0.3) * q;
    }
    float f = fbmN(q * 1.2 + t * 0.2, K(6));
    float rd = 1.0 - abs(2.0 * f - 1.0);
    f = mix(f, rd * rd, K(7));
    f = clamp((f - 0.5) * K(5) + 0.5 + a_mid * 0.1 * K(4), 0.0, 1.0);
    return pal(f) * (0.4 + 0.8 * f) + pal(0.95) * pow(max(0.0, 1.0 - abs(f - 0.5) * 6.0), 3.0) * (0.2 + a_mid * 0.4);
}
vec3 scene21(vec2 p) {                                   // displays the Gray-Scott simulation state (see rd.frag)
    float asp = u_res.x / u_res.y;
    vec2 uv = p / vec2(asp, 1.0) * 0.5 + 0.5;
    vec4 s = texture2D(u_rd, uv);
    float B = s.b + s.a / 255.0;
    vec2 px = vec2(1.0 / 256.0);
    float Bx = texture2D(u_rd, uv + vec2(px.x, 0.0)).b - texture2D(u_rd, uv - vec2(px.x, 0.0)).b;
    float By = texture2D(u_rd, uv + vec2(0.0, px.y)).b - texture2D(u_rd, uv - vec2(0.0, px.y)).b;
    float edge = length(vec2(Bx, By)) * 8.0;
    float v = clamp((B * 3.0 - 0.15 - 0.2) * K(4) * 0.8 + 0.2, 0.0, 1.0);
    return pal(v) * (0.35 + 0.9 * v) + pal(0.9) * edge * K(7) + pal(0.7) * a_beat * 0.05;
}
float bolt(vec2 p, float seed, float jit, float width, float x0) {
    float d = 10.0; float x = x0;
    for (int i = 0; i < 12; i++) {
        float y0 = 1.0 - float(i) * (2.0 / 12.0), y1 = y0 - 2.0 / 12.0;
        float nx = x + (hash21(vec2(float(i), seed)) - 0.5) * 0.35 * jit;
        d = min(d, sdSeg(p, vec2(x, y0), vec2(nx, y1)));
        x = nx;
    }
    return smoothstep(width, 0.0, d) + smoothstep(width * 8.0, 0.0, d) * 0.2;
}
vec3 scene22(vec2 p) {
    float st = floor(u_time * K(4) * 6.0), strike = a_beat * K(7);
    vec3 c = pal(0.04) * (0.5 + fbm(p * 2.0 + u_time * 0.1) * K(5) * 0.8);
    float lit = 0.0;
    for (int i = 0; i < 8; i++) {
        if (float(i) >= K(0)) break;
        float fi = float(i), h = hash21(vec2(fi, st));
        float on = step(0.3 + 0.4 * (1.0 - clamp(strike + a_flux, 0.0, 1.0)), h);
        lit += bolt(p, st + fi * 7.0, K(6), K(3), (hash21(vec2(fi, 3.0)) - 0.5) * 1.6) * on;
    }
    c += pal(0.85) * lit * K(1) + pal(0.6) * min(lit, 1.0) * 0.4;
    c += pal(0.5) * strike * K(2) * 0.15 * (1.0 - length(p) * 0.4);
    return c;
}
vec3 scene23(vec2 p) {
    float asp = u_res.x / u_res.y;
    vec3 c = vec3(0.0);
    for (int b = 0; b < 2; b++) {
        float id = u_beatId - float(b);
        float age = u_beatAge + (b == 1 ? 1.2 : 0.0);
        vec2 o = (hash22(vec2(id, 5.0)) - 0.5) * vec2(asp, 1.0) * 1.2;
        for (int i = 0; i < 48; i++) {
            float fi = float(i), h = hash21(vec2(fi, id)), a = (hash21(vec2(fi * 1.7, id + 3.0)) * 2.0 - 1.0) * PI * K(7) + (1.0 - K(7)) * 0.5 * PI;
            float v = (0.3 + 0.9 * hash21(vec2(id, fi))) * K(1) * K(3);
            vec2 pos = o + vec2(cos(a), sin(a)) * v * age * (1.0 - age * 0.15) - vec2(0.0, 1.0) * K(2) * age * age * 0.5;
            float life = clamp(1.0 - age / (1.5 + 2.0 * K(5)), 0.0, 1.0);
            if (float(i) / 48.0 > K(0) + 0.2) continue;
            c += pal(0.4 + 0.5 * h) * smoothstep(0.012 * K(4) * (0.5 + life), 0.0, length(p - pos)) * life * 1.6;
        }
    }
    float asp2 = asp;
    vec3 prev = texture2D(u_prev, vUV).rgb;
    return max(c, prev * (0.55 + 0.4 * K(5)));
}

// ===================== E: MUSIC-REACTIVE =====================
vec3 scene24(vec2 p) {
    float ang = atan(p.y, p.x) / PI;                       // -1..1
    float x = abs(fract(ang * 0.5 + 0.5 + u_time * K(6) * 0.05 + 0.5) * 2.0 - 1.0); // mirrored spectrum around the ring
    float seg = K(0), cell = floor(x * seg) / seg;
    float w = cell < 0.15 ? K(3) : (cell < 0.5 ? K(4) : K(5));
    float v = specAt(cell) * w;
    float r = length(p), base = K(1) + a_bass * 0.04, bar = base + v * K(2) * 3.0;
    float cf = fract(x * seg), gap = smoothstep(0.0, 0.12, cf) * smoothstep(1.0, 0.88, cf);
    float inside = smoothstep(0.004, 0.0, r - bar) * smoothstep(base - 0.004, base, r) * gap;
    float ring = smoothstep(0.008 + a_beat * 0.015, 0.0, abs(r - base));
    return pal(0.25 + v * 0.7) * inside * 1.3 + pal(0.9) * ring * (0.5 + a_beat) + pal(0.2) * exp(-(r - base) * 6.0) * step(base, r) * K(7) * 0.25 * (0.3 + a_bass);
}
vec3 scene25(vec2 p) {
    float asp = u_res.x / u_res.y;
    float x = clamp(p.x / asp * 0.5 / K(6) + 0.5, 0.0, 1.0);
    float y;
    float lw = K(0);
    float d;
    if (K(7) > 0.5) {                                      // XY (Lissajous) mode: wave vs. delayed wave
        float best = 10.0;
        for (int i = 0; i < 48; i++) { float f = float(i) / 48.0; vec2 pt = vec2(waveAt(f), waveAt(fract(f + 0.12))) * 0.5 * K(2); best = min(best, length(p - pt)); }
        d = best;
    } else {
        y = waveAt(x) * 0.35 * K(2) * (0.6 + 0.6 * a_rms);
        d = abs(p.y - y);
        if (K(4) > 0.5) d = min(d, abs(p.y + y));
    }
    vec3 c = pal(0.8) * smoothstep(lw * 3.0, 0.0, d) * 0.4 * K(3) + pal(1.0) * smoothstep(lw, 0.0, d) * 1.5;
    vec2 gg = abs(fract(p * 4.0 + 0.5) - 0.5);
    c += pal(0.3) * smoothstep(0.012, 0.0, min(gg.x, gg.y)) * K(5) * 0.35;
    vec3 prev = texture2D(u_prev, vUV).rgb;
    return max(c, prev * K(1));
}
vec3 scene26(vec2 p) {
    vec2 q = rot(K(6) * u_time * 0.2) * p;
    float persp = K(2);
    float zf = 1.0 + persp * (q.y * 0.9 + 0.2);
    q.x /= max(zf, 0.2);
    vec2 g = q * K(0) * 0.25 + 0.5;
    vec2 id = floor(g), f = fract(g);
    float cx = (id.x + 0.5) / (K(0) * 0.5);
    float band = specAt(clamp(abs(id.x) / (K(0) * 0.5), 0.0, 1.0)) * mix(K(4), K(5), clamp(abs(id.x) * 2.0 / K(0), 0.0, 1.0));
    float h = band * K(1) * 0.6;
    vec2 e = abs(f - 0.5);
    float line = smoothstep(K(3) * K(0) * 0.5, 0.0, 0.5 - max(e.x, e.y));
    float cell = step(f.y, h * 1.2 + 0.0);
    float fill = smoothstep(0.02, 0.0, max(e.x, e.y) - 0.42) * smoothstep(0.0, 0.02, h - abs(id.y) * 0.08) * K(7);
    return pal(0.3 + band * 0.6) * (line * (0.7 + band * 1.5) + fill * 0.8 * (0.4 + band)) + pal(0.9) * a_beat * 0.06;
}
vec3 scene27(vec2 p) {
    float asp = u_res.x / u_res.y;
    vec3 c = vec3(0.0);
    for (int l = 0; l < 3; l++) {
        float fl = float(l), depth = 1.0 - K(6) * 0.3 * fl;
        vec2 g = vec2(p.x * (8.0 + fl * 5.0) / depth, p.y * (6.0 + fl * 4.0) / depth - u_time * K(1) * (0.2 + 0.15 * fl));
        vec2 id = floor(g), f = fract(g) - 0.5;
        float h = hash21(id + fl * 10.0);
        float xn = clamp(p.x / asp * 0.5 + 0.5, 0.0, 1.0);
        float band = specAt(mix(xn, h, K(4)));
        vec2 off = (hash22(id + fl) - 0.5) * 0.6;
        off.y += band * 0.35;
        float size = (0.06 + 0.12 * h) * K(2) * (0.6 + band * 1.5 + a_beat * K(3) * 0.4);
        float s = smoothstep(size, 0.0, length(f - off)) * step(h, K(0) * 0.8);
        c += pal(0.25 + band * 0.7) * s * (0.9 + 2.0 * band) * K(7);
    }
    vec3 prev = texture2D(u_prev, vUV + vec2(0.0, -0.002 * K(1))).rgb;
    return max(c, prev * K(5) * 0.9);
}

// ===================== F: CREATIVE OVERLAYS =====================
vec3 scene28(vec2 p) {                                    // background for the Typography Reactor; text layers react via u_k in the layer pass
    float rows = floor(K(4) + 0.5);
    float row = floor((p.y * 0.5 + 0.5) * rows);
    float rh = hash21(vec2(row, 3.0));
    float x = p.x * 0.5 + 0.5 * (u_res.x / u_res.y) + u_time * (0.05 + 0.05 * rh) * (mod(row, 2.0) < 0.5 ? 1.0 : -1.0);
    float bars = smoothstep(0.02, 0.0, abs(fract(x * (4.0 + rh * 6.0)) - 0.5) - 0.3 + 0.2 * specAt(rh));
    float band = smoothstep(0.0, 0.03, abs(fract((p.y * 0.5 + 0.5) * rows) - 0.5) - 0.38) ;
    vec3 c = pal(0.1 + 0.15 * rh + u_time * 0.02 * K(2)) * 0.3 * (0.5 + specAt(rh) * 1.5) * (1.0 - band) * K(3);
    c += pal(0.5 + 0.2 * rh) * bars * (1.0 - band) * K(3) * 0.4 * (0.4 + a_bass);
    float r = length(p);
    c += pal(0.8) * smoothstep(0.01, 0.0, abs(r - 0.5 - a_bass * 0.2 * K(0))) * K(3) * 0.5;
    return c + pal(0.1) * 0.15;
}
vec3 collagePanel(sampler2D s, float asp, float has, vec2 p, vec2 o, float rotA, float scl, float idx) {
    vec2 q = rot(rotA) * (p - o) / scl;
    if (abs(q.x) > 1.0 || abs(q.y) > 1.0) return vec3(-1.0);
    if (has < 0.5) return demoImg(q * 1.5 + idx);
    return fetch(s, asp, q);
}
vec3 scene29(vec2 p) {
    float asp = u_res.x / u_res.y;
    vec3 c = pal(0.05) * 0.6;
    float n = floor(K(0) + 0.5);
    for (int i = 0; i < 4; i++) {
        if (float(i) >= n) break;
        float fi = float(i), h = hash21(vec2(fi, 7.0));
        float t = u_time * K(7);
        vec2 o = vec2(sin(t * (0.3 + 0.2 * h) + fi * 2.0) * asp * 0.5 * K(6), cos(t * (0.25 + 0.15 * h) + fi) * 0.4 * K(6)) + (fi - 1.5) * vec2(0.25, 0.1);
        float scl = (0.45 + 0.25 * K(2) * (h - 0.5) * 2.0) * (1.0 + K(4) * a_bass * 0.12 * (mod(fi, 2.0) + 0.5));
        float ra = (h - 0.5) * 2.0 * K(1) + sin(t * 0.3 + fi) * 0.1 * K(1);
        vec3 s = i == 0 ? collagePanel(u_imgA, u_aspA, u_hasImg, p, o, ra, scl, fi)
               : i == 1 ? collagePanel(u_imgB, u_aspB, u_hasImg, p, o, ra, scl, fi)
               : i == 2 ? collagePanel(u_imgC, u_aspC, u_hasC, p, o, ra, scl, fi)
                        : collagePanel(u_imgD, u_aspD, u_hasD, p, o, ra, scl, fi);
        if (s.r < -0.5) continue;
        s = mix(s, s * pal(0.2 + 0.2 * fi) * 1.8, K(5));
        float m = K(3);
        if (m < 0.5) c = s;
        else if (m < 1.5) c = 1.0 - (1.0 - c) * (1.0 - s);
        else if (m < 2.5) c = abs(c - s);
        else c = c + s * 0.7;
    }
    return c;
}

void main() {
    float asp = u_res.x / u_res.y;
    vec2 p = (vUV * 2.0 - 1.0) * vec2(asp, 1.0);
    { float ca = cos(u_rot), sa = sin(u_rot); p = vec2(ca * p.x - sa * p.y, sa * p.x + ca * p.y) / max(u_zoom, 0.05); }
    vec3 c;
    if      (u_scene == 0)  c = scene0(p);   else if (u_scene == 1)  c = scene1(p);   else if (u_scene == 2)  c = scene2(p);
    else if (u_scene == 3)  c = scene3(p);   else if (u_scene == 4)  c = scene4(p);   else if (u_scene == 5)  c = scene5(p);
    else if (u_scene == 6)  c = scene6(p);   else if (u_scene == 7)  c = scene7(p);   else if (u_scene == 8)  c = scene8(p);
    else if (u_scene == 9)  c = scene9(p);   else if (u_scene == 10) c = scene10(p);  else if (u_scene == 11) c = scene11(p);
    else if (u_scene == 12) c = scene12(p);  else if (u_scene == 13) c = scene13(p);  else if (u_scene == 14) c = scene14(p);
    else if (u_scene == 15) c = scene15(p);  else if (u_scene == 16) c = scene16(p);  else if (u_scene == 17) c = scene17(p);
    else if (u_scene == 18) c = scene18(p);  else if (u_scene == 19) c = scene19(p);  else if (u_scene == 20) c = scene20(p);
    else if (u_scene == 21) c = scene21(p);  else if (u_scene == 22) c = scene22(p);  else if (u_scene == 23) c = scene23(p);
    else if (u_scene == 24) c = scene24(p);  else if (u_scene == 25) c = scene25(p);  else if (u_scene == 26) c = scene26(p);
    else if (u_scene == 27) c = scene27(p);  else if (u_scene == 28) c = scene28(p);  else                    c = scene29(p);
    gl_FragColor = vec4(max(c, vec3(0.0)), 1.0);
}
