$input v_worldPos, v_normal, v_tiles, v_centrePos, v_up

#include "../common/common.sh"
#include "../includes/lit.sh"
#include "../includes/terrain_tiles.sh"

//
// Tiled terrain material (TerrainMaterial): each point mixes up to eight
// tiles (terrain_tiles.sh), each a flat colour for now, lit as the standard
// material is
//

uniform vec4 u_terrainParams;  // x = roughness
uniform vec4 u_tileNoise;      // x, y = sample frequencies, z = second sample weight, w = offset

SAMPLER2D(s_tileColours, 0);   // one texel per tile (sRGB)

vec3 TileColour(float tile)
{
    return toLinear(texture2D(s_tileColours, vec2((tile + 0.5) / 23.0, 0.5)).rgb);
}

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
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_viewPos.xyz - v_worldPos);

    float noise = TerrainNoiseWeight(TerrainValueNoise(v_centrePos * u_tileNoise.x),
                                     TerrainValueNoise(v_centrePos * u_tileNoise.y),
                                     u_tileNoise);
    vec4 tiles0, tiles1, weights0, weights1;
    TerrainTileWeights(v_tiles.x, v_tiles.y, v_tiles.z, dot(normalize(v_up), N),
                       noise, tiles0, tiles1, weights0, weights1);

    vec3 colour = TileColour(tiles0.x) * weights0.x + TileColour(tiles0.y) * weights0.y +
                  TileColour(tiles0.z) * weights0.z + TileColour(tiles0.w) * weights0.w +
                  TileColour(tiles1.x) * weights1.x + TileColour(tiles1.y) * weights1.y +
                  TileColour(tiles1.z) * weights1.z + TileColour(tiles1.w) * weights1.w;
    // Skipped reads leave the weights short of 1
    float total = dot(weights0, vec4_splat(1.0)) + dot(weights1, vec4_splat(1.0));
    colour /= max(total, 1e-4);

    // TODO: water below sea level (v_tiles.w)
    vec3 result = LitSurface(N, V, v_worldPos, gl_FragCoord.xy, colour, 0.0,
                             clamp(u_terrainParams.x, 0.04, 1.0), 1.0);
    gl_FragColor = vec4(toGamma(result), 1.0);
}
