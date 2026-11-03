$input v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent

#include "../common/common.sh"
#include "../includes/sky_light.sh"

//
// Standard material: metallic-roughness PBR (glTF 2.0 material model)
// Direct lighting: Cook-Torrance microfacet specular + Lambert diffuse
//  - B. Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013
//  - glTF 2.0 spec, Appendix B: BRDF Implementation
//

//
// LIGHT LIMITS (check light.cpp to match constants)
//

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_POINT_LIGHTS 32
#define MAX_SPOT_LIGHTS 16

#define PI 3.14159265359

//
// UNIFORMS
//

// Camera
uniform vec4 u_viewPos;  // xyz = camera position

// Material (StandardMaterial, stages 0-4). Unset textures are bound to
// neutral defaults: white, or a flat normal for the normal map
uniform vec4 u_meshColor;       // base color factor
uniform vec4 u_materialParams;  // x = metallic factor, y = roughness factor
uniform vec4 u_emissive;        // rgb = emissive factor * strength

SAMPLER2D(s_albedo, 0);               // base color (sRGB)
SAMPLER2D(s_normalMap, 1);            // tangent space normal
SAMPLER2D(s_metallicRoughnessMap, 2); // g = roughness, b = metallic
SAMPLER2D(s_occlusionMap, 3);         // r = occlusion
SAMPLER2D(s_emissiveMap, 4);          // emissive (sRGB)

// Sky light (ambient): diffuse SH + prefiltered reflections
uniform vec4 u_skyLight;        // rgb = color * intensity, w = 1 if enabled
uniform vec4 u_skyLightParams;  // x = last prefiltered mip

SAMPLER2D(s_skySH, 5);
SAMPLERCUBE(s_skyPrefilter, 6);

// Ambient occlusion of the sky light (screen space)
uniform vec4 u_ambientOcclusion;  // x = 1 if enabled, yz = 1 / target size

SAMPLER2D(s_ambientOcclusion, 7);

// Light counts: x=dir, y=point, z=spot, w=unused
uniform vec4 u_lightCounts;

// Directional lights
// TODO: drop the ambient/specular inputs from the Light API, PBR derives
// specular from the material and ambient comes from the sky light
uniform vec4 u_dirLight_direction[MAX_DIRECTIONAL_LIGHTS];
uniform vec4 u_dirLight_ambient[MAX_DIRECTIONAL_LIGHTS];
uniform vec4 u_dirLight_diffuse[MAX_DIRECTIONAL_LIGHTS];  // color * intensity
uniform vec4 u_dirLight_specular[MAX_DIRECTIONAL_LIGHTS];

// Point lights
uniform vec4 u_pointLight_position[MAX_POINT_LIGHTS];
uniform vec4 u_pointLight_ambient[MAX_POINT_LIGHTS];
uniform vec4 u_pointLight_diffuse[MAX_POINT_LIGHTS];
uniform vec4 u_pointLight_specular[MAX_POINT_LIGHTS];
uniform vec4 u_pointLight_attenuation[MAX_POINT_LIGHTS];  // constant, linear, quadratic, unused

// Spot lights
uniform vec4 u_spotLight_position[MAX_SPOT_LIGHTS];
uniform vec4 u_spotLight_direction[MAX_SPOT_LIGHTS];
uniform vec4 u_spotLight_ambient[MAX_SPOT_LIGHTS];
uniform vec4 u_spotLight_diffuse[MAX_SPOT_LIGHTS];
uniform vec4 u_spotLight_specular[MAX_SPOT_LIGHTS];
uniform vec4 u_spotLight_attenuation[MAX_SPOT_LIGHTS];
uniform vec4 u_spotLight_cutoff[MAX_SPOT_LIGHTS];  // inner, outer, unused, unused

//
// BRDF
//

// GGX / Trowbridge-Reitz normal distribution, alpha = roughness^2
// (Karis 2013, eq. 3)
float DistributionGGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Schlick approximation of Smith's geometry term, k = (roughness + 1)^2 / 8
// for punctual lights (Karis 2013, eq. 4)
float GeometrySmith(float NoV, float NoL, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float g_v = NoV / (NoV * (1.0 - k) + k);
    float g_l = NoL / (NoL * (1.0 - k) + k);
    return g_v * g_l;
}

// Schlick's Fresnel approximation (glTF 2.0 spec, Appendix B)
vec3 FresnelSchlick(float VoH, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

// Light reflected towards the viewer by one light arriving from L
vec3 DirectLight(vec3 N, vec3 V, vec3 L, vec3 light, vec3 diffuseColor,
                 vec3 specularColor, float roughness)
{
    float NoL = dot(N, L);
    if (NoL <= 0.0) return vec3_splat(0.0);

    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 F = FresnelSchlick(VoH, specularColor);
    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySmith(NoV, NoL, roughness);

    vec3 specular = F * (D * G / (4.0 * NoV * NoL));
    vec3 diffuse = (1.0 - F) * diffuseColor / PI;

    // Light values are display-referred: a white diffuse surface facing a
    // light of intensity 1 reflects its color, hence the * PI
    // TODO: physical light units (lux, candela) with camera exposure
    return (diffuse + specular) * light * NoL * PI;
}

//
// LIGHTS
//

vec3 CalcDirectionalLight(int idx, vec3 N, vec3 V, vec3 diffuseColor,
                          vec3 specularColor, float roughness)
{
    vec3 L = normalize(-u_dirLight_direction[idx].xyz);
    return DirectLight(N, V, L, u_dirLight_diffuse[idx].rgb, diffuseColor,
                       specularColor, roughness);
}

// TODO: inverse square falloff with a range window (Karis 2013, eq. 9)
float Attenuation(vec4 attenuation, float distance)
{
    return 1.0 / (attenuation.x + attenuation.y * distance +
                  attenuation.z * distance * distance);
}

vec3 CalcPointLight(int idx, vec3 fragPos, vec3 N, vec3 V, vec3 diffuseColor,
                    vec3 specularColor, float roughness)
{
    vec3 toLight = u_pointLight_position[idx].xyz - fragPos;
    vec3 L = normalize(toLight);
    float attenuation = Attenuation(u_pointLight_attenuation[idx], length(toLight));

    return DirectLight(N, V, L, u_pointLight_diffuse[idx].rgb * attenuation,
                       diffuseColor, specularColor, roughness);
}

vec3 CalcSpotLight(int idx, vec3 fragPos, vec3 N, vec3 V, vec3 diffuseColor,
                   vec3 specularColor, float roughness)
{
    vec3 toLight = u_spotLight_position[idx].xyz - fragPos;
    vec3 L = normalize(toLight);
    vec3 spotDir = normalize(u_spotLight_direction[idx].xyz);

    // Smooth cone between the inner and outer angles (cosines)
    float theta = dot(L, -spotDir);
    float innerCutoff = u_spotLight_cutoff[idx].x;
    float outerCutoff = u_spotLight_cutoff[idx].y;
    float cone = clamp((theta - outerCutoff) / (innerCutoff - outerCutoff), 0.0, 1.0);

    float attenuation = Attenuation(u_spotLight_attenuation[idx], length(toLight));

    return DirectLight(N, V, L, u_spotLight_diffuse[idx].rgb * attenuation * cone,
                       diffuseColor, specularColor, roughness);
}

//
// SKY LIGHT
//

// Diffuse sky radiance for a normal, from the projected SH
vec3 SkyIrradiance(vec3 normal)
{
    vec3 irradiance = vec3_splat(0.0);
    for (int i = 0; i < 9; ++i)
    {
        vec2 uv = vec2((float(i) + 0.5) / 9.0, 0.5);
        vec3 coefficient = texture2DLod(s_skySH, uv, 0.0).rgb;
        irradiance += coefficient * ShCosineLobe(i) * ShBasis(i, normal);
    }
    return max(irradiance, vec3_splat(0.0));
}

// Split sum environment BRDF, analytic fit (Karis, mobile)
vec3 EnvBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

// Specular occlusion from ambient occlusion (Lagarde, Moving Frostbite to PBR)
float SpecularOcclusion(float NoV, float ao, float roughness)
{
    return clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

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
    vec4 metallicRoughness = texture2D(s_metallicRoughnessMap, v_texcoord0);
    float roughness = clamp(u_materialParams.y * metallicRoughness.g, 0.04, 1.0);
    float metallic = clamp(u_materialParams.x * metallicRoughness.b, 0.0, 1.0);
    float occlusion = texture2D(s_occlusionMap, v_texcoord0).r;
    vec3 emissive = texture2D(s_emissiveMap, v_texcoord0).rgb * u_emissive.rgb;

    // Metals have no diffuse, dielectrics reflect 4% at normal incidence
    vec3 diffuseColor = baseColor.rgb * (1.0 - metallic);
    vec3 specularColor = mix(vec3_splat(0.04), baseColor.rgb, metallic);

    vec3 result = vec3_splat(0.0);

    // Sky light
    if (u_skyLight.w > 0.5)
    {
        vec3 R = reflect(-V, N);
        float NoV = max(dot(N, V), 0.0);
        float lod = roughness * u_skyLightParams.x;

        // Occlusion (map and screen space) only darkens the sky light,
        // direct lights are unaffected
        float ao = occlusion;
        if (u_ambientOcclusion.x > 0.5) {
            ao *= texture2D(s_ambientOcclusion, gl_FragCoord.xy * u_ambientOcclusion.yz).r;
        }

        vec3 skyDiffuse = SkyIrradiance(N) * diffuseColor * ao;
        vec3 skySpecular = textureCubeLod(s_skyPrefilter, R, lod).rgb *
                           EnvBRDFApprox(specularColor, roughness, NoV) *
                           SpecularOcclusion(NoV, ao, roughness);

        result += (skyDiffuse + skySpecular) * u_skyLight.rgb;
    }

    // Get light counts
    int numDirLights   = int(u_lightCounts.x);
    int numPointLights = int(u_lightCounts.y);
    int numSpotLights  = int(u_lightCounts.z);

    // Accumulate directional lights
    for (int i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i)
    {
        if (i >= numDirLights) break;
        result += CalcDirectionalLight(i, N, V, diffuseColor, specularColor, roughness);
    }

    // Accumulate point lights
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
    {
        if (i >= numPointLights) break;
        result += CalcPointLight(i, v_worldPos, N, V, diffuseColor, specularColor, roughness);
    }

    // Accumulate spot lights
    for (int i = 0; i < MAX_SPOT_LIGHTS; ++i)
    {
        if (i >= numSpotLights) break;
        result += CalcSpotLight(i, v_worldPos, N, V, diffuseColor, specularColor, roughness);
    }

    result += emissive;

    // Output with gamma correction
    // TODO: alpha modes (opaque, mask, blend)
    result = toGamma(result);
    gl_FragColor = vec4(result, baseColor.a);
}
