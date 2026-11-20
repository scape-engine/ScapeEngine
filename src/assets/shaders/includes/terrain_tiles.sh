// Terrain tile picking: which of 23 tiles a terrain point shows, and how much
// of each. Every vertex names two tile groups and a blend between them; a
// group has steep and flat variants (picked by slope), each in two looks
// (picked by noise).
//
// Shared by the terrain shader and its C++ check (src/tests), so it must stay
// GLSL that also compiles as C++ with glm: no swizzles, no integer maths, no
// literal suffixes, outputs through TERRAIN_OUT

#ifndef TERRAIN_OUT
#define TERRAIN_OUT(T) out T
#endif

// The vertex attribute -> tile index (0..22)
float TerrainTileIndex(float attribute)
{
    float v = max(attribute, 0.0);
    return floor(v - 23.0 * floor(v / 23.0) + 0.5);
}

// A group's four tiles: steep A, steep B, flat A, flat B
vec4 TerrainTileVariants(float tile)
{
    if (tile >= 20.0) return vec4(tile, tile, tile, tile);
    if (tile >= 16.0) return vec4(tile, tile, tile + 1.0, tile + 1.0);
    return vec4(tile, tile + 2.0, tile + 1.0, tile + 3.0);
}

// Share of the flat variants, from up . normal (up: away from the planet's
// centre); asin as a series, squared for tiles 16 and up
float TerrainFlatWeight(float up_dot_normal, float tile)
{
    float x = max(up_dot_normal, 0.0);
    float s = (0.0446428582 * pow(x, 7.0) + 0.075000003 * pow(x, 5.0) +
               0.166666672 * pow(x, 3.0) + x) * 0.636638522;
    if (tile >= 16.0) s = s * s;
    float t = clamp((s - 0.3) * 2.5, 0.0, 1.0);
    return clamp((3.0 - 2.0 * t) * t * t, 0.0, 1.0);
}

// Share of the B looks from two noise samples (0..1); scales: z weighs the
// second sample, w offsets the result
float TerrainNoiseWeight(float sample0, float sample1, vec4 scales)
{
    float a = clamp(sample0, 0.0, 1.0) * 2.0 - 1.0;
    float b = clamp(sample1, 0.0, 1.0) * 2.0 - 1.0;
    return 1.0 - clamp((b * scales.z + a) * 0.5 + 0.5 + scales.w, 0.0, 1.0);
}

// A weight sharpened against a texture height (0.5 without textures)
float TerrainHeightBlend(float w, float h)
{
    float t = clamp((w - (h - 0.05)) * 10.0, 0.0, 1.0);
    return (3.0 - 2.0 * t) * t * t;
}

// A tile read's weight: reads under 0.05 are skipped
float TerrainRead(float mix_weight, float share)
{
    return mix_weight > 0.05 ? share * mix_weight : 0.0;
}

// The eight tiles a point shows and their weights: the first group's four
// variants, then the second's
// TODO: tile textures; their heights sharpen the blends (0.5 until then)
void TerrainTileWeights(float attribute0, float attribute1, float blend,
                        float up_dot_normal, float noise_weight,
                        TERRAIN_OUT(vec4) tiles0, TERRAIN_OUT(vec4) tiles1,
                        TERRAIN_OUT(vec4) weights0, TERRAIN_OUT(vec4) weights1)
{
    float tile0 = TerrainTileIndex(attribute0);
    float tile1 = TerrainTileIndex(attribute1);
    tiles0 = TerrainTileVariants(tile0);
    tiles1 = TerrainTileVariants(tile1);

    float look = TerrainHeightBlend(noise_weight, 0.5);
    float flat0 = TerrainHeightBlend(TerrainFlatWeight(up_dot_normal, tile0), 0.5);
    float flat1 = TerrainHeightBlend(TerrainFlatWeight(up_dot_normal, tile1), 0.5);
    float second = TerrainHeightBlend(blend, 0.5);
    // a group is not read at all when its share is below 0.05
    float share0 = second < 0.95 ? 1.0 - second : 0.0;
    float share1 = second > 0.05 ? second : 0.0;

    weights0 = vec4(TerrainRead((1.0 - look) * (1.0 - flat0), share0),
                    TerrainRead(look * (1.0 - flat0), share0),
                    TerrainRead((1.0 - look) * flat0, share0),
                    TerrainRead(look * flat0, share0));
    weights1 = vec4(TerrainRead((1.0 - look) * (1.0 - flat1), share1),
                    TerrainRead(look * (1.0 - flat1), share1),
                    TerrainRead((1.0 - look) * flat1, share1),
                    TerrainRead(look * flat1, share1));
}
