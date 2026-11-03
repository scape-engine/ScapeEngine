// Shared by the sky light SH and prefilter passes
// (path works relative to both the shader and this file)
#include "../includes/sky_light.sh"

SAMPLERCUBE(s_skyCapture, 0);

uniform vec4 u_skyLightCapture;  // x = face, y = flip y, z = roughness, w = capture size
uniform vec4 u_lowerHemisphere;  // rgb = color, w = 1 if the lower hemisphere is solid

// Captured sky radiance, with the lower hemisphere optionally replaced
vec3 SampleSky(vec3 dir, float lod)
{
    if (u_lowerHemisphere.w > 0.5 && dir.y < 0.0)
        return u_lowerHemisphere.rgb;

    return textureCubeLod(s_skyCapture, dir, lod).rgb;
}

// Mip whose texels match the solid angle covered by one sample
float SampleLod(float sample_solid_angle)
{
    float size = u_skyLightCapture.w;
    float texel_solid_angle = 4.0 * SKY_PI / (6.0 * size * size);
    return max(0.5 * log2(sample_solid_angle / texel_solid_angle) + 1.0, 0.0);
}
