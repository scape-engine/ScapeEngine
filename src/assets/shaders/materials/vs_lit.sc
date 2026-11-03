$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent
$output v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent

#include "../common/common.sh"

void main()
{
    // World position
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;

    // Transform the tangent frame to world space (using normal matrix)
    // For uniform scale, transpose(inverse(model)) ≈ model
    v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_tangent = mul(u_model[0], vec4(a_tangent, 0.0)).xyz;
    v_bitangent = mul(u_model[0], vec4(a_bitangent, 0.0)).xyz;

    v_texcoord0 = a_texcoord0;

    // Final clip position
    gl_Position = mul(u_viewProj, worldPos);
}
