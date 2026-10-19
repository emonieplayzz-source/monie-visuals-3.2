#version 120
// monie visuals v3 - Gray-Scott reaction-diffusion step. State (A,B) is packed as 16-bit values in RGBA8: A -> rg, B -> ba.
uniform sampler2D u_state;
uniform vec2  u_texel;
uniform float u_feed, u_kill, u_dt;
uniform vec3  u_seed;      // xy = position (0..1), z = radius (0 = none)
uniform float u_init;      // 1 = (re)initialise
varying vec2 vUV;
float unpack(vec2 hl) { return hl.x + hl.y / 255.0; }
vec2 packv(float v) { v = clamp(v, 0.0, 0.99999); float hi = floor(v * 255.0) / 255.0; return vec2(hi, fract(v * 255.0)); }
vec2 AB(vec2 uv) { vec4 s = texture2D(u_state, fract(uv)); return vec2(unpack(s.rg), unpack(s.ba)); }
float hash(vec2 p) { p = fract(p * vec2(443.897, 441.423)); p += dot(p, p + 19.19); return fract(p.x * p.y); }
void main() {
    vec2 ab;
    if (u_init > 0.5) {
        ab = vec2(1.0, 0.0);
        vec2 cell = floor(vUV * 12.0);
        if (hash(cell) > 0.82 && length(fract(vUV * 12.0) - 0.5) < 0.35) ab = vec2(0.5, 0.25);
    } else {
        vec2 c = AB(vUV);
        vec2 lap = AB(vUV + vec2(u_texel.x, 0.0)) * 0.2 + AB(vUV - vec2(u_texel.x, 0.0)) * 0.2 + AB(vUV + vec2(0.0, u_texel.y)) * 0.2 + AB(vUV - vec2(0.0, u_texel.y)) * 0.2
                 + AB(vUV + u_texel) * 0.05 + AB(vUV - u_texel) * 0.05 + AB(vUV + vec2(u_texel.x, -u_texel.y)) * 0.05 + AB(vUV + vec2(-u_texel.x, u_texel.y)) * 0.05 - c;
        float abb = c.x * c.y * c.y;
        ab = vec2(c.x + (1.0 * lap.x - abb + u_feed * (1.0 - c.x)) * u_dt, c.y + (0.5 * lap.y + abb - (u_kill + u_feed) * c.y) * u_dt);
        if (u_seed.z > 0.0 && length((vUV - u_seed.xy) * vec2(1.0)) < u_seed.z) ab = vec2(0.45, 0.35);
    }
    gl_FragColor = vec4(packv(ab.x), packv(ab.y));
}
