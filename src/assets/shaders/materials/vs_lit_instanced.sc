$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent, v_fade

#include "../common/common.sh"
#include "../includes/fade.sh"

// vs_lit.sc with the model matrix per instance: its columns (glm's
// layout) applied by hand, so every backend reads them alike. Each instance
// fades by its own origin
void main()
{
    // World position
    vec4 worldPos = (i_data0 * a_position.x + i_data1 * a_position.y + i_data2 * a_position.z + i_data3);
    v_worldPos = worldPos.xyz;

    // Transform the tangent frame to world space (using normal matrix)
    // For uniform scale, transpose(inverse(model)) ≈ model
    v_normal = normalize((i_data0 * a_normal.x + i_data1 * a_normal.y + i_data2 * a_normal.z).xyz);
    v_tangent = (i_data0 * a_tangent.x + i_data1 * a_tangent.y + i_data2 * a_tangent.z).xyz;
    v_bitangent = (i_data0 * a_bitangent.x + i_data1 * a_bitangent.y + i_data2 * a_bitangent.z).xyz;

    v_texcoord0 = a_texcoord0;
    v_fade = FadeAmount(i_data3.xyz);

    // Final clip position
    gl_Position = mul(u_viewProj, worldPos);
}
