$input v_worldPos, v_texcoord0, v_axisX, v_axisY, v_axisZ, v_fade

#include <bgfx_shader.sh>
#include "../includes/dither.sh"

// fs_pre_pass.sc for an imposter's card: cut out by the baked coverage, with
// the baked normal

SAMPLER2D(s_imposterAlbedo, 0);
SAMPLER2D(s_imposterNormal, 1);

void main()
{
    DitherDiscard(v_fade, gl_FragCoord.xy);
    float coverage = texture2D(s_imposterAlbedo, v_texcoord0).a;
    if (coverage < 0.5) discard;
    vec3 n = texture2D(s_imposterNormal, v_texcoord0).xyz / coverage * 2.0 - 1.0;
    vec3 N = normalize(v_axisX * n.x + v_axisY * n.y + v_axisZ * n.z);
    gl_FragData[0] = vec4(normalize(mul(u_view, vec4(N, 0.0)).xyz), 1.0);
    gl_FragData[1] = vec4(mul(u_view, vec4(v_worldPos, 1.0)).z, 0.0, 0.0, 1.0);
}
