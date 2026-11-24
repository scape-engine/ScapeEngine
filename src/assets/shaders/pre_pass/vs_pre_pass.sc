$input a_position, a_normal
$output v_viewNormal, v_viewDepth, v_fade

#include <bgfx_shader.sh>
#include "../includes/fade.sh"

void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));

    // For uniform scale, transpose(inverse(modelView)) ≈ modelView
    v_viewNormal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
    v_viewDepth = viewPos.z;
    v_fade = FadeAmount(mul(u_model[0], vec4(0.0, 0.0, 0.0, 1.0)).xyz);

    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
