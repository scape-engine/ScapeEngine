$input a_position, a_normal, a_texcoord0
$output v_worldPos, v_normal, v_texcoord0

#include "../common/common.sh"

void main()
{
    // World position
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;

    // Transform normal to world space (using normal matrix)
    // For uniform scale, transpose(inverse(model)) ≈ model
    v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);

    v_texcoord0 = a_texcoord0;

    // Final clip position
    gl_Position = mul(u_viewProj, worldPos);
}
