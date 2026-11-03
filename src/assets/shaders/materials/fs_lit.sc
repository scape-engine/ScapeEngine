$input v_worldPos, v_normal, v_texcoord0

#include "../common/common.sh"
#include "../includes/sky_light.sh"

//
// LIGHT LIMITS (check light.cpp to match constants)
//

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_POINT_LIGHTS 32
#define MAX_SPOT_LIGHTS 16

// 
// UNIFORMS
// 

// Camera
uniform vec4 u_viewPos;  // xyz = camera position

// Material
uniform vec4 u_meshColor;       // base color fallback
uniform vec4 u_AlbedoMap;       // x=1 if texture bound, 0 otherwise
uniform vec4 u_materialParams;  // x = metallic, y = roughness

SAMPLER2D(s_albedo, 0);

// Sky light (ambient): diffuse SH + prefiltered reflections
uniform vec4 u_skyLight;        // rgb = color * intensity, w = 1 if enabled
uniform vec4 u_skyLightParams;  // x = last prefiltered mip

SAMPLER2D(s_skySH, 1);
SAMPLERCUBE(s_skyPrefilter, 2);

// Light counts: x=dir, y=point, z=spot, w=unused
uniform vec4 u_lightCounts;

// Directional lights
uniform vec4 u_dirLight_direction[MAX_DIRECTIONAL_LIGHTS];
uniform vec4 u_dirLight_ambient[MAX_DIRECTIONAL_LIGHTS];
uniform vec4 u_dirLight_diffuse[MAX_DIRECTIONAL_LIGHTS];
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

// Material (hardcoded, should be uniforms)
#define MATERIAL_SHININESS 32.0

// 
// LIGHTING CALCULATIONS
// 

// Calculates contribution from a directional light
vec3 CalcDirectionalLight(int idx, vec3 normal, vec3 viewDir, vec3 baseColor)
{
    vec3 lightDir = normalize(-u_dirLight_direction[idx].xyz);
    
    // Diffuse (Lambertian)
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), MATERIAL_SHININESS);
    
    // Combine
    vec3 ambient  = u_dirLight_ambient[idx].rgb * baseColor;
    vec3 diffuse  = u_dirLight_diffuse[idx].rgb * diff * baseColor;
    vec3 specular = u_dirLight_specular[idx].rgb * spec;
    
    return ambient + diffuse + specular;
}

// Calculates contribution from a point light
vec3 CalcPointLight(int idx, vec3 fragPos, vec3 normal, vec3 viewDir, vec3 baseColor)
{
    vec3 lightPos = u_pointLight_position[idx].xyz;
    vec3 lightDir = normalize(lightPos - fragPos);
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), MATERIAL_SHININESS);
    
    // Attenuation
    float distance = length(lightPos - fragPos);
    float att_constant  = u_pointLight_attenuation[idx].x;
    float att_linear    = u_pointLight_attenuation[idx].y;
    float att_quadratic = u_pointLight_attenuation[idx].z;
    float attenuation = 1.0 / (att_constant + att_linear * distance + att_quadratic * distance * distance);
    
    // Combine
    vec3 ambient  = u_pointLight_ambient[idx].rgb * baseColor;
    vec3 diffuse  = u_pointLight_diffuse[idx].rgb * diff * baseColor;
    vec3 specular = u_pointLight_specular[idx].rgb * spec;
    
    return (ambient + diffuse + specular) * attenuation;
}

// Calculates contribution from a spot light
vec3 CalcSpotLight(int idx, vec3 fragPos, vec3 normal, vec3 viewDir, vec3 baseColor)
{
    vec3 lightPos = u_spotLight_position[idx].xyz;
    vec3 lightDir = normalize(lightPos - fragPos);
    vec3 spotDir  = normalize(u_spotLight_direction[idx].xyz);
    
    // Spotlight cone
    float theta = dot(lightDir, -spotDir);
    float innerCutoff = u_spotLight_cutoff[idx].x;
    float outerCutoff = u_spotLight_cutoff[idx].y;
    float epsilon = innerCutoff - outerCutoff;
    float intensity = clamp((theta - outerCutoff) / epsilon, 0.0, 1.0);
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), MATERIAL_SHININESS);
    
    // Attenuation
    float distance = length(lightPos - fragPos);
    float att_constant  = u_spotLight_attenuation[idx].x;
    float att_linear    = u_spotLight_attenuation[idx].y;
    float att_quadratic = u_spotLight_attenuation[idx].z;
    float attenuation = 1.0 / (att_constant + att_linear * distance + att_quadratic * distance * distance);
    
    // Combine
    vec3 ambient  = u_spotLight_ambient[idx].rgb * baseColor;
    vec3 diffuse  = u_spotLight_diffuse[idx].rgb * diff * baseColor;
    vec3 specular = u_spotLight_specular[idx].rgb * spec;
    
    // Apply spotlight intensity (ambient unaffected by cone)
    return ambient * attenuation + (diffuse + specular) * attenuation * intensity;
}

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

//
// MAIN
//

void main()
{
    vec3 normal = normalize(v_normal);
    vec3 viewDir = normalize(u_viewPos.xyz - v_worldPos);

    // Resolve base color
    vec3 albedo;
    if (u_AlbedoMap.x > 0.5) {
        albedo = texture2D(s_albedo, v_texcoord0).rgb;
    } else {
        albedo = u_meshColor.rgb;
    }

    float metallic = u_materialParams.x;
    float roughness = u_materialParams.y;
    vec3 baseColor = albedo * (1.0 - metallic);  // diffuse color
    vec3 specularColor = mix(vec3_splat(0.04), albedo, metallic);

    vec3 result = vec3_splat(0.0);

    // Sky light
    if (u_skyLight.w > 0.5)
    {
        vec3 R = reflect(-viewDir, normal);
        float NoV = max(dot(normal, viewDir), 0.0);
        float lod = roughness * u_skyLightParams.x;

        vec3 skyDiffuse = SkyIrradiance(normal) * baseColor;
        vec3 skySpecular = textureCubeLod(s_skyPrefilter, R, lod).rgb *
                           EnvBRDFApprox(specularColor, roughness, NoV);

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
        result += CalcDirectionalLight(i, normal, viewDir, baseColor);
    }
    
    // Accumulate point lights
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
    {
        if (i >= numPointLights) break;
        result += CalcPointLight(i, v_worldPos, normal, viewDir, baseColor);
    }
    
    // Accumulate spot lights
    for (int i = 0; i < MAX_SPOT_LIGHTS; ++i)
    {
        if (i >= numSpotLights) break;
        result += CalcSpotLight(i, v_worldPos, normal, viewDir, baseColor);
    }
    
    // Output with gamma correction
    result = toGamma(result);
    gl_FragColor = vec4(result, u_meshColor.a);
}