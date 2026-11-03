$input v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_meshColor;  // tint, alpha is the icon's opacity

SAMPLER2D(s_albedo, 0);

void main()
{
    gl_FragColor = texture2D(s_albedo, v_texcoord0) * u_meshColor;
}
