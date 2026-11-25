$input v_viewNormal, v_viewDepth, v_texcoord0, v_viewPos

#include <bgfx_shader.sh>

// Material inputs of the alpha mask (StandardMaterial, see fs_lit.sc)
uniform vec4 u_meshColor;       // base color factor
uniform vec4 u_materialParams;  // z = alpha cutoff (0 = opaque),
                                // w = 1 if double-sided
SAMPLER2D(s_albedo, 0);

// Screen-space effect inputs. Cleared to 0: depth 0 means background
void main()
{
    // Masked holes stay holes for screen-space effects
    float alpha = texture2D(s_albedo, v_texcoord0).a * u_meshColor.a;
    if (u_materialParams.z > 0.0 && alpha < u_materialParams.z)
        discard;

    // Double-sided: a back face's normal faces the camera too
    vec3 normal = normalize(v_viewNormal);
    if (u_materialParams.w > 0.5 && dot(normal, v_viewPos) > 0.0)
        normal = -normal;

    gl_FragData[0] = vec4(normal, 1.0);                 // view-space normal
    gl_FragData[1] = vec4(v_viewDepth, 0.0, 0.0, 1.0);  // linear view depth
}
