#version 120
// monie visuals v3 - composite one premultiplied layer buffer onto the composition with a blend mode and opacity.
uniform sampler2D u_dst, u_layer;
uniform float u_opacity;
uniform int   u_blend;     // 0 Normal 1 Add 2 Screen 3 Multiply 4 Overlay 5 Lighten 6 Darken 7 Difference
varying vec2 vUV;
void main() {
    vec3 D = texture2D(u_dst, vUV).rgb;
    vec4 L = texture2D(u_layer, vUV);
    float a = clamp(L.a, 0.0, 1.0) * u_opacity;
    vec3 S = L.a > 0.0005 ? L.rgb / L.a : vec3(0.0);
    vec3 B;
    if      (u_blend == 0) B = S;
    else if (u_blend == 1) B = D + S;
    else if (u_blend == 2) B = 1.0 - (1.0 - D) * (1.0 - S);
    else if (u_blend == 3) B = D * S;
    else if (u_blend == 4) B = mix(2.0 * D * S, 1.0 - 2.0 * (1.0 - D) * (1.0 - S), step(0.5, D));
    else if (u_blend == 5) B = max(D, S);
    else if (u_blend == 6) B = min(D, S);
    else                   B = abs(D - S);
    gl_FragColor = vec4(mix(D, B, a), 1.0);
}
