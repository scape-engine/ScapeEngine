$input a_position, a_texcoord0
$output v_depth, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    // depth-only vertex shader
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);

    // Pass depth to fragment shader
    v_depth = gl_Position.z;

    // Alpha mask lookup
    v_texcoord0 = a_texcoord0;
}
