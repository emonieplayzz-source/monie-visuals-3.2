#version 120
// monie visuals v3 - effect pass. One uber-shader for the 30 effects in Source/Core/EffectDefs.h (branch on u_type).
// Every effect samples u_src (the stack so far) and the result is mixed back by u_amt (already audio-modulated on the CPU).
uniform sampler2D u_src, u_hist, u_grad;
uniform int   u_type;
uniform vec2  u_res;
uniform float u_time, u_amt;
uniform vec4  u_p;               // the effect's 4 parameters, native units
uniform vec3  u_col;
uniform float a_bass, a_beat, a_treb, a_rms, a_flux;
varying vec2 vUV;
#define PI 3.14159265

float hash(vec2 p) { p = fract(p * vec2(443.897, 441.423)); p += dot(p, p + 19.19); return fract(p.x * p.y); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y); }
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { s += a * noise(p); p = p * 2.03 + 3.7; a *= 0.5; } return s; }
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 hueShift(vec3 c, float h) { float a = h * 6.2831853, ca = cos(a), sa = sin(a); vec3 k = vec3(0.57735); return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca); }
vec2 dirOf(float deg) { float a = deg * PI / 180.0; return vec2(cos(a), sin(a)); }
float accA = 0.0, cntA = 0.0;
vec3 S(vec2 uv) { vec4 t = texture2D(u_src, uv); accA += t.a; cntA += 1.0; return t.rgb; }
vec3 gradMap(float t) { return texture2D(u_grad, vec2(clamp(t, 0.002, 0.998), 0.5)).rgb; }

vec3 blurDisc(vec2 uv, float rad, float aspect) {
    vec3 s = vec3(0.0); float w = 0.0;
    for (int i = 0; i < 24; i++) {
        float fi = float(i), r = sqrt((fi + 0.5) / 24.0), a = fi * 2.399963;
        vec2 o = vec2(cos(a), sin(a)) * r * rad / u_res.y; o.x /= aspect;
        s += S(uv + o); w += 1.0;
    }
    return s / w;
}

void main() {
    float asp = u_res.x / u_res.y;
    vec2 uv = vUV, c = uv - 0.5, pc = (uv * 2.0 - 1.0) * vec2(asp, 1.0);
    vec3 src = S(uv), o = src;
    float srcA = texture2D(u_src, uv).a;
    accA = 0.0; cntA = 0.0;
    float amt = u_amt;
    float t = u_time;

    if (u_type == 0) {                                  // liquid
        vec2 q = pc * u_p.x; float d = (0.02 + 0.05 * a_bass) * amt;
        for (int i = 0; i < 3; i++) { float fi = float(i); q += u_p.y * 0.5 * vec2(sin(q.y * 1.7 + t * u_p.z * (0.6 + 0.2 * fi)), cos(q.x * 1.9 - t * u_p.z * (0.7 + 0.2 * fi))); }
        vec2 off = (q - pc * u_p.x) * d / max(u_p.x, 0.1);
        o = S(uv + off * vec2(1.0 / asp, 1.0) * 3.0);
    } else if (u_type == 1) {                           // wave
        vec2 dir = dirOf(u_p.y), per = vec2(-dir.y, dir.x);
        float w = sin(dot(pc, dir) * u_p.x + t * u_p.z) * 0.02 * amt * (1.0 + a_bass);
        o = S(uv + per * w * vec2(1.0 / asp, 1.0));
    } else if (u_type == 2) {                           // swirl
        vec2 ctr = u_p.yz; vec2 d = pc - ctr; float r = length(d);
        float f = smoothstep(u_p.x, 0.0, r);
        float a = u_p.w * f * f * amt * (1.0 + a_bass * 0.5);
        d = rot(a) * d;
        o = S(((ctr + d) / vec2(asp, 1.0)) * 0.5 + 0.5);
    } else if (u_type == 3) {                           // noise displacement
        vec2 q = pc * u_p.x + t * u_p.y; vec2 n = vec2(fbm(q), fbm(q + 11.3)) - 0.5;
        n = mix(vec2(noise(q) - 0.5, noise(q + 5.0) - 0.5), n, u_p.z);
        n += dirOf(u_p.w) * 0.15;
        o = S(uv + n * 0.08 * amt * (1.0 + a_bass * 0.8) * vec2(1.0 / asp, 1.0));
    } else if (u_type == 4) {                           // kaleidoscope
        vec2 q = pc - u_p.zw; float n = floor(u_p.x + 0.5), seg = 2.0 * PI / n;
        float a = atan(q.y, q.x) + u_p.y * PI / 180.0, r = length(q);
        a = abs(mod(a, seg) - seg * 0.5);
        vec2 k = r * vec2(cos(a), sin(a)) + u_p.zw;
        o = S((k / vec2(asp, 1.0)) * 0.5 + 0.5);
    } else if (u_type == 5) {                           // mirror
        vec2 q = uv; float m = floor(u_p.x + 0.5), off = u_p.y;
        if (m < 0.5) q.x = q.x > 0.5 + off ? 1.0 - q.x + 2.0 * off : q.x;
        else if (m < 1.5) q.x = q.x < 0.5 + off ? 1.0 - q.x + 2.0 * off : q.x;
        else if (m < 2.5) q.y = q.y > 0.5 + off ? 1.0 - q.y + 2.0 * off : q.y;
        else { q = vec2(0.5) - abs(uv - 0.5); }
        o = S(clamp(q, 0.0, 1.0));
    } else if (u_type == 6) {                           // rgb split
        vec2 d = dirOf(u_p.y) * u_p.x * amt * (1.0 + a_beat) * vec2(1.0 / asp, 1.0);
        o = vec3(S(uv + d).r, src.g, S(uv - d).b);
    } else if (u_type == 7) {                           // chromatic aberration
        float r = length(c * vec2(asp, 1.0)); vec2 d = c * u_p.x * pow(r * 2.0, u_p.y) * amt * (1.0 + a_treb);
        o = vec3(S(uv + d).r, src.g, S(uv - d).b);
    } else if (u_type == 8) {                           // pixelate
        vec2 g = vec2(u_res.x, u_res.y) / u_p.x; o = S((floor(uv * g) + 0.5) / g);
    } else if (u_type == 9) {                           // posterize
        float lv = u_p.x; o = floor(src * lv + 0.5) / lv;
    } else if (u_type == 10) {                          // threshold
        float l = lum(src); float v = smoothstep(u_p.x - u_p.y, u_p.x + u_p.y + 0.001, l); o = mix(vec3(0.0), u_col, v);
    } else if (u_type == 11) {                          // invert
        o = 1.0 - src;
    } else if (u_type == 12) {                          // blur
        float rad = u_p.x * (1.0 + 0.5 * a_bass);
        if (u_p.z > 0.5) {
            vec2 dir = dirOf(u_p.y) * rad / u_res.y; dir.x /= asp; vec3 s = vec3(0.0);
            for (int i = -8; i <= 8; i++) s += S(uv + dir * float(i) / 8.0);
            o = s / 17.0;
        } else o = blurDisc(uv, rad, asp);
    } else if (u_type == 13) {                          // sharpen
        vec3 b = blurDisc(uv, u_p.x * 2.0, asp); o = src + (src - b) * 1.5;
    } else if (u_type == 14) {                          // bloom
        vec3 b = blurDisc(uv, u_p.y, asp); b = max(b - u_p.x, 0.0) / max(1.0 - u_p.x, 0.05);
        o = src + b * u_p.z * (1.0 + a_beat * 0.6);
    } else if (u_type == 15) {                          // glow
        vec3 b = blurDisc(uv, u_p.x, asp); o = src + mix(b, b * u_col * 2.0, u_p.z) * u_p.y * (0.6 + a_rms);
    } else if (u_type == 16) {                          // grain
        vec2 g = floor(uv * u_res / u_p.x); float tt = floor(t * 24.0 * u_p.z);
        float n = hash(g + tt) - 0.5; vec3 cn = vec3(hash(g + tt + 3.1), hash(g + tt + 7.7), hash(g + tt + 9.3)) - 0.5;
        o = src + mix(vec3(n), cn, u_p.y) * 0.35;
    } else if (u_type == 17) {                          // vignette
        float r = length(c * vec2(asp, 1.0)) * 1.4; o = src * (1.0 - smoothstep(u_p.x, u_p.x + u_p.y, r));
    } else if (u_type == 18) {                          // scanlines
        float s = 0.5 + 0.5 * sin(uv.y * u_p.x * PI); s = pow(s, 1.0 + 3.0 * u_p.y);
        o = src * (1.0 - 0.6 * (1.0 - s)) * (1.0 - u_p.z * 0.1 * sin(t * 60.0));
    } else if (u_type == 19) {                          // feedback trails
        vec2 q = rot(u_p.z) * (pc * (1.0 - u_p.y)); vec3 h = texture2D(u_hist, (q / vec2(asp, 1.0)) * 0.5 + 0.5).rgb;
        o = max(src, h * u_p.x);
    } else if (u_type == 20) {                          // smear
        vec2 d = dirOf(u_p.y) * u_p.x * 0.02 * (1.0 + a_flux); d.x /= asp;
        vec3 h = texture2D(u_hist, uv - d).rgb; o = max(src, h * u_p.z);
    } else if (u_type == 21) {                          // gradient map
        float l = clamp((lum(src) - 0.5) * u_p.y + 0.5 + u_p.x, 0.0, 1.0); o = gradMap(l);
    } else if (u_type == 22) {                          // edge detect
        vec2 px = u_p.y / u_res;
        float tl = lum(S(uv + vec2(-px.x, px.y))), tt = lum(S(uv + vec2(0.0, px.y))), tr = lum(S(uv + px));
        float l = lum(S(uv + vec2(-px.x, 0.0))), r = lum(S(uv + vec2(px.x, 0.0)));
        float bl = lum(S(uv - px)), bb = lum(S(uv + vec2(0.0, -px.y))), br = lum(S(uv + vec2(px.x, -px.y)));
        float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br, gy = -tl - 2.0 * tt - tr + bl + 2.0 * bb + br;
        float e = clamp(length(vec2(gx, gy)) * u_p.x, 0.0, 1.0);
        o = mix(u_col * e, src * 0.6 + u_col * e, u_p.z);
    } else if (u_type == 23) {                          // glitch
        float seed = floor(t * u_p.w); float g = u_p.y * (0.4 + a_beat + a_flux);
        vec2 q = uv; float row = floor(uv.y * (8.0 + 30.0 * hash(vec2(seed, 2.0))));
        if (hash(vec2(row, seed)) > 1.0 - 0.4 * g) q.x += (hash(vec2(seed, row)) - 0.5) * u_p.z * 0.5;
        vec2 bs = vec2(u_p.x / asp, u_p.x); vec2 bid = floor(uv / bs);
        if (hash(bid + seed) > 1.0 - 0.1 * g) q += (vec2(hash(bid + seed + 1.0), hash(bid + seed + 2.0)) - 0.5) * 0.1;
        float s = 0.01 * g; o = vec3(S(q + vec2(s, 0.0)).r, S(q).g, S(q - vec2(s, 0.0)).b);
    } else if (u_type == 24) {                          // vhs
        vec2 q = uv; float band = abs(fract(uv.y * 0.5 - t * 0.12) - 0.5);
        q.x += u_p.x * (band < 0.08 ? 0.05 : 0.0) * sin(uv.y * 40.0 + t * 9.0) + u_p.w * 0.004 * sin(uv.y * 5.0 + t * 2.0);
        float bl = u_p.y * 0.01; o = vec3(S(q + vec2(bl, 0.0)).r, S(q).g, S(q - vec2(bl, 0.0)).b);
        o += (hash(uv * u_res + floor(t * 30.0)) - 0.5) * u_p.z * 0.4;
        o *= 0.92 + 0.08 * sin(uv.y * u_res.y * 1.5);
    } else if (u_type == 25) {                          // color grade
        vec3 g = hueShift(src, u_p.x); g = mix(vec3(lum(g)), g, u_p.y); g = (g - 0.5) * u_p.z + 0.5; g *= u_p.w; o = g;
    } else if (u_type == 26) {                          // radial blur
        vec2 ctr = u_p.yz / vec2(asp, 1.0) * 0.5 + 0.5; vec2 d = (uv - ctr) * u_p.x * 0.12 * (1.0 + a_bass * 0.6);
        vec3 s = vec3(0.0); for (int i = 0; i < 16; i++) s += S(uv - d * float(i) / 16.0); o = s / 16.0;
    } else if (u_type == 27) {                          // halftone
        vec2 q = rot(u_p.y * PI / 180.0) * (uv * u_res / u_p.x); vec2 f = fract(q) - 0.5;
        float l = clamp((lum(src) - 0.5) * u_p.z + 0.5, 0.0, 1.0);
        float r = sqrt(l) * 0.7; float m = smoothstep(r + 0.08, r - 0.08, length(f));
        o = src * m;
    } else if (u_type == 28) {                          // lens warp
        vec2 q = pc - u_p.yz; float r2 = dot(q, q); q *= 1.0 + u_p.x * r2 * amt; q += u_p.yz;
        o = S(clamp((q / vec2(asp, 1.0)) * 0.5 + 0.5, 0.0, 1.0));
    } else {                                            // pixel sort style
        vec2 dir = dirOf(u_p.y) * u_p.z * 0.4 / vec2(asp, 1.0); float thr = u_p.x; vec3 best = src; float bl = lum(src); float found = 0.0;
        for (int i = 1; i < 20; i++) {
            vec2 off = dir * float(i) / 20.0 + (hash(vec2(float(i), floor(uv.y * 80.0))) - 0.5) * u_p.w * 0.04;
            vec3 s = S(uv - off); if (lum(s) > thr && found < 0.5) { best = s; found = 1.0; }
        }
        o = mix(src, best, step(bl, thr));
    }
    float outA = cntA > 0.0 ? accA / cntA : srcA;
    gl_FragColor = vec4(mix(src, max(o, 0.0), clamp(amt, 0.0, 1.0)), mix(srcA, outA, clamp(amt, 0.0, 1.0)));
}
