$input v_ndc

#include <bgfx_shader.sh>

#define KERNEL_SIZE 64  // GraphicsRenderer::kSSAOKernelSize

SAMPLER2D(s_ssaoNormals, 0);
SAMPLER2D(s_ssaoDepth, 1);
SAMPLER2D(s_ssaoNoise, 2);

uniform vec4 u_ssaoKernel[KERNEL_SIZE];  // xyz = hemisphere sample (tangent space)
uniform vec4 u_ssaoParams;      // x = samples, y = radius, z = bias, w = power
uniform vec4 u_ssaoProjection;  // x = proj[0][0], y = proj[1][1]
uniform vec4 u_ssaoScreen;      // xy = 1 / target size, z = NDC y sign, w = 1 / noise size

// Pixels are read from gl_FragCoord: targets are larger than the view (they
// grow with the scene framebuffer) and the view covers their top left

// View-space position of a pixel from its linear depth
vec3 ViewPosition(vec2 pixel, float depth)
{
    vec2 ndc = vec2(pixel.x / u_viewRect.z * 2.0 - 1.0,
                    u_ssaoScreen.z * (pixel.y / u_viewRect.w * 2.0 - 1.0));
    return vec3(ndc.x / u_ssaoProjection.x, ndc.y / u_ssaoProjection.y, 1.0) * depth;
}

// Pixel of a view-space position
vec2 ProjectToPixel(vec3 p)
{
    vec2 ndc = vec2(p.x * u_ssaoProjection.x, p.y * u_ssaoProjection.y) / p.z;
    return vec2((ndc.x * 0.5 + 0.5) * u_viewRect.z,
                (u_ssaoScreen.z * ndc.y * 0.5 + 0.5) * u_viewRect.w);
}

void main()
{
    vec2 pixel = gl_FragCoord.xy;
    vec2 uv = pixel * u_ssaoScreen.xy;

    // Background: nothing to occlude
    float depth = texture2DLod(s_ssaoDepth, uv, 0.0).r;
    if (depth <= 0.0)
    {
        gl_FragColor = vec4_splat(1.0);
        return;
    }

    vec3 P = ViewPosition(pixel, depth);
    vec3 N = normalize(texture2DLod(s_ssaoNormals, uv, 0.0).xyz);

    // Random rotation around the normal, tiled every noise size pixels
    vec3 random = texture2DLod(s_ssaoNoise, pixel * u_ssaoScreen.w, 0.0).xyz * 2.0 - 1.0;
    vec3 T = normalize(random - N * dot(random, N));
    vec3 B = cross(N, T);

    int samples = int(u_ssaoParams.x);
    float radius = u_ssaoParams.y;
    float bias = u_ssaoParams.z;

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        if (i >= samples) break;

        vec3 k = u_ssaoKernel[i].xyz;
        vec3 S = P + (T * k.x + B * k.y + N * k.z) * radius;
        if (S.z <= 0.0) continue;  // behind the camera

        // Only read inside the view, the rest of the target is stale
        vec2 sample_pixel = ProjectToPixel(S);
        if (sample_pixel.x < 0.0 || sample_pixel.y < 0.0 ||
            sample_pixel.x > u_viewRect.z || sample_pixel.y > u_viewRect.w)
            continue;

        float sample_depth = texture2DLod(s_ssaoDepth, sample_pixel * u_ssaoScreen.xy, 0.0).r;
        if (sample_depth <= 0.0) continue;  // background never occludes

        // Ignore occluders far from the pixel (range check)
        float range = smoothstep(0.0, 1.0, radius / abs(P.z - sample_depth));
        occlusion += (sample_depth <= S.z - bias ? 1.0 : 0.0) * range;
    }

    float ao = 1.0 - occlusion / max(float(samples), 1.0);
    ao = pow(clamp(ao, 0.0, 1.0), u_ssaoParams.w);

    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
