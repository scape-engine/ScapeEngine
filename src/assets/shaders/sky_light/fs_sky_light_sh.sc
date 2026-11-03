$input v_ndc

#include <bgfx_shader.sh>
#include "../includes/sky_light_capture.sh"

#define SAMPLE_COUNT 512

// Projects the captured sky onto 9 SH coefficients, one per output texel
void main()
{
    int index = int(min(floor((v_ndc.x * 0.5 + 0.5) * 9.0), 8.0));

    float sample_solid_angle = 4.0 * SKY_PI / float(SAMPLE_COUNT);
    float lod = SampleLod(sample_solid_angle);

    vec3 coefficient = vec3_splat(0.0);
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        // Fibonacci sphere: evenly spread directions
        float y = 1.0 - 2.0 * (float(i) + 0.5) / float(SAMPLE_COUNT);
        float r = sqrt(1.0 - y * y);
        float phi = float(i) * 2.399963;  // golden angle
        vec3 dir = vec3(cos(phi) * r, y, sin(phi) * r);

        coefficient += SampleSky(dir, lod) * ShBasis(index, dir);
    }

    gl_FragColor = vec4(coefficient * sample_solid_angle, 1.0);
}
