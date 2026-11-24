$input v_texcoord0, v_axisX, v_axisY, v_axisZ

#include <bgfx_shader.sh>

// A part's surface into an imposter's atlas: the standard material's base
// colour (fs_lit.sc) and its normal in the model's space with its roughness,
// both at full coverage (the atlas is cleared to zero around them)

uniform vec4 u_meshColor;
uniform vec4 u_materialParams;  // y = roughness factor, z = alpha cutoff
uniform vec4 u_emissive;        // w = 1: double-sided
uniform vec4 u_bakeEye;         // xyz = towards the view's eye (model)

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normalMap, 1);
SAMPLER2D(s_metallicRoughnessMap, 2);

// fs_lit.sc's tangent space normal map
vec3 SurfaceNormal(vec3 N, vec3 T, vec3 B, vec2 uv)
{
    if (dot(T, T) < 1e-8 || dot(B, B) < 1e-8) return N;

    vec3 t = texture2D(s_normalMap, uv).xyz * 2.0 - 1.0;
    return normalize(normalize(T) * t.x - normalize(B) * t.y + N * t.z);
}

void main()
{
    vec4 baseColor = texture2D(s_albedo, v_texcoord0) * u_meshColor;
    if (baseColor.a < u_materialParams.z) discard;

    // A double-sided surface seen from behind shows its back
    vec3 n = normalize(v_axisZ);
    if (u_emissive.w > 0.5 && dot(n, u_bakeEye.xyz) < 0.0) n = -n;
    vec3 N = SurfaceNormal(n, v_axisX, v_axisY, v_texcoord0);
    float roughness = clamp(u_materialParams.y *
                            texture2D(s_metallicRoughnessMap, v_texcoord0).g,
                            0.04, 1.0);

    gl_FragData[0] = vec4(baseColor.rgb, 1.0);
    gl_FragData[1] = vec4(N * 0.5 + 0.5, roughness);
}
