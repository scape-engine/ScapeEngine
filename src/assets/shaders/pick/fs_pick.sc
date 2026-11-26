$input v_viewNormal, v_viewDepth, v_fade

#include <bgfx_shader.sh>
#include "../includes/dither.sh"

// The draw's number as a colour (PickPass), and where its surface is: the
// view-space normal and linear view depth
uniform vec4 u_pickId;

void main()
{
    DitherDiscard(v_fade, gl_FragCoord.xy);
    gl_FragData[0] = u_pickId;
    gl_FragData[1] = vec4(normalize(v_viewNormal), v_viewDepth);
}
