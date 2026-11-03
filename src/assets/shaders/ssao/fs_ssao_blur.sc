$input v_ndc

#include <bgfx_shader.sh>

SAMPLER2D(s_ssaoInput, 0);

uniform vec4 u_ssaoScreen;  // xy = 1 / target size

// 4x4 box blur, the size of the noise tile, removes the noise pattern
void main()
{
    vec2 pixel = gl_FragCoord.xy;
    vec2 last_pixel = u_viewRect.zw - 0.5;

    float sum = 0.0;
    for (int y = -2; y < 2; ++y)
    {
        for (int x = -2; x < 2; ++x)
        {
            // Stay inside the view, the rest of the target is stale
            vec2 offset_pixel = min(pixel + vec2(float(x), float(y)), last_pixel);
            offset_pixel = max(offset_pixel, vec2_splat(0.5));
            sum += texture2DLod(s_ssaoInput, offset_pixel * u_ssaoScreen.xy, 0.0).r;
        }
    }

    gl_FragColor = vec4_splat(sum / 16.0);
}
