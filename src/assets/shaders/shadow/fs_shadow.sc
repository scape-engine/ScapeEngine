$input v_depth, v_texcoord0

#include <bgfx_shader.sh>

// Material inputs of the alpha mask (StandardMaterial, see fs_lit.sc)
uniform vec4 u_meshColor;       // base color factor
uniform vec4 u_materialParams;  // z = alpha cutoff (0 = opaque)
SAMPLER2D(s_albedo, 0);

void main() {
    // Depth only, no color output required. Masked holes let light through
    float alpha = texture2D(s_albedo, v_texcoord0).a * u_meshColor.a;
    if (u_materialParams.z > 0.0 && alpha < u_materialParams.z)
        discard;
}
