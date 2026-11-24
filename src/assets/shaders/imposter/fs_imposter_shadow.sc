$input v_worldPos, v_texcoord0, v_axisX, v_axisY, v_axisZ, v_fade

#include <bgfx_shader.sh>
#include "../includes/dither.sh"

// fs_shadow.sc for an imposter's card: cut out by the baked coverage

SAMPLER2D(s_imposterAlbedo, 0);

void main()
{
    DitherDiscard(v_fade, gl_FragCoord.xy);
    if (texture2D(s_imposterAlbedo, v_texcoord0).a < 0.5) discard;
}
