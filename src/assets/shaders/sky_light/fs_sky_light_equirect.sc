$input v_ndc

#include <bgfx_shader.sh>
#include "../includes/sky_light.sh"

SAMPLER2D(s_skyEquirect, 0);

uniform vec4 u_skyLightCapture;  // x = face, y = flip y, w = capture size
uniform vec4 u_skyLightSource;   // x = rotation around the up axis (radians)

// Per axis: the image is much larger than the capture faces
#define SUPERSAMPLES 4

// Latitude/longitude image lookup, rotated around the up axis
vec3 SampleEquirect(vec3 dir)
{
    float c = cos(u_skyLightSource.x);
    float s = sin(u_skyLightSource.x);
    vec3 d = vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);

    vec2 uv = vec2(atan2(d.z, d.x) / (2.0 * SKY_PI) + 0.5,
                   acos(clamp(d.y, -1.0, 1.0)) / SKY_PI);
    return texture2DLod(s_skyEquirect, uv, 0.0).rgb;
}

// Converts the image into one face of the capture cube
void main()
{
    float texel = 2.0 / u_skyLightCapture.w;

    vec3 color = vec3_splat(0.0);
    for (int y = 0; y < SUPERSAMPLES; ++y)
    {
        for (int x = 0; x < SUPERSAMPLES; ++x)
        {
            vec2 offset = (vec2(float(x), float(y)) + 0.5) / float(SUPERSAMPLES) - 0.5;
            vec3 dir = CubeFaceDirection(u_skyLightCapture.x, v_ndc + offset * texel,
                                         u_skyLightCapture.y);
            color += SampleEquirect(dir);
        }
    }

    gl_FragColor = vec4(color / float(SUPERSAMPLES * SUPERSAMPLES), 1.0);
}
