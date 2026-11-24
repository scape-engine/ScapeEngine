$input a_position
$output v_depth, v_fade

#include <bgfx_shader.sh>
#include "../includes/fade.sh"

void main()
{
    // depth-only vertex shader
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    
    // Pass depth to fragment shader
    v_depth = gl_Position.z;
    v_fade = FadeAmount(mul(u_model[0], vec4(0.0, 0.0, 0.0, 1.0)).xyz);
}
