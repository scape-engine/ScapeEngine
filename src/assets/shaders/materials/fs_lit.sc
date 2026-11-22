$input v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent

#include "../common/common.sh"
#include "../includes/lit.sh"

//
// Standard material: metallic-roughness PBR (glTF 2.0 material model)
// Direct lighting: Cook-Torrance microfacet specular + Lambert diffuse
//  - B. Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013
//  - glTF 2.0 spec, Appendix B: BRDF Implementation
//

//
// UNIFORMS
//

// Material (StandardMaterial, stages 0-4). Unset textures are bound to
// neutral defaults: white, or a flat normal for the normal map
uniform vec4 u_meshColor;       // base color factor
uniform vec4 u_materialParams;  // x = metallic factor, y = roughness factor,
                                // z = alpha cutoff (0: none), w = 1: unlit
uniform vec4 u_emissive;        // rgb = emissive factor * strength

SAMPLER2D(s_albedo, 0);               // base color (sRGB)
SAMPLER2D(s_normalMap, 1);            // tangent space normal
SAMPLER2D(s_metallicRoughnessMap, 2); // g = roughness, b = metallic
SAMPLER2D(s_occlusionMap, 3);         // r = occlusion
SAMPLER2D(s_emissiveMap, 4);          // emissive (sRGB)

//
// NORMAL MAPPING
//

// Tangent space normal map to world space. Texture coordinates have a top
// left origin, so the bitangent (towards +v) points down the image while
// the map's green channel points up (glTF 2.0 spec, Materials: normalTexture).
// Varyings are passed in: on some backends they only exist inside main()
vec3 SurfaceNormal(vec3 N, vec3 T, vec3 B, vec2 uv)
{
    // Meshes without texture coordinates have no tangent frame
    if (dot(T, T) < 1e-8 || dot(B, B) < 1e-8) return N;

    vec3 t = texture2D(s_normalMap, uv).xyz * 2.0 - 1.0;
    return normalize(normalize(T) * t.x - normalize(B) * t.y + N * t.z);
}

//
// MAIN
//

void main()
{
    vec3 N = SurfaceNormal(normalize(v_normal), v_tangent, v_bitangent, v_texcoord0);
    vec3 V = normalize(u_viewPos.xyz - v_worldPos);

    // Material inputs: factors multiplied by their textures
    vec4 baseColor = texture2D(s_albedo, v_texcoord0) * u_meshColor;
    if (baseColor.a < u_materialParams.z) discard;
    vec4 metallicRoughness = texture2D(s_metallicRoughnessMap, v_texcoord0);
    float roughness = clamp(u_materialParams.y * metallicRoughness.g, 0.04, 1.0);
    float metallic = clamp(u_materialParams.x * metallicRoughness.b, 0.0, 1.0);
    float occlusion = texture2D(s_occlusionMap, v_texcoord0).r;
    vec3 emissive = texture2D(s_emissiveMap, v_texcoord0).rgb * u_emissive.rgb;

    vec3 result = u_materialParams.w > 0.5
        ? baseColor.rgb
        : LitSurface(N, V, v_worldPos, gl_FragCoord.xy, baseColor.rgb,
                     metallic, roughness, occlusion);

    result += emissive;

    // Output with gamma correction; the draw's blend state reads the alpha
    result = toGamma(result);
    gl_FragColor = vec4(result, baseColor.a);
}
