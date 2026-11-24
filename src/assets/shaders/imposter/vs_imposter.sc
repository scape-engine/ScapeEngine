$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_worldPos, v_texcoord0, v_axisX, v_axisY, v_axisZ, v_fade

#include <bgfx_shader.sh>
#include "../includes/fade.sh"

// An imposter's card (Imposter): the model's matrix per instance (glm's
// columns), the card turned to the eye and showing the baked view nearest the
// eye's direction, of the material's variant. a_texcoord0 is the card's
// corner, -1..1. The model's axes go to the fragment shader, which
// turns the baked normals with them. Every pass draws it: in the shadow pass
// the eye is the light, so the card casts the shape seen from there

uniform vec4 u_imposter;        // x = views a side, y = variants,
                                // z = radius, w = 1: render target origin
                                // bottom left
uniform vec4 u_imposterCenter;  // xyz = the baked sphere's centre (model),
                                // w = the variant shown

void main()
{
    vec3 axisX = normalize(i_data0.xyz);
    vec3 axisY = normalize(i_data1.xyz);
    vec3 axisZ = normalize(i_data2.xyz);
    float scale = max(length(i_data0.xyz), max(length(i_data1.xyz), length(i_data2.xyz)));
    vec3 c = u_imposterCenter.xyz;
    vec3 center = (i_data0 * c.x + i_data1 * c.y + i_data2 * c.z + i_data3).xyz;

    // Towards the eye, in the model's space and over its upper hemisphere: an
    // orthographic view (shadows) looks along its forward axis
    vec3 toEye = u_proj[3][3] > 0.5
        ? -mul(u_invView, vec4(0.0, 0.0, 1.0, 0.0)).xyz
        : mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - center;
    vec3 d = vec3(dot(toEye, axisX), dot(toEye, axisY), dot(toEye, axisZ));
    d.y = max(d.y, 0.0);
    d = normalize(d + vec3(0.0, 1e-6, 0.0));

    // The nearest baked view on the hemi-octahedral grid (Imposter::Bake)
    float n = u_imposter.x;
    vec2 p = d.xz / (abs(d.x) + abs(d.y) + abs(d.z));
    vec2 tile = clamp(floor((vec2(p.x + p.y, p.x - p.y) * 0.5 + 0.5) * n),
                      0.0, n - 1.0);

    // The card's axes, as a baked view's camera has them: up is the model's
    // y, or z looking straight down
    vec3 right = cross(d, vec3(0.0, 1.0, 0.0));
    right = dot(right, right) < 1e-8 ? cross(d, vec3(0.0, 0.0, 1.0)) : right;
    right = normalize(right);
    vec3 up = cross(-d, right);

    vec2 corner = a_texcoord0;
    float radius = u_imposter.z * scale;
    vec3 rightWorld = axisX * right.x + axisY * right.y + axisZ * right.z;
    vec3 upWorld = axisX * up.x + axisY * up.y + axisZ * up.z;
    vec3 worldPos = center + (rightWorld * corner.x + upWorld * corner.y) * radius;

    // The variant's columns of views, then the view's tile
    float column = u_imposterCenter.w * n + tile.x;
    float flip = u_imposter.w > 0.5 ? 1.0 : -1.0;
    v_texcoord0 = vec2((column + 0.5 + 0.5 * corner.x) / (n * u_imposter.y),
                       (tile.y + 0.5 + 0.5 * corner.y * flip) / n);

    v_worldPos = worldPos;
    v_axisX = axisX;
    v_axisY = axisY;
    v_axisZ = axisZ;
    v_fade = FadeAmount(i_data4.xyz);
    gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));
}
