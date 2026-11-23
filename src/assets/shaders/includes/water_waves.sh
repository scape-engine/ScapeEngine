#ifndef WATER_WAVES_SH
#define WATER_WAVES_SH

// A water surface's wave cascades (cs_wave_gerstner): displacement at a
// point of the surface's plane, in metres from the point under the viewer

// [0] x: cascade 0's texel width, y: cascades, z: sphere radius (0: flat),
// w: texels across a cascade
uniform vec4 u_waterWaves[2];
SAMPLER2DARRAY(s_waveDisplacement, 9);  // ENTITY_TEXTURE_STAGE

vec3 WaveDisplacement(vec2 p)
{
    vec3 sum = vec3(0.0, 0.0, 0.0);
    float width = u_waterWaves[0].x * u_waterWaves[0].w;
    for (int c = 0; c < 16; c++)
    {
        if (float(c) >= u_waterWaves[0].y)
            break;
        vec2 uv = p / width + 0.5;
        // Fades out towards the cascade's edges, so none shows
        vec2 edge = 0.5 - abs(uv - 0.5);
        float weight = clamp(min(edge.x, edge.y) * 10.0, 0.0, 1.0);
        if (weight > 0.0)
            sum += texture2DArrayLod(s_waveDisplacement, vec3(uv, float(c)), 0.0).xyz * weight;
        width *= 2.0;
    }
    return sum;
}

#endif // WATER_WAVES_SH
