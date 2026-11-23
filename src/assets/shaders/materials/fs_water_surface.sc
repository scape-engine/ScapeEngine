$input v_worldPos, v_normal, v_texcoord0, v_tangent, v_bitangent

#include "../common/common.sh"
#include "../includes/lit.sh"
#include "../includes/water_waves.sh"

// Water surface (WaterMaterial), lit as the standard material is
uniform vec4 u_meshColor;       // rgb: colour (linear)
uniform vec4 u_materialParams;  // y: roughness

void main()
{
    // The normal of the displaced surface, from the finest cascade's texels
    vec2 p = v_texcoord0;
    float e = u_waterWaves[0].x;
    vec3 c = WaveDisplacement(p);
    vec3 dx = vec3(e, 0.0, 0.0) + WaveDisplacement(p + vec2(e, 0.0)) - c;
    vec3 dz = vec3(0.0, 0.0, e) + WaveDisplacement(p + vec2(0.0, e)) - c;
    vec3 n = normalize(cross(dz, dx));
    vec3 N = normalize(normalize(v_tangent) * n.x + normalize(v_normal) * n.y +
                       normalize(v_bitangent) * n.z);

    vec3 V = normalize(u_viewPos.xyz - v_worldPos);
    float roughness = clamp(u_materialParams.y, 0.04, 1.0);
    vec3 result = LitSurface(N, V, v_worldPos, gl_FragCoord.xy, u_meshColor.rgb,
                             0.0, roughness, 1.0);
    gl_FragColor = vec4(toGamma(result), 1.0);
}
