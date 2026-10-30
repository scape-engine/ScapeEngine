$input a_position
$output v_worldPos

#include <bgfx_shader.sh>

void main()
{
    // World position: the quad is moved to follow the camera, so the grid
    // pattern must come from world space, not the quad's local space
    v_worldPos = mul(u_model[0], vec4(a_position, 1.0)).xyz;

    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
