$input a_position
$output v_ndc

#include <bgfx_shader.sh>

// Fullscreen pass over the scene view
void main()
{
    v_ndc = a_position;
    gl_Position = vec4(a_position, 0.5, 1.0);
}
