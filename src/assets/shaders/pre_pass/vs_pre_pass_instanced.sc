$input a_position, a_normal, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_viewNormal, v_viewDepth, v_fade

#include <bgfx_shader.sh>
#include "../includes/fade.sh"

// vs_pre_pass.sc with the model matrix per instance: its columns (glm's
// layout) applied by hand, so every backend reads them alike. Each instance
// fades by its anchor
void main()
{
    vec4 worldPos = (i_data0 * a_position.x + i_data1 * a_position.y + i_data2 * a_position.z + i_data3);
    vec4 viewPos = mul(u_view, worldPos);

    // For uniform scale, transpose(inverse(modelView)) ≈ modelView
    v_viewNormal = mul(u_view, (i_data0 * a_normal.x + i_data1 * a_normal.y + i_data2 * a_normal.z)).xyz;
    v_viewDepth = viewPos.z;
    v_fade = FadeAmount(i_data4.xyz);

    gl_Position = mul(u_viewProj, worldPos);
}
