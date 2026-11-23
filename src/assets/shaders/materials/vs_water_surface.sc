$input a_position
$output v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent

#include "../common/common.sh"
#include "../includes/water_waves.sh"

// A grid on the model's xz plane, centred under the viewer, displaced by the
// wave cascades. On a sphere each point lies its distance from the centre
// along the surface (the horizon the farthest), its waves raised along the
// sphere's normal there
void main()
{
    vec2 p = a_position.xz;
    vec3 d = WaveDisplacement(p);
    float radius = u_waterWaves[0].z;

    vec3 n = vec3(0.0, 1.0, 0.0);
    vec3 local = vec3(p.x, 0.0, p.y);
    if (radius > 0.0)
    {
        float len = length(p);
        float angle = min(len / radius, 1.5707964);
        vec2 dir = len > 0.0 ? p / len : vec2(0.0, 0.0);
        n = vec3(dir.x * sin(angle), cos(angle), dir.y * sin(angle));
        local = n * radius - vec3(0.0, radius, 0.0);
    }
    vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
    vec3 b = normalize(vec3(0.0, 0.0, 1.0) - n * n.z);
    local += t * d.x + n * d.y + b * d.z;

    vec4 worldPos = mul(u_model[0], vec4(local, 1.0));
    v_worldPos = worldPos.xyz;
    v_texcoord0 = p;
    v_normal = normalize(mul(u_model[0], vec4(n, 0.0)).xyz);
    v_tangent = normalize(mul(u_model[0], vec4(t, 0.0)).xyz);
    v_bitangent = normalize(mul(u_model[0], vec4(b, 0.0)).xyz);
    gl_Position = mul(u_viewProj, worldPos);
}
