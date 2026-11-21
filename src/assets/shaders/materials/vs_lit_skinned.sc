$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, a_indices, a_weight
$output v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent

#include "../common/common.sh"

// The bones' blend of a model-space point (w 1) or direction (w 0); a vertex
// without weights stays as it is
uniform mat4 u_bones[64];

vec4 Skin(vec4 v, vec4 indices, vec4 weight)
{
    float total = weight.x + weight.y + weight.z + weight.w;
    if (total <= 0.0)
        return v;
    vec4 sum = mul(u_bones[int(indices.x)], v) * weight.x
             + mul(u_bones[int(indices.y)], v) * weight.y
             + mul(u_bones[int(indices.z)], v) * weight.z
             + mul(u_bones[int(indices.w)], v) * weight.w;
    return sum / total;
}

// vs_lit.sc after skinning: the vertex follows its bones (DrawList::SetBones)
void main()
{
    vec3 position = Skin(vec4(a_position, 1.0), a_indices, a_weight).xyz;
    vec4 worldPos = mul(u_model[0], vec4(position, 1.0));
    v_worldPos = worldPos.xyz;

    // For uniform scale, transpose(inverse(model)) ~ model
    v_normal = normalize(mul(u_model[0], Skin(vec4(a_normal, 0.0), a_indices, a_weight)).xyz);
    v_tangent = mul(u_model[0], Skin(vec4(a_tangent, 0.0), a_indices, a_weight)).xyz;
    v_bitangent = mul(u_model[0], Skin(vec4(a_bitangent, 0.0), a_indices, a_weight)).xyz;

    v_texcoord0 = a_texcoord0;

    gl_Position = mul(u_viewProj, worldPos);
}
