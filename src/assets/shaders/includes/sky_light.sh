// Shared by the sky light capture passes and the lit material

#define SKY_PI 3.14159265359

// World direction of a cubemap texel from its face and NDC position.
// flip_y is -1 on backends with a bottom left framebuffer origin
vec3 CubeFaceDirection(float face, vec2 ndc, float flip_y)
{
    float x = ndc.x;
    float y = ndc.y * flip_y;

    vec3 dir;
    if (face < 0.5)      dir = vec3( 1.0,    y,   -x);  // +X
    else if (face < 1.5) dir = vec3(-1.0,    y,    x);  // -X
    else if (face < 2.5) dir = vec3(   x,  1.0,   -y);  // +Y
    else if (face < 3.5) dir = vec3(   x, -1.0,    y);  // -Y
    else if (face < 4.5) dir = vec3(   x,    y,  1.0);  // +Z
    else                 dir = vec3(  -x,    y, -1.0);  // -Z

    return normalize(dir);
}

// Real spherical harmonics basis, bands 0-2 (9 coefficients)
float ShBasis(int i, vec3 d)
{
    if (i == 0) return 0.282095;
    if (i == 1) return 0.488603 * d.y;
    if (i == 2) return 0.488603 * d.z;
    if (i == 3) return 0.488603 * d.x;
    if (i == 4) return 1.092548 * d.x * d.y;
    if (i == 5) return 1.092548 * d.y * d.z;
    if (i == 6) return 0.315392 * (3.0 * d.z * d.z - 1.0);
    if (i == 7) return 1.092548 * d.x * d.z;
    return 0.546274 * (d.x * d.x - d.y * d.y);
}

// Clamped cosine convolution per band, divided by pi (Lambert)
float ShCosineLobe(int i)
{
    if (i == 0) return 1.0;
    if (i < 4) return 2.0 / 3.0;
    return 0.25;
}
