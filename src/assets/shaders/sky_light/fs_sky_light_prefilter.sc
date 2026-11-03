$input v_ndc

#include <bgfx_shader.sh>
#include "../includes/sky_light_capture.sh"

#define SAMPLE_COUNT 64

// GGX prefiltered reflections for one roughness level (one mip)
void main()
{
    vec3 N = CubeFaceDirection(u_skyLightCapture.x, v_ndc, u_skyLightCapture.y);
    float roughness = u_skyLightCapture.z;

    // Mirror reflections: no filtering needed
    if (roughness < 0.001)
    {
        gl_FragColor = vec4(SampleSky(N, 0.0), 1.0);
        return;
    }

    float a = roughness * roughness;
    float a2 = a * a;

    // Tangent frame around N
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    vec3 color = vec3_splat(0.0);
    float weight = 0.0;
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        // Importance sample GGX (golden ratio sequence, no bit ops)
        float u = (float(i) + 0.5) / float(SAMPLE_COUNT);
        float v = fract(float(i) * 0.618034);
        float phi = 2.0 * SKY_PI * v;
        float cos_theta = sqrt((1.0 - u) / (1.0 + (a2 - 1.0) * u));
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        vec3 H = T * (cos(phi) * sin_theta) + B * (sin(phi) * sin_theta) + N * cos_theta;

        // Assumes N = V = R, as split sum prefiltering does
        vec3 L = 2.0 * dot(N, H) * H - N;
        float NoL = dot(N, L);
        if (NoL > 0.0)
        {
            // pdf = D * NoH / (4 * VoH), with V = N
            float d = cos_theta * cos_theta * (a2 - 1.0) + 1.0;
            float pdf = a2 / (SKY_PI * d * d) * 0.25;
            float lod = SampleLod(1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001));

            color += SampleSky(L, lod) * NoL;
            weight += NoL;
        }
    }

    gl_FragColor = vec4(color / max(weight, 0.0001), 1.0);
}
