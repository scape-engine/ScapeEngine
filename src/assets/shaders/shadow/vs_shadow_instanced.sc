$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_depth, v_fade

#include <bgfx_shader.sh>
#include "../includes/fade.sh"

// vs_shadow.sc with the model matrix per instance: its columns (glm's
// layout) applied by hand, so every backend reads them alike. Each instance
// fades by its own origin
void main()
{
    vec4 worldPos = (i_data0 * a_position.x + i_data1 * a_position.y + i_data2 * a_position.z + i_data3);
    gl_Position = mul(u_viewProj, worldPos);

    // Pass depth to fragment shader
    v_depth = gl_Position.z;
    v_fade = FadeAmount(i_data3.xyz);
}
