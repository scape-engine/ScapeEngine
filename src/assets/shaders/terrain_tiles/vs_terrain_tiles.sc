$input a_position, a_normal, a_texcoord0, a_texcoord2
$output v_worldPos, v_normal, v_tiles, v_centrePos, v_up, v_centreNormal, v_fade

#include "../common/common.sh"
#include "../includes/fade.sh"

// a_texcoord0: two tile groups, the blend between them, 1 below sea level
// a_texcoord2: the position relative to the terrain's centre
void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;
    v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_tiles = a_texcoord0;
    v_centrePos = a_texcoord2;
    v_centreNormal = a_normal;
    // Away from the centre, in world space
    v_up = normalize(mul(u_model[0], vec4(a_texcoord2, 0.0)).xyz);
    v_fade = FadeAmount(mul(u_model[0], vec4(0.0, 0.0, 0.0, 1.0)).xyz);

    gl_Position = mul(u_viewProj, worldPos);
}
