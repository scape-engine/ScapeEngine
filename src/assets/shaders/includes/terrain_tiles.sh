// Terrain tiles: which of 23 tiles a terrain point shows, how much of each,
// and the colour they make. Every vertex names two tile groups and a blend
// between them; a group has steep and flat variants (picked by slope), each
// in two looks (picked by noise).
//
// Shared by the terrain shader and its C++ check (src/tests), so it must stay
// GLSL that also compiles as C++ with glm: no swizzles, integers only as loop
// counters, no literal suffixes, outputs through TERRAIN_OUT

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

// A weight sharpened against a tile height
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
// variants, then the second's. The blends are sharpened against the tile
// heights (TerrainHeights): heights0 are the A/B looks of each group's steep
// and flat variants, heights1 each group's steep/flat mix and the groups' mix
// (all 0.5 without images)
void TerrainTileWeights(float attribute0, float attribute1, float blend,
                        float up_dot_normal, float noise_weight,
                        vec4 heights0, vec4 heights1,
                        TERRAIN_OUT(vec4) tiles0, TERRAIN_OUT(vec4) tiles1,
                        TERRAIN_OUT(vec4) weights0, TERRAIN_OUT(vec4) weights1)
{
    float tile0 = TerrainTileIndex(attribute0);
    float tile1 = TerrainTileIndex(attribute1);
    tiles0 = TerrainTileVariants(tile0);
    tiles1 = TerrainTileVariants(tile1);

    float steep0 = TerrainHeightBlend(noise_weight, heights0.x);
    float level0 = TerrainHeightBlend(noise_weight, heights0.y);
    float steep1 = TerrainHeightBlend(noise_weight, heights0.z);
    float level1 = TerrainHeightBlend(noise_weight, heights0.w);
    float flat0 = TerrainHeightBlend(TerrainFlatWeight(up_dot_normal, tile0), heights1.x);
    float flat1 = TerrainHeightBlend(TerrainFlatWeight(up_dot_normal, tile1), heights1.y);
    float second = TerrainHeightBlend(blend, heights1.z);
    // a group is not read at all when its share is below 0.05
    float share0 = second < 0.95 ? 1.0 - second : 0.0;
    float share1 = second > 0.05 ? second : 0.0;

    weights0 = vec4(TerrainRead((1.0 - steep0) * (1.0 - flat0), share0),
                    TerrainRead(steep0 * (1.0 - flat0), share0),
                    TerrainRead((1.0 - level0) * flat0, share0),
                    TerrainRead(level0 * flat0, share0));
    weights1 = vec4(TerrainRead((1.0 - steep1) * (1.0 - flat1), share1),
                    TerrainRead(steep1 * (1.0 - flat1), share1),
                    TerrainRead((1.0 - level1) * flat1, share1),
                    TerrainRead(level1 * flat1, share1));
}

// Tile images are projected along each axis, repeating every 24 m (positions
// relative to the terrain's centre)
vec3 TerrainUv(vec3 centre_position)
{
    vec3 p = centre_position / 24.0;
    return p - 2048.0 * floor(p / 2048.0);
}

// The axis' image coordinates: x reads (y, z), y (z, x), z (x, y)
vec2 TerrainAxisUv(int axis, vec3 uv)
{
    if (axis == 0) return vec2(uv.y, uv.z);
    if (axis == 1) return vec2(uv.z, uv.x);
    return vec2(uv.x, uv.y);
}

// Each axis' share: |n|^64, normalised
vec3 TerrainProjection(vec3 normal)
{
    vec3 p = vec3(pow(abs(normal.x), 64.0), pow(abs(normal.y), 64.0),
                  pow(abs(normal.z), 64.0));
    float sum = p.x + p.y + p.z;
    return sum > 0.0 ? p / sum : vec3(0.0, 0.0, 0.0);
}

// Colours as hue (0..1), saturation and value
vec3 TerrainRgbToHsv(vec3 rgb)
{
    float hi = max(rgb.x, max(rgb.y, rgb.z));
    float lo = min(rgb.x, min(rgb.y, rgb.z));
    float delta = hi - lo;
    if (hi == 0.0 || delta == 0.0) return vec3(0.0, hi == 0.0 ? 0.0 : delta / hi, hi);
    float h = rgb.x == hi ? (rgb.y - rgb.z) / delta
            : rgb.y == hi ? (rgb.z - rgb.x) / delta + 2.0
                          : (rgb.x - rgb.y) / delta + 4.0;
    if (h < 0.0) h += 6.0;
    return vec3(h / 6.0, delta / hi, hi);
}

vec3 TerrainHsvToRgb(vec3 hsv)
{
    vec3 rgb = clamp(abs(fract(vec3(hsv.x + 1.0, hsv.x + 2.0 / 3.0, hsv.x + 1.0 / 3.0)) * 6.0 -
                         3.0) - 1.0, 0.0, 1.0);
    return clamp(mix(vec3(1.0, 1.0, 1.0), rgb, clamp(hsv.y, 0.0, 1.0)) * clamp(hsv.z, 0.0, 1.0),
                 0.0, 1.0);
}

// A tile's height from its image's alpha: target.w shifts it, average.w
// scales it about 0.5
float TerrainHeight(float alpha, vec4 target, vec4 average)
{
    return clamp(((alpha + target.w) - 0.5) * max(average.w + 1.0, 0.0) + 0.5, 0.0, 1.0);
}

// One group's steep and flat heights (x, z) and their inverse mixes (y, w)
// from its four tiles' heights; reads the blends skip count as 0.5
vec4 TerrainGroupHeights(vec4 h, float noise_weight, float flat_weight)
{
    float a = noise_weight < 0.95 ? h.x : 0.5;
    float b = noise_weight > 0.05 ? h.y : 0.5;
    float c = noise_weight < 0.95 ? h.z : 0.5;
    float d = noise_weight > 0.05 ? h.w : 0.5;
    return vec4(flat_weight < 0.95 ? mix(a, b, noise_weight) : 0.5,
                flat_weight < 0.95 ? mix(a, 1.0 - b, noise_weight) : 0.0,
                flat_weight > 0.05 ? mix(c, d, noise_weight) : 0.5,
                flat_weight > 0.05 ? mix(c, 1.0 - d, noise_weight) : 0.0);
}

// One axis' eight blend heights (TerrainTileWeights' heights0, heights1)
// from both groups' tile heights
void TerrainAxisHeights(vec4 h0, vec4 h1, float noise_weight, float flat0,
                        float flat1, float blend,
                        TERRAIN_OUT(vec4) heights0, TERRAIN_OUT(vec4) heights1)
{
    vec4 g0 = TerrainGroupHeights(h0, noise_weight, flat0);
    vec4 g1 = TerrainGroupHeights(h1, noise_weight, flat1);
    bool use0 = blend < 0.95;
    bool use1 = blend > 0.05;
    float mixed0 = use0 ? mix(g0.x, g0.z, flat0) : 0.5;
    float mixed1 = use1 ? mix(g1.x, g1.z, flat1) : 0.5;
    heights0 = vec4(use0 ? g0.y : 0.0, use0 ? g0.w : 0.0,
                    use1 ? g1.y : 0.0, use1 ? g1.w : 0.0);
    heights1 = vec4(use0 ? mix(g0.x, 1.0 - g0.z, flat0) : 0.0,
                    use1 ? mix(g1.x, 1.0 - g1.z, flat1) : 0.0,
                    mix(mixed0, 1.0 - mixed1, blend), 0.0);
}

// An image texel (HSV, height in w) recoloured: the tile colour's hue and
// value over the image's mean, its saturation as a ceiling. A tile without an
// image is its flat colour
vec3 TerrainTexelColour(vec4 texel, vec4 target, vec4 average, float has_image)
{
    if (has_image < 0.5) return TerrainHsvToRgb(vec3(target.x, target.y, target.z));
    return TerrainHsvToRgb(vec3(fract(texel.x - average.x + target.x),
                                min(target.y, texel.y),
                                texel.z - average.z + target.z));
}

#ifdef TERRAIN_SAMPLE
// Element k of eight held in two vec4s (the index stays in range on both
// sides, as HLSL evaluates both)
float TerrainSlot(vec4 slots0, vec4 slots1, int k)
{
    vec4 slots = k < 4 ? slots0 : slots1;
    return slots[k < 4 ? k : k - 4];
}

// The terrain's colour (sRGB values) at a point. The includer defines
// TERRAIN_SAMPLE(tile, u, v): a tile image's RGB and height (wrapping), and
// TERRAIN_TARGET(tile), TERRAIN_AVERAGE(tile), TERRAIN_HAS_IMAGE(tile): the
// tile colour as HSV and its height offset, the image's mean HSV and its
// height scale, and 1 where the tile has an image
// TODO: read only what the blends keep (needs explicit gradients), and light
// with the images' normals
vec3 TerrainShade(float attribute0, float attribute1, float blend, vec3 normal,
                  float up_dot_normal, float noise_weight, vec3 centre_position)
{
    vec3 uv = TerrainUv(centre_position);
    vec3 projection = TerrainProjection(normal);
    float tile0 = TerrainTileIndex(attribute0);
    float tile1 = TerrainTileIndex(attribute1);
    vec4 slots0 = TerrainTileVariants(tile0);
    vec4 slots1 = TerrainTileVariants(tile1);
    float flat0 = TerrainFlatWeight(up_dot_normal, tile0);
    float flat1 = TerrainFlatWeight(up_dot_normal, tile1);

    vec4 texels[24];
    vec4 heights0 = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 heights1 = vec4(0.0, 0.0, 0.0, 0.0);
    for (int a = 0; a < 3; a++)
    {
        vec2 axis_uv = TerrainAxisUv(a, uv);
        float h[8];
        for (int k = 0; k < 8; k++)
        {
            float tile = TerrainSlot(slots0, slots1, k);
            vec4 texel = TERRAIN_SAMPLE(tile, axis_uv.x, axis_uv.y);
            texels[a * 8 + k] = vec4(TerrainRgbToHsv(vec3(texel.x, texel.y, texel.z)), texel.w);
            h[k] = TerrainHeight(texel.w, TERRAIN_TARGET(tile), TERRAIN_AVERAGE(tile));
        }
        // a nearly unused axis keeps 0.5 for all
        vec4 axis0 = vec4(0.5, 0.5, 0.5, 0.5);
        vec4 axis1 = vec4(0.5, 0.5, 0.5, 0.5);
        if (projection[a] > 0.003)
            TerrainAxisHeights(vec4(h[0], h[1], h[2], h[3]), vec4(h[4], h[5], h[6], h[7]),
                               noise_weight, flat0, flat1, blend, axis0, axis1);
        heights0 += projection[a] * axis0;
        heights1 += projection[a] * axis1;
    }

    vec4 tiles0, tiles1, weights0, weights1;
    TerrainTileWeights(attribute0, attribute1, blend, up_dot_normal, noise_weight,
                       heights0, heights1, tiles0, tiles1, weights0, weights1);

    vec3 colour = vec3(0.0, 0.0, 0.0);
    for (int a = 0; a < 3; a++)
    {
        if (projection[a] <= 0.003) continue;
        for (int k = 0; k < 8; k++)
        {
            float tile = TerrainSlot(slots0, slots1, k);
            float weight = TerrainSlot(weights0, weights1, k);
            colour += (projection[a] * weight) *
                      TerrainTexelColour(texels[a * 8 + k], TERRAIN_TARGET(tile),
                                         TERRAIN_AVERAGE(tile), TERRAIN_HAS_IMAGE(tile));
        }
    }
    return colour;
}
#endif
