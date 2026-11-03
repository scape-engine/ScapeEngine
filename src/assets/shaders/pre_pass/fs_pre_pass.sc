$input v_viewNormal, v_viewDepth

#include <bgfx_shader.sh>

// Screen-space effect inputs. Cleared to 0: depth 0 means background
void main()
{
    gl_FragData[0] = vec4(normalize(v_viewNormal), 1.0);  // view-space normal
    gl_FragData[1] = vec4(v_viewDepth, 0.0, 0.0, 1.0);    // linear view depth
}
