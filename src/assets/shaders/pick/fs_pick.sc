$input v_viewNormal, v_viewDepth, v_fade

#include <bgfx_shader.sh>
#include "../includes/dither.sh"

// The draw's number as a colour (PickPass)
uniform vec4 u_pickId;

void main()
{
    DitherDiscard(v_fade, gl_FragCoord.xy);
    gl_FragColor = u_pickId;
}
