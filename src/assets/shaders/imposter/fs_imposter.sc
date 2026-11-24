$input v_worldPos, v_texcoord0, v_axisX, v_axisY, v_axisZ, v_fade

#include "../common/common.sh"
#include "../includes/lit.sh"
#include "../includes/dither.sh"

// An imposter's card, lit like the standard material: the baked colour, and
// the baked normal (model space) and roughness. Both are multiplied by the
// baked coverage (alpha), so filtered edges and mips divide it back out

SAMPLER2D(s_imposterAlbedo, 0);  // rgb = base colour, a = coverage
SAMPLER2D(s_imposterNormal, 1);  // rgb = normal * 0.5 + 0.5, a = roughness

void main()
{
    DitherDiscard(v_fade, gl_FragCoord.xy);
    vec4 albedo = texture2D(s_imposterAlbedo, v_texcoord0);
    if (albedo.a < 0.5) discard;
    vec4 surface = texture2D(s_imposterNormal, v_texcoord0) / albedo.a;
    vec3 n = surface.xyz * 2.0 - 1.0;
    vec3 N = normalize(v_axisX * n.x + v_axisY * n.y + v_axisZ * n.z);
    vec3 V = normalize(u_viewPos.xyz - v_worldPos);

    vec3 result = LitSurface(N, V, v_worldPos, gl_FragCoord.xy,
                             albedo.rgb / albedo.a, 0.0,
                             clamp(surface.a, 0.04, 1.0), 1.0);
    gl_FragColor = vec4(toGamma(result), 1.0);
}
