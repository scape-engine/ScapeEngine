$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent
$output v_texcoord0, v_axisX, v_axisY, v_axisZ

#include <bgfx_shader.sh>

// A part of a model into an imposter's atlas (Imposter::Bake): u_model is
// the part's transform in the model, u_bakeViewProj the view's camera and its
// tile of the atlas. The tangent frame stays in the model's space, as the
// baked normals do

uniform mat4 u_bakeViewProj;

void main()
{
    v_axisX = mul(u_model[0], vec4(a_tangent, 0.0)).xyz;
    v_axisY = mul(u_model[0], vec4(a_bitangent, 0.0)).xyz;
    v_axisZ = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
    v_texcoord0 = a_texcoord0;
    gl_Position = mul(u_bakeViewProj, mul(u_model[0], vec4(a_position, 1.0)));
}
