$input a_position, a_normal, a_indices, a_weight
$output v_viewNormal, v_viewDepth, v_fade

#include <bgfx_shader.sh>
#include "../includes/fade.sh"

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

// vs_pre_pass.sc after skinning
void main()
{
    vec4 position = Skin(vec4(a_position, 1.0), a_indices, a_weight);
    vec4 viewPos = mul(u_modelView, position);

    // For uniform scale, transpose(inverse(modelView)) ~ modelView
    v_viewNormal = mul(u_modelView, Skin(vec4(a_normal, 0.0), a_indices, a_weight)).xyz;
    v_viewDepth = viewPos.z;
    v_fade = FadeAmount(mul(u_model[0], vec4(0.0, 0.0, 0.0, 1.0)).xyz);

    gl_Position = mul(u_modelViewProj, position);
}
