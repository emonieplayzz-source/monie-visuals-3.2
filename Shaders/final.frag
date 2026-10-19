#version 120
// monie visuals v3 - final pass: global colour grade + stylise, then (preview only) guides / selection / before-after split.
uniform sampler2D u_tex, u_orig, u_grad;
uniform vec2  u_res;
uniform float u_time;
uniform float a_bass, a_mid, a_treb, a_beat, a_rms;
uniform float u_master, u_bright, u_contrast, u_sat, u_gamma, u_hue, u_temp, u_tint, u_exposure, u_lift, u_gmix, u_palCycle, u_reactColor,
              u_invert, u_glow, u_rgb, u_grain, u_vignette, u_scan, u_pixel, u_poster, u_thresh, u_edge;
// preview overlays
uniform float u_ovl;          // 0 = clean (export / thumbnails)
uniform float u_guides;       // bitmask: 1 grid, 2 safe area, 4 centre cross, 8 thirds
uniform float u_split;        // before/after: <0 off, else x position 0..1 of the divider (left = original, right = processed)
uniform vec4  u_sel;          // selection: cx cy hx hy in aspect space (hx<=0 = none)
uniform float u_selRot;
uniform vec4  u_status;       // x: 1 = show missing-media badge, y: 1 = shader error hatch
varying vec2 vUV;

float hash(vec2 p) { p = fract(p * vec2(443.897, 441.423)); p += dot(p, p + 19.19); return fract(p.x * p.y); }
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 hueShift(vec3 c, float h) { float a = h * 6.2831853, ca = cos(a), sa = sin(a); vec3 k = vec3(0.57735); return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca); }
vec3 gradMap(float t) { return texture2D(u_grad, vec2(clamp(t, 0.002, 0.998), 0.5)).rgb; }

vec3 grade(sampler2D tex, vec2 uvIn, bool full) {
    float asp = u_res.x / u_res.y;
    vec2 uv = uvIn;
    if (u_pixel > 0.001) { float cells = mix(900.0, 24.0, sqrt(u_pixel)); vec2 g = vec2(cells * asp, cells); uv = (floor(uv * g) + 0.5) / g; }
    vec2 d = uv - 0.5;
    vec2 sh = d * u_rgb * (0.02 + 0.03 * a_treb + 0.02 * a_beat);
    vec3 c = vec3(texture2D(tex, uv + sh).r, texture2D(tex, uv).g, texture2D(tex, uv - sh).b);
    if (u_glow > 0.01) {
        vec3 b = vec3(0.0); float rad = 0.004 + 0.012 * u_glow;
        for (int i = 0; i < 8; i++) {
            float an = float(i) * 0.785398; vec2 o = vec2(cos(an), sin(an)) * rad * vec2(1.0, asp);
            b += max(texture2D(tex, uv + o).rgb - 0.45, 0.0) + max(texture2D(tex, uv + o * 2.2).rgb - 0.45, 0.0);
        }
        c += b / 16.0 * u_glow * (1.5 + a_beat);
    }
    if (u_edge > 0.01) {
        vec2 px = 1.0 / u_res;
        float tl = lum(texture2D(tex, uv + vec2(-px.x, px.y)).rgb), t = lum(texture2D(tex, uv + vec2(0.0, px.y)).rgb), tr = lum(texture2D(tex, uv + px).rgb);
        float l = lum(texture2D(tex, uv + vec2(-px.x, 0.0)).rgb), r = lum(texture2D(tex, uv + vec2(px.x, 0.0)).rgb);
        float bl = lum(texture2D(tex, uv - px).rgb), b2 = lum(texture2D(tex, uv + vec2(0.0, -px.y)).rgb), br = lum(texture2D(tex, uv + vec2(px.x, -px.y)).rgb);
        float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br, gy = -tl - 2.0 * t - tr + bl + 2.0 * b2 + br;
        float e = clamp(length(vec2(gx, gy)) * 2.0, 0.0, 1.0);
        c = mix(c, gradMap(0.95) * e * 2.0 + c * 0.15, u_edge);
    }
    c *= exp2(u_exposure);
    if (u_gmix > 0.01) c = mix(c, gradMap(clamp(lum(c), 0.0, 1.0)), u_gmix);
    c.r += u_temp * 0.15; c.b -= u_temp * 0.15; c.g += u_tint * 0.12; c.r -= u_tint * 0.05; c.b -= u_tint * 0.05;
    c = hueShift(c, u_hue + u_time * 0.03 * u_palCycle + a_rms * u_reactColor * 0.25 + a_beat * u_reactColor * 0.04);
    c = mix(vec3(lum(c)), c, u_sat);
    c = (c - 0.5) * u_contrast + 0.5;
    c *= u_bright * mix(0.35, 1.1, u_master);
    c = pow(max(c, 0.0), vec3(1.0 / u_gamma));
    c = c * (1.0 - u_lift) + u_lift;
    if (u_poster > 0.01) { float lv = mix(24.0, 2.0, u_poster); c = floor(c * lv + 0.5) / lv; }
    if (u_thresh > 0.01) { float l = lum(c); c = mix(c, mix(gradMap(0.05), gradMap(0.95), step(mix(0.9, 0.2, u_thresh), l)), min(1.0, u_thresh * 4.0)); }
    c = mix(c, 1.0 - c, u_invert);
    c *= 1.0 - u_scan * 0.35 * (0.5 + 0.5 * sin(uvIn.y * u_res.y * 3.14159));
    c *= 1.0 - u_vignette * smoothstep(0.25, 0.9, length(d * vec2(asp, 1.0)) * 1.2);
    c += (hash(uvIn * u_res + u_time * 61.0) - 0.5) * u_grain * 0.25;
    return clamp(c, 0.0, 1.0);
}
float segLine(float v, float at, float w) { return smoothstep(w, 0.0, abs(v - at)); }
float boxLine(vec2 p, vec2 c, vec2 h, float w) { vec2 q = abs(p - c) - h; float d = abs(max(q.x, q.y)); return smoothstep(w, 0.0, d) * step(max(q.x, q.y), w); }

void main() {
    float asp = u_res.x / u_res.y;
    vec3 c;
    if (u_split >= 0.0 && u_ovl > 0.5) c = vUV.x < u_split ? texture2D(u_orig, vUV).rgb : grade(u_tex, vUV, true);
    else c = grade(u_tex, vUV, true);
    if (u_ovl > 0.5) {
        vec2 px = 1.0 / u_res; float w = 1.2 * px.y;
        vec2 p = (vUV * 2.0 - 1.0) * vec2(asp, 1.0);
        vec3 gcol = vec3(1.0);
        float g = 0.0;
        if (mod(u_guides, 2.0) >= 1.0) { vec2 f = abs(fract(vUV * vec2(16.0, 9.0)) - 0.5); g += 0.25 * (smoothstep(0.5 - px.x * 16.0 * 1.2, 0.5, f.x * 1.0 + 0.0) + smoothstep(0.5 - px.y * 9.0 * 1.2, 0.5, f.y)); }
        if (mod(floor(u_guides / 2.0), 2.0) >= 1.0) { // title-safe 90% and action-safe 93% rectangles
            vec2 q = abs(vUV - 0.5); g += 0.55 * (step(max(q.x / 0.45, q.y / 0.45), 1.0) - step(max(q.x / 0.45, q.y / 0.45), 1.0 - 3.0 * px.y)) ;
            g += 0.35 * (step(max(q.x / 0.465, q.y / 0.465), 1.0) - step(max(q.x / 0.465, q.y / 0.465), 1.0 - 3.0 * px.y));
        }
        if (mod(floor(u_guides / 4.0), 2.0) >= 1.0) g += 0.5 * (segLine(vUV.x, 0.5, px.x * 1.2) + segLine(vUV.y, 0.5, px.y * 1.2));
        if (mod(floor(u_guides / 8.0), 2.0) >= 1.0) g += 0.45 * (segLine(vUV.x, 1.0 / 3.0, px.x * 1.2) + segLine(vUV.x, 2.0 / 3.0, px.x * 1.2) + segLine(vUV.y, 1.0 / 3.0, px.y * 1.2) + segLine(vUV.y, 2.0 / 3.0, px.y * 1.2));
        c = mix(c, vec3(1.0), clamp(g, 0.0, 0.8) * 0.6);
        if (u_split >= 0.0) c = mix(c, vec3(1.0), segLine(vUV.x, u_split, px.x * 1.5));
        if (u_sel.z > 0.0) {                                         // selection frame (rotated rectangle)
            vec2 l = p - u_sel.xy; float cr = cos(u_selRot), sr = sin(u_selRot); l = vec2(cr * l.x + sr * l.y, -sr * l.x + cr * l.y);
            vec2 q = abs(l) - u_sel.zw; float d = max(q.x, q.y);
            float edge = smoothstep(0.004, 0.0, abs(d));
            vec2 cq = abs(abs(l) - u_sel.zw); float hnd = step(max(cq.x, cq.y), 0.016);
            c = mix(c, vec3(0.25, 0.65, 1.0), max(edge, hnd));
        }
        if (u_status.y > 0.5) c = mix(c, vec3(0.8, 0.1, 0.1), 0.35 * step(0.5, fract((vUV.x + vUV.y) * 30.0)));
    }
    gl_FragColor = vec4(c, 1.0);
}
