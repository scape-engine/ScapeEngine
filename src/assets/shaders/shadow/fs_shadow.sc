$input v_depth, v_fade

#include <bgfx_shader.sh>
#include "../includes/dither.sh"

void main() {
    // Depth only, no color output required; faded casters dither too
    DitherDiscard(v_fade, gl_FragCoord.xy);
}