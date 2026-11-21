$input a_position, a_indices, a_weight
$output v_depth

#include <bgfx_shader.sh>

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

// vs_shadow.sc after skinning
void main()
{
    vec4 position = Skin(vec4(a_position, 1.0), a_indices, a_weight);
    vec4 worldPos = mul(u_model[0], position);
    gl_Position = mul(u_viewProj, worldPos);

    v_depth = gl_Position.z;
}
