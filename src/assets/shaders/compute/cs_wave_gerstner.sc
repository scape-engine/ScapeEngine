#include "bgfx_compute.sh"

// Gerstner waves summed into one texture layer per cascade (z): each texel's
// displacement (x along the surface's x axis, y up, z along its z axis). A
// sine is the parabola 8s - 16s|s|; depth attenuates a wave by
// clamp(max(k * depth, 0.25) / pi, 0, 1)

// Per cascade, and one past the last: x its first group of four waves, y
// its texel width (metres)
BUFFER_RO(b_cascades, vec4, 0);
// Seven per group: direction x, direction y, phase, wave number, omega,
// amplitude, time
BUFFER_RO(b_waves, vec4, 1);
IMAGE2D_ARRAY_WO(i_displacement, rgba16f, 2);

// [0] w: texels across a cascade; [1] x: water depth (metres)
uniform vec4 u_waterWaves[2];

vec4 ParabolaSin(vec4 theta)
{
    vec4 s = fract(theta * 0.15915494 + 0.5) - 0.5;
    return s * 8.0 - 16.0 * s * abs(s);
}

NUM_THREADS(8, 8, 1)
void main()
{
    ivec3 id = ivec3(gl_GlobalInvocationID);
    float size = u_waterWaves[0].w;
    if (float(id.x) >= size || float(id.y) >= size)
        return;

    vec4 cascade = b_cascades[id.z];
    vec2 p = ((vec2(id.xy) + 0.5) / size - 0.5) * size * cascade.y;
    float depth = u_waterWaves[1].x;

    vec3 sum = vec3(0.0, 0.0, 0.0);
    int last = int(b_cascades[id.z + 1].x);
    for (int g = int(cascade.x); g < last; g++)
    {
        int b = g * 7;
        vec4 dirX = b_waves[b];
        vec4 dirY = b_waves[b + 1];
        vec4 k = b_waves[b + 3];
        vec4 amplitude = b_waves[b + 5];
        vec4 attenuation = clamp(max(k * depth, vec4_splat(0.25)) * 0.31830987, 0.0, 1.0);
        vec4 theta = (k * dirX * p.x + k * dirY * p.y + b_waves[b + 2]) - b_waves[b + 4] * b_waves[b + 6];
        vec4 across = -amplitude * ParabolaSin(theta) * attenuation;
        vec4 up = amplitude * ParabolaSin(theta + 1.5707964) * attenuation;
        sum += vec3(dot(across * dirX, vec4_splat(1.0)), dot(up, vec4_splat(1.0)),
                    dot(across * dirY, vec4_splat(1.0)));
    }
    imageStore(i_displacement, id, vec4(sum, 0.0));
}
