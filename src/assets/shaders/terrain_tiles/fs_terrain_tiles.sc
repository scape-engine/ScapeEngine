$input v_worldPos, v_normal, v_tiles, v_centrePos, v_up, v_centreNormal, v_fade

#include "../common/common.sh"
#include "../includes/lit.sh"
#include "../includes/dither.sh"

//
// Tiled terrain material (TerrainMaterial): each point mixes up to eight
// tiles (terrain_tiles.sh), each its image recoloured by the tile colour (or
// the flat colour), lit as the standard material is
//

uniform vec4 u_terrainParams;  // x = roughness
uniform vec4 u_tileNoise;      // x, y = sample frequencies, z = second sample weight, w = offset

// Per tile (columns), point sampled: row 0 the tile colour as HSV and its
// height offset, row 1 the image's mean HSV and its height scale, row 2 x = 1
// where the tile has an image
SAMPLER2D(s_tileConstants, 0);
// The tile images, one layer per tile: colour (sRGB values, read as stored)
// and height in alpha
SAMPLER2DARRAY(s_tileImages, 1);

vec4 TileConstant(float tile, float row)
{
    return texture2DLod(s_tileConstants, vec2((tile + 0.5) / 23.0, (row + 0.5) / 3.0), 0.0);
}

#define TERRAIN_SAMPLE(tile, u, v) texture2DArray(s_tileImages, vec3(u, v, tile))
#define TERRAIN_TARGET(tile) TileConstant(tile, 0.0)
#define TERRAIN_AVERAGE(tile) TileConstant(tile, 1.0)
#define TERRAIN_HAS_IMAGE(tile) TileConstant(tile, 2.0).x
#include "../includes/terrain_tiles.sh"

// Value noise, 0..1
// TODO: a noise texture, as tiling noise costs less than hashing per pixel
float TerrainHash(vec3 p)
{
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float TerrainValueNoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(TerrainHash(i + vec3(0.0, 0.0, 0.0)), TerrainHash(i + vec3(1.0, 0.0, 0.0)), u.x),
                   mix(TerrainHash(i + vec3(0.0, 1.0, 0.0)), TerrainHash(i + vec3(1.0, 1.0, 0.0)), u.x), u.y),
               mix(mix(TerrainHash(i + vec3(0.0, 0.0, 1.0)), TerrainHash(i + vec3(1.0, 0.0, 1.0)), u.x),
                   mix(TerrainHash(i + vec3(0.0, 1.0, 1.0)), TerrainHash(i + vec3(1.0, 1.0, 1.0)), u.x), u.y),
               u.z);
}

void main()
{
    DitherDiscard(v_fade, gl_FragCoord.xy);
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_viewPos.xyz - v_worldPos);

    float noise = TerrainNoiseWeight(TerrainValueNoise(v_centrePos * u_tileNoise.x),
                                     TerrainValueNoise(v_centrePos * u_tileNoise.y),
                                     u_tileNoise);
    vec3 colour = toLinear(TerrainShade(v_tiles.x, v_tiles.y, v_tiles.z,
                                        normalize(v_centreNormal),
                                        dot(normalize(v_up), N), noise, v_centrePos));

    // TODO: water below sea level (v_tiles.w)
    vec3 result = LitSurface(N, V, v_worldPos, gl_FragCoord.xy, colour, 0.0,
                             clamp(u_terrainParams.x, 0.04, 1.0), 1.0);
    gl_FragColor = vec4(toGamma(result), 1.0);
}
